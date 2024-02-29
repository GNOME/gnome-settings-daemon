/*
 *
 *  gnome-bluetooth - Bluetooth integration for GNOME
 *
 *  Copyright (C) 2012  Bastien Nocera <hadess@hadess.net>
 *  Copyright © 2017 Endless Mobile, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixoutputstream.h>

#include "rfkill-glib.h"
#include <gudev/gudev.h>

enum {
	CHANGED,
	LAST_SIGNAL
};

enum {
	PROP_RFKILL_INPUT_INHIBITED = 1
};

static int signals[LAST_SIGNAL] = { 0 };

struct _CcRfkillGlib {
	GObject parent;

	GUdevClient *udev;
	gchar *device_file;

	GOutputStream *stream;
	GIOChannel *channel;
	guint watch_id;

	/* rfkill-input inhibitor */
	gboolean noinput;
	int noinput_fd;

	/* Pending Bluetooth enablement.
	 * If (@change_all_timeout_id != 0), then (task != NULL). The converse
	 * does not necessarily hold. */
	guint change_all_timeout_id;
	GTask *task;
};

G_DEFINE_TYPE (CcRfkillGlib, cc_rfkill_glib, G_TYPE_OBJECT)

#define CHANGE_ALL_TIMEOUT 500

static const char *type_to_string (unsigned int type);

static void
cancel_current_task (CcRfkillGlib *rfkill)
{
	if (rfkill->task != NULL) {
		g_cancellable_cancel (g_task_get_cancellable (rfkill->task));
		g_clear_object (&rfkill->task);
	}

	if (rfkill->change_all_timeout_id != 0) {
		g_source_remove (rfkill->change_all_timeout_id);
		rfkill->change_all_timeout_id = 0;
	}
}

/* Note that this can return %FALSE without setting @error. */
gboolean
cc_rfkill_glib_send_change_all_event_finish (CcRfkillGlib        *rfkill,
					     GAsyncResult        *res,
					     GError             **error)
{
	g_return_val_if_fail (CC_RFKILL_IS_GLIB (rfkill), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, rfkill), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (res, cc_rfkill_glib_send_change_all_event), FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

static void
write_change_all_again_done_cb (GObject      *source_object,
				GAsyncResult *res,
				gpointer      user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	CcRfkillGlib *rfkill = g_task_get_source_object (task);
	g_autoptr(GError) error = NULL;
	gssize ret;

	g_debug ("Finished writing second RFKILL_OP_CHANGE_ALL event");

	ret = g_output_stream_write_finish (G_OUTPUT_STREAM (source_object), res, &error);
	if (ret < 0)
		g_task_return_error (task, g_steal_pointer (&error));
	else
		g_task_return_boolean (task, ret >= 0);

	/* If this @task has been cancelled, it may have been superceded. */
	if (rfkill->task == task)
		g_clear_object (&rfkill->task);
}

static gboolean
write_change_all_timeout_cb (CcRfkillGlib *rfkill)
{
	struct rfkill_event *event;

	g_assert (rfkill->task != NULL);

	g_debug ("Sending second RFKILL_OP_CHANGE_ALL timed out");

	event = g_task_get_task_data (rfkill->task);
	g_task_return_new_error (rfkill->task,
				 G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
				 "Enabling rfkill for %s timed out",
				 type_to_string (event->type));

	g_clear_object (&rfkill->task);
	rfkill->change_all_timeout_id = 0;

	return G_SOURCE_REMOVE;
}

static void
write_change_all_done_cb (GObject      *source_object,
			  GAsyncResult *res,
			  gpointer      user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	CcRfkillGlib *rfkill = g_task_get_source_object (task);
	g_autoptr(GError) error = NULL;
	gssize ret;
	struct rfkill_event *event;

	g_debug ("Sending original RFKILL_OP_CHANGE_ALL event done");

	event = g_task_get_task_data (task);
	ret = g_output_stream_write_finish (G_OUTPUT_STREAM (source_object), res, &error);
	if (ret < 0) {
		g_task_return_error (task, g_steal_pointer (&error));
		goto bail;
	} else if (event->soft == 1 ||
		   event->type != RFKILL_TYPE_BLUETOOTH) {
		g_task_return_boolean (task, ret >= 0);
		goto bail;
	}

	g_assert (rfkill->change_all_timeout_id == 0);
	rfkill->change_all_timeout_id = g_timeout_add (CHANGE_ALL_TIMEOUT,
						       (GSourceFunc) write_change_all_timeout_cb,
						       rfkill);

	return;

bail:
	/* If this @task has been cancelled, it may have been superceded. */
	if (rfkill->task == task)
		g_clear_object (&rfkill->task);
}

void
cc_rfkill_glib_send_change_all_event (CcRfkillGlib        *rfkill,
				      guint                rfkill_type,
				      gboolean             enable,
				      GCancellable        *cancellable,
				      GAsyncReadyCallback  callback,
				      gpointer             user_data)
{
	g_autoptr(GTask) task = NULL;
	struct rfkill_event *event;
	g_autoptr(GCancellable) task_cancellable = NULL;

	g_return_if_fail (CC_RFKILL_IS_GLIB (rfkill));
	g_return_if_fail (rfkill->stream);

	task_cancellable = g_cancellable_new ();
	g_signal_connect_object (cancellable, "cancelled",
				 (GCallback) g_cancellable_cancel,
				 task_cancellable,
				 G_CONNECT_SWAPPED);
	/* Now check if it is cancelled already */
	if (g_cancellable_is_cancelled (cancellable))
		g_cancellable_cancel (task_cancellable);

	task = g_task_new (rfkill, task_cancellable, callback, user_data);
	g_task_set_source_tag (task, cc_rfkill_glib_send_change_all_event);

	/* Clear any previous task. */
	cancel_current_task (rfkill);
	g_assert (rfkill->task == NULL);

	/* Start writing out a new event. */
	event = g_new0 (struct rfkill_event, 1);
	event->op = RFKILL_OP_CHANGE_ALL;
	event->type = rfkill_type;
	event->soft = enable ? 1 : 0;

	g_task_set_task_data (task, event, g_free);
	rfkill->task = g_object_ref (task);
	rfkill->change_all_timeout_id = 0;

	g_output_stream_write_async (rfkill->stream,
				     event, sizeof(struct rfkill_event),
				     G_PRIORITY_DEFAULT,
				     task_cancellable, write_change_all_done_cb,
				     g_object_ref (task));
}

static const char *
type_to_string (unsigned int type)
{
	switch (type) {
	case RFKILL_TYPE_ALL:
		return "ALL";
	case RFKILL_TYPE_WLAN:
		return "WLAN";
	case RFKILL_TYPE_BLUETOOTH:
		return "BLUETOOTH";
	case RFKILL_TYPE_UWB:
		return "UWB";
	case RFKILL_TYPE_WIMAX:
		return "WIMAX";
	case RFKILL_TYPE_WWAN:
		return "WWAN";
	default:
		return "UNKNOWN";
	}
}

static const char *
op_to_string (unsigned int op)
{
	switch (op) {
	case RFKILL_OP_ADD:
		return "ADD";
	case RFKILL_OP_DEL:
		return "DEL";
	case RFKILL_OP_CHANGE:
		return "CHANGE";
	case RFKILL_OP_CHANGE_ALL:
		return "CHANGE_ALL";
	default:
		g_assert_not_reached ();
	}
}

static void
print_event (struct rfkill_event *event)
{
	g_debug ("RFKILL event: idx %u type %u (%s) op %u (%s) soft %u hard %u",
		 event->idx,
		 event->type, type_to_string (event->type),
		 event->op, op_to_string (event->op),
		 event->soft, event->hard);
}

static gboolean
got_change_event (GList *events)
{
	GList *l;

	g_assert (events != NULL);

	for (l = events ; l != NULL; l = l->next) {
		struct rfkill_event *event = l->data;

		if (event->op == RFKILL_OP_CHANGE)
			return TRUE;
	}

	return FALSE;
}

static void
emit_changed_signal_and_free (CcRfkillGlib *rfkill,
			      GList        *events)
{
	if (events == NULL)
		return;

	g_signal_emit (G_OBJECT (rfkill),
		       signals[CHANGED],
		       0, events);

	if (rfkill->change_all_timeout_id > 0 &&
	    got_change_event (events)) {
		struct rfkill_event *event;

		g_debug ("Received a change event after a RFKILL_OP_CHANGE_ALL event, re-sending RFKILL_OP_CHANGE_ALL");

		event = g_task_get_task_data (rfkill->task);
		g_output_stream_write_async (rfkill->stream,
					     event, sizeof(struct rfkill_event),
					     G_PRIORITY_DEFAULT,
					     g_task_get_cancellable (rfkill->task),
					     write_change_all_again_done_cb,
					     g_object_ref (rfkill->task));

		g_source_remove (rfkill->change_all_timeout_id);
		rfkill->change_all_timeout_id = 0;
	}

	g_list_free_full (events, g_free);
}

static gboolean
event_cb (GIOChannel   *source,
	  GIOCondition  condition,
	  CcRfkillGlib   *rfkill)
{
	GList *events;

	events = NULL;

	if (condition & G_IO_IN) {
		GIOStatus status;
		struct rfkill_event event = { 0 };
		gsize read;

		status = g_io_channel_read_chars (source,
						  (char *) &event,
						  sizeof(event),
						  &read,
						  NULL);

		while (status == G_IO_STATUS_NORMAL && read >= RFKILL_EVENT_SIZE_V1) {
			struct rfkill_event *event_ptr;

			print_event (&event);

			event_ptr = g_memdup2 (&event, sizeof (event));
			events = g_list_prepend (events, event_ptr);

			status = g_io_channel_read_chars (source,
							  (char *) &event,
							  sizeof(event),
							  &read,
							  NULL);
		}
		events = g_list_reverse (events);
	} else {
		g_debug ("Something unexpected happened on rfkill fd");
		return FALSE;
	}

	emit_changed_signal_and_free (rfkill, events);

	return TRUE;
}

static void
cc_rfkill_glib_init (CcRfkillGlib *rfkill)
{
	rfkill->device_file = NULL;
	rfkill->noinput_fd = -1;
}

static gboolean
_cc_rfkill_glib_open (CcRfkillGlib  *rfkill,
                      GError       **error)
{
	int fd;
	int ret;

	g_return_val_if_fail (CC_RFKILL_IS_GLIB (rfkill), FALSE);
	g_return_val_if_fail (rfkill->stream == NULL, FALSE);
	g_assert (rfkill->device_file);

	fd = open (rfkill->device_file, O_RDWR);

	if (fd < 0) {
		g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (errno),
		                     "Could not open RFKILL control device, please verify your installation");
		return FALSE;
	}

	ret = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (ret < 0) {
		g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (errno),
		                     "Can't set RFKILL control device to non-blocking");
		close(fd);
		return FALSE;
	}

	/* Setup monitoring */
	rfkill->channel = g_io_channel_unix_new (fd);
	g_io_channel_set_encoding (rfkill->channel, NULL, NULL);
	g_io_channel_set_buffered (rfkill->channel, FALSE);
	rfkill->watch_id = g_io_add_watch (rfkill->channel,
					   G_IO_IN | G_IO_HUP | G_IO_ERR,
					   (GIOFunc) event_cb,
					   rfkill);

	/* Setup write stream */
	rfkill->stream = g_unix_output_stream_new (fd, TRUE);

	return TRUE;
}

static void
uevent_cb (GUdevClient *client,
           gchar       *action,
           GUdevDevice *device,
           gpointer     user_data)
{
	CcRfkillGlib  *rfkill = CC_RFKILL_GLIB (user_data);

	if (g_strcmp0 (action, "add") != 0)
		return;

	if (g_strcmp0 (g_udev_device_get_name (device), "rfkill") == 0) {
		g_autoptr(GError) error = NULL;

		g_debug ("Rfkill device has been created");

		if (g_udev_device_get_device_file (device)) {
			g_clear_pointer (&rfkill->device_file, g_free);
			rfkill->device_file = g_strdup (g_udev_device_get_device_file (device));
		} else {
			g_warning ("rfkill udev device does not have a device file!");
		}

		if (!_cc_rfkill_glib_open (rfkill, &error))
			g_warning ("Could not open rfkill device: %s", error->message);
		else
			g_debug ("Opened rfkill device after uevent");

		g_clear_object (&rfkill->udev);

		/* Sync rfkill input inhibition state*/
		cc_rfkill_glib_set_rfkill_input_inhibited (rfkill, rfkill->noinput);
	}
}

gboolean
cc_rfkill_glib_open (CcRfkillGlib  *rfkill,
                     GError       **error)
{
	const char * const subsystems[] = { "misc", NULL };
	GUdevDevice *device;

	rfkill->udev = g_udev_client_new (subsystems);
	g_debug ("Setting up uevent listener");
	g_signal_connect (rfkill->udev, "uevent", G_CALLBACK (uevent_cb), rfkill);

	/* Simulate uevent if device already exists. */
	device = g_udev_client_query_by_subsystem_and_name (rfkill->udev, "misc", "rfkill");
	if (device)
		uevent_cb (rfkill->udev, "add", device, rfkill);

	return TRUE;
}

#define RFKILL_INPUT_INHIBITED(rfkill) (rfkill->noinput_fd >= 0)

gboolean
cc_rfkill_glib_get_rfkill_input_inhibited (CcRfkillGlib        *rfkill)
{
	g_return_val_if_fail (CC_RFKILL_IS_GLIB (rfkill), FALSE);

	return rfkill->noinput;
}

void
cc_rfkill_glib_set_rfkill_input_inhibited (CcRfkillGlib *rfkill,
					   gboolean      inhibit)
{
	g_return_if_fail (CC_RFKILL_IS_GLIB (rfkill));

	/* Shortcut in case we don't have an rfkill device */
	if (!rfkill->stream) {
		if (rfkill->noinput == inhibit)
			return;

		rfkill->noinput = inhibit;
		g_object_notify (G_OBJECT (rfkill), "rfkill-input-inhibited");

		return;
	}

	if (!inhibit && RFKILL_INPUT_INHIBITED(rfkill)) {
		close (rfkill->noinput_fd);
		g_debug ("Closed rfkill noinput FD.");

		rfkill->noinput_fd = -1;
	}

	if (inhibit && !RFKILL_INPUT_INHIBITED(rfkill)) {
		int fd, res;
		/* Open write only as we don't want to do any IO to it ever. */
		fd = open (rfkill->device_file, O_WRONLY);
		if (fd < 0) {
			if (errno == EACCES)
				g_warning ("Could not open RFKILL control device, please verify your installation");
			else
				g_debug ("Could not open RFKILL control device: %s", g_strerror (errno));
			return;
		}

		res = ioctl (fd, RFKILL_IOCTL_NOINPUT, (long) 0);
		if (res != 0) {
			g_warning ("Could not disable kernel handling of RFKILL related keys: %s", g_strerror (errno));
			close (fd);
			return;
		}

		g_debug ("Opened rfkill-input inhibitor.");

		rfkill->noinput_fd = fd;
	}

	if (rfkill->noinput != RFKILL_INPUT_INHIBITED(rfkill)) {
		rfkill->noinput = RFKILL_INPUT_INHIBITED(rfkill);
		g_object_notify (G_OBJECT (rfkill), "rfkill-input-inhibited");
	}
}

static void
cc_rfkill_glib_set_property (GObject      *object,
			 guint	       prop_id,
			 const GValue *value,
			 GParamSpec   *pspec)
{
	CcRfkillGlib *rfkill = CC_RFKILL_GLIB (object);

	switch (prop_id) {
	case PROP_RFKILL_INPUT_INHIBITED:
		cc_rfkill_glib_set_rfkill_input_inhibited (rfkill, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
cc_rfkill_glib_get_property (GObject    *object,
			 guint	     prop_id,
			 GValue	    *value,
			 GParamSpec *pspec)
{
	CcRfkillGlib *rfkill = CC_RFKILL_GLIB (object);

	switch (prop_id) {
	case PROP_RFKILL_INPUT_INHIBITED:
		g_value_set_boolean (value, rfkill->noinput);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
cc_rfkill_glib_finalize (GObject *object)
{
	CcRfkillGlib *rfkill = CC_RFKILL_GLIB (object);

	cancel_current_task (rfkill);

	/* cleanup monitoring */
	if (rfkill->watch_id > 0) {
		g_source_remove (rfkill->watch_id);
		rfkill->watch_id = 0;
		g_io_channel_shutdown (rfkill->channel, FALSE, NULL);
		g_io_channel_unref (rfkill->channel);
	}
	g_clear_object (&rfkill->stream);

	if (RFKILL_INPUT_INHIBITED(rfkill)) {
		close (rfkill->noinput_fd);
		rfkill->noinput_fd = -1;
	}

	g_clear_pointer (&rfkill->device_file, g_free);
	g_clear_object (&rfkill->udev);

	G_OBJECT_CLASS(cc_rfkill_glib_parent_class)->finalize(object);
}

static void
cc_rfkill_glib_class_init(CcRfkillGlibClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;

	object_class->set_property = cc_rfkill_glib_set_property;
	object_class->get_property = cc_rfkill_glib_get_property;
	object_class->finalize = cc_rfkill_glib_finalize;

	g_object_class_install_property (object_class,
					 PROP_RFKILL_INPUT_INHIBITED,
					 g_param_spec_boolean ("rfkill-input-inhibited",
							       "Rfkill input inhibited",
							       "Whether to prevent the kernel from handling RFKILL related key events.",
							       FALSE,
							       G_PARAM_READWRITE));

	signals[CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

}

CcRfkillGlib *
cc_rfkill_glib_new (void)
{
	return CC_RFKILL_GLIB (g_object_new (CC_RFKILL_TYPE_GLIB, NULL));
}

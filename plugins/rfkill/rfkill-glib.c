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

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixoutputstream.h>

#include "rfkill-glib.h"

enum {
	CHANGED,
	LAST_SIGNAL
};

static int signals[LAST_SIGNAL] = { 0 };

struct _CcRfkillGlib {
	GObject parent;

	GOutputStream *stream;
	GIOChannel *channel;
	guint watch_id;

	/* Pending Bluetooth enablement */
	guint change_all_timeout_id;
	struct rfkill_event *event;
	GTask *task;
	GCancellable *cancellable;
};

G_DEFINE_TYPE (CcRfkillGlib, cc_rfkill_glib, G_TYPE_OBJECT)

#define CHANGE_ALL_TIMEOUT 500

static const char *type_to_string (unsigned int type);

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
	CcRfkillGlib *rfkill = user_data;
	g_autoptr(GError) error = NULL;
	gssize ret;

	g_debug ("Finished writing second RFKILL_OP_CHANGE_ALL event");

	ret = g_output_stream_write_finish (G_OUTPUT_STREAM (source_object), res, &error);
	if (ret < 0)
		g_task_return_error (rfkill->task, g_steal_pointer (&error));
	else
		g_task_return_boolean (rfkill->task, ret >= 0);

	g_clear_object (&rfkill->task);
	g_clear_pointer (&rfkill->event, g_free);
}

static gboolean
write_change_all_timeout_cb (CcRfkillGlib *rfkill)
{
	g_assert (rfkill->event);

	g_debug ("Sending second RFKILL_OP_CHANGE_ALL timed out");

	g_task_return_new_error (rfkill->task,
				 G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
				 "Enabling rfkill for %s timed out",
				 type_to_string (rfkill->event->type));

	g_clear_object (&rfkill->task);
	g_clear_pointer (&rfkill->event, g_free);
	g_clear_object (&rfkill->cancellable);
	rfkill->change_all_timeout_id = 0;

	return G_SOURCE_REMOVE;
}

static void
write_change_all_done_cb (GObject      *source_object,
			  GAsyncResult *res,
			  gpointer      user_data)
{
	CcRfkillGlib *rfkill = user_data;
	g_autoptr(GError) error = NULL;
	gssize ret;

	g_debug ("Sending original RFKILL_OP_CHANGE_ALL event done");

	ret = g_output_stream_write_finish (G_OUTPUT_STREAM (source_object), res, &error);
	if (ret < 0) {
		g_task_return_error (rfkill->task, g_steal_pointer (&error));
		goto bail;
	} else if (rfkill->event->soft == 1 ||
		   rfkill->event->type != RFKILL_TYPE_BLUETOOTH) {
		g_task_return_boolean (rfkill->task, ret >= 0);
		goto bail;
	}

	rfkill->change_all_timeout_id = g_timeout_add (CHANGE_ALL_TIMEOUT,
						       (GSourceFunc) write_change_all_timeout_cb,
						       rfkill);

	return;

bail:
	g_clear_object (&rfkill->task);
	g_clear_pointer (&rfkill->event, g_free);
	g_clear_object (&rfkill->cancellable);
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

	g_return_if_fail (CC_RFKILL_IS_GLIB (rfkill));
	g_return_if_fail (rfkill->stream);

	task = g_task_new (rfkill, cancellable, callback, user_data);
	g_task_set_source_tag (task, cc_rfkill_glib_send_change_all_event);

	/* Clear any previous task. */
	if (rfkill->change_all_timeout_id > 0) {
		g_source_remove (rfkill->change_all_timeout_id);
		rfkill->change_all_timeout_id = 0;
		write_change_all_timeout_cb (rfkill);
	}

	g_assert (rfkill->event == NULL);
	g_assert (rfkill->task == NULL);
	g_assert (rfkill->cancellable == NULL);

	/* Start writing out a new event. */
	event = g_new0 (struct rfkill_event, 1);
	event->op = RFKILL_OP_CHANGE_ALL;
	event->type = rfkill_type;
	event->soft = enable ? 1 : 0;

	rfkill->event = event;
	rfkill->task = g_object_ref (task);
	rfkill->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	rfkill->change_all_timeout_id = 0;

	g_output_stream_write_async (rfkill->stream,
				     event, sizeof(struct rfkill_event),
				     G_PRIORITY_DEFAULT,
				     cancellable, write_change_all_done_cb, rfkill);
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
		g_debug ("Received a change event after a RFKILL_OP_CHANGE_ALL event, re-sending RFKILL_OP_CHANGE_ALL");

		g_output_stream_write_async (rfkill->stream,
					     rfkill->event, sizeof(struct rfkill_event),
					     G_PRIORITY_DEFAULT,
					     rfkill->cancellable, write_change_all_again_done_cb, rfkill);

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
		struct rfkill_event event;
		gsize read;

		status = g_io_channel_read_chars (source,
						  (char *) &event,
						  sizeof(event),
						  &read,
						  NULL);

		while (status == G_IO_STATUS_NORMAL && read == sizeof(event)) {
			struct rfkill_event *event_ptr;

			print_event (&event);

			event_ptr = g_memdup (&event, sizeof(event));
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
}

int
cc_rfkill_glib_open (CcRfkillGlib *rfkill)
{
	int fd;
	int ret;
	GList *events;

	g_return_val_if_fail (CC_RFKILL_IS_GLIB (rfkill), -1);
	g_return_val_if_fail (rfkill->stream == NULL, -1);

	fd = open("/dev/rfkill", O_RDWR);
	if (fd < 0) {
		if (errno == EACCES)
			g_warning ("Could not open RFKILL control device, please verify your installation");
		return fd;
	}

	ret = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (ret < 0) {
		g_debug ("Can't set RFKILL control device to non-blocking");
		close(fd);
		return ret;
	}

	events = NULL;

	while (1) {
		struct rfkill_event event;
		struct rfkill_event *event_ptr;
		ssize_t len;

		len = read(fd, &event, sizeof(event));
		if (len < 0) {
			if (errno == EAGAIN)
				break;
			g_debug ("Reading of RFKILL events failed");
			break;
		}

		if (len != RFKILL_EVENT_SIZE_V1) {
			g_warning ("Wrong size of RFKILL event\n");
			continue;
		}

		if (event.op != RFKILL_OP_ADD)
			continue;

		g_debug ("Read killswitch of type '%s' (idx=%d): soft %d hard %d",
			 type_to_string (event.type),
			 event.idx, event.soft, event.hard);

		event_ptr = g_memdup (&event, sizeof(event));
		events = g_list_prepend (events, event_ptr);
	}

	/* Setup monitoring */
	rfkill->channel = g_io_channel_unix_new (fd);
	rfkill->watch_id = g_io_add_watch (rfkill->channel,
					   G_IO_IN | G_IO_HUP | G_IO_ERR,
					   (GIOFunc) event_cb,
					   rfkill);

	if (events) {
		events = g_list_reverse (events);
		emit_changed_signal_and_free (rfkill, events);
	} else {
		g_debug ("No rfkill device available on startup");
	}

	/* Setup write stream */
	rfkill->stream = g_unix_output_stream_new (fd, TRUE);

	return fd;
}

static void
cc_rfkill_glib_finalize (GObject *object)
{
	CcRfkillGlib *rfkill = CC_RFKILL_GLIB (object);

	if (rfkill->change_all_timeout_id > 0)
		write_change_all_timeout_cb (rfkill);

	/* cleanup monitoring */
	if (rfkill->watch_id > 0) {
		g_source_remove (rfkill->watch_id);
		rfkill->watch_id = 0;
		g_io_channel_shutdown (rfkill->channel, FALSE, NULL);
		g_io_channel_unref (rfkill->channel);
	}
	g_clear_object (&rfkill->stream);

	G_OBJECT_CLASS(cc_rfkill_glib_parent_class)->finalize(object);
}

static void
cc_rfkill_glib_class_init(CcRfkillGlibClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;

	object_class->finalize = cc_rfkill_glib_finalize;

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

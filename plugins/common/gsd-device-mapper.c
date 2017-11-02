/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Carlos Garnacho <carlosg@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <gtk/gtkx.h>
#include <X11/Xatom.h>

#if HAVE_WACOM
#include <libwacom/libwacom.h>
#endif

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-rr.h>
#undef GNOME_DESKTOP_USE_UNSTABLE_API

#include "gsd-device-manager.h"
#include "gsd-device-mapper.h"
#include "gsd-input-helper.h"
#include "gsd-enums.h"

typedef struct _GsdInputInfo GsdInputInfo;
typedef struct _GsdOutputInfo GsdOutputInfo;
typedef struct _MappingHelper MappingHelper;
typedef struct _DeviceMapHelper DeviceMapHelper;

#define KEY_DISPLAY  "display"
#define KEY_ROTATION "rotation"

typedef enum {
	GSD_INPUT_IS_SYSTEM_INTEGRATED = 1 << 0, /* eg. laptop tablets/touchscreens */
	GSD_INPUT_IS_SCREEN_INTEGRATED = 1 << 1, /* eg. Wacom Cintiq devices */
	GSD_INPUT_IS_TOUCH             = 1 << 2, /* touch device, either touchscreen or tablet */
	GSD_INPUT_IS_PEN               = 1 << 3, /* tablet pen */
	GSD_INPUT_IS_PAD               = 1 << 4, /* pad device, most usually in tablets */
	GSD_INPUT_IS_CURSOR            = 1 << 5  /* pointer-like device in tablets */
} GsdInputCapabilityFlags;

typedef enum {
	GSD_PRIO_BUILTIN,            /* Output is builtin, applies mainly to system-integrated devices */
	GSD_PRIO_MATCH_SIZE,	     /* Size from input device and output match */
	GSD_PRIO_EDID_MATCH_FULL,    /* Full EDID model match, eg. "Cintiq 12WX" */
	GSD_PRIO_EDID_MATCH_PARTIAL, /* Partial EDID model match, eg. "Cintiq" */
	GSD_PRIO_EDID_MATCH_VENDOR,  /* EDID vendor match, eg. "WAC" for Wacom */
	N_OUTPUT_PRIORITIES
} GsdOutputPriority;

struct _GsdInputInfo {
	GsdDevice *device;
	GSettings *settings;
	GsdDeviceMapper *mapper;
	GsdOutputInfo *output;
	GsdOutputInfo *guessed_output;
	guint changed_id;
	GsdInputCapabilityFlags capabilities;
};

struct _GsdOutputInfo {
	GnomeRROutput *output;
	GList *input_devices;
};

struct _DeviceMapHelper {
	GsdInputInfo *input;
	GnomeRROutput *candidates[N_OUTPUT_PRIORITIES];
	GsdOutputPriority highest_prio;
	guint n_candidates;
};

struct _MappingHelper {
	GArray *device_maps;
};

struct _GsdDeviceMapper {
	GObject parent_instance;
	GdkScreen *screen;
	GnomeRRScreen *rr_screen;
	GHashTable *input_devices; /* GsdDevice -> GsdInputInfo */
	GHashTable *output_devices; /* GnomeRROutput -> GsdOutputInfo */
#if HAVE_WACOM
	WacomDeviceDatabase *wacom_db;
#endif
};

struct _GsdDeviceMapperClass {
	GObjectClass parent_class;
};

enum {
	PROP_0,
	PROP_SCREEN
};

enum {
	DEVICE_CHANGED,
	N_SIGNALS
};

static GnomeRROutput * input_info_find_size_match (GsdInputInfo  *input,
                                                   GnomeRRScreen *rr_screen);
static void            input_info_update_settings_output (GsdInputInfo *info);


static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE (GsdDeviceMapper, gsd_device_mapper, G_TYPE_OBJECT)

/* Finds an output which matches the given EDID information. Any NULL
 * parameter will be interpreted to match any value. */
static GnomeRROutput *
find_output_by_edid (GnomeRRScreen *rr_screen,
		     const gchar   *edid[3])
{
	GnomeRROutput **outputs;
	GnomeRROutput *retval = NULL;
	guint i;

	g_return_val_if_fail (rr_screen != NULL, NULL);

	outputs = gnome_rr_screen_list_outputs (rr_screen);

	for (i = 0; outputs[i] != NULL; i++) {
		gchar *vendor, *product, *serial;
		gboolean match;

		gnome_rr_output_get_ids_from_edid (outputs[i], &vendor,
						   &product, &serial);

		g_debug ("Checking for match between ['%s','%s','%s'] and ['%s','%s','%s']", \
			 edid[0], edid[1], edid[2], vendor, product, serial);

		match = (edid[0] == NULL || g_strcmp0 (edid[0], vendor)	 == 0) && \
			(edid[1] == NULL || g_strcmp0 (edid[1], product) == 0) && \
			(edid[2] == NULL || g_strcmp0 (edid[2], serial)	 == 0);

		g_free (vendor);
		g_free (product);
		g_free (serial);

		if (match) {
			retval = outputs[i];
			break;
		}
	}

	if (retval == NULL)
		g_debug ("Did not find a matching output for EDID '%s,%s,%s'",
			 edid[0], edid[1], edid[2]);
	return retval;
}

static GnomeRROutput *
find_builtin_output (GnomeRRScreen *rr_screen)
{
	GnomeRROutput **outputs;
	guint i;

	g_return_val_if_fail (rr_screen != NULL, NULL);

	outputs = gnome_rr_screen_list_outputs (rr_screen);

	for (i = 0; outputs[i] != NULL; i++) {
		if (!gnome_rr_output_is_builtin_display (outputs[i]))
			continue;

		return outputs[i];
	}

	g_debug ("Did not find a built-in monitor");
	return NULL;
}

static MappingHelper *
mapping_helper_new (void)
{
	MappingHelper *helper;

	helper = g_new0 (MappingHelper, 1);
	helper->device_maps = g_array_new (FALSE, FALSE, sizeof (DeviceMapHelper));

	return helper;
}

static void
mapping_helper_free (MappingHelper *helper)
{
	g_array_unref (helper->device_maps);
	g_free (helper);
}

static void
mapping_helper_add (MappingHelper *helper,
		    GsdInputInfo  *input,
		    GnomeRROutput *outputs[N_OUTPUT_PRIORITIES])
{
	guint i, pos, highest = N_OUTPUT_PRIORITIES;
	DeviceMapHelper info = { 0 }, *prev;

	info.input = input;

	for (i = 0; i < N_OUTPUT_PRIORITIES; i++) {
		if (outputs[i] == NULL)
			continue;

		if (highest > i)
			highest = i;

		info.candidates[i] = outputs[i];
		info.n_candidates++;
	}

	info.highest_prio = highest;
	pos = helper->device_maps->len;

	for (i = 0; i < helper->device_maps->len; i++) {
		prev = &g_array_index (helper->device_maps, DeviceMapHelper, i);

		if (prev->highest_prio < info.highest_prio)
			pos = i;
	}

	if (pos >= helper->device_maps->len)
		g_array_append_val (helper->device_maps, info);
	else
		g_array_insert_val (helper->device_maps, pos, info);
}

/* This function gets a map of outputs, sorted by confidence, for a given device,
 * the array can actually contain NULLs if no output matched a priority. */
static void
input_info_guess_candidates (GsdInputInfo  *input,
			     GnomeRROutput *outputs[N_OUTPUT_PRIORITIES])
{
	gboolean found = FALSE;
	const gchar *name;
	gchar **split;
	gint i;

	name = gsd_device_get_name (input->device);

	if (input->capabilities & GSD_INPUT_IS_SCREEN_INTEGRATED) {
		outputs[GSD_PRIO_MATCH_SIZE] = input_info_find_size_match (input, input->mapper->rr_screen);
		found |= outputs[GSD_PRIO_MATCH_SIZE] != NULL;
	}

	split = g_strsplit (name, " ", -1);

	/* On Wacom devices that are integrated on a not-in-system screen (eg. Cintiqs),
	 * there is usually a minimal relation between the input device name and the EDID
	 * vendor/model fields. Attempt to find matching outputs and fill in the map
	 * from GSD_PRIO_EDID_MATCH_FULL to GSD_PRIO_EDID_MATCH_VENDOR.
	 */
	if (input->capabilities & GSD_INPUT_IS_SCREEN_INTEGRATED &&
	    g_str_has_prefix (name, "Wacom ")) {
		gchar *product = g_strdup_printf ("%s %s", split[1], split[2]);
		const gchar *edids[][3] = {
			{ "WAC", product, NULL },
			{ "WAC", split[1], NULL },
			{ "WAC", NULL, NULL }
		};

		for (i = 0; i < G_N_ELEMENTS (edids); i++) {
			/* i + 1 matches the desired priority, we skip GSD_PRIO_BUILTIN here */
			outputs[i + GSD_PRIO_EDID_MATCH_FULL] =
				find_output_by_edid (input->mapper->rr_screen,
						     edids[i]);
			found |= outputs[i + GSD_PRIO_EDID_MATCH_FULL] != NULL;
		}

		g_free (product);
	}

	/* For input devices that we certainly know that are system-integrated, or
	 * for screen-integrated devices we weren't able to find an output for,
	 * find the builtin screen.
	 */
	if ((input->capabilities & GSD_INPUT_IS_SYSTEM_INTEGRATED) ||
	    (!found && input->capabilities & GSD_INPUT_IS_SCREEN_INTEGRATED)) {
		outputs[GSD_PRIO_BUILTIN] =
			find_builtin_output (input->mapper->rr_screen);
	}

	g_strfreev (split);
}

static gboolean
output_has_input_type (GsdOutputInfo *info,
		       guint	      capabilities)
{
	GList *devices;

	for (devices = info->input_devices; devices; devices = devices->next) {
		GsdInputInfo *input = devices->data;

		if (input->capabilities == capabilities)
			return TRUE;
	}

	return FALSE;
}

static GnomeRROutput *
settings_get_display (GSettings	      *settings,
		      GsdDeviceMapper *mapper)
{
	GnomeRROutput *output = NULL;
	gchar **edid;
	guint nvalues;

	edid = g_settings_get_strv (settings, KEY_DISPLAY);
	nvalues = g_strv_length (edid);

	if (nvalues == 3) {
		output = find_output_by_edid (mapper->rr_screen, (const gchar **) edid);
	} else {
		g_warning ("Unable to get display property. Got %d items, "
			   "expected %d items.\n", nvalues, 3);
	}

	g_strfreev (edid);

	return output;
}

static void
settings_set_display (GSettings	    *settings,
		      GnomeRROutput *output)
{
	gchar **prev, *edid[4] = { NULL, NULL, NULL, NULL };
	GVariant *value;
	gsize nvalues;

	if (output) {
		prev = g_settings_get_strv (settings, KEY_DISPLAY);
		nvalues = g_strv_length (prev);

		gnome_rr_output_get_ids_from_edid (output, &edid[0],
						   &edid[1], &edid[2]);

		if (nvalues != 3 ||
		    strcmp (prev[0], edid[0]) != 0 ||
		    strcmp (prev[1], edid[1]) != 0 ||
		    strcmp (prev[2], edid[2]) != 0) {
			value = g_variant_new_strv ((const gchar * const *) &edid, 3);
			g_settings_set_value (settings, KEY_DISPLAY, value);
		}

		g_free (edid[0]);
		g_free (edid[1]);
		g_free (edid[2]);
		g_strfreev (prev);
	} else {
		g_settings_reset (settings, KEY_DISPLAY);
	}
}

static void
input_info_set_output (GsdInputInfo  *input,
		       GsdOutputInfo *output,
		       gboolean	      guessed,
		       gboolean	      save)
{
	GnomeRROutput *rr_output = NULL;
	GsdOutputInfo **ptr;

	if (guessed) {
		/* If there is already a non-guessed input, go for it */
		if (input->output)
			return;

		ptr = &input->guessed_output;
	} else {
		/* Unset guessed output */
		if (input->guessed_output)
			input_info_set_output (input, NULL, TRUE, FALSE);
		ptr = &input->output;
	}

	if (*ptr == output)
		return;

	if (*ptr) {
		(*ptr)->input_devices = g_list_remove ((*ptr)->input_devices,
						       input);
	}

	if (output) {
		output->input_devices = g_list_prepend (output->input_devices,
							input);
		rr_output = output->output;
	}

	if (input->settings && !guessed && save)
		settings_set_display (input->settings, rr_output);

	*ptr = output;
}

static GsdOutputInfo *
input_info_get_output (GsdInputInfo *input)
{
	if (!input)
		return NULL;

	if (input->output)
		return input->output;

	if (input->guessed_output)
		return input->guessed_output;

	if (input->mapper->output_devices &&
	    g_hash_table_size (input->mapper->output_devices) == 1) {
		GsdOutputInfo *output;
		GHashTableIter iter;

		g_hash_table_iter_init (&iter, input->mapper->output_devices);
		g_hash_table_iter_next (&iter, NULL, (gpointer *) &output);

		return output;
	}

	return NULL;
}

static gint
monitor_for_output (GnomeRROutput *output)
{
	GdkScreen *screen = gdk_screen_get_default ();
	GnomeRRCrtc *crtc = gnome_rr_output_get_crtc (output);
	gint x, y;

	if (!crtc)
		return -1;

	gnome_rr_crtc_get_position (crtc, &x, &y);

	return gdk_screen_get_monitor_at_point (screen, x, y);
}

static gboolean
output_get_dimensions (GnomeRROutput *output,
		       guint         *width,
		       guint         *height)
{
	GdkScreen *screen = gdk_screen_get_default ();
	gint monitor_num;

	monitor_num = monitor_for_output (output);

	if (monitor_num < 0)
		return FALSE;

	*width = gdk_screen_get_monitor_width_mm (screen, monitor_num);
	*height = gdk_screen_get_monitor_height_mm (screen, monitor_num);
	return TRUE;
}

static GnomeRROutput *
input_info_find_size_match (GsdInputInfo  *input,
			    GnomeRRScreen *rr_screen)
{
	guint i, input_width, input_height, output_width, output_height;
	gdouble min_width_diff, min_height_diff;
	GnomeRROutput **outputs, *match = NULL;

	g_return_val_if_fail (rr_screen != NULL, NULL);

	if (!gsd_device_get_dimensions (input->device,
					&input_width, &input_height))
		return NULL;

	/* Restrict the matches to be below a narrow percentage */
	min_width_diff = min_height_diff = 0.05;

	g_debug ("Input device '%s' has %dx%d mm",
		 gsd_device_get_name (input->device), input_width, input_height);

	outputs = gnome_rr_screen_list_outputs (rr_screen);

	for (i = 0; outputs[i] != NULL; i++) {
		gdouble width_diff, height_diff;

		if (!output_get_dimensions (outputs[i], &output_width, &output_height))
			continue;

		width_diff = ABS (1 - ((gdouble) output_width / input_width));
		height_diff = ABS (1 - ((gdouble) output_height / input_height));

		g_debug ("Output '%s' has size %dx%d mm, deviation from "
			 "input device size: %.2f width, %.2f height ",
			 gnome_rr_output_get_name (outputs[i]),
			 output_width, output_height, width_diff, height_diff);

		if (width_diff <= min_width_diff && height_diff <= min_height_diff) {
			match = outputs[i];
			min_width_diff = width_diff;
			min_height_diff = height_diff;
		}
	}

	if (match) {
		g_debug ("Output '%s' is considered a best size match (%.2f / %.2f)",
			 gnome_rr_output_get_name (match),
			 min_width_diff, min_height_diff);
	} else {
		g_debug ("No input/output size match was found\n");
	}

	return match;
}

static void
input_info_remap (GsdInputInfo *input)
{
	GsdOutputInfo *output;

	if (input->capabilities & GSD_INPUT_IS_PAD)
		return;
	if ((input->capabilities &
	    (GSD_INPUT_IS_SCREEN_INTEGRATED | GSD_INPUT_IS_SYSTEM_INTEGRATED)) == 0)
		return;

	output = input_info_get_output (input);
	settings_set_display (gsd_device_get_settings (input->device),
			      output ? output->output : NULL);
	g_signal_emit (input->mapper, signals[DEVICE_CHANGED], 0, input->device);
}

static void
mapper_apply_helper_info (GsdDeviceMapper *mapper,
			  MappingHelper	  *helper)
{
	guint i, j;

	/* Now, decide which input claims which output */
	for (i = 0; i < helper->device_maps->len; i++) {
		GsdOutputInfo *last = NULL, *output = NULL;
		DeviceMapHelper *info;

		info = &g_array_index (helper->device_maps, DeviceMapHelper, i);

		for (j = 0; j < N_OUTPUT_PRIORITIES; j++) {
			if (!info->candidates[j])
				continue;

			output = g_hash_table_lookup (mapper->output_devices,
						      info->candidates[j]);

			if (!output)
				continue;

			last = output;

			if ((info->input->capabilities &
			     (GSD_INPUT_IS_SYSTEM_INTEGRATED | GSD_INPUT_IS_SCREEN_INTEGRATED))) {
				/* A single output is hardly going to have multiple integrated input
				 * devices with the same capabilities, so punt to any next output.
				 */
				if (output_has_input_type (output, info->input->capabilities))
					continue;
			}

			input_info_set_output (info->input, output, TRUE, FALSE);
			break;
		}

		/* Assign the last candidate if we came up empty */
		if (!info->input->guessed_output && last)
			input_info_set_output (info->input, last, TRUE, FALSE);

		input_info_remap (info->input);
	}
}

static void
mapper_recalculate_candidates (GsdDeviceMapper *mapper)
{
	MappingHelper *helper;
	GHashTableIter iter;
	GsdInputInfo *input;

	helper = mapping_helper_new ();
	g_hash_table_iter_init (&iter, mapper->input_devices);

	while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &input)) {
		GnomeRROutput *outputs[N_OUTPUT_PRIORITIES] = { 0 };

		input_info_update_settings_output (input);

		/* Avoid opaque device with an output from settings and
		 * system-integrated devices that won't get remapped anyway
		 */
		if (input->output &&
                    (input->capabilities & GSD_INPUT_IS_SCREEN_INTEGRATED) == 0)
			continue;

		/* reset the current output */
		input_info_set_output (input, NULL, FALSE, FALSE);
		input_info_guess_candidates (input, outputs);
		mapping_helper_add (helper, input, outputs);
	}

	mapper_apply_helper_info (mapper, helper);
	mapping_helper_free (helper);
}

static void
mapper_recalculate_input (GsdDeviceMapper *mapper,
			  GsdInputInfo	  *input)
{
	GnomeRROutput *outputs[N_OUTPUT_PRIORITIES] = { 0 };
	MappingHelper *helper;

	/* Device has an output from settings */
	if (input->output)
		return;

	helper = mapping_helper_new ();
	input_info_guess_candidates (input, outputs);
	mapping_helper_add (helper, input, outputs);

	mapper_apply_helper_info (mapper, helper);
	mapping_helper_free (helper);
}

static void
input_info_update_capabilities (GsdInputInfo *info)
{
	GsdDeviceType type;

#if HAVE_WACOM
	WacomDevice *wacom_device;
	const gchar *devpath;

	info->capabilities = 0;
	devpath = gsd_device_get_device_file (info->device);
	wacom_device = libwacom_new_from_path (info->mapper->wacom_db, devpath,
					       WFALLBACK_GENERIC, NULL);

	if (wacom_device) {
		WacomIntegrationFlags integration_flags;

		integration_flags = libwacom_get_integration_flags (wacom_device);

		if (integration_flags & WACOM_DEVICE_INTEGRATED_DISPLAY)
			info->capabilities |= GSD_INPUT_IS_SCREEN_INTEGRATED;

		if (integration_flags & WACOM_DEVICE_INTEGRATED_SYSTEM)
			info->capabilities |= GSD_INPUT_IS_SYSTEM_INTEGRATED;

		libwacom_destroy (wacom_device);
	}
#else
	info->capabilities = 0;
#endif /* HAVE_WACOM */

	/* Fallback to GsdDeviceType */
	type = gsd_device_get_device_type (info->device);

	if (type & GSD_DEVICE_TYPE_TOUCHSCREEN)
		info->capabilities |= GSD_INPUT_IS_TOUCH | GSD_INPUT_IS_SCREEN_INTEGRATED;
	else if (type & GSD_DEVICE_TYPE_TABLET)
		info->capabilities |= GSD_INPUT_IS_PEN;
}

static void
input_info_update_settings_output (GsdInputInfo *info)
{
	GsdOutputInfo *output = NULL;
	GnomeRROutput *rr_output;

	if (!info->settings || !info->mapper->rr_screen)
		return;

	if ((info->capabilities &
	    (GSD_INPUT_IS_SCREEN_INTEGRATED | GSD_INPUT_IS_SYSTEM_INTEGRATED)) == 0)
		return;

	rr_output = settings_get_display (info->settings, info->mapper);

	if (rr_output)
		output = g_hash_table_lookup (info->mapper->output_devices,
					      rr_output);

	if (output == info->output)
		return;

	if (output) {
		input_info_set_output (info, output, FALSE, FALSE);
		input_info_remap (info);
	} else {
		/* Guess an output for this device */
		input_info_set_output (info, NULL, FALSE, FALSE);
		mapper_recalculate_input (info->mapper, info);
	}
}

static void
device_settings_changed_cb (GSettings	 *settings,
			    gchar	 *key,
			    GsdInputInfo *input)
{
	if (g_str_equal (key, KEY_DISPLAY)) {
		input_info_update_settings_output (input);
	} else if (g_str_equal (key, KEY_ROTATION)) {
		/* Remap the device so the new rotation is applied */
		input_info_remap (input);
	}
}

static GsdInputInfo *
input_info_new (GsdDevice	*device,
		GsdDeviceMapper *mapper)
{
	GsdInputInfo *info;

	info = g_new0 (GsdInputInfo, 1);
	info->device = device;
	info->settings = gsd_device_get_settings (device);;
	info->mapper = mapper;

	if (info->settings) {
		info->changed_id = g_signal_connect (info->settings, "changed",
						     G_CALLBACK (device_settings_changed_cb),
						     info);
	}

	input_info_update_capabilities (info);
	input_info_update_settings_output (info);

	return info;
}

static void
input_info_free (GsdInputInfo *info)
{
	input_info_set_output (info, NULL, FALSE, FALSE);
	input_info_set_output (info, NULL, TRUE, FALSE);

	if (info->settings && info->changed_id)
		g_signal_handler_disconnect (info->settings, info->changed_id);

	if (info->settings)
		g_object_unref (info->settings);

	g_free (info);
}

static GsdOutputInfo *
output_info_new (GnomeRROutput *output)
{
	GsdOutputInfo *info;

	info = g_new0 (GsdOutputInfo, 1);
	info->output = output;

	return info;
}

static void
output_info_free (GsdOutputInfo *info)
{
	while (info->input_devices) {
		GsdInputInfo *input = info->input_devices->data;

		if (input->output == info)
			input_info_set_output (input, NULL, FALSE, FALSE);
		if (input->guessed_output == info)
			input_info_set_output (input, NULL, TRUE, FALSE);

		info->input_devices = g_list_remove (info->input_devices,
						     info->input_devices);
	}

	g_free (info);
}

static void
gsd_device_mapper_finalize (GObject *object)
{
	GsdDeviceMapper *mapper = GSD_DEVICE_MAPPER (object);

	g_hash_table_unref (mapper->input_devices);

	if (mapper->output_devices)
		g_hash_table_unref (mapper->output_devices);

#if HAVE_WACOM
	libwacom_database_destroy (mapper->wacom_db);
#endif

	G_OBJECT_CLASS (gsd_device_mapper_parent_class)->finalize (object);
}

static void
_device_mapper_update_outputs (GsdDeviceMapper *mapper)
{
	GnomeRROutput **outputs;
	GHashTable *map;
	gint i = 0;

	/* This *must* only be ever called after screen initialization */
	g_assert (mapper->rr_screen != NULL);

	map = g_hash_table_new_full (NULL, NULL, NULL,
				     (GDestroyNotify) output_info_free);
	outputs = gnome_rr_screen_list_outputs (mapper->rr_screen);

	while (outputs[i]) {
		GsdOutputInfo *info = NULL;

		if (mapper->output_devices) {
			info = g_hash_table_lookup (mapper->output_devices,
						    outputs[i]);

			if (info)
				g_hash_table_steal (mapper->output_devices,
						    outputs[i]);
		}

		if (!info)
			info = output_info_new (outputs[i]);

		g_hash_table_insert (map, outputs[i], info);
		i++;
	}

	if (mapper->output_devices)
		g_hash_table_unref (mapper->output_devices);

	mapper->output_devices = map;
	mapper_recalculate_candidates (mapper);
}

static void
screen_changed_cb (GnomeRRScreen   *rr_screen,
		   GsdDeviceMapper *mapper)
{
	_device_mapper_update_outputs (mapper);
}

static void
on_rr_screen_ready (GObject      *object,
		    GAsyncResult *result,
		    gpointer      user_data)
{
	GError *error;
	GsdDeviceMapper *mapper = user_data;

	error = NULL;
	mapper->rr_screen = gnome_rr_screen_new_finish (result, &error);

	if (!mapper->rr_screen) {
		g_warning ("Failed to construct RR screen: %s", error->message);
		g_error_free (error);
		return;
	}

	g_signal_connect (mapper->rr_screen, "changed",
			  G_CALLBACK (screen_changed_cb), mapper);
	_device_mapper_update_outputs (mapper);
}

static void
gsd_device_mapper_constructed (GObject *object)
{
	GsdDeviceMapper *mapper;

	mapper = GSD_DEVICE_MAPPER (object);
	gnome_rr_screen_new_async (mapper->screen, on_rr_screen_ready, mapper);
}

static void
gsd_device_mapper_set_property (GObject	     *object,
				guint	      param_id,
				const GValue *value,
				GParamSpec   *pspec)
{
	GsdDeviceMapper *mapper = GSD_DEVICE_MAPPER (object);

	switch (param_id) {
	case PROP_SCREEN:
		mapper->screen = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
	}
}

static void
gsd_device_mapper_get_property (GObject	   *object,
				guint	    param_id,
				GValue	   *value,
				GParamSpec *pspec)
{
	GsdDeviceMapper *mapper = GSD_DEVICE_MAPPER (object);

	switch (param_id) {
	case PROP_SCREEN:
		g_value_set_object (value, mapper->screen);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
	}
}

static void
gsd_device_mapper_class_init (GsdDeviceMapperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = gsd_device_mapper_set_property;
	object_class->get_property = gsd_device_mapper_get_property;
	object_class->finalize = gsd_device_mapper_finalize;
	object_class->constructed = gsd_device_mapper_constructed;

	g_object_class_install_property (object_class,
					 PROP_SCREEN,
					 g_param_spec_object ("screen",
							      "Screen",
							      "Screen",
							      GDK_TYPE_SCREEN,
							      G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_READWRITE));
	signals[DEVICE_CHANGED] =
		g_signal_new ("device-changed",
			      GSD_TYPE_DEVICE_MAPPER,
			      G_SIGNAL_RUN_LAST, 0,
			      NULL, NULL, NULL,
			      G_TYPE_NONE, 1, GSD_TYPE_DEVICE);
}

static void
gsd_device_mapper_init (GsdDeviceMapper *mapper)
{
	mapper->input_devices = g_hash_table_new_full (NULL, NULL, NULL,
						       (GDestroyNotify) input_info_free);
#if HAVE_WACOM
	mapper->wacom_db = libwacom_database_new ();
#endif
}

GsdDeviceMapper *
gsd_device_mapper_get (void)
{
	GsdDeviceMapper *mapper;
	GdkScreen *screen;

	screen = gdk_screen_get_default ();
	g_return_val_if_fail (screen != NULL, NULL);

	mapper = g_object_get_data (G_OBJECT (screen), "gsd-device-mapper-data");

	if (!mapper) {
		mapper = g_object_new (GSD_TYPE_DEVICE_MAPPER, "screen", screen, NULL);
		g_object_set_data_full (G_OBJECT (screen), "gsd-device-mapper-data",
                                        mapper, (GDestroyNotify) g_object_unref);
	}

	return mapper;
}

void
gsd_device_mapper_add_input (GsdDeviceMapper *mapper,
			     GsdDevice	     *device)
{
	GsdInputInfo *info;

	g_return_if_fail (mapper != NULL);
	g_return_if_fail (device != NULL);

	if (g_hash_table_contains (mapper->input_devices, device))
		return;

	info = input_info_new (device, mapper);
	g_hash_table_insert (mapper->input_devices, device, info);
}

void
gsd_device_mapper_remove_input (GsdDeviceMapper *mapper,
				GsdDevice	*device)
{
	g_return_if_fail (mapper != NULL);
	g_return_if_fail (device != NULL);

	g_hash_table_remove (mapper->input_devices, device);
}

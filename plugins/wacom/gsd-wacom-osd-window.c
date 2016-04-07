/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Olivier Fourdan <ofourdan@redhat.com>
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <cairo.h>
#include <librsvg/rsvg.h>

#include "gsd-wacom-osd-window.h"
#include "gsd-wacom-device.h"
#include "gsd-wacom-button-editor.h"
#include "gsd-enums.h"

#define ROTATION_KEY                "rotation"
#define ACTION_TYPE_KEY             "action-type"
#define CUSTOM_ACTION_KEY           "custom-action"
#define CUSTOM_ELEVATOR_ACTION_KEY  "custom-elevator-action"
#define RES_PATH                    "/org/gnome/settings-daemon/plugins/wacom/"

#define BACK_OPACITY		0.8
#define OPACITY_IN_EDITION	"0.5"
#define INACTIVE_COLOR		"#ededed"
#define ACTIVE_COLOR		"#729fcf"
#define STROKE_COLOR		"#000000"
#define DARK_COLOR		"#535353"
#define BACK_COLOR		"#000000"

#define BUTTON_HIGHLIGHT_DURATION     250 /* ms */
#define BUTTON_TRANSITION_DURATION    150 /* ms */
#define BUTTON_TIMER_STEP             25  /* ms */
#define BUTTON_FULL_TRANSITION        1.0 /* % */

#define CURSOR_HIDE_TIMEOUT	2 /* seconds */

#define CSS_NORMAL_BUTTON	\
	"%s.%s {\n"		\
	"    opacity: %s\n"	\
	"}\n"

#define CSS_EDITING_BUTTON		\
	"%s.%s {\n"			\
	"    stroke: %s !important;\n"	\
	"    fill: %s !important;\n"	\
	"}\n"

static struct {
	const gchar     *color_name;
	const gchar     *color_value;
} css_color_table[] = {
	{ "inactive_color", INACTIVE_COLOR },
	{ "active_color",   ACTIVE_COLOR   },
	{ "stroke_color",   STROKE_COLOR   },
	{ "dark_color",     DARK_COLOR     },
	{ "back_color",     BACK_COLOR     }
};

static gchar *
replace_string (gchar **string, const gchar *search, const char *replacement)
{
	GRegex *regex;
	gchar *res;

	g_return_val_if_fail (*string != NULL, NULL);
	g_return_val_if_fail (string != NULL, NULL);
	g_return_val_if_fail (search != NULL, *string);
	g_return_val_if_fail (replacement != NULL, *string);

	regex = g_regex_new (search, 0, 0, NULL);
	res = g_regex_replace_literal (regex, *string, -1, 0, replacement, 0, NULL);
	g_regex_unref (regex);
	/* The given string is freed and replaced by the resulting replacement */
	g_free (*string);
	*string = res;

	return res;
}

static gchar
get_last_char (gchar *string)
{
	size_t pos;

	g_return_val_if_fail (string != NULL, '\0');
	pos = strlen (string);
	g_return_val_if_fail (pos > 0, '\0');

	return string[pos - 1];
}

static double
get_rotation_in_radian (GsdWacomRotation rotation)
{
	switch (rotation) {
	case GSD_WACOM_ROTATION_NONE:
		return 0.0;
		break;
	case GSD_WACOM_ROTATION_HALF:
		return G_PI;
		break;
	/* We only support left-handed/right-handed */
	case GSD_WACOM_ROTATION_CCW:
	case GSD_WACOM_ROTATION_CW:
	default:
		break;
	}

	/* Fallback */
	return 0.0;
}

static gboolean
get_sub_location (RsvgHandle *handle,
                  const char *sub,
                  cairo_t    *cr,
                  double     *x,
                  double     *y)
{
	RsvgPositionData  position;
	double tx, ty;

	if (!rsvg_handle_get_position_sub (handle, &position, sub))
		return FALSE;

	tx = (double) position.x;
	ty = (double) position.y;
	cairo_user_to_device (cr, &tx, &ty);

	if (x)
		*x = tx;
	if (y)
		*y = ty;

	return TRUE;
}

static gboolean
get_image_size (const char *filename, int *width, int *height)
{
	RsvgHandle       *handle;
	RsvgDimensionData dimensions;
	GError* error = NULL;

	if (filename == NULL)
		return FALSE;

	handle = rsvg_handle_new_from_file (filename, &error);
	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		g_error_free (error);
	}
	if (handle == NULL)
		return FALSE;

	/* Compute image size */
	rsvg_handle_get_dimensions (handle, &dimensions);
	g_object_unref (handle);

	if (dimensions.width == 0 || dimensions.height == 0)
		return FALSE;

	if (width)
		*width = dimensions.width;

	if (height)
		*height = dimensions.height;

	return TRUE;
}

static int
get_pango_vertical_offset (PangoLayout *layout)
{
	const PangoFontDescription *desc;
	PangoContext               *context;
	PangoLanguage              *language;
	PangoFontMetrics           *metrics;
	int                         baseline;
	int                         strikethrough;
	int                         thickness;

	context = pango_layout_get_context (layout);
	language = pango_language_get_default ();
	desc = pango_layout_get_font_description (layout);
	metrics = pango_context_get_metrics (context, desc, language);

	baseline = pango_layout_get_baseline (layout);
	strikethrough =  pango_font_metrics_get_strikethrough_position (metrics);
	thickness =  pango_font_metrics_get_underline_thickness (metrics);

	return PANGO_PIXELS (baseline - strikethrough - thickness / 2);
}

#define GSD_TYPE_WACOM_OSD_BUTTON         (gsd_wacom_osd_button_get_type ())
#define GSD_WACOM_OSD_BUTTON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_WACOM_OSD_BUTTON, GsdWacomOSDButton))
#define GSD_WACOM_OSD_BUTTON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSD_TYPE_WACOM_OSD_BUTTON, GsdWacomOSDButtonClass))
#define GSD_IS_WACOM_OSD_BUTTON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_WACOM_OSD_BUTTON))
#define GSD_IS_WACOM_OSD_BUTTON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_WACOM_OSD_BUTTON))
#define GSD_WACOM_OSD_BUTTON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_WACOM_OSD_BUTTON, GsdWacomOSDButtonClass))

typedef struct GsdWacomOSDButtonPrivate GsdWacomOSDButtonPrivate;

typedef struct {
        GObject                   parent;
        GsdWacomOSDButtonPrivate *priv;
} GsdWacomOSDButton;

typedef struct {
        GObjectClass              parent_class;
} GsdWacomOSDButtonClass;

GType                     gsd_wacom_osd_button_get_type        (void) G_GNUC_CONST;

enum {
	PROP_OSD_BUTTON_0,
	PROP_OSD_BUTTON_ID,
	PROP_OSD_BUTTON_CLASS,
	PROP_OSD_BUTTON_LABEL,
	PROP_OSD_BUTTON_ACTIVE,
	PROP_OSD_BUTTON_VISIBLE
};

#define GSD_WACOM_OSD_BUTTON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
					     GSD_TYPE_WACOM_OSD_BUTTON, \
					     GsdWacomOSDButtonPrivate))
#define MATCH_ID(b,s) (g_strcmp0 (b->priv->id, s) == 0)

struct GsdWacomOSDButtonPrivate {
	GtkWidget                *widget;
	char                     *id;
	char                     *class;
	char                     *label;
	double                    label_x;
	double                    label_y;
	GsdWacomTabletButtonType  type;
	GsdWacomTabletButtonPos   position;
	gboolean                  active;
	gboolean                  visible;
	GdkRGBA                   active_color;
	GdkRGBA                   inactive_color;

	/* Animations */
	gboolean                  next_state;            /* what the next state should be, "active" or not */
	guint                     timeout_id;            /* ms */
	gint                      elapsed_time;          /* ms */
	gdouble                   transition_percentage; /* 0.0 to 1.0 */
};

static void     gsd_wacom_osd_button_class_init  (GsdWacomOSDButtonClass *klass);
static void     gsd_wacom_osd_button_init        (GsdWacomOSDButton      *osd_button);
static void     gsd_wacom_osd_button_finalize    (GObject                *object);

G_DEFINE_TYPE (GsdWacomOSDButton, gsd_wacom_osd_button, G_TYPE_OBJECT)

static void
gsd_wacom_osd_button_set_id (GsdWacomOSDButton *osd_button,
			     const gchar       *id)
{
	g_return_if_fail (GSD_IS_WACOM_OSD_BUTTON (osd_button));

	osd_button->priv->id = g_strdup (id);
}

static void
gsd_wacom_osd_button_set_class (GsdWacomOSDButton *osd_button,
			        const gchar       *class)
{
	g_return_if_fail (GSD_IS_WACOM_OSD_BUTTON (osd_button));

	osd_button->priv->class = g_strdup (class);
}

static gchar*
gsd_wacom_osd_button_get_label_class (GsdWacomOSDButton *osd_button)
{
	gchar *label_class;

	label_class = g_strconcat ("#Label", osd_button->priv->class, NULL);

	return (label_class);
}

static void
gsd_wacom_osd_button_set_label (GsdWacomOSDButton *osd_button,
				const gchar       *str)
{
	g_return_if_fail (GSD_IS_WACOM_OSD_BUTTON (osd_button));

	g_free (osd_button->priv->label);
	osd_button->priv->label = g_strdup (str ? str : "");
}

static void
gsd_wacom_osd_button_set_button_type (GsdWacomOSDButton        *osd_button,
				      GsdWacomTabletButtonType  type)
{
	g_return_if_fail (GSD_IS_WACOM_OSD_BUTTON (osd_button));

	osd_button->priv->type = type;
}

static void
gsd_wacom_osd_button_set_position (GsdWacomOSDButton        *osd_button,
				   GsdWacomTabletButtonPos   position)
{
	g_return_if_fail (GSD_IS_WACOM_OSD_BUTTON (osd_button));

	osd_button->priv->position = position;
}

static void
gsd_wacom_osd_button_set_location (GsdWacomOSDButton        *osd_button,
				   double                    x,
				   double                    y)
{
	g_return_if_fail (GSD_IS_WACOM_OSD_BUTTON (osd_button));

	osd_button->priv->label_x = x;
	osd_button->priv->label_y = y;
}

static void
gsd_wacom_osd_button_redraw (GsdWacomOSDButton *osd_button)
{
	GdkWindow *window;

	g_return_if_fail (GTK_IS_WIDGET (osd_button->priv->widget));

	window = gtk_widget_get_window (GTK_WIDGET (osd_button->priv->widget));
	gdk_window_invalidate_rect (window, NULL, FALSE);
}

static gboolean
gsd_wacom_osd_button_timer (GsdWacomOSDButton *osd_button)
{
	gint     total_timeout;
	gdouble  transition_step;
	gboolean ret = G_SOURCE_CONTINUE;

	total_timeout = BUTTON_TRANSITION_DURATION;
	/* if the state is active, then we need it to be "on" at least
	 * for the time of BUTTON_HIGHLIGHT_DURATION after the fade-in */
	if (osd_button->priv->active)
		total_timeout += BUTTON_HIGHLIGHT_DURATION;

	transition_step = (gdouble) BUTTON_TIMER_STEP / BUTTON_TRANSITION_DURATION;
	osd_button->priv->transition_percentage = MIN (osd_button->priv->transition_percentage + transition_step,
						       BUTTON_FULL_TRANSITION);

	osd_button->priv->elapsed_time += BUTTON_TIMER_STEP;
	if (osd_button->priv->elapsed_time > total_timeout) {
		/* If the next state and the current one are the same, then
		 * we finish this timer */
		if (osd_button->priv->active == osd_button->priv->next_state) {
			ret = G_SOURCE_REMOVE;
			osd_button->priv->timeout_id = 0;
		} else
			osd_button->priv->active = osd_button->priv->next_state;

		osd_button->priv->elapsed_time = 0;
		osd_button->priv->transition_percentage = 0;
	}

	gsd_wacom_osd_button_redraw (osd_button);

	return ret;
}

static void
invert_button_color_transition (GsdWacomOSDButton *osd_button)
{
	osd_button->priv->elapsed_time = (BUTTON_HIGHLIGHT_DURATION + BUTTON_TRANSITION_DURATION) - osd_button->priv->elapsed_time;
	osd_button->priv->transition_percentage = BUTTON_FULL_TRANSITION - osd_button->priv->transition_percentage;
}

static void
extend_on_state (GsdWacomOSDButton *osd_button)
{
	osd_button->priv->elapsed_time = BUTTON_TRANSITION_DURATION;
	osd_button->priv->transition_percentage = BUTTON_FULL_TRANSITION;
}

static void
gsd_wacom_osd_button_set_active (GsdWacomOSDButton *osd_button,
				 gboolean           active)
{
	gboolean previous_state;

	g_return_if_fail (GSD_IS_WACOM_OSD_BUTTON (osd_button));

        osd_button->priv->next_state = active;
	previous_state = osd_button->priv->active;
	if (osd_button->priv->timeout_id == 0) {
		osd_button->priv->active = active;
		osd_button->priv->timeout_id = g_timeout_add (BUTTON_TIMER_STEP,
							      (GSourceFunc) gsd_wacom_osd_button_timer,
							      osd_button);
		g_source_set_name_by_id (osd_button->priv->timeout_id, "[gnome-settings-daemon] gsd_wacom_osd_button_timer");
	} else if (osd_button->priv->next_state) {
		/* it was on and now should be on again */
		if (previous_state == active) {
			/* if the button has transitioned to on already, then it
			 * needs to remain on for the default amount of time again */
			if (osd_button->priv->elapsed_time > BUTTON_TRANSITION_DURATION)
				extend_on_state (osd_button);
		} else {
			/* if we have an on-going timeout which was fading out and
			 * now we need to fade it in again, we just invert the transition*/
			invert_button_color_transition (osd_button);
		}
	}
}

static void
gsd_wacom_osd_button_set_visible (GsdWacomOSDButton *osd_button,
				  gboolean           visible)
{
	g_return_if_fail (GSD_IS_WACOM_OSD_BUTTON (osd_button));

	osd_button->priv->visible = visible;
}

static GsdWacomOSDButton *
gsd_wacom_osd_button_new (GtkWidget *widget,
                          gchar *id)
{
	GsdWacomOSDButton *osd_button;

	osd_button = GSD_WACOM_OSD_BUTTON (g_object_new (GSD_TYPE_WACOM_OSD_BUTTON,
	                                                 "id", id,
	                                                 NULL));
	osd_button->priv->widget = widget;

	return osd_button;
}

static void
gsd_wacom_osd_button_set_property (GObject        *object,
				   guint           prop_id,
				   const GValue   *value,
				   GParamSpec     *pspec)
{
	GsdWacomOSDButton *osd_button;

	osd_button = GSD_WACOM_OSD_BUTTON (object);

	switch (prop_id) {
	case PROP_OSD_BUTTON_ID:
		gsd_wacom_osd_button_set_id (osd_button, g_value_get_string (value));
		break;
	case PROP_OSD_BUTTON_CLASS:
		gsd_wacom_osd_button_set_class (osd_button, g_value_get_string (value));
		break;
	case PROP_OSD_BUTTON_LABEL:
		gsd_wacom_osd_button_set_label (osd_button, g_value_get_string (value));
		break;
	case PROP_OSD_BUTTON_ACTIVE:
		gsd_wacom_osd_button_set_active (osd_button, g_value_get_boolean (value));
		break;
	case PROP_OSD_BUTTON_VISIBLE:
		gsd_wacom_osd_button_set_visible (osd_button, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gsd_wacom_osd_button_get_property (GObject        *object,
				   guint           prop_id,
				   GValue         *value,
				   GParamSpec     *pspec)
{
	GsdWacomOSDButton *osd_button;

	osd_button = GSD_WACOM_OSD_BUTTON (object);

	switch (prop_id) {
	case PROP_OSD_BUTTON_ID:
		g_value_set_string (value, osd_button->priv->id);
		break;
	case PROP_OSD_BUTTON_CLASS:
		g_value_set_string (value, osd_button->priv->class);
		break;
	case PROP_OSD_BUTTON_LABEL:
		g_value_set_string (value, osd_button->priv->label);
		break;
	case PROP_OSD_BUTTON_ACTIVE:
		g_value_set_boolean (value, osd_button->priv->active);
		break;
	case PROP_OSD_BUTTON_VISIBLE:
		g_value_set_boolean (value, osd_button->priv->visible);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gsd_wacom_osd_button_class_init (GsdWacomOSDButtonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = gsd_wacom_osd_button_set_property;
	object_class->get_property = gsd_wacom_osd_button_get_property;
	object_class->finalize = gsd_wacom_osd_button_finalize;

	g_object_class_install_property (object_class,
	                                 PROP_OSD_BUTTON_ID,
	                                 g_param_spec_string ("id",
	                                                      "Button Id",
	                                                      "The Wacom Button ID",
	                                                      "",
	                                                      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_OSD_BUTTON_CLASS,
	                                 g_param_spec_string ("class",
	                                                      "Button Class",
	                                                      "The Wacom Button Class",
	                                                      "",
	                                                      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_OSD_BUTTON_LABEL,
	                                 g_param_spec_string ("label",
	                                                      "Label",
	                                                      "The button label",
	                                                      "",
	                                                      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_OSD_BUTTON_ACTIVE,
	                                 g_param_spec_boolean ("active",
	                                                       "Active",
	                                                       "Whether the button is active",
	                                                       FALSE,
	                                                       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_OSD_BUTTON_VISIBLE,
	                                 g_param_spec_boolean ("visible",
	                                                       "Visible",
	                                                       "Whether the button is visible",
	                                                       TRUE,
	                                                       G_PARAM_READWRITE));

	g_type_class_add_private (klass, sizeof (GsdWacomOSDButtonPrivate));
}

static void
gsd_wacom_osd_button_init (GsdWacomOSDButton *osd_button)
{
	osd_button->priv = GSD_WACOM_OSD_BUTTON_GET_PRIVATE (osd_button);

	gdk_rgba_parse (&osd_button->priv->active_color, ACTIVE_COLOR);
	gdk_rgba_parse (&osd_button->priv->inactive_color, INACTIVE_COLOR);
}

static void
gsd_wacom_osd_button_finalize (GObject *object)
{
	GsdWacomOSDButton *osd_button;
	GsdWacomOSDButtonPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GSD_IS_WACOM_OSD_BUTTON (object));

	osd_button = GSD_WACOM_OSD_BUTTON (object);

	g_return_if_fail (osd_button->priv != NULL);

	priv = osd_button->priv;

	if (priv->timeout_id > 0)
		g_source_remove (priv->timeout_id);
	g_clear_pointer (&priv->id, g_free);
	g_clear_pointer (&priv->class, g_free);
	g_clear_pointer (&priv->label, g_free);

	G_OBJECT_CLASS (gsd_wacom_osd_button_parent_class)->finalize (object);
}

/* Compute the new actual position once rotation is applied */
static GsdWacomTabletButtonPos
get_actual_position (GsdWacomTabletButtonPos position,
		     GsdWacomRotation        rotation)
{
	switch (rotation) {
	case GSD_WACOM_ROTATION_NONE:
		return position;
		break;
	case GSD_WACOM_ROTATION_HALF:
		if (position == WACOM_TABLET_BUTTON_POS_LEFT)
			return WACOM_TABLET_BUTTON_POS_RIGHT;
		if (position == WACOM_TABLET_BUTTON_POS_RIGHT)
			return WACOM_TABLET_BUTTON_POS_LEFT;
		if (position == WACOM_TABLET_BUTTON_POS_TOP)
			return WACOM_TABLET_BUTTON_POS_BOTTOM;
		if (position == WACOM_TABLET_BUTTON_POS_BOTTOM)
			return WACOM_TABLET_BUTTON_POS_TOP;
		break;
	/* We only support left-handed/right-handed */
	case GSD_WACOM_ROTATION_CCW:
	case GSD_WACOM_ROTATION_CW:
	default:
		break;
	}
	/* fallback */
	return position;
}

static gchar *
rgb_color_to_hex (GdkRGBA *color)
{
	gchar *hex_color;

	hex_color = g_strdup_printf ("#%02X%02X%02X",
				     (guint) (color->red * 255),
				     (guint) (color->green * 255),
				     (guint) (color->blue * 255));

	return hex_color;
}

static GdkRGBA *
transition_rgba_colors (GdkRGBA *from_rgba,
			GdkRGBA *to_rgba,
			gdouble  transition_percentage)
{
	GdkRGBA *new_color = gdk_rgba_copy (from_rgba);

	if (transition_percentage == 0.0)
		return new_color;

	new_color->red -= (from_rgba->red - to_rgba->red) * transition_percentage;
	new_color->green -= (from_rgba->green - to_rgba->green) * transition_percentage;
	new_color->blue -= (from_rgba->blue - to_rgba->blue) * transition_percentage;

	return new_color;
}

static gchar *
gsd_wacom_osd_button_get_color_str (GsdWacomOSDButton *osd_button)
{
	GdkRGBA *current_color, *from_color, *to_color;
	gboolean free_current_color = FALSE;
	gchar   *color_str;

	/* If we got an on-going transition, then we need to apply it */
	if (osd_button->priv->timeout_id > 0) {
		if (osd_button->priv->active) {
			from_color = &osd_button->priv->inactive_color;
			to_color = &osd_button->priv->active_color;
		} else {
			from_color = &osd_button->priv->active_color;
			to_color = &osd_button->priv->inactive_color;
		}

		current_color = transition_rgba_colors (from_color, to_color, osd_button->priv->transition_percentage);

		free_current_color = TRUE;
	} else
		current_color = osd_button->priv->active ? &osd_button->priv->active_color : &osd_button->priv->inactive_color;

	color_str = rgb_color_to_hex (current_color);

	if (free_current_color)
		gdk_rgba_free (current_color);

	return color_str;
}

static void
gsd_wacom_osd_button_draw_label (GsdWacomOSDButton *osd_button,
			         GtkStyleContext   *style_context,
			         PangoContext      *pango_context,
			         cairo_t           *cr,
			         GsdWacomRotation   rotation)
{
	GsdWacomOSDButtonPrivate *priv;
	PangoLayout              *layout;
	PangoRectangle            logical_rect;
	GsdWacomTabletButtonPos   actual_position;
	double                    lx, ly;
	gchar                    *markup, *color_str;

	g_return_if_fail (GSD_IS_WACOM_OSD_BUTTON (osd_button));

	priv = osd_button->priv;
	if (priv->visible == FALSE)
		return;

	actual_position = get_actual_position (priv->position, rotation);
	layout = pango_layout_new (pango_context);
	color_str = gsd_wacom_osd_button_get_color_str (osd_button);
	markup = g_strdup_printf ("<span foreground=\"%s\" weight=\"normal\">%s</span>", color_str, priv->label);
	g_free (color_str);
	pango_layout_set_markup (layout, markup, -1);
	g_free (markup);

	pango_layout_get_pixel_extents (layout, NULL, &logical_rect);
	switch (actual_position) {
	case WACOM_TABLET_BUTTON_POS_LEFT:
		pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
		lx = priv->label_x + logical_rect.x;
		ly = priv->label_y + logical_rect.y - get_pango_vertical_offset (layout);
		break;
	case WACOM_TABLET_BUTTON_POS_RIGHT:
		pango_layout_set_alignment (layout, PANGO_ALIGN_RIGHT);
		lx = priv->label_x + logical_rect.x - logical_rect.width;
		ly = priv->label_y + logical_rect.y - get_pango_vertical_offset (layout);
		break;
	case WACOM_TABLET_BUTTON_POS_TOP:
		pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
		lx = priv->label_x + logical_rect.x - logical_rect.width / 2;
		ly = priv->label_y + logical_rect.y;
		break;
	case WACOM_TABLET_BUTTON_POS_BOTTOM:
		pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
		lx = priv->label_x + logical_rect.x - logical_rect.width / 2;
		ly = priv->label_y + logical_rect.y - logical_rect.height;
		break;
	default:
		g_warning ("Unhandled button position");
		pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
		lx = priv->label_x + logical_rect.x - logical_rect.width / 2;
		ly = priv->label_y + logical_rect.y - logical_rect.height / 2;
		break;
	}
	gtk_render_layout (style_context, cr, lx, ly, layout);
	g_object_unref (layout);
}

enum {
  PROP_OSD_WINDOW_0,
  PROP_OSD_WINDOW_MESSAGE,
  PROP_OSD_WINDOW_GSD_WACOM_DEVICE,
  PROP_OSD_WINDOW_EDITION_MODE
};

#define GSD_WACOM_OSD_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
					     GSD_TYPE_WACOM_OSD_WINDOW, \
					     GsdWacomOSDWindowPrivate))

struct GsdWacomOSDWindowPrivate
{
	RsvgHandle               *handle;
	GsdWacomDevice           *pad;
	GsdWacomRotation          rotation;
	GdkRectangle              screen_area;
	GdkRectangle              monitor_area;
	GdkRectangle              tablet_area;
	char                     *message;
	char                     *edition_mode_message;
	char                     *regular_mode_message;
	GList                    *buttons;
	guint                     cursor_timeout;
	gboolean                  edition_mode;
	GsdWacomOSDButton        *current_button;
	GtkWidget                *editor;
	GtkWidget                *change_mode_button;
};

static void     gsd_wacom_osd_window_class_init  (GsdWacomOSDWindowClass *klass);
static void     gsd_wacom_osd_window_init        (GsdWacomOSDWindow      *osd_window);
static void     gsd_wacom_osd_window_finalize    (GObject                *object);

G_DEFINE_TYPE (GsdWacomOSDWindow, gsd_wacom_osd_window, GTK_TYPE_WINDOW)

static gboolean
osd_window_editing_button (GsdWacomOSDWindow *self)
{
	return self->priv->edition_mode && gtk_widget_get_visible (self->priv->editor);
}

static RsvgHandle *
load_rsvg_with_base (const char  *css_string,
		     const char  *original_layout_path,
		     GError     **error)
{
	RsvgHandle *handle;
	char *dirname;

	handle = rsvg_handle_new ();

	dirname = g_path_get_dirname (original_layout_path);
	rsvg_handle_set_base_uri (handle, dirname);
	g_free (dirname);

	if (!rsvg_handle_write (handle,
				(guint8 *) css_string,
				strlen (css_string),
				error)) {
		g_object_unref (handle);
		return NULL;
	}
	if (!rsvg_handle_close (handle, error)) {
		g_object_unref (handle);
		return NULL;
	}

	return handle;
}

static void
gsd_wacom_osd_window_update (GsdWacomOSDWindow *osd_window)
{
	GError      *error = NULL;
	gchar       *width, *height;
	gchar       *buttons_section;
	gchar       *css_string;
	const gchar *layout_file;
	GBytes      *css_data;
        guint i;
	GList *l;

	g_return_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window));
	g_return_if_fail (GSD_IS_WACOM_DEVICE (osd_window->priv->pad));

	css_data = g_resources_lookup_data (RES_PATH "tablet-layout.css", 0, &error);
	if (error != NULL) {
		g_printerr ("GResource error: %s\n", error->message);
		g_clear_pointer (&error, g_error_free);
	}
	if (css_data == NULL)
		return;
	css_string = g_strdup ((gchar *) g_bytes_get_data (css_data, NULL));
	g_bytes_unref(css_data);

	width = g_strdup_printf ("%d", osd_window->priv->tablet_area.width);
	replace_string (&css_string, "layout_width", width);
	g_free (width);

	height = g_strdup_printf ("%d", osd_window->priv->tablet_area.height);
	replace_string (&css_string, "layout_height", height);
	g_free (height);

	/* Build the buttons section */
	buttons_section = g_strdup ("");
	for (l = osd_window->priv->buttons; l != NULL; l = l->next) {
		gchar *color_str;
		GsdWacomOSDButton *osd_button = l->data;

		if (osd_button->priv->visible == FALSE)
			continue;

		if (osd_window_editing_button (osd_window) &&
		    osd_button != osd_window->priv->current_button) {
			buttons_section = g_strdup_printf (CSS_NORMAL_BUTTON,
							   buttons_section,
							   osd_button->priv->class,
							   OPACITY_IN_EDITION);
		} else {
			color_str = gsd_wacom_osd_button_get_color_str (osd_button);
			buttons_section = g_strdup_printf (CSS_EDITING_BUTTON,
							   buttons_section,
							   osd_button->priv->class,
							   color_str,
							   color_str);
			g_free (color_str);
		}
	}
	replace_string (&css_string, "buttons_section", buttons_section);
	g_free (buttons_section);

        for (i = 0; i < G_N_ELEMENTS (css_color_table); i++)
		replace_string (&css_string,
		                css_color_table[i].color_name,
		                css_color_table[i].color_value);

	layout_file = gsd_wacom_device_get_layout_path (osd_window->priv->pad);
	g_debug ("Using layout path: %s", layout_file);
	replace_string (&css_string, "layout_file", layout_file);

	/* Render the SVG with the CSS applied */
	g_clear_object (&osd_window->priv->handle);
	osd_window->priv->handle = load_rsvg_with_base (css_string, layout_file, &error);
	if (osd_window->priv->handle == NULL) {
		g_debug ("CSS applied:\n%s\n", css_string);
		g_printerr ("RSVG error: %s\n", error->message);
		g_clear_error (&error);
	}
	g_free (css_string);
}

static void
gsd_wacom_osd_window_draw_message (GsdWacomOSDWindow   *osd_window,
				   GtkStyleContext     *style_context,
				   PangoContext        *pango_context,
				   cairo_t             *cr)
{
	GdkRectangle  *monitor_area = &osd_window->priv->monitor_area;
	PangoRectangle logical_rect;
	PangoLayout *layout;
	char *markup;
	double x;
	double y;

	if (osd_window->priv->message == NULL || osd_window_editing_button (osd_window))
		return;

	layout = pango_layout_new (pango_context);
	pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);

	markup = g_strdup_printf ("<span foreground=\"white\">%s</span>", osd_window->priv->message);
	pango_layout_set_markup (layout, markup, -1);
	g_free (markup);

	pango_layout_get_pixel_extents (layout, NULL, &logical_rect);
	x = (monitor_area->width - logical_rect.width) / 2 + logical_rect.x;
	y = (monitor_area->height - logical_rect.height) / 2 + logical_rect.y;

	gtk_render_layout (style_context, cr, x, y, layout);
	g_object_unref (layout);
}

static void
gsd_wacom_osd_window_draw_labels (GsdWacomOSDWindow   *osd_window,
				  GtkStyleContext     *style_context,
				  PangoContext        *pango_context,
				  cairo_t             *cr)
{
	GList *l;

	for (l = osd_window->priv->buttons; l != NULL; l = l->next) {
		GsdWacomOSDButton *osd_button = l->data;

		if (osd_button->priv->visible == FALSE)
			continue;

		gsd_wacom_osd_button_draw_label (osd_button,
			                         style_context,
			                         pango_context,
			                         cr,
			                         osd_window->priv->rotation);
	}
}

static void
gsd_wacom_osd_window_place_buttons (GsdWacomOSDWindow *osd_window,
				    cairo_t           *cr)
{
	GList            *l;

	g_return_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window));

	for (l = osd_window->priv->buttons; l != NULL; l = l->next) {
		GsdWacomOSDButton *osd_button = l->data;
		double             label_x, label_y;
		gchar             *sub;

		sub = gsd_wacom_osd_button_get_label_class (osd_button);
		if (!get_sub_location (osd_window->priv->handle, sub, cr, &label_x, &label_y)) {
			g_warning ("Failed to retrieve %s position", sub);
			g_free (sub);
			continue;
		}
		g_free (sub);
		gsd_wacom_osd_button_set_location (osd_button, label_x, label_y);
	}
}

/* Note: this function does modify the given cairo context */
static void
gsd_wacom_osd_window_adjust_cairo (GsdWacomOSDWindow *osd_window,
                                   cairo_t           *cr)
{
	double         scale, twidth, theight;
	GdkRectangle  *tablet_area  = &osd_window->priv->tablet_area;
	GdkRectangle  *screen_area  = &osd_window->priv->screen_area;
	GdkRectangle  *monitor_area = &osd_window->priv->monitor_area;

	/* Rotate */
	cairo_rotate (cr, get_rotation_in_radian (osd_window->priv->rotation));

	/* Scale to fit in window */
	scale = MIN ((double) monitor_area->width / tablet_area->width,
	             (double) monitor_area->height / tablet_area->height);
	cairo_scale (cr, scale, scale);

	/* Center the result in window */
	twidth = (double) tablet_area->width;
	theight = (double) tablet_area->height;
	cairo_user_to_device_distance (cr, &twidth, &theight);

	twidth = ((double) monitor_area->width - twidth) / 2.0;
	theight = ((double) monitor_area->height - theight) / 2.0;
	cairo_device_to_user_distance (cr, &twidth, &theight);

	twidth = twidth + (double) (monitor_area->x - screen_area->x);
	theight = theight + (double) (monitor_area->y - screen_area->y);

	cairo_translate (cr, twidth, theight);
}

static gboolean
gsd_wacom_osd_window_draw (GtkWidget *widget,
			   cairo_t   *cr)
{
	GsdWacomOSDWindow *osd_window = GSD_WACOM_OSD_WINDOW (widget);

	g_return_val_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window), FALSE);
	g_return_val_if_fail (GSD_IS_WACOM_DEVICE (osd_window->priv->pad), FALSE);

	if (gtk_cairo_should_draw_window (cr, gtk_widget_get_window (widget))) {
		GtkStyleContext     *style_context;
		PangoContext        *pango_context;

		style_context = gtk_widget_get_style_context (widget);
		pango_context = gtk_widget_get_pango_context (widget);

		cairo_set_source_rgba (cr, 0, 0, 0, BACK_OPACITY);
		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint (cr);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

		/* Save original matrix */
		cairo_save (cr);

		/* Apply new cairo transformation matrix */
		gsd_wacom_osd_window_adjust_cairo (osd_window, cr);

		/* And render the tablet layout */
		gsd_wacom_osd_window_update (osd_window);
		rsvg_handle_render_cairo (osd_window->priv->handle, cr);

		gsd_wacom_osd_window_place_buttons (osd_window, cr);

		/* Reset to original matrix */
		cairo_restore (cr);

		/* Draw button labels and message */
		gsd_wacom_osd_window_draw_labels (osd_window,
		                                  style_context,
		                                  pango_context,
		                                  cr);
		gsd_wacom_osd_window_draw_message (osd_window,
		                                   style_context,
		                                   pango_context,
		                                   cr);
	}

	GTK_WIDGET_CLASS (gsd_wacom_osd_window_parent_class)->draw (widget, cr);

	return FALSE;
}

static gchar *
get_escaped_accel_shortcut (const gchar *accel)
{
	guint keyval;
	GdkModifierType mask;
	gchar *str, *label;

	if (accel == NULL || accel[0] == '\0')
		return g_strdup (C_("Action type", "None"));

	gtk_accelerator_parse (accel, &keyval, &mask);

	str = gtk_accelerator_get_label (keyval, mask);
	label = g_markup_printf_escaped (C_("Action type", "Send Keystroke %s"), str);
	g_free (str);

	return label;
}

static gchar *
get_tablet_button_label_normal (GsdWacomDevice       *device,
				GsdWacomTabletButton *button)
{
	GsdWacomActionType type;
	gchar *name, *str;

	type = g_settings_get_enum (button->settings, ACTION_TYPE_KEY);
	if (type == GSD_WACOM_ACTION_TYPE_NONE)
		return g_strdup (C_("Action type", "None"));

	if (type == GSD_WACOM_ACTION_TYPE_HELP)
		return g_strdup (C_("Action type", "Show On-Screen Help"));

	if (type == GSD_WACOM_ACTION_TYPE_SWITCH_MONITOR)
		return g_strdup (C_("Action type", "Switch Monitor"));

	str = g_settings_get_string (button->settings, CUSTOM_ACTION_KEY);
	if (str == NULL || *str == '\0') {
		g_free (str);
		return g_strdup (C_("Action type", "None"));
	}

	name = get_escaped_accel_shortcut (str);
	g_free (str);

	return name;
}

static gchar *
get_tablet_button_label_touch  (GsdWacomDevice       *device,
				GsdWacomTabletButton *button,
				GtkDirectionType      dir)
{
	char **strv, *name, *str;

	strv = g_settings_get_strv (button->settings, CUSTOM_ELEVATOR_ACTION_KEY);
	name = NULL;

	if (strv) {
		if (g_strv_length (strv) >= 1 && dir == GTK_DIR_UP)
			name = g_strdup (strv[0]);
		else if (g_strv_length (strv) >= 2 && dir == GTK_DIR_DOWN)
			name = g_strdup (strv[1]);
		g_strfreev (strv);
	}

	str = get_escaped_accel_shortcut (name);
	g_free (name);
	name = str;

	/* With multiple modes, also show the current mode for that action */
	if (gsd_wacom_device_get_num_modes (device, button->group_id) > 1) {
		name = g_strdup_printf (_("Mode %d: %s"), button->idx + 1, str);
		g_free (str);
	}

	return name;
}

static gchar *
get_tablet_button_label (GsdWacomDevice       *device,
	                 GsdWacomTabletButton *button,
	                 GtkDirectionType      dir)
{
	g_return_val_if_fail (button, NULL);

	if (!button->settings)
		goto out;

	switch (button->type) {
	case WACOM_TABLET_BUTTON_TYPE_NORMAL:
		return get_tablet_button_label_normal (device, button);
		break;
	case WACOM_TABLET_BUTTON_TYPE_RING:
	case WACOM_TABLET_BUTTON_TYPE_STRIP:
		return get_tablet_button_label_touch (device, button, dir);
		break;
	case WACOM_TABLET_BUTTON_TYPE_HARDCODED:
	default:
		break;
	}
out:
	return g_strdup (button->name);
}

static gchar*
get_tablet_button_class_name (GsdWacomTabletButton *tablet_button,
                              GtkDirectionType      dir)
{
	gchar *id;
	gchar  c;

	id = tablet_button->id;
	switch (tablet_button->type) {
	case WACOM_TABLET_BUTTON_TYPE_RING:
		if (id[0] == 'l') /* left-ring */
			return g_strdup_printf ("Ring%s", (dir == GTK_DIR_UP ? "CCW" : "CW"));
		if (id[0] == 'r') /* right-ring */
			return g_strdup_printf ("Ring2%s", (dir == GTK_DIR_UP ? "CCW" : "CW"));
		g_warning ("Unknown ring type '%s'", id);
		return NULL;
		break;
	case WACOM_TABLET_BUTTON_TYPE_STRIP:
		if (id[0] == 'l') /* left-strip */
			return g_strdup_printf ("Strip%s", (dir == GTK_DIR_UP ? "Up" : "Down"));
		if (id[0] == 'r') /* right-strip */
			return g_strdup_printf ("Strip2%s", (dir == GTK_DIR_UP ? "Up" : "Down"));
		g_warning ("Unknown strip type '%s'", id);
		return NULL;
		break;
	case WACOM_TABLET_BUTTON_TYPE_NORMAL:
	case WACOM_TABLET_BUTTON_TYPE_HARDCODED:
		c = get_last_char (id);
		return g_strdup_printf ("%c", g_ascii_toupper (c));
		break;
	default:
		g_warning ("Unknown button type '%s'", id);
		break;
	}

	return NULL;
}

static gchar*
get_tablet_button_id_name (GsdWacomTabletButton *tablet_button,
                           GtkDirectionType      dir)
{
	gchar *id;
	gchar  c;

	id = tablet_button->id;
	switch (tablet_button->type) {
	case WACOM_TABLET_BUTTON_TYPE_RING:
		return g_strconcat (id, (dir == GTK_DIR_UP ? "-ccw" : "-cw"), NULL);
		break;
	case WACOM_TABLET_BUTTON_TYPE_STRIP:
		return g_strconcat (id, (dir == GTK_DIR_UP ? "-up" : "-down"), NULL);
		break;
	case WACOM_TABLET_BUTTON_TYPE_NORMAL:
	case WACOM_TABLET_BUTTON_TYPE_HARDCODED:
		c = get_last_char (id);
		return g_strdup_printf ("%c", g_ascii_toupper (c));
		break;
	default:
		g_warning ("Unknown button type '%s'", id);
		break;
	}

	return NULL;
}

static gint
get_elevator_current_mode (GsdWacomOSDWindow    *osd_window,
                           GsdWacomTabletButton *elevator_button)
{
	GList *list, *l;
	gint   mode;

	mode = 1;
	/* Search in the list of buttons the corresponding
	 * mode-switch button and get the current mode
	 */
	list = gsd_wacom_device_get_buttons (osd_window->priv->pad);
	for (l = list; l != NULL; l = l->next) {
		GsdWacomTabletButton *tablet_button = l->data;

		if (tablet_button->type != WACOM_TABLET_BUTTON_TYPE_HARDCODED)
			continue;
		if (elevator_button->group_id != tablet_button->group_id)
			continue;
		mode = gsd_wacom_device_get_current_mode (osd_window->priv->pad,
		                                          tablet_button->group_id);
		break;
	}
	g_list_free (list);

	return mode;
}

static void
redraw_window (GsdWacomOSDWindow *self)
{
	GdkWindow *window;

	window = gtk_widget_get_window (GTK_WIDGET (self));
	if (window)
		gdk_window_invalidate_rect (window, NULL, FALSE);
}

static GsdWacomOSDButton *
gsd_wacom_osd_window_add_button_with_dir (GsdWacomOSDWindow    *osd_window,
                                          GsdWacomTabletButton *tablet_button,
                                          GtkDirectionType      dir)
{
	GsdWacomOSDButton    *osd_button;
	gchar                *str;

	str = get_tablet_button_id_name (tablet_button, dir);
	osd_button = gsd_wacom_osd_button_new (GTK_WIDGET (osd_window), str);
	g_free (str);

	str = get_tablet_button_class_name (tablet_button, dir);
	gsd_wacom_osd_button_set_class (osd_button, str);
	g_free (str);

	str = get_tablet_button_label (osd_window->priv->pad, tablet_button, dir);
	gsd_wacom_osd_button_set_label (osd_button, str);
	g_free (str);

	gsd_wacom_osd_button_set_button_type (osd_button, tablet_button->type);
	gsd_wacom_osd_button_set_position (osd_button, tablet_button->pos);
	osd_window->priv->buttons = g_list_append (osd_window->priv->buttons, osd_button);

	return osd_button;
}

static void
gsd_wacom_osd_window_add_tablet_button (GsdWacomOSDWindow    *osd_window,
                                        GsdWacomTabletButton *tablet_button)
{
	GsdWacomOSDButton    *osd_button;
	gint                  mode;

	switch (tablet_button->type) {
	case WACOM_TABLET_BUTTON_TYPE_NORMAL:
	case WACOM_TABLET_BUTTON_TYPE_HARDCODED:
		osd_button = gsd_wacom_osd_window_add_button_with_dir (osd_window,
		                                                       tablet_button,
		                                                       0);
		gsd_wacom_osd_button_set_visible (osd_button, TRUE);
		break;
	case WACOM_TABLET_BUTTON_TYPE_RING:
	case WACOM_TABLET_BUTTON_TYPE_STRIP:
		mode = get_elevator_current_mode (osd_window, tablet_button) - 1;

		/* Add 2 buttons per elevator, one "Up"... */
		osd_button = gsd_wacom_osd_window_add_button_with_dir (osd_window,
		                                                       tablet_button,
		                                                       GTK_DIR_UP);
		gsd_wacom_osd_button_set_visible (osd_button, tablet_button->idx == mode);

		/* ... and one "Down" */
		osd_button = gsd_wacom_osd_window_add_button_with_dir (osd_window,
		                                                       tablet_button,
		                                                       GTK_DIR_DOWN);
		gsd_wacom_osd_button_set_visible (osd_button, tablet_button->idx == mode);

		break;
	default:
		g_warning ("Unknown button type");
		break;
	}
}

static char *
get_regular_mode_message (GsdWacomOSDWindow *osd_window)
{
	const gchar *name;
	gchar       *message;

	name = gsd_wacom_device_get_name (osd_window->priv->pad);
	message = g_strdup_printf ("<big><b>%s</b></big>\n<span foreground=\"%s\">%s</span>",
				   name, INACTIVE_COLOR, _("(press any key to exit)"));

	return message;
}

static char *
get_edition_mode_message (GsdWacomOSDWindow *osd_window)
{
	return g_strdup_printf ("<big><b>%s</b></big>\n<span foreground=\"%s\">%s</span>",
				_("Push a button to configure"), INACTIVE_COLOR, _("(Esc to cancel)"));
}

static void
close_editor (GsdWacomOSDWindow *self)
{
	if (self->priv->current_button)
		self->priv->current_button->priv->visible = TRUE;

	gtk_widget_hide (self->priv->editor);
	self->priv->current_button = NULL;
}

static void
edition_mode_changed (GsdWacomOSDWindow *self)
{
	if (self->priv->edition_mode)
		gsd_wacom_osd_window_set_message (self, self->priv->edition_mode_message);
	else {
		gsd_wacom_osd_window_set_message (self, self->priv->regular_mode_message);

		close_editor (self);
	}

	redraw_window (self);
}

static void
on_change_mode_button_clicked (GtkButton         *button,
			       GsdWacomOSDWindow *window)
{
	window->priv->edition_mode = !window->priv->edition_mode;

	edition_mode_changed (window);
}

static void
osd_window_set_cursor (GsdWacomOSDWindow *window, GdkCursorType cursor_type)
{
	GdkCursor *cursor;
	GdkWindow *gdk_window;

	gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
	cursor = gdk_cursor_new (cursor_type);
	gdk_window_set_cursor (gdk_window, cursor);
	g_object_unref (cursor);
}

static void
show_cursor (GsdWacomOSDWindow *window)
{
	osd_window_set_cursor (window, GDK_LEFT_PTR);
}

static void
hide_cursor (GsdWacomOSDWindow *window)
{
	osd_window_set_cursor (window, GDK_BLANK_CURSOR);
}

static gboolean
cursor_timeout_source_func (gpointer data)
{
	GsdWacomOSDWindow *window = GSD_WACOM_OSD_WINDOW (data);
	hide_cursor (window);
	window->priv->cursor_timeout = 0;
	return G_SOURCE_REMOVE;
}

static void
cursor_timeout_stop (GsdWacomOSDWindow *window)
{
	if (window->priv->cursor_timeout != 0)
		g_source_remove (window->priv->cursor_timeout);
	window->priv->cursor_timeout = 0;
}

static gboolean
gsd_wacom_osd_window_motion_notify_event (GtkWidget      *widget,
					  GdkEventMotion *event)
{
	GsdWacomOSDWindow *window;
	GdkInputSource     source;

	window = GSD_WACOM_OSD_WINDOW (widget);

	source = gdk_device_get_source (event->device);
	if (source != GDK_SOURCE_MOUSE)
		return FALSE;

	show_cursor (window);
	cursor_timeout_stop (window);
	window->priv->cursor_timeout = g_timeout_add_seconds (CURSOR_HIDE_TIMEOUT,
							      cursor_timeout_source_func,
							      window);
	g_source_set_name_by_id (window->priv->cursor_timeout, "[gnome-settings-daemon] cursor_timeout_source_func");

	return FALSE;
}

/*
 * Returns the rotation to apply a device to get a representation relative to
 * the current rotation of the output.
 * (This function is _not_ the same as in gsd-wacom-manager.c)
 */
static GsdWacomRotation
display_relative_rotation (GsdWacomRotation device_rotation,
			   GsdWacomRotation output_rotation)
{
	GsdWacomRotation rotations[] = { GSD_WACOM_ROTATION_HALF,
	                                 GSD_WACOM_ROTATION_CW,
	                                 GSD_WACOM_ROTATION_NONE,
	                                 GSD_WACOM_ROTATION_CCW };
	guint i;

	if (device_rotation == output_rotation)
		return GSD_WACOM_ROTATION_NONE;

	if (output_rotation == GSD_WACOM_ROTATION_NONE)
		return device_rotation;

	for (i = 0; i < G_N_ELEMENTS (rotations); i++) {
		if (device_rotation == rotations[i])
			break;
	}

	if (output_rotation == GSD_WACOM_ROTATION_HALF)
		return rotations[(i + G_N_ELEMENTS (rotations) - 2) % G_N_ELEMENTS (rotations)];

	if (output_rotation == GSD_WACOM_ROTATION_CW)
		return rotations[(i + 1) % G_N_ELEMENTS (rotations)];

	if (output_rotation == GSD_WACOM_ROTATION_CCW)
		return rotations[(i + G_N_ELEMENTS (rotations) - 1) % G_N_ELEMENTS (rotations)];

	/* fallback */
	return GSD_WACOM_ROTATION_NONE;
}

static void
grab_keyboard (GsdWacomOSDWindow *self)
{
	GdkDevice        *kbd = NULL;
	GdkWindow        *gdk_window;
	GdkDisplay       *display;
	GdkDeviceManager *device_manager;
	GList            *devices, *l;

	gdk_window = gtk_widget_get_window (GTK_WIDGET (self));
	display = gtk_widget_get_display (GTK_WIDGET (self));
	device_manager = gdk_display_get_device_manager (display);
	devices = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_MASTER);

	for (l = devices; l != NULL; l = l->next) {
		GdkDevice *current_device;

		current_device = l->data;
		if (gdk_device_get_source (current_device) == GDK_SOURCE_KEYBOARD) {
			kbd = current_device;
			break;
		}
	}
	g_list_free (devices);

	g_assert (kbd);

	gdk_device_grab (kbd, gdk_window, GDK_OWNERSHIP_WINDOW, FALSE,
			 GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK,
			 NULL, GDK_CURRENT_TIME);
}

static void
gsd_wacom_osd_window_show (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (gsd_wacom_osd_window_parent_class)->show (widget);

	grab_keyboard (GSD_WACOM_OSD_WINDOW (widget));
}

static void
on_button_edited (GsdWacomButtonEditor *editor,
		  GsdWacomOSDWindow    *self)
{
	GsdWacomTabletButton *button;
	GtkDirectionType      dir;
	char                 *str;

	button = gsd_wacom_button_editor_get_button (editor, &dir);

	if (button == NULL || self->priv->current_button == NULL)
		return;

	str = get_tablet_button_label (self->priv->pad, button, dir);
	gsd_wacom_osd_button_set_label (self->priv->current_button, str);
	g_free (str);

	gsd_wacom_osd_button_redraw (self->priv->current_button);
}

static void
on_button_editing_done (GtkWidget         *editor,
			GsdWacomOSDWindow *self)
{
	close_editor (self);
	redraw_window (self);
	grab_keyboard (self);
}

static gboolean
on_get_child_position (GtkOverlay        *overlay,
		       GtkWidget         *widget,
		       GdkRectangle      *allocation,
		       GsdWacomOSDWindow *self)
{
	GsdWacomOSDButton       *button;
	GtkRequisition           requisition;
	GsdWacomTabletButtonPos  position;

	button = self->priv->current_button;

	if (button == NULL)
		return FALSE;

	gtk_widget_get_preferred_size (widget, NULL, &requisition);

	allocation->x = button->priv->label_x;
	allocation->y = button->priv->label_y;
	allocation->width = requisition.width;
	allocation->height = requisition.height;

	position = get_actual_position (button->priv->position, self->priv->rotation);

	if (position == WACOM_TABLET_BUTTON_POS_LEFT) {
		allocation->y -= requisition.height / 2.0;
	} else if (position == WACOM_TABLET_BUTTON_POS_RIGHT) {
		allocation->x -= requisition.width;
		allocation->y -= requisition.height / 2.0;
	} else if (position == WACOM_TABLET_BUTTON_POS_BOTTOM) {
		allocation->x -= requisition.width / 2.0;
		allocation->y -= requisition.height;
	} else if (position == WACOM_TABLET_BUTTON_POS_TOP) {
		allocation->x -= requisition.width / 2.0;
	}

	return TRUE;
}

static void
gsd_wacom_osd_window_realized (GtkWidget *widget,
                               gpointer   data)
{
	GsdWacomOSDWindow *osd_window = GSD_WACOM_OSD_WINDOW (widget);
	GdkWindow         *gdk_window;
	GdkRGBA            transparent;
	GdkScreen         *screen;
	gint               monitor;
	gboolean           status;

	g_return_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window));
	g_return_if_fail (GSD_IS_WACOM_DEVICE (osd_window->priv->pad));

	if (!gtk_widget_get_realized (widget))
		return;

	screen = gtk_widget_get_screen (widget);
	gdk_window = gtk_widget_get_window (widget);

	transparent.red = transparent.green = transparent.blue = 0.0;
	transparent.alpha = BACK_OPACITY;
	gdk_window_set_background_rgba (gdk_window, &transparent);

	hide_cursor (osd_window);

	/* Determine the monitor for that device and set appropriate fullscreen mode*/
	monitor = gsd_wacom_device_get_display_monitor (osd_window->priv->pad);

	if (monitor == GSD_WACOM_SET_ALL_MONITORS) {
		/* Pick a monitor for the OSD to appear */
		monitor = gdk_screen_get_primary_monitor (screen);
	}

	gdk_screen_get_monitor_geometry (screen, monitor, &osd_window->priv->screen_area);
	osd_window->priv->monitor_area = osd_window->priv->screen_area;
	gdk_window_set_fullscreen_mode (gdk_window, GDK_FULLSCREEN_ON_CURRENT_MONITOR);

	gtk_window_set_default_size (GTK_WINDOW (osd_window),
	                             osd_window->priv->screen_area.width,
	                             osd_window->priv->screen_area.height);

	status = get_image_size (gsd_wacom_device_get_layout_path (osd_window->priv->pad),
	                         &osd_window->priv->tablet_area.width,
	                         &osd_window->priv->tablet_area.height);
	if (status == FALSE)
		osd_window->priv->tablet_area = osd_window->priv->monitor_area;

	/* Position the window at its expected postion before moving
	 * to fullscreen, so the window will be on the right monitor.
	 */
	gtk_window_move (GTK_WINDOW (osd_window),
	                 osd_window->priv->screen_area.x,
	                 osd_window->priv->screen_area.y);

	gtk_window_fullscreen (GTK_WINDOW (osd_window));
	gtk_window_set_keep_above (GTK_WINDOW (osd_window), TRUE);
}

static gboolean
gsd_wacom_osd_window_key_release_event (GtkWidget    *widget,
					GdkEventKey  *event)
{
	GsdWacomOSDWindow *osd_window;
	osd_window = GSD_WACOM_OSD_WINDOW (widget);

	if (event->type != GDK_KEY_RELEASE)
		goto out;

	if (osd_window->priv->edition_mode) {
		if (event->keyval == GDK_KEY_Escape && !gtk_widget_get_visible (osd_window->priv->editor))
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (osd_window->priv->change_mode_button),
						      FALSE);

		goto out;
	}

	gtk_widget_destroy (widget);

 out:
	GTK_WIDGET_CLASS (gsd_wacom_osd_window_parent_class)->key_release_event (widget, event);

	return FALSE;
}

static void
gsd_wacom_osd_window_set_device (GsdWacomOSDWindow *osd_window,
				 GsdWacomDevice    *device)
{
	GsdWacomRotation  device_rotation;
	GsdWacomRotation  output_rotation;
	GSettings        *settings;
	GList            *list, *l;

	g_return_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window));
	g_return_if_fail (GSD_IS_WACOM_DEVICE (device));

	/* If we had a layout previously handled, get rid of it */
	if (osd_window->priv->handle)
		g_object_unref (osd_window->priv->handle);
	osd_window->priv->handle = NULL;

	/* Bind the device with the OSD window */
	if (osd_window->priv->pad)
		g_object_weak_unref (G_OBJECT(osd_window->priv->pad),
		                     (GWeakNotify) gtk_widget_destroy,
		                     osd_window);
	osd_window->priv->pad = device;
	g_object_weak_ref (G_OBJECT(osd_window->priv->pad),
	                   (GWeakNotify) gtk_widget_destroy,
	                   osd_window);

	/* Capture current rotation, we do not update that later, OSD window is meant to be short lived */
	settings = gsd_wacom_device_get_settings (osd_window->priv->pad);
	device_rotation = g_settings_get_enum (settings, ROTATION_KEY);
	output_rotation = gsd_wacom_device_get_display_rotation (osd_window->priv->pad);
	osd_window->priv->rotation = display_relative_rotation (device_rotation, output_rotation);

	/* Create the buttons */
	list = gsd_wacom_device_get_buttons (device);
	for (l = list; l != NULL; l = l->next) {
		GsdWacomTabletButton *tablet_button = l->data;

		gsd_wacom_osd_window_add_tablet_button (osd_window, tablet_button);
	}
	g_list_free (list);

	g_clear_pointer (&osd_window->priv->regular_mode_message, g_free);
	osd_window->priv->regular_mode_message = get_regular_mode_message (osd_window);

}

GsdWacomDevice *
gsd_wacom_osd_window_get_device (GsdWacomOSDWindow *osd_window)
{
	g_return_val_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window), NULL);

	return osd_window->priv->pad;
}

void
gsd_wacom_osd_window_set_message (GsdWacomOSDWindow *osd_window,
				  const gchar       *str)
{
	g_return_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window));

	g_free (osd_window->priv->message);
	osd_window->priv->message = g_strdup (str);
}

const char *
gsd_wacom_osd_window_get_message (GsdWacomOSDWindow *osd_window)
{
	g_return_val_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window), NULL);

	return osd_window->priv->message;
}

static void
gsd_wacom_osd_window_set_property (GObject        *object,
				   guint           prop_id,
				   const GValue   *value,
				   GParamSpec     *pspec)
{
	GsdWacomOSDWindow *osd_window;

	osd_window = GSD_WACOM_OSD_WINDOW (object);

	switch (prop_id) {
	case PROP_OSD_WINDOW_MESSAGE:
		gsd_wacom_osd_window_set_message (osd_window, g_value_get_string (value));
		break;
	case PROP_OSD_WINDOW_EDITION_MODE:
		osd_window->priv->edition_mode = g_value_get_boolean (value);
		break;
	case PROP_OSD_WINDOW_GSD_WACOM_DEVICE:
		gsd_wacom_osd_window_set_device (osd_window, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gsd_wacom_osd_window_get_property (GObject        *object,
				   guint           prop_id,
				   GValue         *value,
				   GParamSpec     *pspec)
{
	GsdWacomOSDWindow *osd_window;

	osd_window = GSD_WACOM_OSD_WINDOW (object);

	switch (prop_id) {
	case PROP_OSD_WINDOW_MESSAGE:
		g_value_set_string (value, osd_window->priv->message);
		break;
	case PROP_OSD_WINDOW_EDITION_MODE:
		g_value_set_boolean (value, osd_window->priv->edition_mode);
		break;
	case PROP_OSD_WINDOW_GSD_WACOM_DEVICE:
		g_value_set_object (value, (GObject*) osd_window->priv->pad);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
gsd_wacom_osd_window_set_active (GsdWacomOSDWindow    *osd_window,
				 GsdWacomTabletButton *button,
				 GtkDirectionType      dir,
				 gboolean              active)
{
	GsdWacomOSDWindowPrivate *priv;
	GList     *l;
	gchar     *id;

	g_return_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window));
	g_return_if_fail (button != NULL);

	priv = osd_window->priv;

	if (priv->current_button)
		priv->current_button->priv->visible = TRUE;

	id = get_tablet_button_id_name (button, dir);
	for (l = priv->buttons; l != NULL; l = l->next) {
		GsdWacomOSDButton *osd_button = l->data;
		if (MATCH_ID (osd_button, id)) {
			if (priv->edition_mode && button->type != WACOM_TABLET_BUTTON_TYPE_HARDCODED)
				priv->current_button = osd_button;
			else
				gsd_wacom_osd_button_set_active (osd_button, active);
		}
	}
	g_free (id);

	if (priv->edition_mode) {
		if (priv->current_button)
			priv->current_button->priv->visible = FALSE;

		if (button->type == WACOM_TABLET_BUTTON_TYPE_HARDCODED)
			return;

		gtk_widget_hide (priv->editor);
		gsd_wacom_button_editor_set_button (GSD_WACOM_BUTTON_EDITOR (priv->editor), button, dir);
		gtk_widget_show (priv->editor);

		redraw_window (osd_window);

		return;
	}
}

void
gsd_wacom_osd_window_set_mode (GsdWacomOSDWindow    *osd_window,
                               gint                  group_id,
                               gint                  mode)
{
	GList                *list, *l;

	list = gsd_wacom_device_get_buttons (osd_window->priv->pad);
	for (l = list; l != NULL; l = l->next) {
		GsdWacomTabletButton *tablet_button = l->data;
		GList                *l2;
		gchar                *id_up, *id_down;

		if (tablet_button->type != WACOM_TABLET_BUTTON_TYPE_STRIP &&
		    tablet_button->type != WACOM_TABLET_BUTTON_TYPE_RING)
			continue;
		if (tablet_button->group_id != group_id)
			continue;

		id_up = get_tablet_button_id_name (tablet_button, GTK_DIR_UP);
		id_down = get_tablet_button_id_name (tablet_button, GTK_DIR_DOWN);

		for (l2 = osd_window->priv->buttons; l2 != NULL; l2 = l2->next) {
			GsdWacomOSDButton *osd_button = l2->data;
			gboolean           visible = (tablet_button->idx == mode - 1);

			if (MATCH_ID (osd_button, id_up) || MATCH_ID (osd_button, id_down)) {
				gsd_wacom_osd_button_set_visible (osd_button, visible);

				if (osd_window->priv->current_button) {
					gchar *current_id;
					GtkDirectionType dir;

					gsd_wacom_button_editor_get_button (GSD_WACOM_BUTTON_EDITOR (osd_window->priv->editor), &dir);
					current_id = get_tablet_button_id_name (tablet_button, dir);

					if (MATCH_ID (osd_button, current_id) && visible) {
						osd_window->priv->current_button = osd_button;

						gtk_widget_hide (osd_window->priv->editor);
						gsd_wacom_button_editor_set_button (GSD_WACOM_BUTTON_EDITOR (osd_window->priv->editor), tablet_button, dir);
						gtk_widget_show (osd_window->priv->editor);
					}
				}

				redraw_window (osd_window);
			}
		}

		g_free (id_up);
		g_free (id_down);

	}
	g_list_free (list);
}

gboolean
gsd_wacom_osd_window_get_edition_mode (GsdWacomOSDWindow *osd_window)
{
	g_return_val_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window), FALSE);

	return osd_window->priv->edition_mode;
}

void
gsd_wacom_osd_window_set_edition_mode (GsdWacomOSDWindow *osd_window, gboolean edition_mode)
{
	g_return_if_fail (GSD_IS_WACOM_OSD_WINDOW (osd_window));

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (osd_window->priv->change_mode_button),
				      edition_mode);
}

GtkWidget *
gsd_wacom_osd_window_new (GsdWacomDevice       *pad,
                          const gchar          *message)
{
	GsdWacomOSDWindow *osd_window;
	GdkScreen         *screen;
	GdkVisual         *visual;
	GtkWidget         *button, *box, *overlay;
	GtkStyleContext   *style_context;

	osd_window = GSD_WACOM_OSD_WINDOW (g_object_new (GSD_TYPE_WACOM_OSD_WINDOW,
	                                                 "type",              GTK_WINDOW_POPUP,
	                                                 "skip-pager-hint",   TRUE,
	                                                 "skip-taskbar-hint", TRUE,
	                                                 "focus-on-map",      TRUE,
	                                                 "decorated",         FALSE,
	                                                 "deletable",         FALSE,
	                                                 "accept-focus",      TRUE,
	                                                 "wacom-device",      pad,
	                                                 "message",           message,
	                                                 NULL));

	/* Must set the visual before realizing the window */
	gtk_widget_set_app_paintable (GTK_WIDGET (osd_window), TRUE);
	screen = gdk_screen_get_default ();
	visual = gdk_screen_get_rgba_visual (screen);
	if (visual == NULL)
		visual = gdk_screen_get_system_visual (screen);
	gtk_widget_set_visual (GTK_WIDGET (osd_window), visual);

	osd_window->priv->editor = gsd_wacom_button_editor_new ();
	g_signal_connect (osd_window->priv->editor, "button-edited",
			  G_CALLBACK (on_button_edited),
			  osd_window);
	g_signal_connect (osd_window->priv->editor, "done-editing",
			  G_CALLBACK (on_button_editing_done),
			  osd_window);

	g_signal_connect (GTK_WIDGET (osd_window), "realize",
	                  G_CALLBACK (gsd_wacom_osd_window_realized),
	                  NULL);

	overlay = gtk_overlay_new ();
	gtk_container_add (GTK_CONTAINER (osd_window), overlay);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add (GTK_CONTAINER (overlay), box);

	gtk_overlay_add_overlay (GTK_OVERLAY (overlay), osd_window->priv->editor);

	button = gtk_toggle_button_new_with_label (_("Edit"));
	g_object_set (button, "halign", GTK_ALIGN_CENTER, NULL);

	style_context = gtk_widget_get_style_context (button);
	gtk_style_context_add_class (style_context, "osd");

	gtk_box_pack_end (GTK_BOX (box), button, FALSE, FALSE, 12);
	osd_window->priv->change_mode_button = button;

	gtk_widget_show (overlay);
	gtk_widget_show (box);
	gtk_widget_show (osd_window->priv->change_mode_button);

	g_signal_connect (osd_window->priv->change_mode_button, "clicked",
			  G_CALLBACK (on_change_mode_button_clicked),
			  osd_window);

	g_signal_connect (overlay, "get-child-position",
			  G_CALLBACK (on_get_child_position),
			  osd_window);

	osd_window->priv->regular_mode_message = get_regular_mode_message (osd_window);

	edition_mode_changed (osd_window);

	return GTK_WIDGET (osd_window);
}

static void
gsd_wacom_osd_window_class_init (GsdWacomOSDWindowClass *klass)
{
	GObjectClass *gobject_class;
	GtkWidgetClass *widget_class;

	gobject_class = G_OBJECT_CLASS (klass);
	widget_class  = GTK_WIDGET_CLASS (klass);

	gobject_class->set_property = gsd_wacom_osd_window_set_property;
	gobject_class->get_property = gsd_wacom_osd_window_get_property;
	gobject_class->finalize     = gsd_wacom_osd_window_finalize;
	widget_class->draw          = gsd_wacom_osd_window_draw;
	widget_class->motion_notify_event = gsd_wacom_osd_window_motion_notify_event;
	widget_class->show          = gsd_wacom_osd_window_show;
	widget_class->key_release_event = gsd_wacom_osd_window_key_release_event;

	g_object_class_install_property (gobject_class,
	                                 PROP_OSD_WINDOW_MESSAGE,
	                                 g_param_spec_string ("message",
	                                                      "Window message",
	                                                      "The message shown in the OSD window",
	                                                      "",
	                                                      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
	                                 PROP_OSD_WINDOW_GSD_WACOM_DEVICE,
	                                 g_param_spec_object ("wacom-device",
	                                                      "Wacom device",
	                                                      "The Wacom device represented by the OSD window",
	                                                      GSD_TYPE_WACOM_DEVICE,
	                                                      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class,
	                                 PROP_OSD_WINDOW_EDITION_MODE,
	                                 g_param_spec_boolean ("edition-mode",
							       "Edition mode",
							       "The edition mode of the OSD Window.",
							       FALSE,
							       G_PARAM_READWRITE));


	g_type_class_add_private (klass, sizeof (GsdWacomOSDWindowPrivate));
}

static void
gsd_wacom_osd_window_init (GsdWacomOSDWindow *osd_window)
{
	GtkSettings *settings;
	osd_window->priv = GSD_WACOM_OSD_WINDOW_GET_PRIVATE (osd_window);
	osd_window->priv->cursor_timeout = 0;
	gtk_widget_add_events (GTK_WIDGET (osd_window), GDK_POINTER_MOTION_MASK);

	osd_window->priv->edition_mode = FALSE;

	osd_window->priv->edition_mode_message = get_edition_mode_message (osd_window);

	settings = gtk_settings_get_default ();
	g_object_set (G_OBJECT (settings), "gtk-application-prefer-dark-theme", TRUE, NULL);
}

static void
gsd_wacom_osd_window_finalize (GObject *object)
{
	GsdWacomOSDWindow *osd_window;
	GsdWacomOSDWindowPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GSD_IS_WACOM_OSD_WINDOW (object));

	osd_window = GSD_WACOM_OSD_WINDOW (object);
	g_return_if_fail (osd_window->priv != NULL);

	priv = osd_window->priv;
	cursor_timeout_stop (osd_window);
	g_clear_object (&priv->handle);
	g_clear_pointer (&priv->message, g_free);
	g_clear_pointer (&priv->regular_mode_message, g_free);
	g_clear_pointer (&priv->edition_mode_message, g_free);

	if (priv->pad) {
		g_object_weak_unref (G_OBJECT(priv->pad),
				     (GWeakNotify) gtk_widget_destroy,
				     osd_window);
	}

	if (priv->buttons) {
		g_list_free_full (priv->buttons, g_object_unref);
		priv->buttons = NULL;
	}

	G_OBJECT_CLASS (gsd_wacom_osd_window_parent_class)->finalize (object);
}

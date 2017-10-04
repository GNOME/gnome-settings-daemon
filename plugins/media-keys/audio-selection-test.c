/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Bastien Nocera <hadess@hadess.net>
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
 */

#include "config.h"

#include <gtk/gtk.h>

#define AUDIO_SELECTION_DBUS_NAME               "org.gnome.Shell.AudioDeviceSelection"
#define AUDIO_SELECTION_DBUS_PATH               "/org/gnome/Shell/AudioDeviceSelection"
#define AUDIO_SELECTION_DBUS_INTERFACE          "org.gnome.Shell.AudioDeviceSelection"

static guint audio_selection_watch_id;
static guint audio_selection_signal_id;
static GDBusConnection *audio_selection_conn;
static gboolean audio_selection_requested;
static GtkWidget *check_headphones, *check_headset, *check_micro;
static GtkWidget *button, *label;

/* Copy-paste from gvc-mixer-control.h */
typedef enum
{
	GVC_HEADSET_PORT_CHOICE_NONE        = 0,
	GVC_HEADSET_PORT_CHOICE_HEADPHONES  = 1 << 0,
	GVC_HEADSET_PORT_CHOICE_HEADSET     = 1 << 1,
	GVC_HEADSET_PORT_CHOICE_MIC         = 1 << 2
} GvcHeadsetPortChoice;

typedef struct {
	GvcHeadsetPortChoice choice;
	gchar *name;
} AudioSelectionChoice;

static AudioSelectionChoice audio_selection_choices[] = {
	{ GVC_HEADSET_PORT_CHOICE_HEADPHONES,   "headphones" },
	{ GVC_HEADSET_PORT_CHOICE_HEADSET,      "headset" },
	{ GVC_HEADSET_PORT_CHOICE_MIC,          "microphone" },
};

static void
audio_selection_done (GDBusConnection *connection,
		      const gchar     *sender_name,
		      const gchar     *object_path,
		      const gchar     *interface_name,
		      const gchar     *signal_name,
		      GVariant        *parameters,
		      gpointer         data)
{
	const gchar *choice;

	if (!audio_selection_requested)
		return;

	choice = NULL;
	g_variant_get_child (parameters, 0, "&s", &choice);
	if (!choice)
		return;

	gtk_label_set_text (GTK_LABEL (label), choice);

	audio_selection_requested = FALSE;
}

static void
audio_selection_needed (GvcHeadsetPortChoice  choices)
{
	gchar *args[G_N_ELEMENTS (audio_selection_choices) + 1];
	guint i, n;

	if (!audio_selection_conn)
		return;

	n = 0;
	for (i = 0; i < G_N_ELEMENTS (audio_selection_choices); ++i) {
		if (choices & audio_selection_choices[i].choice)
			args[n++] = audio_selection_choices[i].name;
	}
	args[n] = NULL;

	audio_selection_requested = TRUE;
	g_dbus_connection_call (audio_selection_conn,
				AUDIO_SELECTION_DBUS_NAME,
				AUDIO_SELECTION_DBUS_PATH,
				AUDIO_SELECTION_DBUS_INTERFACE,
				"Open",
				g_variant_new ("(^as)", args),
				NULL,
				G_DBUS_CALL_FLAGS_NONE,
				-1, NULL, NULL, NULL);
}

static void
update_ask_button (void)
{
	guint num_buttons = 0;
	gboolean active = FALSE;

	/* Need gnome-shell running */
	if (audio_selection_conn == NULL)
		goto end;

	/* Need at least 2 choices */
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_headphones)))
		num_buttons++;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_headset)))
		num_buttons++;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_micro)))
		num_buttons++;

	if (num_buttons < 2)
		goto end;

	/* And no questions in flight */
	if (audio_selection_requested)
		goto end;

	active = TRUE;

end:
	gtk_widget_set_sensitive (GTK_WIDGET (button), active);
}

static void
audio_selection_appeared (GDBusConnection *connection,
			  const gchar     *name,
			  const gchar     *name_owner,
			  gpointer         data)
{
	audio_selection_conn = connection;
	audio_selection_signal_id =
		g_dbus_connection_signal_subscribe (connection,
						    AUDIO_SELECTION_DBUS_NAME,
						    AUDIO_SELECTION_DBUS_INTERFACE,
						    "DeviceSelected",
						    AUDIO_SELECTION_DBUS_PATH,
						    NULL,
						    G_DBUS_SIGNAL_FLAGS_NONE,
						    audio_selection_done,
						    NULL,
						    NULL);
	update_ask_button ();
}

static void
audio_selection_vanished (GDBusConnection *connection,
			  const gchar     *name,
			  gpointer         data)
{
	if (audio_selection_signal_id)
		g_dbus_connection_signal_unsubscribe (audio_selection_conn,
						      audio_selection_signal_id);
	audio_selection_signal_id = 0;
	audio_selection_conn = NULL;
	update_ask_button ();
}

static void
watch_gnome_shell (void)
{
	audio_selection_watch_id =
		g_bus_watch_name (G_BUS_TYPE_SESSION,
				  AUDIO_SELECTION_DBUS_NAME,
				  G_BUS_NAME_WATCHER_FLAGS_NONE,
				  audio_selection_appeared,
				  audio_selection_vanished,
				  NULL,
				  NULL);
}

static void
check_buttons_changed (GtkToggleButton *button,
		       gpointer         user_data)
{
	update_ask_button ();
}

static void
button_clicked (GtkButton *button,
		gpointer   user_data)
{
	guint choices = 0;

	gtk_label_set_text (GTK_LABEL (label), "");

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_headphones)))
		choices |= GVC_HEADSET_PORT_CHOICE_HEADPHONES;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_headset)))
		choices |= GVC_HEADSET_PORT_CHOICE_HEADSET;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_micro)))
		choices |= GVC_HEADSET_PORT_CHOICE_MIC;

	audio_selection_needed (choices);
}

static void
setup_ui (void)
{
	GtkWidget *window;
	GtkWidget *box;

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	g_signal_connect (GTK_WINDOW (window), "delete-event",
			  G_CALLBACK (gtk_main_quit), NULL);
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_add (GTK_CONTAINER (window), box);

	check_headphones = gtk_check_button_new_with_label ("Headphones");
	g_signal_connect (check_headphones, "toggled",
			  G_CALLBACK (check_buttons_changed), NULL);
	gtk_container_add (GTK_CONTAINER (box), check_headphones);

	check_headset = gtk_check_button_new_with_label ("Headset");
	g_signal_connect (check_headset, "toggled",
			  G_CALLBACK (check_buttons_changed), NULL);
	gtk_container_add (GTK_CONTAINER (box), check_headset);

	check_micro = gtk_check_button_new_with_label ("Microphone");
	g_signal_connect (check_micro, "toggled",
			  G_CALLBACK (check_buttons_changed), NULL);
	gtk_container_add (GTK_CONTAINER (box), check_micro);

	button = gtk_button_new_with_label ("Ask!");
	g_signal_connect (button, "clicked",
			  G_CALLBACK (button_clicked), NULL);
	gtk_container_add (GTK_CONTAINER (box), button);
	gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);

	label = gtk_label_new ("");
	gtk_container_add (GTK_CONTAINER (box), label);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_headphones), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_headset), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_micro), TRUE);

	gtk_widget_show_all (window);
}

int main (int argc, char **argv)
{
	gtk_init (&argc, &argv);

	setup_ui ();
	watch_gnome_shell ();

	gtk_main ();

	return 0;
}

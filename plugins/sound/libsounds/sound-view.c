#include <config.h>
#include <string.h>
#include "sound-view.h"
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>

static GtkVBoxClass *sound_view_parent_class;

enum {
	CATEGORY,
	EVENT
} SoundViewRowType;

struct _SoundViewPrivate
{
	GtkWidget *table;
	GPtrArray *combo_box_info;
	SoundProperties *props;
	GtkWidget *label;
};

typedef struct
{
	SoundView *view;
	SoundEvent *event;
	gint old_active_index;
	gchar *full_filename;
	GtkComboBox *combo_box;
	GtkButton *button;
} ComboBoxEntryInfo;

enum {
	EVENT_COLUMN,
	FILE_COLUMN,
	SORT_DATA_COLUMN,
	TYPE_COLUMN,
	DATA_COLUMN,
	NUM_COLUMNS
};

#define MAPPING_SIZE 7
static char* mapping_logicalnames[MAPPING_SIZE] = {
	N_("Login"),
        N_("Logout"),
        N_("Boing"),
	N_("Siren"),
	N_("Clink"),
	N_("Beep"),
	N_("No sound")
};
static char* mapping_filenames[MAPPING_SIZE] =  {
	"startup3.wav",
	"shutdown1.wav",
	"info.wav",
	"error.wav",
	"gtk-events/clicked.wav",
	"gtk-events/activate.wav",
	""
};

static void sound_view_reload (SoundView *view);

static void
sound_view_destroy (GtkObject *object)
{
	SoundView *view = SOUND_VIEW (object);

	if (view->priv != NULL)
	{
		if(view->priv->combo_box_info)
            g_ptr_array_free(view->priv->combo_box_info, TRUE);
		g_free (view->priv);
		view->priv = NULL;
	}

	if (GTK_OBJECT_CLASS (sound_view_parent_class)->destroy)
		GTK_OBJECT_CLASS (sound_view_parent_class)->destroy (object);
}

static void
sound_view_class_init (GtkObjectClass *object_class)
{
	sound_view_parent_class = gtk_type_class (gtk_vbox_get_type ());

	object_class->destroy = sound_view_destroy;
}

static gint get_mapping_position(const char *filename)
{
	gint x;
	for(x = 0; x < MAPPING_SIZE; x++)
	{
		if(!g_ascii_strcasecmp(mapping_filenames[x], filename))
			return x;
	}
	return -1;
}

static void show_play_error(gchar *msg)
{
	GtkWidget *md =
		gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, msg);
	gtk_dialog_run (GTK_DIALOG (md));
	gtk_widget_destroy (md);
	
	return;
}

static void play_preview_cb (GtkButton *button, gpointer user_data)
{
    ComboBoxEntryInfo *info = (ComboBoxEntryInfo*) user_data;
	GtkComboBox *combo_box = (GtkComboBox*) info->combo_box;

	gchar *filename = NULL, *temp;
	gint active_index = gtk_combo_box_get_active(combo_box);

	if(active_index < 0)
	{
		g_warning("play_cb() - Problem: ComboBox should be active");
		return;
	}

	if(active_index < MAPPING_SIZE)
		filename = g_strdup(mapping_filenames[active_index]);
	else
		filename = g_strdup(info->full_filename);

	if(!filename || !strlen(filename) )
	{
		g_free(filename);
		show_play_error(_("Sound not set for this event.") );
		return;
	}

	if('/' != filename[0] )
	{
		temp = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_SOUND, filename, TRUE, NULL);
		g_free(filename);
		if(!temp) {
			show_play_error(_("The sound file for this event does not exist.\n"
									 "You may want to install the gnome-audio package "
									 "for a set of default sounds.") );
			return;
		}
		filename = temp;
	}

	if(! g_file_test (filename, G_FILE_TEST_EXISTS) )
	{
		g_free(filename);
		show_play_error(_("The sound file for this event does not exist.") );
		return;
	}

    gnome_sound_play (filename);
	g_free (filename);
}

static void combo_box_changed_cb(GtkComboBox *combo, gpointer user_data)
{
	ComboBoxEntryInfo *info = (ComboBoxEntryInfo*) user_data;
    SoundEvent *event = (SoundEvent*)info->event;

	gchar *filename = NULL;
	gint active_index = gtk_combo_box_get_active(combo);
	if(active_index < MAPPING_SIZE)
		filename = mapping_filenames[active_index];
	else if(active_index == MAPPING_SIZE) //ie they want the file chooser
	{
		gboolean valid_wav_file_chosen = FALSE;
		GtkWidget *dialog;

		if(info->old_active_index < MAPPING_SIZE)
			filename = mapping_filenames[info->old_active_index];
		else
            filename = info->full_filename;

		if('/' != filename[0])
			filename = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_SOUND, filename, TRUE, NULL);
        filename = g_path_get_dirname(filename);

		dialog = gtk_file_chooser_dialog_new(_("Select Sound File"), NULL,
			GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), filename);

		while(!valid_wav_file_chosen && gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
		{
			gchar *mime_type;

			filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
			mime_type = gnome_vfs_get_mime_type (filename);
            if (mime_type && !strcmp (mime_type, "audio/x-wav") )
			{
				valid_wav_file_chosen = TRUE;
			}
			else
			{
				GtkWidget *msg_dialog;
				gchar *msg;

				msg = g_strdup_printf (_("The file %s is not a valid wav file"),filename);
				msg_dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, msg);
				gtk_dialog_run (GTK_DIALOG (msg_dialog));
				gtk_widget_destroy (msg_dialog);
				g_free (msg);
			}
			g_free (mime_type);
		}
		gtk_widget_destroy(dialog);
		if(valid_wav_file_chosen)
		{
			gchar *temp;

			info->full_filename = g_strdup(filename);
			temp = g_filename_display_basename(filename);
			gtk_combo_box_remove_text(combo, MAPPING_SIZE + 1);
			gtk_combo_box_append_text(combo, temp);
			g_free(temp);
			active_index = MAPPING_SIZE + 1;
			gtk_combo_box_set_active(combo, active_index);
			sound_event_set_oldfile(event, filename);
		}
		else
		{
			gtk_combo_box_set_active(combo, info->old_active_index);
			return;
		}
	}
	else
		filename = info->full_filename;

	gtk_widget_set_sensitive(GTK_WIDGET(info->button), filename && strlen(filename));

	//printf("new filename is %s\n", filename);
	info->old_active_index = active_index;
	sound_event_set_file (event, filename);
	sound_properties_event_changed (info->view->priv->props, event);
}

static GtkWidget*
create_populate_combo_box()
{
	GtkWidget *combo_box;
	GtkCellRenderer *cell;
	GtkListStore *store;
	gint index;

	store = gtk_list_store_new (1, G_TYPE_STRING);
	combo_box = gtk_combo_box_new_with_model (GTK_TREE_MODEL (store));
	g_object_unref (store);

	cell = gtk_cell_renderer_text_new ();
	g_object_set(cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), cell, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), cell,
									"text", 0,
									NULL);

	for(index = 0; index < MAPPING_SIZE; index++) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), _(mapping_logicalnames[index]));
	}
	gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), _("Select sound file..."));

	return combo_box;
}

static gchar* generate_event_label(SoundEvent *event)
{
	//For now I just make the mnemonic char the first char in string. Do we want to
	//come up with an explicit mapping
	return g_strdup_printf("_%s:", event->desc);
}

static void
add_sound_item (GtkTable *table, SoundView *view, SoundEvent *event)
{
	GtkWidget *label;
	GtkWidget *combo;
	gint index;
	ComboBoxEntryInfo *info;
	gchar *temp;

	info = g_new0(ComboBoxEntryInfo, 1);

	temp = generate_event_label(event);
	label = gtk_label_new_with_mnemonic (temp);
	g_free(temp);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_table_attach (GTK_TABLE (table), label,
		0, 1, table->nrows - 1, table->nrows,
		GTK_FILL, GTK_SHRINK, 0, 0);

	combo = create_populate_combo_box();
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), combo);

	/* If the user currently has a custom sound file selected as opposed to a standard sound
	   file (ie one in the mappings_*) then add this file to the box. Otherwise don't add the
	   current file as it is already added as a standard mapping, but do check to see if there
	   is an old custom file that we need to add to the list so the user can easily revert back
	   to the old file just by selecting it rather then having to go find it via the chooser
	*/
	index = get_mapping_position(event->file);
	if(-1 == index)
	{
		info->full_filename = g_strdup(event->file);
		temp = g_filename_display_basename(event->file);
		//printf("Debug - result of g_filename_display_basename is %s\n", temp);
		gtk_combo_box_append_text (GTK_COMBO_BOX (combo), temp);
		g_free(temp);
		index = MAPPING_SIZE + 1;
	}
	else
	{
		if(event->oldfile)
		{
			info->full_filename = g_strdup(event->oldfile);
			temp = g_filename_display_basename(event->oldfile);
			//printf("Debug - result of old g_filename_display_basename is %s\n", temp);
			gtk_combo_box_append_text (GTK_COMBO_BOX (combo), temp);
			g_free(temp);
		}
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), index);

	info->event = event;
	info->view = view;
	info->old_active_index = index;
	info->combo_box = GTK_COMBO_BOX(combo);
	g_ptr_array_add(view->priv->combo_box_info, info);
	g_signal_connect (combo, "changed", G_CALLBACK(combo_box_changed_cb), info);

	gtk_table_attach (GTK_TABLE (table), combo,
		1, 2, table->nrows - 1, table->nrows,
		GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 3);

	info->button = GTK_BUTTON (gtk_button_new_from_stock (GTK_STOCK_MEDIA_PLAY));
	g_signal_connect (G_OBJECT(info->button), "clicked", (GCallback) play_preview_cb, info);

	index = gtk_combo_box_get_active(info->combo_box);
	if(index < MAPPING_SIZE)
		temp = mapping_filenames[index];
	else
		temp = info->full_filename;
	gtk_widget_set_sensitive(GTK_WIDGET(info->button), temp && strlen(temp));

	gtk_table_attach (GTK_TABLE (table), GTK_WIDGET (info->button),
		2, 3, table->nrows - 1, table->nrows,
		GTK_SHRINK, GTK_SHRINK, 0, 3);

	gtk_table_resize (table, table->nrows + 1, table->ncols);
}

static void
sound_view_init (GtkObject *object)
{
	SoundView *view = SOUND_VIEW (object);
	GtkWidget *align;
	gchar *str;

	gtk_box_set_spacing (GTK_BOX (view), 6);
	view->priv = g_new0 (SoundViewPrivate, 1);
	view->priv->combo_box_info = g_ptr_array_new();

	str = g_strdup_printf ("<span weight=\"bold\">%s</span>", _("System Sounds"));
	view->priv->label = gtk_label_new (str);
	g_free (str);
	gtk_label_set_use_markup (GTK_LABEL (view->priv->label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (view->priv->label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (view), view->priv->label, FALSE, FALSE, 0);

	align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
  	gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
	gtk_box_pack_start (GTK_BOX (view), align, FALSE, FALSE, 0);

	view->priv->table = gtk_table_new (1, 3, FALSE);
	gtk_table_set_col_spacing(GTK_TABLE(view->priv->table), 0, 12);
	gtk_table_set_col_spacing(GTK_TABLE(view->priv->table), 1, 6);
	gtk_table_set_row_spacings(GTK_TABLE(view->priv->table), 0);
	gtk_container_add (GTK_CONTAINER (align), view->priv->table);
	gtk_widget_show_all (align);
}

GtkType
sound_view_get_type (void)
{
	static GtkType type = 0;

	if (!type)
	{
		GTypeInfo info =
		{
			sizeof (SoundViewClass),
			NULL, NULL,
			(GClassInitFunc) sound_view_class_init,
			NULL, NULL,
			sizeof (SoundView),
			0,
			(GInstanceInitFunc) sound_view_init
		};
		
		type = g_type_register_static (gtk_vbox_get_type (), "SoundView", &info, 0);
	}

	return type;
}

GtkWidget*
sound_view_new (SoundProperties *props)
{
	SoundView *view = g_object_new (sound_view_get_type (), NULL);
	view->priv->props = props;
	sound_view_reload (view);

	return GTK_WIDGET (view);
}

static void
foreach_cb (gchar *category, gchar *desc, GList *events, SoundView *view)
{
	GList *l;

	/* No events in this category... */
	if (!events)
		return;

	for (l = events; l != NULL; l = l->next)
	{
		SoundEvent *event = l->data;
		add_sound_item (GTK_TABLE(view->priv->table), view, event);

	}
}

static void
sound_view_reload (SoundView *view)
{
	g_return_if_fail (SOUND_IS_VIEW (view));
	
	sound_properties_category_foreach (view->priv->props,
					   (SoundPropertiesCategoryForeachFunc) foreach_cb, view);
}


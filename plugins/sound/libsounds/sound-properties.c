#include <config.h>
#include "sound-properties.h"
#include <string.h>
#include <stdlib.h>
/* opendir */
#include <sys/types.h>
#include <dirent.h>

#include <gtk/gtk.h>
#include <libgnome/gnome-config.h>
#include <libgnome/gnome-program.h>
#include <libgnome/gnome-util.h>

#define ensure_category(category) ((category && category[0]) ? category : "gnome-2")

static GtkObjectClass *sound_properties_parent_class;

struct _SoundPropertiesPrivate
{
	GHashTable *hash;
	GPtrArray *events;
	int frozen;
	guint events_added;
};

enum
{
	EVENT_ADDED,
	EVENT_REMOVED,
	EVENT_CHANGED,
	CHANGED,
	LAST_SIGNAL
};

static guint sound_properties_signals[LAST_SIGNAL] = { 0 };

typedef struct 
{
	GList *list;
	GHashTable *hash;
	gchar *desc;
} SoundSubList;

typedef struct
{
	SoundPropertiesCategoryForeachFunc func;
	gpointer user_data;
} SoundClosure;

static void
cleanup_cb (gpointer key, gpointer value, gpointer user_data)
{
	SoundSubList *l;
	
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);

	g_free (key);
	l = value;
	g_list_free (l->list);
	g_hash_table_destroy (l->hash);
	g_free (l->desc);
	g_free (l);
}

static void
sound_properties_destroy (GtkObject *object)
{
	SoundProperties *props = SOUND_PROPERTIES (object);
	int i;

	if (props->priv != NULL) {
		g_hash_table_foreach (props->priv->hash, cleanup_cb, NULL);
		g_hash_table_destroy (props->priv->hash);

		for (i = 0; i < props->priv->events->len; i++)
			sound_event_free (g_ptr_array_index (props->priv->events, i)); 

		g_ptr_array_free (props->priv->events, FALSE);
		
		g_free (props->priv);
		props->priv = NULL;
	}

	if (GTK_OBJECT_CLASS (sound_properties_parent_class)->destroy)
		GTK_OBJECT_CLASS (sound_properties_parent_class)->destroy (object);
}

static void
sound_properties_class_init (GtkObjectClass *object_class)
{
	sound_properties_parent_class = gtk_type_class (gtk_object_get_type ());

	sound_properties_signals[EVENT_ADDED] =
		g_signal_new ("event_added",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SoundPropertiesClass, event_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	sound_properties_signals[EVENT_REMOVED] =
		g_signal_new ("event_removed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SoundPropertiesClass, event_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	sound_properties_signals[EVENT_CHANGED] =
		g_signal_new ("event_changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SoundPropertiesClass, event_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	sound_properties_signals[CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SoundPropertiesClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	object_class->destroy = sound_properties_destroy;
}

static void
sound_properties_init (GtkObject *object)
{
	SoundProperties *props = SOUND_PROPERTIES (object);
	
	props->priv = g_new0 (SoundPropertiesPrivate, 1);
	props->priv->hash = g_hash_table_new (g_str_hash, g_str_equal);
	props->priv->events = g_ptr_array_new ();
	props->priv->frozen = 0;
	props->priv->events_added = 0;
}


GType
sound_properties_get_type (void)
{
	static GType type = 0;

	if (!type)
	{
		GTypeInfo info =
		{
			sizeof (SoundPropertiesClass),
			NULL, NULL,
			(GClassInitFunc) sound_properties_class_init,
			NULL, NULL,
			sizeof (SoundProperties),
			0,
			(GInstanceInitFunc) sound_properties_init
		};
		
		type = g_type_register_static (GTK_TYPE_OBJECT, "SoundProperties", &info, 0);
	}

	return type;
}

SoundProperties*
sound_properties_new (void)
{
	return g_object_new (sound_properties_get_type (), NULL);
}

static void
sound_properties_freeze (SoundProperties *props)
{
	g_return_if_fail (SOUND_IS_PROPERTIES (props));

	props->priv->frozen++;
}

static void
sound_properties_thaw (SoundProperties *props)
{
	g_return_if_fail (SOUND_IS_PROPERTIES (props));

	props->priv->frozen--;

	if (props->priv->frozen < 1 && props->priv->events_added)
	{
		if (props->priv->events_added == 1)
		{
			SoundEvent *event;
			event = g_ptr_array_index (props->priv->events, props->priv->events->len - 1);
		
			g_signal_emit (GTK_OBJECT (props),
				       sound_properties_signals[EVENT_ADDED], 0,
				       event);
		}
		else
			sound_properties_changed (props);

		props->priv->events_added = 0;
	}
}

void	
sound_properties_add_directory (SoundProperties *props, gchar *directory,
				gboolean is_user, gchar *themedir)
{
	DIR *dir;
	struct dirent *dent;
	
	g_return_if_fail (SOUND_IS_PROPERTIES (props));
	g_return_if_fail (directory != NULL);

	dir = opendir (directory);
	if (!dir)
		return;

	sound_properties_freeze (props);

	while ((dent = readdir (dir)))
	{
		gchar *file;
		
		if (dent->d_name[0] == '.')
			continue;
		
		file = g_build_filename (directory, dent->d_name, NULL);
		sound_properties_add_file (props, file, is_user, themedir);
		g_free (file);
	}

	sound_properties_thaw (props);
	closedir (dir);
}

static gchar *
strip_ext (const gchar *filename, const gchar *suffix)
{	
	gchar *ret;
	int flen, elen;
	
	g_return_val_if_fail (filename != NULL, NULL);
	
	flen = strlen (filename);
	
	if (!suffix)
	{
		int i;
		for (i = flen - 1; i >=0; i--)
		{
			if (filename[i] == '/')
				return NULL;

			if (filename[i] == '.')
			{
				ret = g_new0 (gchar, i + 1);
				strncpy (ret, filename, i); 
				return ret;
			}
		}
		return NULL;
	}
				
	elen = strlen (suffix);
	if (g_ascii_strcasecmp (filename + flen - elen, suffix) == 0)
	{
		ret = g_new0 (gchar, flen - elen + 1);
		strncpy (ret, filename, flen - elen);
		return ret;
	}
	return NULL;
}

static SoundSubList*
ensure_hash (SoundProperties *props, gchar *category, gchar *cat_desc)
{
	SoundSubList *ret;
	
	g_return_val_if_fail (SOUND_IS_PROPERTIES (props), NULL);

	if (!(ret = g_hash_table_lookup (props->priv->hash, ensure_category (category))))
	{
		ret = g_new0 (SoundSubList, 1);
		ret->hash = g_hash_table_new (g_str_hash, g_str_equal);
		ret->list = NULL;
		ret->desc = cat_desc;
		g_hash_table_insert (props->priv->hash,
				     g_strdup (ensure_category (category)),
				     ret);
	}

	return ret;
}

static void
sound_properties_add_event (SoundProperties *props, gchar *prefix,
			    gchar *category, gchar *cat_desc, gchar *name,
			    gboolean is_user, gchar *themedir)
{
	SoundEvent *event, *old = NULL;
	gchar *path;
	gchar *file, *desc;
										
	g_return_if_fail (SOUND_IS_PROPERTIES (props));
	g_return_if_fail (prefix != NULL);
	g_return_if_fail (category != NULL);
	g_return_if_fail (name != NULL);
	
	path = g_strconcat (prefix, "/", name, "/", NULL);
	gnome_config_push_prefix (path);
	
	event = sound_event_new ();
	sound_event_set_category (event, category);
	sound_event_set_name (event, name);
	if ((old = sound_properties_lookup_event (props, category, name)))
	{
		sound_event_free (event);
		event = old;
	}

	file = gnome_config_get_string ("file");
	if (themedir && file && file[0] != '/')
	{
		gchar *tmp = g_build_filename (themedir, file, NULL);
		g_free (file);
		file = tmp;
	}
	sound_event_set_file (event, file);
	g_free (file);

	file = gnome_config_get_string ("oldfile");
	if (themedir && file && file[0] != '/')
	{
		gchar *tmp = g_build_filename (themedir, file, NULL);
		g_free (file);
		file = tmp;
	}
	if(file)
        sound_event_set_oldfile (event, file);
	g_free (file);

	desc = gnome_config_get_translated_string ("description");
	if (desc)
	{
		if (strcmp (desc, ""))
			sound_event_set_desc (event, desc);
		g_free (desc);
	}

	event->modified = is_user;
	if (themedir)
		event->theme = TRUE;

	gnome_config_pop_prefix ();

	if (!old)
	{
		SoundSubList *sub = ensure_hash (props, category, cat_desc);
		g_hash_table_insert (sub->hash, event->name, event);
		sub->list = g_list_append (sub->list, event);
		g_ptr_array_add (props->priv->events, event);
		props->priv->events_added++;
	}

	g_free (path);
}

void
sound_properties_add_file (SoundProperties *props, gchar *filename,
			   gboolean is_user, gchar *themedir)
{
	gchar *prefix, *basename;
	gchar *category, *cat_desc, *name;
	gpointer i;

	g_return_if_fail (SOUND_IS_PROPERTIES (props));
	g_return_if_fail (filename != NULL);

	basename = g_path_get_basename (filename);
	category = strip_ext (basename, ".soundlist");
	g_free (basename);
	
	if (!category)
		return;

	/* We now only do "system" sounds */
	if (g_ascii_strcasecmp(category, "gnome-2") &&	g_ascii_strcasecmp(category, "gtk-events-2"))
	{
		g_free (category);
		return;
	}
	
	prefix = g_strconcat ("=", filename, "=", NULL);
	
	sound_properties_freeze (props);

	gnome_config_push_prefix (prefix);
	cat_desc = gnome_config_get_translated_string ("__section_info__/description");
	gnome_config_pop_prefix ();
			
	i = gnome_config_init_iterator_sections (prefix);
	    
	while ((i = gnome_config_iterator_next (i, &name, NULL)))
	{
		if (!strcmp (name, "__section_info__"))
		{
			g_free (name);
			continue;
		}

		sound_properties_add_event (props, prefix, category, cat_desc,
					    name, is_user, themedir);
		g_free (name);
	}

	g_free (category);
	g_free (prefix);
	
	sound_properties_thaw (props);
}

void
sound_properties_add_defaults (SoundProperties *props, gchar *themedir)
{
	gchar *path[4];
	int i, theme = -1;
	
	g_return_if_fail (SOUND_IS_PROPERTIES (props));
	
	sound_properties_freeze (props);
	
	i = 0;
	path[i++] = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_CONFIG, "sound/events", TRUE, NULL);
	if (themedir)
		path[theme = i++] = g_strdup (themedir);
	path[i++] = gnome_util_home_file ("sound/events");
	path[i++] = NULL;
	
	for (i = 0; path[i] != NULL; i++)
	{
		sound_properties_add_directory (props, path[i], (path[i+1] == NULL), (theme == i) ? themedir : NULL);
		g_free (path[i]);
	}

	sound_properties_thaw (props);
}

void
sound_properties_event_changed (SoundProperties *props, SoundEvent *event)
{
	g_return_if_fail (SOUND_IS_PROPERTIES (props));

	g_signal_emit (GTK_OBJECT (props),
		       sound_properties_signals[EVENT_CHANGED], 0,
		       event);
}

SoundEvent*
sound_properties_lookup_event (SoundProperties *props, gchar *category,
		               gchar *name)
{
	SoundSubList *list;

	g_return_val_if_fail (SOUND_IS_PROPERTIES (props), NULL);
	list = g_hash_table_lookup (props->priv->hash,
				    ensure_category (category));
	
	if (!list)
		return NULL;

	return g_hash_table_lookup (list->hash, name);
}

guint
sound_properties_total_events (SoundProperties *props)
{
	g_return_val_if_fail (SOUND_IS_PROPERTIES (props), 0);

	return props->priv->events->len;
}

SoundEvent*
sound_properties_get_nth_event (SoundProperties *props, guint n)
{
	g_return_val_if_fail (SOUND_IS_PROPERTIES (props), NULL);
	g_return_val_if_fail (n < props->priv->events->len, NULL);

	return g_ptr_array_index (props->priv->events, n); 
}
							
void
sound_properties_foreach (SoundProperties *props,
			  SoundPropertiesForeachFunc func, gpointer user_data)
{
	int i;
	
	g_return_if_fail (SOUND_IS_PROPERTIES (props));
	
	for (i = 0; i < props->priv->events->len; i++)
		func (g_ptr_array_index (props->priv->events, i), user_data); 
}

void
sound_properties_changed (SoundProperties *props)
{
	g_return_if_fail (SOUND_IS_PROPERTIES (props));

	g_signal_emit (GTK_OBJECT (props),
		       sound_properties_signals[CHANGED], 0);
}

void
sound_properties_remove_event (SoundProperties *props, SoundEvent *event)
{
	g_return_if_fail (SOUND_IS_PROPERTIES (props));
	g_return_if_fail (event != NULL);
	
	g_ptr_array_remove (props->priv->events, event);
	g_signal_emit (GTK_OBJECT (props),
		       sound_properties_signals[EVENT_REMOVED], 0,
		       event);
	sound_event_free (event);
}

void
sound_properties_save (SoundProperties *props, gchar *directory, gboolean save_theme)
{
	int i;
	
	g_return_if_fail (SOUND_IS_PROPERTIES (props));
	g_return_if_fail (directory != NULL);

	for (i = 0; i < props->priv->events->len; i++)
	{
		SoundEvent *event = g_ptr_array_index (props->priv->events, i);
		gchar *path;
		const gchar *category;
		
		if (!(event->modified || (save_theme && event->theme)))
			continue;

		category = ensure_category (event->category);
		
		path = g_strdup_printf ("=%s/sound/events/%s.soundlist=/%s/file",
			directory,
			category,
			event->name);
		gnome_config_set_string (path,
					 (event->file) ? event->file : "");
		g_free (path);

		if(event->oldfile)
		{
			path = g_strdup_printf ("=%s/sound/events/%s.soundlist=/%s/oldfile",
				directory,
				category,
				event->name);
			gnome_config_set_string (path, event->oldfile);
			g_free (path);
		}

		/* Bah, have to make GNOME1 copies */
		if (!g_ascii_strcasecmp (category, "gnome-2") ||
		    !g_ascii_strcasecmp (category, "gtk-events-2"))
		{
			gchar *other = g_strndup (category, strlen (category) - 2);
			gchar *base = strip_ext (directory, ".gnome2");
			if (base)
			{
				gchar *gnome1 = g_build_filename (base, ".gnome", NULL);
				path = g_strdup_printf ("=%s/sound/events/%s.soundlist=/%s/file", gnome1, other, event->name);
				gnome_config_set_string (path, (event->file) ? event->file : "");
				g_free (path);
				g_free (gnome1);
				g_free (base);
			}
			g_free (other);
		}
	}

	gnome_config_sync ();
}

void
sound_properties_user_save (SoundProperties *props)
{
	gchar *directory;
	
	g_return_if_fail (SOUND_IS_PROPERTIES (props));

	directory = gnome_util_home_file (NULL);
	directory[strlen (directory) - 1] = '\0';
	sound_properties_save (props, directory, FALSE);
	g_free (directory);
}

GList*
sound_properties_lookup_category (SoundProperties *props, gchar *category)
{
	SoundSubList *sub;
	
	g_return_val_if_fail (SOUND_IS_PROPERTIES (props), NULL);
	g_return_val_if_fail (category != NULL, NULL);

	sub = g_hash_table_lookup (props->priv->hash,
				   ensure_category (category));

	if (sub)
		return sub->list;
	else
		return NULL;
}

static void
category_cb (gpointer key, gpointer value, gpointer data)
{
	SoundClosure *closure = data;
	SoundSubList *sub = value;

	closure->func (key, sub->desc, sub->list, closure->user_data);
}

void
sound_properties_category_foreach (SoundProperties *props,
				   SoundPropertiesCategoryForeachFunc func,
				   gpointer user_data)
{
	SoundClosure closure;

	g_return_if_fail (SOUND_IS_PROPERTIES (props));
	
	closure.func = func;
	closure.user_data = user_data;

	g_hash_table_foreach (props->priv->hash, category_cb, &closure);
}


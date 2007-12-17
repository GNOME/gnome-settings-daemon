#ifndef __SOUND_PROPERTIES_H__
#define __SOUND_PROPERTIES_H__

#include <gtk/gtkobject.h>
#include "sound-event.h"

#define SOUND_PROPERTIES_TYPE			(sound_properties_get_type ())
#define SOUND_PROPERTIES(obj)			(GTK_CHECK_CAST ((obj), SOUND_PROPERTIES_TYPE, SoundProperties))
#define SOUND_PROPERTIES_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), SOUND_PROPERTIES_TYPE, SoundPropertiesClass))
#define SOUND_IS_PROPERTIES(obj)		(GTK_CHECK_TYPE ((obj), SOUND_PROPERTIES_TYPE))
#define SOUND_IS_PROPERTIES_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), SOUND_PROPERTIES_TYPE))

typedef struct _SoundProperties SoundProperties;
typedef struct _SoundPropertiesPrivate SoundPropertiesPrivate;
typedef struct _SoundPropertiesClass SoundPropertiesClass;

typedef void (*SoundPropertiesForeachFunc) (SoundEvent *, gpointer);
typedef void (*SoundPropertiesCategoryForeachFunc) (gchar *, gchar *, GList*, gpointer);

struct _SoundProperties
{
	GtkObject parent;

	SoundPropertiesPrivate *priv;
};

struct _SoundPropertiesClass
{
	GtkObjectClass parent_class;

	void (*event_added) (SoundProperties *props, SoundEvent *event);
	void (*event_removed) (SoundProperties *props, SoundEvent *event);
	void (*event_changed) (SoundProperties *props, SoundEvent *event);
	void (*changed) (SoundProperties *props);
};

GType			sound_properties_get_type (void);
SoundProperties*	sound_properties_new (void);

void			sound_properties_add_directory (SoundProperties *props,
							gchar *directory,
							gboolean is_user,
							gchar *themedir);
void			sound_properties_add_file (SoundProperties *props,
						   gchar *filename,
						   gboolean is_user,
						   gchar *themedir);
void			sound_properties_add_defaults (SoundProperties *props,
						       gchar *themedir);


void			sound_properties_event_changed (SoundProperties *props,
							SoundEvent *event);

void			sound_properties_changed (SoundProperties *props);

SoundEvent*		sound_properties_lookup_event (SoundProperties *props,
						       gchar *category,
						       gchar *name);

GList*			sound_properties_lookup_category (SoundProperties *props, gchar *category);

guint			sound_properties_total_events (SoundProperties *props);

SoundEvent*		sound_properties_get_nth_event (SoundProperties *prop,
							guint n);
							
void			sound_properties_foreach (SoundProperties *props,
						  SoundPropertiesForeachFunc func,
						  gpointer user_data);
void			sound_properties_category_foreach (SoundProperties *props,
						  SoundPropertiesCategoryForeachFunc func,
						  gpointer user_data);

void 			sound_properties_remove_event (SoundProperties *props,
						       SoundEvent *event);

void			sound_properties_save (SoundProperties *props,
					       gchar *directory,
					       gboolean save_theme);

void			sound_properties_user_save (SoundProperties *props);

#endif /* __SOUND_PROPERTIES_H__ */

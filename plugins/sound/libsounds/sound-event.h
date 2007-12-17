#ifndef __SOUND_EVENT_H__
#define __SOUND_EVENT_H__

#include <glib.h>

typedef struct _SoundEventPrivate SoundEventPrivate;

typedef struct
{
	gchar *category;
	gchar *name;
	gchar *file;
	gchar *oldfile;
	gchar *desc;
	gboolean modified;
	gboolean theme;
} SoundEvent;

SoundEvent* 	sound_event_new (void);
void 		sound_event_free (SoundEvent *event);

void		sound_event_set_category (SoundEvent *event, gchar *category);
void		sound_event_set_name (SoundEvent *event, gchar *name);
void		sound_event_set_file (SoundEvent *event, gchar *file);
void		sound_event_set_oldfile (SoundEvent *event, gchar *file);
void		sound_event_set_desc (SoundEvent *event, gchar *desc);

int		sound_event_compare (SoundEvent *a, SoundEvent *b);
gchar*		sound_event_compose_key (SoundEvent *event);
gchar*		sound_event_compose_unknown_key (gchar *category, gchar *name);

#endif /* __SOUND_EVENT_H__ */

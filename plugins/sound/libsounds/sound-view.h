#ifndef __SOUND_VIEW_H__
#define __SOUND_VIEW_H__

#include <gtk/gtkvbox.h>
#include <gtk/gtkwidget.h>
#include "sound-properties.h"

#define SOUND_VIEW_TYPE			(sound_view_get_type ())
#define SOUND_VIEW(obj)			(GTK_CHECK_CAST ((obj), SOUND_VIEW_TYPE, SoundView))
#define SOUND_VIEW_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), SOUND_VIEW_TYPE, SoundViewClass))
#define SOUND_IS_VIEW(obj)		(GTK_CHECK_TYPE ((obj), SOUND_VIEW_TYPE))
#define SOUND_IS_VIEW_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), SOUND_VIEW_TYPE))

typedef struct _SoundView SoundView;
typedef struct _SoundViewPrivate SoundViewPrivate;
typedef struct _SoundViewClass SoundViewClass;

struct _SoundView
{
	GtkVBox parent;

	SoundViewPrivate *priv;
};

struct _SoundViewClass
{
	GtkVBoxClass parent_class;
};

GtkType			sound_view_get_type (void);
GtkWidget*		sound_view_new (SoundProperties *props);

#endif /* __SOUND_VIEW_H__ */

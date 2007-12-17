#include "sound-event.h"
#include <string.h>

SoundEvent*
sound_event_new (void)
{
	return g_new0 (SoundEvent, 1);
}

void
sound_event_free (SoundEvent *event)
{
	g_return_if_fail (event != NULL);

	g_free (event->category);
	g_free (event->name);
	g_free (event->file);
	g_free (event->oldfile);
	g_free (event->desc);
	g_free (event);
}

void
sound_event_set_category (SoundEvent *event, gchar *category)
{
	g_return_if_fail (event != NULL);

	if (event->category)
		g_free (event->category);
	event->category = g_strdup (category);
}

void
sound_event_set_name (SoundEvent *event, gchar *name)
{
	g_return_if_fail (event != NULL);

	if (event->name)
		g_free (event->name);
	event->name = g_strdup (name);
}

void
sound_event_set_file (SoundEvent *event, gchar *file)
{
	g_return_if_fail (event != NULL);

	if (event->file)
		g_free (event->file);
	event->file = g_strdup (file);
	event->modified = TRUE;
}

void
sound_event_set_oldfile (SoundEvent *event, gchar *file)
{
	g_return_if_fail (event != NULL);

	if (event->oldfile)
		g_free (event->oldfile);
	event->oldfile = g_strdup (file);
}

void
sound_event_set_desc (SoundEvent *event, gchar *desc)
{
	g_return_if_fail (event != NULL);

	if (event->desc)
		g_free (event->desc);
	event->desc = g_strdup (desc);
}

int
sound_event_compare (SoundEvent *a, SoundEvent *b)
{
	int ret;
	gchar *key_a, *key_b;
	
	g_return_val_if_fail (a != NULL, 0);
	g_return_val_if_fail (b != NULL, 0);
	
	key_a = sound_event_compose_key (a);
	key_b = sound_event_compose_key (b);
	
	ret = strcmp (key_a, key_b);
	
	g_free (key_a);
	g_free (key_b);

	return ret;
}

gchar*
sound_event_compose_key (SoundEvent *event)
{
	g_return_val_if_fail (event != NULL, NULL);

	return sound_event_compose_unknown_key (event->category, event->name);
}

gchar*
sound_event_compose_unknown_key (gchar *category, gchar *name)
{
	return g_strconcat ((category) ? category : "",
			    "/",
			    (name) ? name : "",
			    NULL);

}

/* acme-volume-thinkpad.c

   Adds Thinkpad speaker volume reading.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   By Lorne Applebaum <4lorne@gmail.com>
*/

#include "config.h"
#include <stdio.h>
#include "acme-volume-thinkpad.h"

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (AcmeVolumeThinkpad, acme_volume_thinkpad, ACME_TYPE_VOLUME)

/* Parses the ACPI data found at /proc/acpi/ibm/volume.  It typically
 looks like

level:		3
mute:		off
commands:	up, down, mute
commands:	level <level> (<level> is 0-15)


The ibm-acpi kernel driver is required */
static gboolean
acme_volume_thinkpad_parse_acpi (int *level, gboolean *mute_status)
{
	FILE *fd;
	char mute_string [4];

	fd = fopen (ACME_VOLUME_THINKPAD_ACPI_PATH, "r");
	if (fd == 0)
		return FALSE;


	if (fscanf (fd, "level:  %d  mute: %3s", level, mute_string) != 2)
	{
		fclose (fd);
		return FALSE;
	}

	*mute_status = strcmp (mute_string, "off");

	fclose (fd);
	return TRUE;
}

static void
acme_volume_thinkpad_set_mute (AcmeVolume *vol, gboolean val)
{
	/* No need to set anything.  Hardware takes care of it. */
}

static gboolean
acme_volume_thinkpad_get_mute (AcmeVolume *vol)
{
	int volume;
	gboolean mute;
	if (acme_volume_thinkpad_parse_acpi (&volume, &mute))
		return mute;
	return FALSE;
}

static int
acme_volume_thinkpad_get_volume (AcmeVolume *vol)
{
	int volume;
	gboolean mute;
	if (acme_volume_thinkpad_parse_acpi (&volume, &mute))
	{
		/* Convert to a volume between 0 and 100.  The ibm-acpi
		   driver outputs a value between 0 and 15 */
		return CLAMP (volume*7, 0, 100);
	}
	else
		return 0;
}

static void
acme_volume_thinkpad_set_volume (AcmeVolume *vol, int val)
{
	/* No need to set anything.  Hardware takes care of it. */
}

static void
acme_volume_thinkpad_init (AcmeVolumeThinkpad *self)
{
}

static void
acme_volume_thinkpad_class_init (AcmeVolumeThinkpadClass *klass)
{
	AcmeVolumeClass *volume_class = ACME_VOLUME_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	volume_class->set_volume = acme_volume_thinkpad_set_volume;
	volume_class->get_volume = acme_volume_thinkpad_get_volume;
	volume_class->set_mute = acme_volume_thinkpad_set_mute;
	volume_class->get_mute = acme_volume_thinkpad_get_mute;
}

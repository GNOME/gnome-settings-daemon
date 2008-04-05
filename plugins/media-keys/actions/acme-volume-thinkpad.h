 /* acme-volume-thinkpad.h

   Adds Thinkpad speaker volume support.

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

#include <glib.h>
#include <glib-object.h>
#include "acme-volume.h"

#define ACME_TYPE_VOLUME_THINKPAD	(acme_volume_thinkpad_get_type ())
#define ACME_VOLUME_THINKPAD(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), ACME_TYPE_VOLUME_THINKPAD, AcmeVolumeThinkpad))
#define ACME_VOLUME_THINKPAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), ACME_TYPE_VOLUME_THINKPAD, AcmeVolumeThinkpadClass))
#define ACME_IS_VOLUME_THINKPAD(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), ACME_TYPE_VOLUME_THINKPAD))
#define ACME_VOLUME_THINKPAD_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), ACME_TYPE_VOLUME_THINKPAD, AcmeVolumeThinkpadClass))

typedef struct AcmeVolumeThinkpad AcmeVolumeThinkpad;
typedef struct AcmeVolumeThinkpadClass AcmeVolumeThinkpadClass;

struct AcmeVolumeThinkpad {
	AcmeVolume parent;
};

struct AcmeVolumeThinkpadClass {
	AcmeVolumeClass parent;
};

GType acme_volume_thinkpad_get_type		(void);

#define ACME_VOLUME_THINKPAD_ACPI_PATH          "/proc/acpi/ibm/volume"

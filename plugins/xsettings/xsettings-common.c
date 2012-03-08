/*
 * Copyright © 2001 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Red Hat not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Red Hat makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL RED HAT
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN 
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Owen Taylor, Red Hat, Inc.
 */

#include <glib.h>

#include "string.h"
#include "stdlib.h"

#include <X11/Xlib.h>
#include <X11/Xmd.h>		/* For CARD32 */

#include "xsettings-common.h"

XSettingsSetting *
xsettings_setting_new (const gchar *name)
{
  XSettingsSetting *result;

  result = g_slice_new (XSettingsSetting);
  result->name = g_strdup (name);
  result->value = NULL;
  result->last_change_serial = 0;

  return result;
}

void
xsettings_setting_set (XSettingsSetting *setting,
                       GVariant         *value)
{
  if (setting->value)
    g_variant_unref (setting->value);

  setting->value = value ? g_variant_ref (value) : NULL;
}

gboolean
xsettings_setting_equal (XSettingsSetting *setting,
                         GVariant         *value)
{
  return setting->value && g_variant_equal (setting->value, value);
}

void
xsettings_setting_free (XSettingsSetting *setting)
{
  if (setting->value)
    g_variant_unref (setting->value);

  g_free (setting->name);

  g_slice_free (XSettingsSetting, setting);
}

char
xsettings_byte_order (void)
{
  CARD32 myint = 0x01020304;
  return (*(char *)&myint == 1) ? MSBFirst : LSBFirst;
}

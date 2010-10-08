/*
 * Copyright © 2010 Bastien Nocera <hadess@hadess.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors:
 *	Bastien Nocera <hadess@hadess.net>
 */

#ifndef __gsd_enums_h__
#define __gsd_enums_h__

typedef enum
{
  GSD_FONT_ANTIALIASING_MODE_NONE,
  GSD_FONT_ANTIALIASING_MODE_GRAYSCALE,
  GSD_FONT_ANTIALIASING_MODE_RGBA
} GsdFontAntialiasingMode;

typedef enum
{
  GSD_FONT_HINTING_NONE,
  GSD_FONT_HINTING_SLIGHT,
  GSD_FONT_HINTING_MEDIUM,
  GSD_FONT_HINTING_FULL
} GsdFontHinting;

typedef enum
{
  GSD_FONT_RGBA_ORDER_RGBA,
  GSD_FONT_RGBA_ORDER_RGB,
  GSD_FONT_RGBA_ORDER_BGR,
  GSD_FONT_RGBA_ORDER_VRGB,
  GSD_FONT_RGBA_ORDER_VBGR
} GsdFontRgbaOrder;

typedef enum
{
  GSD_SMARTCARD_REMOVAL_ACTION_NONE,
  GSD_SMARTCARD_REMOVAL_ACTION_LOCK_SCREEN,
  GSD_SMARTCARD_REMOVAL_ACTION_FORCE_LOGOUT
} GsdSmartcardRemovalAction;

typedef enum
{
  GSD_TOUCHPAD_SCROLL_METHOD_DISABLED,
  GSD_TOUCHPAD_SCROLL_METHOD_EDGE_SCROLLING,
  GSD_TOUCHPAD_SCROLL_METHOD_TWO_FINGER_SCROLLING
} GsdTouchpadScrollMethod;

#endif /* __gsd_enums_h__ */

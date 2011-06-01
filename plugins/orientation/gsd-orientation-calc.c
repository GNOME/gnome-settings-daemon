/**
   @file orientationinterpreter.cpp
   @brief OrientationInterpreter

   <p>
   Copyright (C) 2009-2010 Nokia Corporation
   Copyright (C) 2011 Red Hat, Inc.

   @author Üstün Ergenoglu <ext-ustun.ergenoglu@nokia.com>
   @author Timo Rongas <ext-timo.2.rongas@nokia.com>
   @author Lihan Guo <lihan.guo@digia.com>
   @author Bastien Nocera <hadess@hadess.net> (C port)

   This file is part of Sensord.

   Sensord is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License
   version 2.1 as published by the Free Software Foundation.

   Sensord is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with Sensord.  If not, see <http://www.gnu.org/licenses/>.
   </p>
 */
#include "config.h"

#include <math.h>
#include <stdlib.h>

#include "gsd-orientation-calc.h"

#define DEFAULT_THRESHOLD 250
#define RADIANS_TO_DEGREES 180.0/M_PI
#define SAME_AXIS_LIMIT 5

#define THRESHOLD_LANDSCAPE  25
#define THRESHOLD_PORTRAIT  20

OrientationUp gsd_orientation_calc (OrientationUp prev,
				    int x, int y, int z)
{
    int rotation;
    OrientationUp ret = ORIENTATION_UNDEFINED;

    /* Portrait check */
    rotation = round (atan ((double) x / sqrt (y * y + z * z)) * RADIANS_TO_DEGREES);

    if (abs (rotation) > THRESHOLD_PORTRAIT) {
        ret = (rotation >= 0) ? ORIENTATION_LEFT_UP : ORIENTATION_RIGHT_UP;

        /* Some threshold to switching between portrait modes */
        if (prev == ORIENTATION_LEFT_UP || prev == ORIENTATION_RIGHT_UP) {
            if (abs (rotation) < SAME_AXIS_LIMIT) {
                ret = prev;
            }
        }

    } else {
        /* Landscape check */
        rotation = round (atan ((double) y / sqrt (x * x + z * z)) * RADIANS_TO_DEGREES);

        if (abs (rotation) > THRESHOLD_LANDSCAPE) {
            ret = (rotation >= 0) ? ORIENTATION_BOTTOM_UP : ORIENTATION_NORMAL;

            /* Some threshold to switching between landscape modes */
            if (prev == ORIENTATION_BOTTOM_UP || prev == ORIENTATION_NORMAL) {
                if (abs (rotation) < SAME_AXIS_LIMIT) {
                    ret = prev;
                }
            }
        }
    }

    return ret;
}


/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors: Ray Strode
 */

#ifndef __GSD_IDENTITY_TEST_H__
#define __GSD_IDENTITY_TEST_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define GSD_TYPE_IDENTITY_TEST           (gsd_identity_test_get_type ())
#define GSD_IDENTITY_TEST(obj)           (G_TYPE_CHECK_INSTANCE_CAST (obj, GSD_TYPE_IDENTITY_TEST, GsdIdentityTest))
#define GSD_IDENTITY_TEST_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST (cls, GSD_TYPE_IDENTITY_TEST, GsdIdentityTestClass))
#define GSD_IS_IDENTITY_TEST(obj)        (G_TYPE_CHECK_INSTANCE_TYPE (obj, GSD_TYPE_IDENTITY_TEST))
#define GSD_IS_IDENTITY_TEST_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE (obj, GSD_TYPE_IDENTITY_TEST))
#define GSD_IDENTITY_TEST_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GSD_TYPE_IDENTITY_TEST, GsdIdentityTestClass))

typedef struct _GsdIdentityTest           GsdIdentityTest;
typedef struct _GsdIdentityTestClass      GsdIdentityTestClass;
typedef struct _GsdIdentityTestPrivate    GsdIdentityTestPrivate;

struct _GsdIdentityTest
{
        GObject parent_instance;
        GsdIdentityTestPrivate *priv;
};

struct _GsdIdentityTestClass
{
        GObjectClass parent_class;
};

GType                gsd_identity_test_get_type  (void);
GsdIdentityTest*     gsd_identity_test_new       (void);

void                 gsd_identity_test_start (GsdIdentityTest  *test,
                                              GError          **error);
void                 gsd_identity_test_stop (GsdIdentityTest  *test);
G_END_DECLS

#endif /* __GSD_IDENTITY_TEST_H__ */

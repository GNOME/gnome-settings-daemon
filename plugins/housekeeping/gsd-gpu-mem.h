/*
 * Copyright (C) 2017  Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef _GSD_GPU_MEM_H
#define _GSD_GPU_MEM_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_GPU_MEM (gsd_gpu_mem_get_type ())
G_DECLARE_FINAL_TYPE (GsdGpuMem, gsd_gpu_mem,
                      GSD, GPU_MEM, GObject)

G_END_DECLS

#endif /* _GSD_GPU_MEM_H */

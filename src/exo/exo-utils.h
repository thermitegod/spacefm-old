/*-
 * Copyright (c) 2005-2006 Benedikt Meurer <benny@xfce.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#ifndef __EXO_UTILS_H__
#define __EXO_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

void                    exo_noop        (void);
gpointer                exo_noop_null   (void) G_GNUC_PURE;
gboolean                exo_noop_true   (void) G_GNUC_PURE;
gboolean                exo_noop_false  (void) G_GNUC_PURE;

G_END_DECLS

#endif /* !__EXO_UTILS_H__ */

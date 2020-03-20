/* $Id: exo-private.h 22884 2006-08-26 12:40:43Z benny $ */
/*-
 * Copyright (c) 2004-2006 os-cillation e.K.
 *
 * Written by Benedikt Meurer <benny@xfce.org>.
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
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __EXO_PRIVATE_H__
#define __EXO_PRIVATE_H__

#include <gtk/gtk.h>
#include <glib/gi18n.h>

G_BEGIN_DECLS;

#define _exo_assert_not_reached()          g_assert_not_reached()
#define _exo_return_if_fail(expr)          g_return_if_fail(expr)
#define _exo_return_val_if_fail(expr, val) g_return_val_if_fail(expr, (val))

/* support macros for the slice allocator */
#define _exo_slice_free(type, ptr) \
    G_STMT_START                   \
    {                              \
        g_slice_free(type, (ptr)); \
    }                              \
    G_STMT_END

void _exo_i18n_init(void) G_GNUC_INTERNAL;

void _exo_gtk_widget_send_focus_change(GtkWidget* widget, gboolean in) G_GNUC_INTERNAL;

GType _exo_g_type_register_simple(GType type_parent, const gchar* type_name_static, guint class_size,
                                  gpointer class_init, guint instance_size, gpointer instance_init) G_GNUC_INTERNAL;

void _exo_g_type_add_interface_simple(GType instance_type, GType interface_type,
                                      GInterfaceInitFunc interface_init_func) G_GNUC_INTERNAL;

G_END_DECLS;

#endif /* !__EXO_PRIVATE_H__ */

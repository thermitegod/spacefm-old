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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_LIBINTL_H
#include <libintl.h>
#endif

#include "exo-private.h"
#include "exo-string.h"

void _exo_gtk_widget_send_focus_change(GtkWidget* widget, gboolean in)
{
#if (GTK_MAJOR_VERSION == 2)
    if (in)
        GTK_WIDGET_SET_FLAGS(widget, GTK_HAS_FOCUS);
    else
        GTK_WIDGET_UNSET_FLAGS(widget, GTK_HAS_FOCUS);
#endif

    GdkEvent* fevent;

    g_object_ref(G_OBJECT(widget));

    fevent = gdk_event_new(GDK_FOCUS_CHANGE);
    fevent->focus_change.type = GDK_FOCUS_CHANGE;
    fevent->focus_change.window = g_object_ref(gtk_widget_get_window(widget));
    fevent->focus_change.in = in;

    gtk_widget_send_focus_change(widget, fevent);

    g_object_unref(G_OBJECT(widget));
    gdk_event_free(fevent);
}

/**
 * _exo_g_type_register_simple:
 * @type_parent      : the parent #GType.
 * @type_name_static : the name of the new #GType, must reside in static
 *                     storage and remain unchanged during the lifetime
 *                     of the process.
 * @class_size       : the size of the class structure in bytes.
 * @class_init       : the class init function or %NULL.
 * @instance_size    : the size of the instance structure in bytes.
 * @instance_init    : the constructor function or %NULL.
 *
 * Simple wrapper for g_type_register_static(), which takes the most
 * important aspects of the type as parameters to avoid relocations
 * when using static constant #GTypeInfo<!---->s.
 *
 * Return value: the newly registered #GType.
 **/
GType _exo_g_type_register_simple(GType type_parent, const char* type_name_static, uint class_size,
                                  gpointer class_init, uint instance_size, gpointer instance_init)
{
    /* generate the type info (on the stack) */
    GTypeInfo info = {
        class_size,
        NULL,
        NULL,
        class_init,
        NULL,
        NULL,
        instance_size,
        0,
        instance_init,
        NULL,
    };

    /* register the static type */
    return g_type_register_static(type_parent, I_(type_name_static), &info, 0);
}

/**
 * _exo_g_type_add_interface_simple:
 * @instance_type       : the #GType which should implement the @interface_type.
 * @interface_type      : the #GType of the interface.
 * @interface_init_func : initialization function for the interface.
 *
 * Simple wrapper for g_type_add_interface_static(), which helps to avoid unnecessary
 * relocations for the #GInterfaceInfo<!---->s.
 **/
void _exo_g_type_add_interface_simple(GType instance_type, GType interface_type, GInterfaceInitFunc interface_init_func)
{
    GInterfaceInfo info = {
        interface_init_func,
        NULL,
        NULL,
    };

    g_type_add_interface_static(instance_type, interface_type, &info);
}

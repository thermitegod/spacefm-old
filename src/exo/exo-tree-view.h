/* $Id: exo-tree-view.h 20828 2006-04-09 17:16:48Z benny $ */
/*-
 * Copyright (c) 2004-2006 Benedikt Meurer <benny@xfce.org>
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

/* Modified by Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 * on 2008.07.13 for use in PCManFM */

#ifndef __EXO_TREE_VIEW_H__
#define __EXO_TREE_VIEW_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS;

typedef struct _ExoTreeViewPrivate ExoTreeViewPrivate;
typedef struct _ExoTreeViewClass ExoTreeViewClass;
typedef struct _ExoTreeView ExoTreeView;

#define EXO_TYPE_TREE_VIEW    (exo_tree_view_get_type())
#define EXO_TREE_VIEW(obj)    (G_TYPE_CHECK_INSTANCE_CAST((obj), EXO_TYPE_TREE_VIEW, ExoTreeView))
#define EXO_IS_TREE_VIEW(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), EXO_TYPE_TREE_VIEW))

struct _ExoTreeViewClass
{
    /*< private >*/
    GtkTreeViewClass __parent__;

    /*< private >*/
    /*
      void (*reserved1) (void);
      void (*reserved2) (void);
      void (*reserved3) (void);
      void (*reserved4) (void);
      void (*reserved5) (void);
      void (*reserved6) (void);
      void (*reserved7) (void);
      void (*reserved8) (void);
    */
};

struct _ExoTreeView
{
    /*< private >*/
    GtkTreeView __parent__;

    /*< private >*/
    ExoTreeViewPrivate* priv;
};

GType exo_tree_view_get_type(void) G_GNUC_CONST;

GtkWidget* exo_tree_view_new(void) G_GNUC_MALLOC;

gboolean exo_tree_view_get_single_click(const ExoTreeView* tree_view);
void exo_tree_view_set_single_click(ExoTreeView* tree_view, gboolean single_click);

uint exo_tree_view_get_single_click_timeout(const ExoTreeView* tree_view);
void exo_tree_view_set_single_click_timeout(ExoTreeView* tree_view, uint single_click_timeout);

/* 2008.07.16 added by Hong Jen Yee for PCManFM.
 * If activable column is set, only the specified column can be activated.
 * Other columns are viewed as blank area and won't receive mouse clicks.
 */
GtkTreeViewColumn* exo_tree_view_get_activable_column(ExoTreeView* tree_view);
void exo_tree_view_set_activable_column(ExoTreeView* tree_view, GtkTreeViewColumn* column);

G_END_DECLS;

#endif /* !__EXO_TREE_VIEW_H__ */

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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#ifndef __EXO_CELL_RENDERER_ELLIPSIZED_TEXT_H__
#define __EXO_CELL_RENDERER_ELLIPSIZED_TEXT_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _ExoCellRendererEllipsizedTextPrivate ExoCellRendererEllipsizedTextPrivate;
typedef struct _ExoCellRendererEllipsizedTextClass   ExoCellRendererEllipsizedTextClass;
typedef struct _ExoCellRendererEllipsizedText        ExoCellRendererEllipsizedText;

#define EXO_TYPE_CELL_RENDERER_ELLIPSIZED_TEXT            (exo_cell_renderer_ellipsized_text_get_type ())
#define EXO_CELL_RENDERER_ELLIPSIZED_TEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXO_TYPE_CELL_RENDERER_ELLIPSIZED_TEXT, ExoCellRendererEllipsizedText))

struct _ExoCellRendererEllipsizedTextClass
{
  /*< private >*/
  GtkCellRendererTextClass __parent__;
};

/**
 * ExoCellRendererEllipsizedText:
 *
 * The #ExoIconCellRendererEllipsizedText
 * struct contains only private fields and should not be directly accessed.
 **/
struct _ExoCellRendererEllipsizedText
{
  /*< private >*/
  GtkCellRendererText                   __parent__;
  ExoCellRendererEllipsizedTextPrivate *priv;
};

GType            exo_cell_renderer_ellipsized_text_get_type (void) G_GNUC_CONST;

GtkCellRenderer *exo_cell_renderer_ellipsized_text_new      (void) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif /* !__EXO_CELL_RENDERER_ELLIPSIZED_TEXT_H__ */

/*
 *  C Interface: ptkutils
 *
 * Description: Some GUI utilities
 *
 *
 * Author: Hong Jen Yee (PCMan) <pcman.tw (AT) gmail.com>, (C) 2006
 *
 * Copyright: See COPYING file that comes with this distribution
 *
 */

/*
  I don't like GtkUIManager provided by gtk+, so I implement my own. ;-)
*/

#ifndef _PTK_UTILS_H_
#define _PTK_UTILS_H_

#include <gtk/gtk.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

#define PTK_IS_STOCK_ITEM(ent)      (ent->label && (*(guint32*)ent->label) == *(guint32*)"gtk-")
#define PTK_IS_CHECK_MENU_ITEM(ent) (ent->stock_icon == (char*)1)
#define PTK_IS_RADIO_MENU_ITEM(ent) (ent->stock_icon == (char*)2)

struct _PtkMenuItemEntry
{
    const char* label;      /* or stock id */
    const char* stock_icon; /* or menu type  1: check, 2: radio */
    GCallback callback;
    guint key;
    GdkModifierType mod;
    struct _PtkMenuItemEntry* sub_menu;
    GtkWidget** ret;
    GtkWidget* menu; // MOD
};
typedef struct _PtkMenuItemEntry PtkMenuItemEntry;

struct _PtkToolItemEntry
{
    const char* label;      /* or stock id */
    const char* stock_icon; /* or menu type  1: check, 2: radio */
    GCallback callback;
    struct _PtkMenuItemEntry* menu; /* NULL: normal, 1: check, 2: radio, 3: empty menu, > 3: menu */
    GtkWidget** ret;
};
typedef struct _PtkToolItemEntry PtkToolItemEntry;

GtkWidget* ptk_menu_new_from_data(PtkMenuItemEntry* entries, gpointer cb_data, GtkAccelGroup* accel_group);

void ptk_menu_add_items_from_data(GtkWidget* menu, PtkMenuItemEntry* entries, gpointer cb_data,
                                  GtkAccelGroup* accel_group);

/* The string 'message' can contain pango markups.
 * If special characters like < and > are used in the string,
 * they should be escaped with g_markup_escape_text().
 */
void ptk_show_error(GtkWindow* parent, const char* title, const char* message);

/* gtk_dialog_run will disable all parent windows of the dialog.
 * However, sometimes we need modaless dialogs. So here it is.
 */
int ptk_dialog_run_modaless(GtkDialog* dlg);

GtkBuilder* _gtk_builder_new_from_file(const char* path, const char* file, GError** err);

void transpose_nonlatin_keypress(GdkEventKey* event);

G_END_DECLS

#endif

/*
 * SpaceFM ptk-location-view.c
 *
 * Copyright (C) 2015 IgnorantGuru <ignorantguru@gmx.com>
 * Copyright (C) 2006 Hong Jen Yee (PCMan) <pcman.tw (AT) gmail.com>
 *
 * License: See COPYING file
 *
 * In SpaceFM, this file was changed extensively, separating bookmarks list
 * and adding non-HAL device manager features
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h> /* for realpath */
#include <errno.h>

#include "ptk-location-view.h"
#include "ptk-utils.h"
#include "ptk-file-browser.h"
#include "ptk-handler.h"
#include "ptk-file-task.h"

#include "settings.h"
#include "main-window.h"

#include "../vfs/vfs-volume.h"
#include "../vfs/vfs-dir.h"
#include "../vfs/vfs-utils.h" /* for vfs_load_icon */
#include "../vfs/vfs-app-desktop.h"

#include "gtk2-compat.h"

#include <glib/gprintf.h>

#include "utils.h"

#include <linux/limits.h> //PATH_MAX

static GtkTreeModel* model = NULL;
static int n_vols = 0;
static uint theme_changed = 0; /* GtkIconTheme::"changed" handler */

GdkPixbuf* global_icon_bookmark = NULL;
GdkPixbuf* global_icon_submenu = NULL;

static void ptk_location_view_init_model(GtkListStore* list);

static void on_volume_event(VFSVolume* vol, VFSVolumeState state, gpointer user_data);

static void add_volume(VFSVolume* vol, gboolean set_icon);
static void remove_volume(VFSVolume* vol);
static void update_volume(VFSVolume* vol);

static gboolean on_button_press_event(GtkTreeView* view, GdkEventButton* evt, gpointer user_data);
gboolean on_key_press_event(GtkWidget* w, GdkEventKey* event, PtkFileBrowser* file_browser);

static void on_bookmark_model_destroy(gpointer data, GObject* object);
static void on_bookmark_device(GtkMenuItem* item, VFSVolume* vol);
static void on_bookmark_row_inserted(GtkTreeModel* list, GtkTreePath* tree_path, GtkTreeIter* iter,
                                     PtkFileBrowser* file_browser);
/*
static gboolean update_drag_dest_row( GtkWidget *widget, GdkDragContext *drag_context,
                                      int x, int y, uint time, gpointer user_data );

static gboolean on_drag_motion( GtkWidget *widget, GdkDragContext *drag_context,
                                int x, int y, uint time, gpointer user_data );

static gboolean on_drag_drop( GtkWidget *widget, GdkDragContext *drag_context,
                              int x, int y, uint time, gpointer user_data );

static void on_drag_data_received( GtkWidget *widget, GdkDragContext *drag_context,
                                   int x, int y, GtkSelectionData *data, uint info,
                                   uint time, gpointer user_data);
*/

static gboolean try_mount(GtkTreeView* view, VFSVolume* vol);

static void on_open_tab(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2);
static void on_open(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2);

enum
{
    COL_ICON = 0,
    COL_NAME,
    COL_PATH,
    COL_DATA,
    N_COLS
};

typedef struct _AutoOpen
{
    PtkFileBrowser* file_browser;
    char* device_file;
    dev_t devnum;
    char* mount_point;
    gboolean keep_point;
    int job;
} AutoOpen;

gboolean volume_is_visible(VFSVolume* vol);
gboolean run_command(char* command, char** output);
void update_all();

// do not translate - bash security
const char* press_enter_to_close = "[ Finished ]  Press Enter to close";

/*  Drag & Drop/Clipboard targets  */
// static GtkTargetEntry drag_targets[] = { {"text/uri-list", 0 , 0 } };

static void on_model_destroy(gpointer data, GObject* object)
{
    GtkIconTheme* icon_theme;

    vfs_volume_remove_callback(on_volume_event, (gpointer)object);

    model = NULL;
    n_vols = 0;

    icon_theme = gtk_icon_theme_get_default();
    g_signal_handler_disconnect(icon_theme, theme_changed);
}

void update_volume_icons()
{
    if (!model)
        return;

    // GtkListStore* list = GTK_LIST_STORE( model );
    GtkIconTheme* icon_theme = gtk_icon_theme_get_default();
    int icon_size = app_settings.small_icon_size;
    if (icon_size > PANE_MAX_ICON_SIZE)
        icon_size = PANE_MAX_ICON_SIZE;

    GtkTreeIter it;
    if (gtk_tree_model_get_iter_first(model, &it))
    {
        do
        {
            VFSVolume* vol;
            gtk_tree_model_get(model, &it, COL_DATA, &vol, -1);
            if (vol)
            {
                GdkPixbuf* icon;
                if (vfs_volume_get_icon(vol))
                    icon = vfs_load_icon(icon_theme, vfs_volume_get_icon(vol), icon_size);
                else
                    icon = NULL;
                gtk_list_store_set(GTK_LIST_STORE(model), &it, COL_ICON, icon, -1);
                if (icon)
                    g_object_unref(icon);
            }
        } while (gtk_tree_model_iter_next(model, &it));
    }
}

void update_all_icons()
{
    update_volume_icons();
    // dev_icon_network is used by bookmark view
    main_window_update_all_bookmark_views();
}

void update_change_detection()
{
    const GList* l;

    // update all windows/all panels/all browsers
    for (l = fm_main_window_get_all(); l; l = l->next)
    {
        FMMainWindow* a_window = FM_MAIN_WINDOW(l->data);
        int p;
        for (p = 1; p < 5; p++)
        {
            GtkNotebook* notebook = GTK_NOTEBOOK(a_window->panel[p - 1]);
            int n = gtk_notebook_get_n_pages(notebook);
            int i;
            for (i = 0; i < n; ++i)
            {
                const char* pwd;
                PtkFileBrowser* file_browser = PTK_FILE_BROWSER(gtk_notebook_get_nth_page(notebook, i));
                if (file_browser && (pwd = ptk_file_browser_get_cwd(file_browser)))
                {
                    // update current dir change detection
                    file_browser->dir->avoid_changes = vfs_volume_dir_avoid_changes(pwd);
                    // update thumbnail visibility
                    ptk_file_browser_show_thumbnails(file_browser,
                                                     app_settings.show_thumbnail ? app_settings.max_thumb_size : 0);
                }
            }
        }
    }
}

void update_all()
{
    if (!model)
        return;

    const GList* volumes = vfs_volume_get_all_volumes();
    const GList* l;
    for (l = volumes; l; l = l->next)
    {
        VFSVolume* vol = (VFSVolume*)l->data;
        if (vol)
        {
            // search model for volume vol
            GtkTreeIter it;
            gboolean havevol;
            if (gtk_tree_model_get_iter_first(model, &it))
            {
                VFSVolume* v = NULL;
                do
                {
                    gtk_tree_model_get(model, &it, COL_DATA, &v, -1);
                } while (v != vol && gtk_tree_model_iter_next(model, &it));
                havevol = (v == vol);
            }
            else
                havevol = FALSE;

            if (volume_is_visible(vol))
            {
                if (havevol)
                {
                    update_volume(vol);

                    // attempt automount in case settings changed
                    vol->automount_time = 0;
                    vol->ever_mounted = FALSE;
                    vfs_volume_automount(vol);
                }
                else
                    add_volume(vol, TRUE);
            }
            else if (havevol)
                remove_volume(vol);
        }
    }
}

static void update_names()
{
    const GList* l;
    const GList* volumes = vfs_volume_get_all_volumes();
    for (l = volumes; l; l = l->next)
    {
        if (l->data)
        {
            GtkTreeIter it;
            VFSVolume* v;
            VFSVolume* vol;

            vol = l->data;
            vfs_volume_set_info(vol);

            // search model for volume vol
            if (gtk_tree_model_get_iter_first(model, &it))
            {
                do
                {
                    gtk_tree_model_get(model, &it, COL_DATA, &v, -1);
                } while (v != vol && gtk_tree_model_iter_next(model, &it));
                if (v == vol)
                    update_volume(vol);
            }
        }
    }
}

gboolean ptk_location_view_chdir(GtkTreeView* location_view, const char* cur_dir)
{
    if (!cur_dir || !GTK_IS_TREE_VIEW(location_view))
        return FALSE;

    GtkTreeIter it;
    VFSVolume* vol;

    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(location_view);
    if (gtk_tree_model_get_iter_first(model, &it))
    {
        do
        {
            gtk_tree_model_get(model, &it, COL_DATA, &vol, -1);
            const char* mount_point = vfs_volume_get_mount_point(vol);
            if (mount_point && !strcmp(cur_dir, mount_point))
            {
                gtk_tree_selection_select_iter(tree_sel, &it);
                GtkTreePath* path = gtk_tree_model_get_path(model, &it);
                if (path)
                {
                    gtk_tree_view_scroll_to_cell(location_view, path, NULL, TRUE, .25, 0);
                    gtk_tree_path_free(path);
                }
                return TRUE;
            }
        } while (gtk_tree_model_iter_next(model, &it));
    }
    gtk_tree_selection_unselect_all(tree_sel);
    return FALSE;
}

VFSVolume* ptk_location_view_get_selected_vol(GtkTreeView* location_view)
{
    // g_printf("ptk_location_view_get_selected_vol    view = %d\n", location_view );
    GtkTreeIter it;
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(location_view));
    if (gtk_tree_selection_get_selected(tree_sel, NULL, &it))
    {
        VFSVolume* vol;
        gtk_tree_model_get(model, &it, COL_DATA, &vol, -1);
        return vol;
    }
    return NULL;
}

char* ptk_location_view_get_selected_dir(GtkTreeView* location_view)
{
    GtkTreeIter it;
    char* real_path = NULL;

    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(location_view);
    if (gtk_tree_selection_get_selected(tree_sel, NULL, &it))
    {
        gtk_tree_model_get(model, &it, COL_PATH, &real_path, -1);
        if (!real_path || real_path[0] == '\0' || !g_file_test(real_path, G_FILE_TEST_EXISTS))
        {
            VFSVolume* vol;
            gtk_tree_model_get(model, &it, COL_DATA, &vol, -1);
            // if( ! vfs_volume_is_mounted( vol ) )
            //    try_mount( location_view, vol );
            if (!vfs_volume_is_mounted(vol))
                return NULL;
            real_path = (char*)vfs_volume_get_mount_point(vol);
            if (real_path)
            {
                gtk_list_store_set(GTK_LIST_STORE(model), &it, COL_PATH, real_path, -1);
                return g_strdup(real_path);
            }
            else
                return NULL;
        }
    }
    return real_path;
}

void on_row_activated(GtkTreeView* view, GtkTreePath* tree_path, GtkTreeViewColumn* col, PtkFileBrowser* file_browser)
{
    // g_printf("on_row_activated   view = %d\n", view );
    if (!file_browser)
        return;

    GtkTreeIter it;
    if (!gtk_tree_model_get_iter(model, &it, tree_path))
        return;

    VFSVolume* vol;
    gtk_tree_model_get(model, &it, COL_DATA, &vol, -1);
    if (!vol)
        return;

    if (xset_opener(file_browser, 2))
        return;

    if (!vfs_volume_is_mounted(vol) && vol->device_type == DEVICE_TYPE_BLOCK)

    {
        try_mount(view, vol);
        if (vfs_volume_is_mounted(vol))
        {
            const char* mount_point = vfs_volume_get_mount_point(vol);
            if (mount_point && mount_point[0] != '\0')
            {
                gtk_list_store_set(GTK_LIST_STORE(model), &it, COL_PATH, mount_point, -1);
            }
        }
    }
    if (vfs_volume_is_mounted(vol) && vol->mount_point)
    {
        if (xset_get_b("dev_newtab"))
        {
            ptk_file_browser_emit_open(file_browser, vol->mount_point, PTK_OPEN_NEW_TAB);
            ptk_location_view_chdir(view, ptk_file_browser_get_cwd(file_browser));
        }
        else
        {
            if (strcmp(vol->mount_point, ptk_file_browser_get_cwd(file_browser)))
                ptk_file_browser_chdir(file_browser, vol->mount_point, PTK_FB_CHDIR_ADD_HISTORY);
        }
    }
}

gboolean ptk_location_view_open_block(const char* block, gboolean new_tab)
{
    // open block device file if in volumes list

    // may be link so get real path
    char buf[PATH_MAX + 1];
    char* canon = realpath(block, buf);

    const GList* l = vfs_volume_get_all_volumes();
    for (; l; l = l->next)
    {
        if (!g_strcmp0(vfs_volume_get_device((VFSVolume*)l->data), canon))
        {
            VFSVolume* vol = (VFSVolume*)l->data;
            if (new_tab)
                on_open_tab(NULL, vol, NULL);
            else
                on_open(NULL, vol, NULL);
            return TRUE;
        }
    }
    return FALSE;
}

void ptk_location_view_init_model(GtkListStore* list)
{
    const GList* l = vfs_volume_get_all_volumes();
    vfs_volume_add_callback(on_volume_event, model);

    for (; l; l = l->next)
    {
        add_volume((VFSVolume*)l->data, FALSE);
    }
    update_volume_icons();
}

GtkWidget* ptk_location_view_new(PtkFileBrowser* file_browser)
{


    if (!model)
    {
        GtkListStore* list = gtk_list_store_new(N_COLS, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
        g_object_weak_ref(G_OBJECT(list), on_model_destroy, NULL);
        model = (GtkTreeModel*)list;
        ptk_location_view_init_model(list);
        GtkIconTheme* icon_theme = gtk_icon_theme_get_default();
        theme_changed = g_signal_connect(icon_theme, "changed", G_CALLBACK(update_volume_icons), NULL);
    }
    else
    {
        g_object_ref(G_OBJECT(model));
    }

    GtkWidget* view = gtk_tree_view_new_with_model(model);
    g_object_unref(G_OBJECT(model));
    // g_printf("ptk_location_view_new   view = %d\n", view );
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(tree_sel, GTK_SELECTION_SINGLE);

    /*gtk_tree_view_enable_model_drag_dest (
        GTK_TREE_VIEW( view ),
        drag_targets, G_N_ELEMENTS( drag_targets ), GDK_ACTION_LINK ); */ //MOD

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

    // g_signal_connect( view, "drag-motion", G_CALLBACK( on_drag_motion ), NULL );  //MOD
    // g_signal_connect( view, "drag-drop", G_CALLBACK( on_drag_drop ), NULL );  //MOD
    // g_signal_connect( view, "drag-data-received", G_CALLBACK( on_drag_data_received ), NULL );  //MOD

    GtkCellRenderer* renderer;
    GtkTreeViewColumn* col = gtk_tree_view_column_new();
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, renderer, FALSE);
    gtk_tree_view_column_set_attributes(col, renderer, "pixbuf", COL_ICON, NULL);

    renderer = gtk_cell_renderer_text_new();
    // g_signal_connect( renderer, "edited", G_CALLBACK(on_bookmark_edited), view );  //MOD
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_set_attributes(col, renderer, "text", COL_NAME, NULL);
    gtk_tree_view_column_set_min_width(col, 10);

    if (GTK_IS_TREE_SORTABLE(model)) // why is this needed to stop error on new tab?
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model), COL_NAME,
                                             GTK_SORT_ASCENDING); // MOD
    // gtk_tree_view_column_set_sort_indicator( col, TRUE );  //MOD
    // gtk_tree_view_column_set_sort_column_id( col, COL_NAME );   //MOD
    // gtk_tree_view_column_set_sort_order( col, GTK_SORT_ASCENDING );  //MOD

    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

    g_object_set_data(G_OBJECT(view), "file_browser", file_browser);

    g_signal_connect(view, "row-activated", G_CALLBACK(on_row_activated), file_browser);

    g_signal_connect(view, "button-press-event", G_CALLBACK(on_button_press_event), NULL);
    g_signal_connect(view, "key-press-event", G_CALLBACK(on_key_press_event), file_browser);

    // set font
    if (xset_get_s_panel(file_browser->mypanel, "font_dev"))
    {
        PangoFontDescription* font_desc =
            pango_font_description_from_string(xset_get_s_panel(file_browser->mypanel, "font_dev"));
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_override_font(view, font_desc);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_widget_modify_font(view, font_desc);
#endif
        pango_font_description_free(font_desc);
    }
    return view;
}

void on_volume_event(VFSVolume* vol, VFSVolumeState state, gpointer user_data)
{
    switch (state)
    {
    case VFS_VOLUME_ADDED:
        add_volume(vol, TRUE);
        break;
    case VFS_VOLUME_REMOVED:
        remove_volume(vol);
        break;
    case VFS_VOLUME_CHANGED: // CHANGED may occur before ADDED !
        if (!volume_is_visible(vol))
            remove_volume(vol);
        else
            update_volume(vol);
        break;
    default:
        break;
    }
}

void add_volume(VFSVolume* vol, gboolean set_icon)
{
    if (!volume_is_visible(vol))
        return;

    // sfm - vol already exists?
    GtkTreeIter it;
    VFSVolume* v = NULL;
    if (gtk_tree_model_get_iter_first(model, &it))
    {
        do
        {
            gtk_tree_model_get(model, &it, COL_DATA, &v, -1);
        } while (v != vol && gtk_tree_model_iter_next(model, &it));
    }
    if (v == vol)
        return;

    // get mount point
    const char* mnt = vfs_volume_get_mount_point(vol);
    if (mnt && !*mnt)
        mnt = NULL;

    // add to model
    gtk_list_store_insert_with_values(GTK_LIST_STORE(model),
                                      &it,
                                      0,
                                      COL_NAME,
                                      vfs_volume_get_disp_name(vol),
                                      COL_PATH,
                                      mnt,
                                      COL_DATA,
                                      vol,
                                      -1);
    if (set_icon)
    {
        GtkIconTheme* icon_theme = gtk_icon_theme_get_default();
        int icon_size = app_settings.small_icon_size;
        if (icon_size > PANE_MAX_ICON_SIZE)
            icon_size = PANE_MAX_ICON_SIZE;
        GdkPixbuf* icon = vfs_load_icon(icon_theme, vfs_volume_get_icon(vol), icon_size);
        gtk_list_store_set(GTK_LIST_STORE(model), &it, COL_ICON, icon, -1);
        if (icon)
            g_object_unref(icon);
    }
    ++n_vols;
}

void remove_volume(VFSVolume* vol)
{
    if (!vol)
        return;

    GtkTreeIter it;
    VFSVolume* v = NULL;

    if (gtk_tree_model_get_iter_first(model, &it))
    {
        do
        {
            gtk_tree_model_get(model, &it, COL_DATA, &v, -1);
        } while (v != vol && gtk_tree_model_iter_next(model, &it));
    }
    if (v != vol)
        return;
    gtk_list_store_remove(GTK_LIST_STORE(model), &it);
    --n_vols;
}

void update_volume(VFSVolume* vol)
{
    if (!vol)
        return;

    GtkTreeIter it;
    VFSVolume* v = NULL;

    if (gtk_tree_model_get_iter_first(model, &it))
    {
        do
        {
            gtk_tree_model_get(model, &it, COL_DATA, &v, -1);
        } while (v != vol && gtk_tree_model_iter_next(model, &it));
    }
    if (v != vol)
    {
        add_volume(vol, TRUE);
        return;
    }

    GtkIconTheme* icon_theme = gtk_icon_theme_get_default();
    int icon_size = app_settings.small_icon_size;
    if (icon_size > PANE_MAX_ICON_SIZE)
        icon_size = PANE_MAX_ICON_SIZE;

    GdkPixbuf* icon = vfs_load_icon(icon_theme, vfs_volume_get_icon(vol), icon_size);
    gtk_list_store_set(GTK_LIST_STORE(model),
                       &it,
                       COL_ICON,
                       icon,
                       COL_NAME,
                       vfs_volume_get_disp_name(vol),
                       COL_PATH,
                       vfs_volume_get_mount_point(vol),
                       -1);
    if (icon)
        g_object_unref(icon);
}

char* ptk_location_view_get_mount_point_dir(const char* name)
{
    char* parent = NULL;

    // clean mount points
    if (name)
        ptk_location_view_clean_mount_points();

    XSet* set = xset_get("dev_automount_dirs");
    if (set->s)
    {
        if (g_str_has_prefix(set->s, "~/"))
            parent = g_build_filename(g_get_home_dir(), set->s + 2, NULL);
        else
            parent = g_strdup(set->s);
        if (parent)
        {
            const char* varname[] = {"$USER", "$UID", "$HOME", "$XDG_RUNTIME_DIR", "$XDG_CACHE_HOME"};
            int i;
            for (i = 0; i < G_N_ELEMENTS(varname); i++)
            {
                char* value;
                if (!strstr(parent, varname[i]))
                    continue;
                switch (i)
                {
                case 0: // $USER
                    value = g_strdup(g_get_user_name());
                    break;
                case 1: // $UID
                    value = g_strdup_printf("%d", geteuid());
                    break;
                case 2: // $HOME
                    value = g_strdup(g_get_home_dir());
                    break;
                case 3: // $XDG_RUNTIME_DIR
                    value = g_strdup(g_get_user_runtime_dir());
                    break;
                case 4: // $XDG_CACHE_HOME
                    value = g_strdup(g_get_user_cache_dir());
                    break;
                default:
                    value = g_strdup("");
                }
                char* str = parent;
                parent = replace_string(parent, varname[i], value, FALSE);
                g_free(str);
                g_free(value);
            }
            g_mkdir_with_parents(parent, 0700);
        }
        if (!have_rw_access(parent))
        {
            g_free(parent);
            parent = NULL;
        }
    }
    if (!parent)
        return g_build_filename(g_get_user_cache_dir(), "spacefm-mount", name, NULL);
    char* path = g_build_filename(parent, name, NULL);
    g_free(parent);
    return path;
}

void ptk_location_view_clean_mount_points()
{
    /* This function was moved from vfs-volume-nohal.c because HAL
     * build also requires it. */
    char* path;
    int i;

    // clean cache and Auto-Mount|Mount Dirs  (eg for fuse mounts)
    for (i = 0; i < 2; i++)
    {
        char* del_path;
        if (i == 0)
            path = g_build_filename(g_get_user_cache_dir(), "spacefm-mount", NULL);
        else // i == 1
        {
            del_path = ptk_location_view_get_mount_point_dir(NULL);
            if (!g_strcmp0(del_path, path))
            {
                // Auto-Mount|Mount Dirs is not set or valid
                g_free(del_path);
                break;
            }
            g_free(path);
            path = del_path;
        }
        GDir* dir;
        if ((dir = g_dir_open(path, 0, NULL)) != NULL)
        {
            const char* name;
            while ((name = g_dir_read_name(dir)) != NULL)
            {
                del_path = g_build_filename(path, name, NULL);
                rmdir(del_path); // removes empty, non-mounted directories
                g_free(del_path);
            }
            g_dir_close(dir);
        }
    }
    g_free(path);

    // clean udevil mount points
    char* udevil = g_find_program_in_path("udevil");
    if (udevil)
    {
        char* command = g_strdup_printf("%s -c \"sleep 1 ; %s clean\"", BASHPATH, udevil);
        g_free(udevil);
        print_command(command);
        g_spawn_command_line_async(command, NULL);
        g_free(command);
    }
}

char* ptk_location_view_create_mount_point(int mode, VFSVolume* vol, netmount_t* netmount, const char* path)
{
    char* mname = NULL;
    char* str;
    if (mode == HANDLER_MODE_FS && vol)
    {
        char* bdev = g_path_get_basename(vol->device_file);
        if (vol->label && vol->label[0] != '\0' && vol->label[0] != ' ' && g_utf8_validate(vol->label, -1, NULL) &&
            !strchr(vol->label, '/'))
            mname = g_strdup_printf("%.20s", vol->label);
        else if (vol->udi && vol->udi[0] != '\0' && g_utf8_validate(vol->udi, -1, NULL))
        {
            str = g_path_get_basename(vol->udi);
            mname = g_strdup_printf("%s-%.20s", bdev, str);
            g_free(str);
        }
        // else if ( device->id_uuid && device->id_uuid[0] != '\0' )
        //    mname = g_strdup_printf( "%s-%s", bdev, device->id_uuid );
        else
            mname = g_strdup(bdev);
        g_free(bdev);
    }
    else if (mode == HANDLER_MODE_NET)
    {
        if (netmount->host && g_utf8_validate(netmount->host, -1, NULL))
        {
            char* parent_dir = NULL;
            if (netmount->path)
            {
                parent_dir = replace_string(netmount->path, "/", "-", FALSE);
                g_strstrip(parent_dir);
                while (g_str_has_suffix(parent_dir, "-"))
                    parent_dir[strlen(parent_dir) - 1] = '\0';
                while (g_str_has_prefix(parent_dir, "-"))
                {
                    str = parent_dir;
                    parent_dir = g_strdup(str + 1);
                    g_free(str);
                }
                if (parent_dir[0] == '\0' || !g_utf8_validate(parent_dir, -1, NULL) || strlen(parent_dir) > 30)
                {
                    g_free(parent_dir);
                    parent_dir = NULL;
                }
            }
            if (parent_dir)
                mname = g_strdup_printf("%s-%s-%s", netmount->fstype, netmount->host, parent_dir);
            else if (netmount->host && netmount->host[0])
                mname = g_strdup_printf("%s-%s", netmount->fstype, netmount->host);
            else
                mname = g_strdup_printf("%s", netmount->fstype);
            g_free(parent_dir);
        }
        else
            mname = g_strdup(netmount->fstype);
    }
    else if (mode == HANDLER_MODE_FILE && path)
        mname = g_path_get_basename(path);

    // remove spaces
    if (mname && strchr(mname, ' '))
    {
        g_strstrip(mname);
        str = mname;
        mname = replace_string(mname, " ", "", FALSE);
        g_free(str);
    }

    if (mname && !mname[0])
    {
        g_free(mname);
        mname = NULL;
    }
    if (!mname)
        mname = g_strdup("mount");

    // complete mount point
    char* point1 = ptk_location_view_get_mount_point_dir(mname);
    g_free(mname);
    int r = 2;
    char* point = g_strdup(point1);

    // attempt to remove existing dir - succeeds only if empty and unmounted
    rmdir(point);
    while (g_file_test(point, G_FILE_TEST_EXISTS))
    {
        g_free(point);
        point = g_strdup_printf("%s-%d", point1, r++);
        rmdir(point);
    }
    g_free(point1);
    if (g_mkdir_with_parents(point, 0700) == -1)
        g_warning("Error creating mount point directory '%s': %s", point, g_strerror(errno));
    return point;
}

void on_autoopen_net_cb(VFSFileTask* task, AutoOpen* ao)
{
    const GList* l;

    if (!(ao && ao->device_file))
        return;

    // try to find device of mounted url.  url in mtab may differ from
    // user-entered url
    VFSVolume* device_file_vol = NULL;
    VFSVolume* mount_point_vol = NULL;
    const GList* volumes = vfs_volume_get_all_volumes();
    for (l = volumes; l; l = l->next)
    {
        VFSVolume* vol = (VFSVolume*)l->data;
        if (vol->is_mounted)
        {
            if (!strcmp(vol->device_file, ao->device_file))
            {
                device_file_vol = vol;
                break;
            }
            else if (!mount_point_vol && ao->mount_point && !vol->should_autounmount &&
                     !g_strcmp0(vol->mount_point, ao->mount_point))
                // found an unspecial mount point that matches the ao mount point -
                // save for later use if no device file match found
                mount_point_vol = vol;
        }
    }

    if (!device_file_vol)
    {
        if (mount_point_vol)
        {
            // g_printf("on_autoopen_net_cb used mount point:\n    mount_point = %s\n    device_file = %s\n
            // ao->device_file = %s\n", ao->mount_point, mount_point_vol->device_file, ao->device_file );
            device_file_vol = mount_point_vol;
        }
    }
    if (device_file_vol)
    {
        // copy the user-entered url to udi
        g_free(device_file_vol->udi);
        device_file_vol->udi = g_strdup(ao->device_file);

        // mark as special mount
        device_file_vol->should_autounmount = TRUE;

        // open in browser
        // if fuse fails, device may be in mtab even though mount point doesn't
        // exist, so test for mount point exists
        if (GTK_IS_WIDGET(ao->file_browser) && g_file_test(device_file_vol->mount_point, G_FILE_TEST_IS_DIR))
        {
            GDK_THREADS_ENTER();
            ptk_file_browser_emit_open(ao->file_browser, device_file_vol->mount_point, ao->job);
            GDK_THREADS_LEAVE();

            if (ao->job == PTK_OPEN_NEW_TAB && GTK_IS_WIDGET(ao->file_browser))
            {
                if (ao->file_browser->side_dev)
                    ptk_location_view_chdir(GTK_TREE_VIEW(ao->file_browser->side_dev),
                                            ptk_file_browser_get_cwd(ao->file_browser));
                if (ao->file_browser->side_book)
                    ptk_bookmark_view_chdir(GTK_TREE_VIEW(ao->file_browser->side_book), ao->file_browser, TRUE);
            }
        }
    }

    if (!ao->keep_point)
        ptk_location_view_clean_mount_points();

    g_free(ao->device_file);
    g_free(ao->mount_point);
    g_slice_free(AutoOpen, ao);
}

void ptk_location_view_mount_network(PtkFileBrowser* file_browser, const char* url, gboolean new_tab,
                                     gboolean force_new_mount)
{
    char* line;
    char* mount_point = NULL;
    netmount_t* netmount = NULL;

    // split url
    if (split_network_url(url, &netmount) != 1)
    {
        // not a valid url
        xset_msg_dialog(GTK_WIDGET(file_browser),
                        GTK_MESSAGE_ERROR,
                        _("Invalid URL"),
                        NULL,
                        0,
                        _("The entered URL is not valid."),
                        NULL,
                        NULL);
        return;
    }

    /*
    g_printf( "\nurl=%s\n", netmount->url );
    g_printf( "  fstype=%s\n", netmount->fstype );
    g_printf( "  host=%s\n", netmount->host );
    g_printf( "  port=%s\n", netmount->port );
    g_printf( "  user=%s\n", netmount->user );
    g_printf( "  pass=%s\n", netmount->pass );
    g_printf( "  path=%s\n\n", netmount->path );
    */

    // already mounted?
    if (!force_new_mount)
    {
        const GList* l;
        VFSVolume* vol;
        const GList* volumes = vfs_volume_get_all_volumes();
        for (l = volumes; l; l = l->next)
        {
            vol = (VFSVolume*)l->data;
            // test against mtab url and copy of user-entered url (udi)
            if (strstr(vol->device_file, netmount->url) || strstr(vol->udi, netmount->url))
            {
                if (vol->is_mounted && vol->mount_point && have_x_access(vol->mount_point))
                {
                    if (new_tab)
                    {
                        ptk_file_browser_emit_open(file_browser, vol->mount_point, PTK_OPEN_NEW_TAB);
                    }
                    else
                    {
                        if (strcmp(vol->mount_point, ptk_file_browser_get_cwd(file_browser)))
                            ptk_file_browser_chdir(file_browser, vol->mount_point, PTK_FB_CHDIR_ADD_HISTORY);
                    }
                    goto _net_free;
                }
            }
        }
    }

    // get mount command
    gboolean run_in_terminal;
    gboolean ssh_udevil = FALSE;
    char* cmd =
        vfs_volume_handler_cmd(HANDLER_MODE_NET, HANDLER_MOUNT, NULL, NULL, netmount, &run_in_terminal, &mount_point);
    if (!cmd)
    {
        xset_msg_dialog(GTK_WIDGET(file_browser),
                        GTK_MESSAGE_ERROR,
                        _("Handler Not Found"),
                        NULL,
                        0,
                        _("No network handler is configured for this URL, or no mount command is set.  Add a handler "
                          "in Devices|Settings|Protocol Handlers."),
                        NULL,
                        NULL);
        goto _net_free;
    }

    // task
    char* keepterm;
    if (ssh_udevil)
        keepterm = g_strdup_printf("if [ $? -ne 0 ];then\n    read -p \"%s\"\nelse\n    echo;read -p \"Press Enter to "
                                   "close (closing this window may unmount sshfs)\"\nfi\n",
                                   press_enter_to_close);
    else if (run_in_terminal)
        keepterm = g_strdup_printf("[[ $? -eq 0 ]] || ( read -p '%s: ' )\n", press_enter_to_close);
    else
        keepterm = g_strdup("");

    line = g_strdup_printf("%s%s\n%s", ssh_udevil ? "echo Connecting...\n\n" : "", cmd, keepterm);
    g_free(keepterm);
    g_free(cmd);

    char* task_name = g_strdup_printf(_("Open URL %s"), netmount->url);
    PtkFileTask* task = ptk_file_exec_new(task_name, NULL, GTK_WIDGET(file_browser), file_browser->task_view);
    g_free(task_name);
    task->task->exec_command = line;
    task->task->exec_sync = !ssh_udevil;
    task->task->exec_export = TRUE;
    task->task->exec_browser = file_browser;
    task->task->exec_popup = FALSE;
    task->task->exec_show_output = FALSE;
    task->task->exec_show_error = TRUE;
    task->task->exec_terminal = run_in_terminal;
    task->task->exec_keep_terminal = FALSE;
    XSet* set = xset_get("dev_icon_network");
    task->task->exec_icon = g_strdup(set->icon);

    // autoopen
    if (!ssh_udevil) // !sync
    {
        AutoOpen* ao;
        ao = g_slice_new0(AutoOpen);
        ao->device_file = g_strdup(netmount->url);
        ao->devnum = 0;
        ao->file_browser = file_browser;
        ao->mount_point = mount_point;
        mount_point = NULL;
        if (new_tab)
            ao->job = PTK_OPEN_NEW_TAB;
        else
            ao->job = PTK_OPEN_DIR;
        task->complete_notify = (GFunc)on_autoopen_net_cb;
        task->user_data = ao;
    }
    ptk_file_task_run(task);

_net_free:
    g_free(mount_point);
    g_free(netmount->url);
    g_free(netmount->fstype);
    g_free(netmount->host);
    g_free(netmount->ip);
    g_free(netmount->port);
    g_free(netmount->user);
    g_free(netmount->pass);
    g_free(netmount->path);
    g_slice_free(netmount_t, netmount);
}

static void popup_missing_mount(GtkWidget* view, int job)
{
    const char* cmd;

    if (job == 0)
        cmd = _("mount");
    else
        cmd = _("unmount");
    char* msg = g_strdup_printf(_("No handler is configured for this device type, or no %s command is set.  Add a "
                                  "handler in Settings|Device Handlers or Protocol Handlers."),
                                cmd);
    xset_msg_dialog(view, GTK_MESSAGE_ERROR, _("Handler Not Found"), NULL, 0, msg, NULL, NULL);
    g_free(msg);
}

static void on_mount(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    GtkWidget* view;
    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");

    if (!view || !vol)
        return;
    if (!vol->device_file)
        return;
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
    // Note: file_browser may be NULL
    if (!GTK_IS_WIDGET(file_browser))
        file_browser = NULL;

    // task
    gboolean run_in_terminal;
    char* line = vfs_volume_get_mount_command(vol, xset_get_s("dev_mount_options"), &run_in_terminal);
    if (!line)
    {
        popup_missing_mount(view, 0);
        return;
    }
    char* task_name = g_strdup_printf(_("Mount %s"), vol->device_file);
    PtkFileTask* task = ptk_file_exec_new(task_name, NULL, view, file_browser ? file_browser->task_view : NULL);
    g_free(task_name);

    if (strstr(line, "udisks ")) // udisks v1
        task->task->exec_type = VFS_EXEC_UDISKS;
    char* keep_term;
    if (run_in_terminal)
        keep_term = g_strdup_printf("\n[[ $? -eq 0 ]] || ( read -p '%s: ' )\n", press_enter_to_close);
    else
        keep_term = g_strdup("");

    task->task->exec_command = g_strdup_printf("%s%s", line, keep_term);
    g_free(line);
    g_free(keep_term);
    task->task->exec_sync = !run_in_terminal;
    task->task->exec_export = !!file_browser;
    task->task->exec_browser = file_browser;
    task->task->exec_popup = FALSE;
    task->task->exec_show_output = FALSE;
    task->task->exec_show_error = TRUE;
    task->task->exec_terminal = run_in_terminal;
    task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
    vol->inhibit_auto = TRUE;
    ptk_file_task_run(task);
}

static void on_mount_root(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    GtkWidget* view;
    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");

    XSet* set = xset_get("dev_root_mount");
    char* options = vfs_volume_get_mount_options(vol, xset_get_s("dev_mount_options"));
    if (!options)
        options = g_strdup("");
    char* msg = g_strdup_printf(
        _("Enter mount command:\n\nUse:\n\t%%%%v\tdevice file ( %s )\n\t%%%%o\tvolume-specific mount options\n\t\t( %s "
          ")\n\nNote: fstab overrides some options\n\nEDIT WITH CARE   This command is run as root"),
        vol->device_file,
        options);

    if (!set->s)
        set->s = g_strdup(set->z);
    char* old_set_s = g_strdup(set->s);

    if (xset_text_dialog(view,
                         _("Mount As Root"),
                         xset_get_image("GTK_STOCK_DIALOG_WARNING", GTK_ICON_SIZE_DIALOG),
                         TRUE,
                         _("MOUNT AS ROOT"),
                         msg,
                         set->s,
                         &set->s,
                         set->z,
                         TRUE,
                         set->line) &&
        set->s)
    {
        gboolean change_root = (!old_set_s || strcmp(old_set_s, set->s));

        char* s1 = replace_string(set->s, "%v", vol->device_file, FALSE);
        char* cmd = replace_string(s1, "%o", options, FALSE);
        g_free(s1);
        s1 = cmd;
        cmd = g_strdup_printf("echo %s; echo; %s", s1, s1);
        g_free(s1);

        // task
        PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
        char* task_name = g_strdup_printf(_("Mount As Root %s"), vol->device_file);
        PtkFileTask* task = ptk_file_exec_new(task_name, NULL, view, file_browser->task_view);
        g_free(task_name);
        task->task->exec_command = cmd;
        task->task->exec_write_root = change_root;
        if (strstr(cmd, "udisks ")) // udisks v1
            task->task->exec_type = VFS_EXEC_UDISKS;
        task->task->exec_as_user = g_strdup_printf("root");
        task->task->exec_sync = TRUE;
        task->task->exec_popup = FALSE;
        task->task->exec_show_output = FALSE;
        task->task->exec_show_error = TRUE;
        task->task->exec_export = FALSE;
        task->task->exec_browser = file_browser;
        task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
        ptk_file_task_run(task);
    }
    g_free(msg);
    g_free(options);
}

static void on_umount_root(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    GtkWidget* view;
    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");

    XSet* set = xset_get("dev_root_unmount");
    char* msg = g_strdup_printf(_("Enter unmount command:\n\nUse:\n\t%%%%v\tdevice file ( %s )\n\nEDIT WITH CARE   "
                                  "This command is run as root"),
                                vol->device_file);
    if (!set->s)
        set->s = g_strdup(set->z);
    char* old_set_s = g_strdup(set->s);

    if (xset_text_dialog(view,
                         _("Unmount As Root"),
                         xset_get_image("GTK_STOCK_DIALOG_WARNING", GTK_ICON_SIZE_DIALOG),
                         TRUE,
                         _("UNMOUNT AS ROOT"),
                         msg,
                         set->s,
                         &set->s,
                         set->z,
                         TRUE,
                         set->line) &&
        set->s)
    {
        gboolean change_root = (!old_set_s || strcmp(old_set_s, set->s));

        // task
        char* s1 = replace_string(set->s, "%v", vol->device_file, FALSE);
        char* cmd = g_strdup_printf("echo %s; echo; %s", s1, s1);
        g_free(s1);
        PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
        char* task_name = g_strdup_printf(_("Unmount As Root %s"), vol->device_file);
        PtkFileTask* task = ptk_file_exec_new(task_name, NULL, view, file_browser->task_view);
        g_free(task_name);
        if (strstr(cmd, "udisks ")) // udisks v1
            task->task->exec_type = VFS_EXEC_UDISKS;
        task->task->exec_command = cmd;
        task->task->exec_write_root = change_root;
        task->task->exec_as_user = g_strdup_printf("root");
        task->task->exec_sync = TRUE;
        task->task->exec_popup = FALSE;
        task->task->exec_show_output = FALSE;
        task->task->exec_show_error = TRUE;
        task->task->exec_export = FALSE;
        task->task->exec_browser = file_browser;
        task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
        ptk_file_task_run(task);
    }
    g_free(msg);
}

static void on_umount(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    GtkWidget* view;
    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
    // Note: file_browser may be NULL
    if (!GTK_IS_WIDGET(file_browser))
        file_browser = NULL;

    /*
    if ( vol->device_type != DEVICE_TYPE_BLOCK )
    {
        char* str = g_find_program_in_path( "udevil" );
        if ( !str )
        {
            g_free( str );
            xset_msg_dialog( GTK_WIDGET( file_browser ), GTK_MESSAGE_ERROR,
                            _("udevil Not Installed"), NULL, 0,
                            _("Unmounting a network share requires udevil to be installed."),
                            NULL, NULL );
            return;
        }
        g_free( str );
    }
    */

    // task
    gboolean run_in_terminal;
    char* line = vfs_volume_device_unmount_cmd(vol, &run_in_terminal);
    if (!line)
    {
        popup_missing_mount(view, 1);
        return;
    }
    char* task_name = g_strdup_printf(_("Unmount %s"), vol->device_file);
    PtkFileTask* task = ptk_file_exec_new(task_name, NULL, view, file_browser ? file_browser->task_view : NULL);
    g_free(task_name);
    if (strstr(line, "udisks ")) // udisks v1
        task->task->exec_type = VFS_EXEC_UDISKS;
    char* keep_term;
    if (run_in_terminal)
        keep_term = g_strdup_printf("\n[[ $? -eq 0 ]] || ( read -p '%s: ' )\n", press_enter_to_close);
    else
        keep_term = g_strdup("");
    task->task->exec_command = g_strdup_printf("%s%s", line, keep_term);
    g_free(line);
    g_free(keep_term);
    task->task->exec_sync = !run_in_terminal;
    task->task->exec_export = !!file_browser;
    task->task->exec_browser = file_browser;
    task->task->exec_popup = FALSE;
    task->task->exec_show_output = FALSE;
    task->task->exec_show_error = TRUE;
    task->task->exec_terminal = run_in_terminal;
    task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
    ptk_file_task_run(task);
}

static void on_eject(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    PtkFileTask* task;
    char* line;
    GtkWidget* view;
    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
    // Note: file_browser may be NULL
    if (!GTK_IS_WIDGET(file_browser))
        file_browser = NULL;

    /*
    if ( vol->device_type != DEVICE_TYPE_BLOCK )
    {
        char* str = g_find_program_in_path( "udevil" );
        if ( !str )
        {
            g_free( str );
            xset_msg_dialog( GTK_WIDGET( file_browser ), GTK_MESSAGE_ERROR,
                            _("udevil Not Installed"), NULL, 0,
                            _("Unmounting a network share requires udevil to be installed."),
                            NULL, NULL );
            return;
        }
        g_free( str );
    }
    */

    if (vfs_volume_is_mounted(vol))
    {
        // task
        char* wait;
        char* wait_done;
        char* eject;
        gboolean run_in_terminal;

        char* unmount = vfs_volume_device_unmount_cmd(vol, &run_in_terminal);
        if (!unmount)
        {
            popup_missing_mount(view, 1);
            return;
        }

#ifdef DEPRECATED_HW
        if (vol->device_type == DEVICE_TYPE_BLOCK && (vol->is_optical || vol->requires_eject))
#else
        if (vol->device_type == DEVICE_TYPE_BLOCK && (vol->requires_eject))
#endif
            eject = g_strdup_printf("\neject %s", vol->device_file);
        else
            eject = g_strdup("\nexit 0");

        if (!file_browser && !run_in_terminal && vol->device_type == DEVICE_TYPE_BLOCK)
        {
            char* prog = g_find_program_in_path(g_get_prgname());
            if (!prog)
                prog = g_strdup(g_get_prgname());
            if (!prog)
                prog = g_strdup("spacefm");
            // run from desktop window - show a pending dialog
            wait = g_strdup_printf("%s -g --title 'Remove %s' --label '\\nPlease wait while device %s is synced and "
                                   "unmounted...' >/dev/null &\nwaitp=$!\n",
                                   prog,
                                   vol->device_file,
                                   vol->device_file);
            // sleep .2 here to ensure spacefm -g isn't killed too quickly causing hang
            wait_done = g_strdup("\n( sleep .2; kill $waitp 2>/dev/null ) &");
            g_free(prog);
        }
        else
        {
            wait = g_strdup("");
            wait_done = g_strdup("");
        }
        if (run_in_terminal)
            line = g_strdup_printf(
                "echo 'Unmounting %s...'\n%s%s\nif [ $? -ne 0 ];then\n    read -p '%s: '\n    exit 1\nelse\n    %s\nfi",
                vol->device_file,
                vol->device_type == DEVICE_TYPE_BLOCK ? "sync\n" : "",
                unmount,
                press_enter_to_close,
                eject);
        else
            line = g_strdup_printf("%s%s%s\nuerr=$?%s\nif [ $uerr -ne 0 ];then\n    exit 1\nfi%s",
                                   wait,
                                   vol->device_type == DEVICE_TYPE_BLOCK ? "sync\n" : "",
                                   unmount,
                                   wait_done,
                                   eject);
        g_free(eject);
        g_free(wait);
        g_free(wait_done);
        char* task_name = g_strdup_printf("Remove %s", vol->device_file);
        task = ptk_file_exec_new(task_name, NULL, view, file_browser ? file_browser->task_view : NULL);
        g_free(task_name);
        if (strstr(line, "udisks ")) // udisks v1
            task->task->exec_type = VFS_EXEC_UDISKS;
        task->task->exec_command = line;
        task->task->exec_sync = !run_in_terminal;
        task->task->exec_export = !!file_browser;
        task->task->exec_browser = file_browser;
        task->task->exec_show_error = TRUE;
        task->task->exec_terminal = run_in_terminal;
        task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
    }
#ifdef DEPRECATED_HW
    else if (vol->device_type == DEVICE_TYPE_BLOCK && (vol->is_optical || vol->requires_eject))
#else
    else if (vol->device_type == DEVICE_TYPE_BLOCK && (vol->requires_eject))
#endif
    {
        // task
        line = g_strdup_printf("eject %s", vol->device_file);
        char* task_name = g_strdup_printf(_("Remove %s"), vol->device_file);
        task = ptk_file_exec_new(task_name, NULL, view, file_browser ? file_browser->task_view : NULL);
        g_free(task_name);
        task->task->exec_command = line;
        task->task->exec_sync = FALSE;
        task->task->exec_show_error = FALSE;
        task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
    }
    else
    {
        // task
        line = g_strdup_printf("sync");
        char* task_name = g_strdup_printf(_("Remove %s"), vol->device_file);
        task = ptk_file_exec_new(task_name, NULL, view, file_browser ? file_browser->task_view : NULL);
        g_free(task_name);
        task->task->exec_command = line;
        task->task->exec_sync = FALSE;
        task->task->exec_show_error = FALSE;
        task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
    }
    ptk_file_task_run(task);
}

gboolean on_autoopen_cb(VFSFileTask* task, AutoOpen* ao)
{
    const GList* l;
    // g_printf("on_autoopen_cb\n");
    const GList* volumes = vfs_volume_get_all_volumes();
    for (l = volumes; l; l = l->next)
    {
        if (((VFSVolume*)l->data)->devnum == ao->devnum)
        {
            VFSVolume* vol = (VFSVolume*)l->data;
            vol->inhibit_auto = FALSE;
            if (vol->is_mounted)
            {
                if (GTK_IS_WIDGET(ao->file_browser))
                {
                    GDK_THREADS_ENTER(); // hangs on dvd mount without this - why?
                    ptk_file_browser_emit_open(ao->file_browser, vol->mount_point, ao->job);
                    GDK_THREADS_LEAVE();
                }
                else
                    open_in_prog(vol->mount_point);
            }
            break;
        }
    }
    if (GTK_IS_WIDGET(ao->file_browser) && ao->job == PTK_OPEN_NEW_TAB && ao->file_browser->side_dev)
        ptk_location_view_chdir(GTK_TREE_VIEW(ao->file_browser->side_dev), ptk_file_browser_get_cwd(ao->file_browser));
    g_free(ao->device_file);
    g_slice_free(AutoOpen, ao);
    return FALSE;
}

static gboolean try_mount(GtkTreeView* view, VFSVolume* vol)
{
    if (!view || !vol)
        return FALSE;
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
    if (!file_browser)
        return FALSE;
    // task
    gboolean run_in_terminal;
    char* line = vfs_volume_get_mount_command(vol, xset_get_s("dev_mount_options"), &run_in_terminal);
    if (!line)
    {
        popup_missing_mount(GTK_WIDGET(view), 0);
        return FALSE;
    }
    char* task_name = g_strdup_printf(_("Mount %s"), vol->device_file);
    PtkFileTask* task = ptk_file_exec_new(task_name, NULL, GTK_WIDGET(view), file_browser->task_view);
    g_free(task_name);
    if (strstr(line, "udisks ")) // udisks v1
        task->task->exec_type = VFS_EXEC_UDISKS;
    char* keep_term;
    if (run_in_terminal)
        keep_term = g_strdup_printf("\n[[ $? -eq 0 ]] || ( read -p '%s: ' )\n", press_enter_to_close);
    else
        keep_term = g_strdup("");
    task->task->exec_command = g_strdup_printf("%s%s", line, keep_term);
    g_free(line);
    g_free(keep_term);
    task->task->exec_sync = TRUE;
    task->task->exec_export = TRUE;
    task->task->exec_browser = file_browser;
    task->task->exec_popup = FALSE;
    task->task->exec_show_output = FALSE;
    task->task->exec_show_error = TRUE; // set to true for error on click
    task->task->exec_terminal = run_in_terminal;
    task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));

    // autoopen
    AutoOpen* ao = g_slice_new0(AutoOpen);
    ao->devnum = vol->devnum;
    ao->device_file = NULL;
    ao->file_browser = file_browser;
    if (xset_get_b("dev_newtab"))
        ao->job = PTK_OPEN_NEW_TAB;
    else
        ao->job = PTK_OPEN_DIR;
    ao->mount_point = NULL;
    task->complete_notify = (GFunc)on_autoopen_cb;
    task->user_data = ao;
    vol->inhibit_auto = TRUE;

    ptk_file_task_run(task);

    return vfs_volume_is_mounted(vol);
}

static void on_open_tab(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    PtkFileBrowser* file_browser;
    GtkWidget* view;

    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");
    if (view)
        file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
    else
        file_browser = (PtkFileBrowser*)fm_main_window_get_current_file_browser(NULL);

    if (!file_browser || !vol)
        return;

    if (!vol->is_mounted)
    {
        // get mount command
        gboolean run_in_terminal;
        char* line = vfs_volume_get_mount_command(vol, xset_get_s("dev_mount_options"), &run_in_terminal);
        if (!line)
        {
            popup_missing_mount(view, 0);
            return;
        }

        // task
        char* task_name = g_strdup_printf(_("Mount %s"), vol->device_file);
        PtkFileTask* task = ptk_file_exec_new(task_name, NULL, view, file_browser->task_view);
        g_free(task_name);
        if (strstr(line, "udisks ")) // udisks v1
            task->task->exec_type = VFS_EXEC_UDISKS;
        char* keep_term;
        if (run_in_terminal)
            keep_term = g_strdup_printf("\n[[ $? -eq 0 ]] || ( read -p '%s: ' )\n", press_enter_to_close);
        else
            keep_term = g_strdup("");
        task->task->exec_command = g_strdup_printf("%s%s", line, keep_term);
        g_free(line);
        g_free(keep_term);
        task->task->exec_sync = TRUE;
        task->task->exec_export = TRUE;
        task->task->exec_browser = file_browser;
        task->task->exec_popup = FALSE;
        task->task->exec_show_output = FALSE;
        task->task->exec_show_error = TRUE;
        task->task->exec_terminal = run_in_terminal;
        task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));

        // autoopen
        AutoOpen* ao = g_slice_new0(AutoOpen);
        ao->devnum = vol->devnum;
        ao->device_file = NULL;
        ao->file_browser = file_browser;
        ao->job = PTK_OPEN_NEW_TAB;
        ao->mount_point = NULL;
        task->complete_notify = (GFunc)on_autoopen_cb;
        task->user_data = ao;
        vol->inhibit_auto = TRUE;

        ptk_file_task_run(task);
    }
    else
        ptk_file_browser_emit_open(file_browser, vol->mount_point, PTK_OPEN_NEW_TAB);
}

static void on_open(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    PtkFileBrowser* file_browser;
    GtkWidget* view;
    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");
    if (view)
        file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
    else
        file_browser = (PtkFileBrowser*)fm_main_window_get_current_file_browser(NULL);

    if (!vol)
        return;

    // Note: file_browser may be NULL
    if (!GTK_IS_WIDGET(file_browser))
        file_browser = NULL;

    if (!vol->is_mounted)
    {
        // get mount command
        gboolean run_in_terminal;
        char* line = vfs_volume_get_mount_command(vol, xset_get_s("dev_mount_options"), &run_in_terminal);
        if (!line)
        {
            popup_missing_mount(view, 0);
            return;
        }

        // task
        char* task_name = g_strdup_printf(_("Mount %s"), vol->device_file);
        PtkFileTask* task = ptk_file_exec_new(task_name, NULL, view, file_browser ? file_browser->task_view : NULL);
        g_free(task_name);
        if (strstr(line, "udisks ")) // udisks v1
            task->task->exec_type = VFS_EXEC_UDISKS;
        char* keep_term;
        if (run_in_terminal)
            keep_term = g_strdup_printf("\n[[ $? -eq 0 ]] || ( read -p '%s: ' )\n", press_enter_to_close);
        else
            keep_term = g_strdup("");
        task->task->exec_command = g_strdup_printf("%s%s", line, keep_term);
        g_free(line);
        g_free(keep_term);
        task->task->exec_sync = TRUE;
        task->task->exec_export = !!file_browser;
        task->task->exec_browser = file_browser;
        task->task->exec_popup = FALSE;
        task->task->exec_show_output = FALSE;
        task->task->exec_show_error = TRUE;
        task->task->exec_terminal = run_in_terminal;
        task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));

        // autoopen
        AutoOpen* ao = g_slice_new0(AutoOpen);
        ao->devnum = vol->devnum;
        ao->device_file = NULL;
        ao->file_browser = file_browser;
        ao->job = PTK_OPEN_DIR;
        ao->mount_point = NULL;
        task->complete_notify = (GFunc)on_autoopen_cb;
        task->user_data = ao;
        vol->inhibit_auto = TRUE;

        ptk_file_task_run(task);
    }
    else if (file_browser)
        ptk_file_browser_emit_open(file_browser, vol->mount_point, PTK_OPEN_DIR);
    else
        open_in_prog(vol->mount_point);
}

static void on_remount(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    char* line;
    GtkWidget* view;
    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");

    // get user options
    XSet* set = xset_get("dev_remount_options");
    if (!xset_text_dialog(view, set->title, NULL, TRUE, set->desc, NULL, set->s, &set->s, set->z, FALSE, set->line))
        return;

    gboolean mount_in_terminal;
    gboolean unmount_in_terminal = FALSE;
    char* mount_command = vfs_volume_get_mount_command(vol, set->s, &mount_in_terminal);
    if (!mount_command)
    {
        popup_missing_mount(view, 0);
        return;
    }

    // task
    char* task_name = g_strdup_printf(_("Remount %s"), vol->device_file);
    PtkFileTask* task = ptk_file_exec_new(task_name, NULL, view, file_browser->task_view);
    g_free(task_name);
    if (vfs_volume_is_mounted(vol))
    {
        // udisks can't remount, so unmount and mount
        char* unmount_command = vfs_volume_device_unmount_cmd(vol, &unmount_in_terminal);
        if (!unmount_command)
        {
            g_free(mount_command);
            popup_missing_mount(view, 1);
            return;
        }
        if (mount_in_terminal || unmount_in_terminal)
            line = g_strdup_printf("%s\nif [ $? -ne 0 ];then\n    read -p '%s: '\n    exit 1\nelse\n    %s\n    [[ $? "
                                   "-eq 0 ]] || ( read -p '%s: ' )\nfi",
                                   unmount_command,
                                   press_enter_to_close,
                                   mount_command,
                                   press_enter_to_close);
        else
            line = g_strdup_printf("%s\nif [ $? -ne 0 ];then\n    exit 1\nelse\n    %s\nfi",
                                   unmount_command,
                                   mount_command);
        g_free(mount_command);
        g_free(unmount_command);
    }
    else
        line = mount_command;
    if (strstr(line, "udisks ")) // udisks v1
        task->task->exec_type = VFS_EXEC_UDISKS;
    task->task->exec_command = line;
    task->task->exec_sync = !mount_in_terminal && !unmount_in_terminal;
    task->task->exec_export = TRUE;
    task->task->exec_browser = file_browser;
    task->task->exec_popup = FALSE;
    task->task->exec_show_output = FALSE;
    task->task->exec_show_error = TRUE;
    task->task->exec_terminal = mount_in_terminal || unmount_in_terminal;
    task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
    vol->inhibit_auto = TRUE;
    ptk_file_task_run(task);
}

static void on_reload(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    char* line;
    PtkFileTask* task;

    GtkWidget* view;
    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");

    if (vfs_volume_is_mounted(vol))
    {
        // task
        char* eject;
        gboolean run_in_terminal;
        char* unmount = vfs_volume_device_unmount_cmd(vol, &run_in_terminal);
        if (!unmount)
        {
            popup_missing_mount(view, 1);
            return;
        }

#ifdef DEPRECATED_HW
        if (vol->is_optical || vol->requires_eject)
#else
        if (vol->requires_eject)
#endif
            eject = g_strdup_printf("\nelse\n    eject %s\n    sleep 0.3\n    eject -t %s",
                                    vol->device_file,
                                    vol->device_file);
        else
            eject = g_strdup("");

        if (run_in_terminal)
            line = g_strdup_printf(
                "echo 'Unmounting %s...'\nsync\n%s\nif [ $? -ne 0 ];then\n    read -p '%s: '\n    exit 1%s\nfi",
                vol->device_file,
                unmount,
                press_enter_to_close,
                eject);
        else
            line = g_strdup_printf("sync\n%s\nif [ $? -ne 0 ];then\n    exit 1%s\nfi", unmount, eject);
        g_free(eject);
        g_free(unmount);
        char* task_name = g_strdup_printf("Reload %s", vol->device_file);
        task = ptk_file_exec_new(task_name, NULL, view, file_browser->task_view);
        g_free(task_name);
        if (strstr(line, "udisks ")) // udisks v1
            task->task->exec_type = VFS_EXEC_UDISKS;
        task->task->exec_command = line;
        task->task->exec_sync = !run_in_terminal;
        task->task->exec_export = TRUE;
        task->task->exec_browser = file_browser;
        task->task->exec_show_error = TRUE;
        task->task->exec_terminal = run_in_terminal;
        task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
    }
#ifdef DEPRECATED_HW
    else if (vol->is_optical || vol->requires_eject)
#else
    else if (vol->requires_eject)
#endif
    {
        // task
        line = g_strdup_printf("eject %s; sleep 0.3; eject -t %s", vol->device_file, vol->device_file);
        char* task_name = g_strdup_printf(_("Reload %s"), vol->device_file);
        task = ptk_file_exec_new(task_name, NULL, view, file_browser->task_view);
        g_free(task_name);
        task->task->exec_command = line;
        task->task->exec_sync = FALSE;
        task->task->exec_show_error = FALSE;
        task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
    }
    else
        return;
    ptk_file_task_run(task);
    //    vol->ever_mounted = FALSE;
}

static void on_sync(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    GtkWidget* view;
    if (!item)
        view = view2;
    else
    {
        g_signal_stop_emission_by_name(item, "activate");
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");
    }

    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");

    PtkFileTask* task = ptk_file_exec_new(_("Sync"), NULL, view, file_browser->task_view);
    task->task->exec_browser = NULL;
    task->task->exec_action = g_strdup_printf("sync");
    task->task->exec_command = g_strdup_printf("sync");
    task->task->exec_as_user = NULL;
    task->task->exec_sync = TRUE;
    task->task->exec_popup = FALSE;
    task->task->exec_show_output = FALSE;
    task->task->exec_show_error = TRUE;
    task->task->exec_terminal = FALSE;
    task->task->exec_export = FALSE;
    // task->task->exec_icon = g_strdup_printf( "start-here" );
    ptk_file_task_run(task);
}

static void on_root_fstab(GtkMenuItem* item, GtkWidget* view)
{
    char* fstab_path = g_build_filename(SYSCONFDIR, "fstab", NULL);
    xset_edit(view, fstab_path, TRUE, FALSE);
    g_free(fstab_path);
}

static void on_root_udevil(GtkMenuItem* item, GtkWidget* view)
{
    char* udevil_path = g_build_filename(SYSCONFDIR, "udevil", NULL);
    char* udevil_conf = g_build_filename(SYSCONFDIR, "udevil", "udevil.conf", NULL);
    char* msg = g_strdup_printf(_("The %s directory was not found.  Is udevil installed?"), udevil_path);
    if (g_file_test(udevil_path, G_FILE_TEST_IS_DIR))
        xset_edit(view, udevil_conf, TRUE, FALSE);
    else
        xset_msg_dialog(view, GTK_MESSAGE_ERROR, _("Directory Missing"), NULL, 0, msg, NULL, NULL);
    g_free(udevil_path);
    g_free(udevil_conf);
    g_free(msg);
}

static void on_prop(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    GtkWidget* view;
    char* cmd;

    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");

    if (!vol || !view)
        return;

    if (!vol->device_file)
        return;

    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");

    // use handler command if available
    gboolean run_in_terminal;
    if (vol->device_type == DEVICE_TYPE_NETWORK)
    {
        // is a network - try to get prop command
        netmount_t* netmount = NULL;
        if (split_network_url(vol->udi, &netmount) == 1)
        {
            cmd = vfs_volume_handler_cmd(HANDLER_MODE_NET, HANDLER_PROP, vol, NULL, netmount, &run_in_terminal, NULL);
            g_free(netmount->url);
            g_free(netmount->fstype);
            g_free(netmount->host);
            g_free(netmount->ip);
            g_free(netmount->port);
            g_free(netmount->user);
            g_free(netmount->pass);
            g_free(netmount->path);
            g_slice_free(netmount_t, netmount);

            if (!cmd)
            {
                cmd = g_strdup_printf("echo MOUNT\nmount | grep \" on %s \"\necho\necho PROCESSES\n/usr/bin/lsof -w "
                                      "\"%s\" | head -n 500\n",
                                      vol->mount_point,
                                      vol->mount_point);
                run_in_terminal = FALSE;
            }
            else if (strstr(cmd, "%a"))
            {
                char* pointq = bash_quote(vol->mount_point);
                char* str = cmd;
                cmd = replace_string(cmd, "%a", pointq, FALSE);
                g_free(str);
                g_free(pointq);
            }
        }
        else
            return;
    }
    else if (vol->device_type == DEVICE_TYPE_OTHER && mtab_fstype_is_handled_by_protocol(vol->fs_type))
    {
        cmd = vfs_volume_handler_cmd(HANDLER_MODE_NET, HANDLER_PROP, vol, NULL, NULL, &run_in_terminal, NULL);
        if (!cmd)
        {
            cmd = g_strdup_printf(
                "echo MOUNT\nmount | grep \" on %s \"\necho\necho PROCESSES\n/usr/bin/lsof -w \"%s\" | head -n 500\n",
                vol->mount_point,
                vol->mount_point);
            run_in_terminal = FALSE;
        }
        else if (strstr(cmd, "%a"))
        {
            char* pointq = bash_quote(vol->mount_point);
            char* str = cmd;
            cmd = replace_string(cmd, "%a", pointq, FALSE);
            g_free(str);
            g_free(pointq);
        }
    }
    else
        cmd = vfs_volume_handler_cmd(HANDLER_MODE_FS, HANDLER_PROP, vol, NULL, NULL, &run_in_terminal, NULL);

    // create task
    // Note: file_browser may be NULL
    if (!GTK_IS_WIDGET(file_browser))
        file_browser = NULL;
    char* task_name = g_strdup_printf(_("Properties %s"), vol->device_file);
    PtkFileTask* task = ptk_file_exec_new(task_name,
                                          NULL,
                                          file_browser ? GTK_WIDGET(file_browser) : view,
                                          file_browser ? file_browser->task_view : NULL);
    g_free(task_name);
    task->task->exec_browser = file_browser;

    if (cmd)
    {
        task->task->exec_command = cmd;
        task->task->exec_sync = !run_in_terminal;
        task->task->exec_export = !!file_browser;
        task->task->exec_browser = file_browser;
        task->task->exec_popup = TRUE;
        task->task->exec_show_output = TRUE;
        task->task->exec_terminal = run_in_terminal;
        task->task->exec_keep_terminal = run_in_terminal;
        task->task->exec_scroll_lock = TRUE;
        task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
        // task->task->exec_keep_tmp = TRUE;
        ptk_file_task_run(task);
        return;
    }

    // no handler command - show default properties
    char size_str[64];
    char* df;
    char* udisks;
    char* lsof;
    char* infobash;
    char* path;
    char* flags;
    char* old_flags;
    char* uuid = NULL;
    char* fstab = NULL;
    char* info;
    char* esc_path;

    char* fstab_path = g_build_filename(SYSCONFDIR, "fstab", NULL);

    char* base = g_path_get_basename(vol->device_file);
    if (base)
    {
        // /bin/ls -l /dev/disk/by-uuid | grep ../sdc2 | sed 's/.* \([a-fA-F0-9\-]*\) -> .*/\1/'
        cmd = g_strdup_printf(
            "%s -c \"/bin/ls -l /dev/disk/by-uuid | grep '\\.\\./%s$' | sed 's/.* \\([a-fA-F0-9-]*\\) -> .*/\\1/'\"",
            BASHPATH,
            base);
        print_command(cmd);
        g_spawn_command_line_sync(cmd, &uuid, NULL, NULL, NULL);
        g_free(cmd);
        if (uuid && strlen(uuid) < 9)
        {
            g_free(uuid);
            uuid = NULL;
        }
        if (uuid)
        {
            if ((old_flags = strchr(uuid, '\n')))
                old_flags[0] = '\0';
        }

        if (uuid)
        {
            cmd = g_strdup_printf("%s -c \"cat %s | grep -e '%s' -e '%s'\"",
                                  BASHPATH,
                                  fstab_path,
                                  uuid,
                                  vol->device_file);
            print_command(cmd);
            // cmd = g_strdup_printf( "bash -c \"cat /etc/fstab | grep -e ^[#\\ ]*UUID=$(/bin/ls -l /dev/disk/by-uuid |
            // grep \\.\\./%s | sed 's/.* \\([a-fA-F0-9\-]*\\) -> \.*/\\1/')\\ */ -e '^[# ]*%s '\"", base,
            // vol->device_file );
            g_spawn_command_line_sync(cmd, &fstab, NULL, NULL, NULL);
            g_free(cmd);
        }

        if (!fstab)
        {
            cmd = g_strdup_printf("%s -c \"cat %s | grep '%s'\"", BASHPATH, fstab_path, vol->device_file);
            print_command(cmd);
            // cmd = g_strdup_printf( "bash -c \"cat /etc/fstab | grep '^[# ]*%s '\"", vol->device_file );
            g_spawn_command_line_sync(cmd, &fstab, NULL, NULL, NULL);
            g_free(cmd);
        }

        if (fstab && strlen(fstab) < 9)
        {
            g_free(fstab);
            fstab = NULL;
        }
        if (fstab)
        {
            /// if ( old_flags = strchr( fstab, '\n' ) )
            ///    old_flags[0] = '\0';
            while (strstr(fstab, "  "))
            {
                old_flags = fstab;
                fstab = replace_string(fstab, "  ", " ", FALSE);
                g_free(old_flags);
            }
        }
    }
    g_free(fstab_path);

    // g_printf("dev=%s\nuuid=%s\nfstab=%s\n", vol->device_file, uuid, fstab );
    if (uuid && fstab)
    {
        info = g_strdup_printf("echo FSTAB ; echo '%s'; echo INFO ; echo 'UUID=%s' ; ", fstab, uuid);
        g_free(uuid);
        g_free(fstab);
    }
    else if (uuid && !fstab)
    {
        info = g_strdup_printf("echo FSTAB ; echo '( not found )' ; echo ; echo INFO ; echo 'UUID=%s' ; ", uuid);
        g_free(uuid);
    }
    else if (!uuid && fstab)
    {
        info = g_strdup_printf("echo FSTAB ; echo '%s' ; echo INFO ; ", fstab);
        g_free(fstab);
    }
    else
        info = g_strdup_printf("echo FSTAB ; echo '( not found )' ; echo ; echo INFO ; ");

    flags = g_strdup_printf("echo %s ; echo '%s'       ", "DEVICE", vol->device_file);
    if (vol->is_removable)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s removable", flags);
        g_free(old_flags);
    }
    else
    {
        old_flags = flags;
        flags = g_strdup_printf("%s internal", flags);
        g_free(old_flags);
    }

    if (vol->requires_eject)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s ejectable", flags);
        g_free(old_flags);
    }

#ifdef DEPRECATED_HW
    if (vol->is_optical)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s optical", flags);
        g_free(old_flags);
    }
#endif
    if (vol->is_table)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s table", flags);
        g_free(old_flags);
    }

    if (!vol->is_user_visible)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s policy_hide", flags);
        g_free(old_flags);
    }
    if (vol->nopolicy)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s policy_noauto", flags);
        g_free(old_flags);
    }

    if (vol->is_mounted)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s mounted", flags);
        g_free(old_flags);
    }
    else if (vol->is_mountable && !vol->is_table)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s mountable", flags);
        g_free(old_flags);
    }
    else
    {
        old_flags = flags;
        flags = g_strdup_printf("%s no_media", flags);
        g_free(old_flags);
    }

#ifdef DEPRECATED_HW
    if (vol->is_blank)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s blank", flags);
        g_free(old_flags);
    }
    if (vol->is_audiocd)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s audiocd", flags);
        g_free(old_flags);
    }
    if (vol->is_dvd)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s dvd", flags);
        g_free(old_flags);
    }
#endif

    if (vol->is_mounted)
    {
        old_flags = flags;
        flags = g_strdup_printf("%s ; mount | grep \"%s \" | sed 's/\\/dev.*\\( on .*\\)/\\1/' ; echo ; ",
                                flags,
                                vol->device_file);
        g_free(old_flags);
    }
    else
    {
        old_flags = flags;
        flags = g_strdup_printf("%s ; echo ; ", flags);
        g_free(old_flags);
    }

    if (vol->is_mounted)
    {
        path = g_find_program_in_path("df");
        if (!path)
            df = g_strdup_printf("echo %s ; echo \"( please install df )\" ; echo ; ", "USAGE");
        else
        {
            esc_path = bash_quote(vol->mount_point);
            df = g_strdup_printf("echo %s ; %s -hT %s ; echo ; ", "USAGE", path, esc_path);
            g_free(path);
            g_free(esc_path);
        }
    }
    else
    {
        if (vol->is_mountable)
        {
            vfs_file_size_to_string_format(size_str, vol->size, "%.1f %s");
            df = g_strdup_printf("echo %s ; echo \"%s      %s  %s  ( not mounted )\" ; echo ; ",
                                 "USAGE",
                                 vol->device_file,
                                 vol->fs_type ? vol->fs_type : "",
                                 size_str);
        }
        else
            df = g_strdup_printf("echo %s ; echo \"%s      ( no media )\" ; echo ; ", "USAGE", vol->device_file);
    }

    char* udisks_info = vfs_volume_device_info(vol);
    udisks = g_strdup_printf("%s\ncat << EOF\n%s\nEOF\necho ; ", info, udisks_info);
    g_free(udisks_info);

    if (vol->is_mounted)
    {
        path = g_find_program_in_path("lsof");
        if (!path)
            lsof = g_strdup_printf("echo %s ; echo \"( %s lsof )\" ; echo ; ", "PROCESSES", "please install");
        else
        {
            if (!strcmp(vol->mount_point, "/"))
                lsof = g_strdup_printf("echo %s ; %s -w | grep /$ | head -n 500 ; echo ; ", "PROCESSES", path);
            else
            {
                esc_path = bash_quote(vol->mount_point);
                lsof = g_strdup_printf("echo %s ; %s -w %s | head -n 500 ; echo ; ", "PROCESSES", path, esc_path);
                g_free(esc_path);
            }
            g_free(path);
        }
    }
    else
        lsof = g_strdup("");
    /*  not desirable ?
        if ( path = g_find_program_in_path( "infobash" ) )
        {
            infobash = g_strdup_printf( "echo SYSTEM ; %s -v3 0 ; echo ; ", path );
            g_free( path );
        }
        else
    */
    infobash = g_strdup("");

    task->task->exec_command = g_strdup_printf("%s%s%s%s%s", flags, df, udisks, lsof, infobash);
    task->task->exec_sync = TRUE;
    task->task->exec_popup = TRUE;
    task->task->exec_show_output = TRUE;
    task->task->exec_export = FALSE;
    task->task->exec_scroll_lock = TRUE;
    task->task->exec_icon = g_strdup(vfs_volume_get_icon(vol));
    // task->task->exec_keep_tmp = TRUE;
    ptk_file_task_run(task);
    g_free(df);
    g_free(udisks);
    g_free(lsof);
    g_free(infobash);
}

static void on_showhide(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    char* msg;
    GtkWidget* view;
    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");

    XSet* set = xset_get("dev_show_hide_volumes");
    if (vol)
    {
        char* devid = vol->udi;
        devid = strrchr(devid, '/');
        if (devid)
            devid++;
        msg = g_strdup_printf(_("%sCurrently Selected Device: %s\nVolume Label: %s\nDevice ID: %s"),
                              set->desc,
                              vol->device_file,
                              vol->label,
                              devid);
    }
    else
        msg = g_strdup(set->desc);
    if (xset_text_dialog(view, set->title, NULL, TRUE, msg, NULL, set->s, &set->s, NULL, FALSE, set->line))
        update_all();
    g_free(msg);
}

static void on_automountlist(GtkMenuItem* item, VFSVolume* vol, GtkWidget* view2)
{
    char* msg;
    GtkWidget* view;
    if (!item)
        view = view2;
    else
        view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");

    XSet* set = xset_get("dev_automount_volumes");
    if (vol)
    {
        char* devid = vol->udi;
        devid = strrchr(devid, '/');
        if (devid)
            devid++;
        msg = g_strdup_printf(_("%sCurrently Selected Device: %s\nVolume Label: %s\nDevice ID: %s"),
                              set->desc,
                              vol->device_file,
                              vol->label,
                              devid);
    }
    else
        msg = g_strdup(set->desc);
    if (xset_text_dialog(view, set->title, NULL, TRUE, msg, NULL, set->s, &set->s, NULL, FALSE, set->line))
    {
        // update view / automount all?
    }
    g_free(msg);
}

static void on_handler_show_config(GtkMenuItem* item, GtkWidget* view, XSet* set2)
{
    XSet* set;
    int mode;

    if (!item)
        set = set2;
    else
        set = (XSet*)g_object_get_data(G_OBJECT(item), "set");

    if (!g_strcmp0(set->name, "dev_fs_cnf"))
        mode = HANDLER_MODE_FS;
    else if (!g_strcmp0(set->name, "dev_net_cnf"))
        mode = HANDLER_MODE_NET;
    else
        return;
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
    ptk_handler_show_config(mode, file_browser, NULL);
}

void open_external_tab(const char* path)
{
    char* prog = g_find_program_in_path(g_get_prgname());
    if (!prog)
        prog = g_strdup(g_get_prgname());
    if (!prog)
        prog = g_strdup("spacefm");
    char* quote_path = bash_quote(path);
    char* command = g_strdup_printf("%s -t %s", prog, quote_path);
    print_command(command);
    g_spawn_command_line_async(command, NULL);
    g_free(prog);
    g_free(quote_path);
    g_free(command);
}

gboolean volume_is_visible(VFSVolume* vol)
{
    // check show/hide
    int i, j;
    char* test;
    char* value;
    char* showhidelist = g_strdup_printf(" %s ", xset_get_s("dev_show_hide_volumes"));
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 2; j++)
        {
            if (i == 0)
                value = vol->device_file;
            else if (i == 1)
                value = vol->label;
            else
            {
                if ((value = vol->udi))
                {
                    value = strrchr(value, '/');
                    if (value)
                        value++;
                }
            }
            if (value && value[0] != '\0')
            {
                if (j == 0)
                    test = g_strdup_printf(" +%s ", value);
                else
                    test = g_strdup_printf(" -%s ", value);
                if (strstr(showhidelist, test))
                {
                    g_free(test);
                    g_free(showhidelist);
                    return (j == 0);
                }
                g_free(test);
            }
        }
    }
    g_free(showhidelist);

    // network
    if (vol->device_type == DEVICE_TYPE_NETWORK)
        return xset_get_b("dev_show_net");

    // other - eg fuseiso mounted file
    if (vol->device_type == DEVICE_TYPE_OTHER)
        return xset_get_b("dev_show_file");

    // loop
    if (g_str_has_prefix(vol->device_file, "/dev/loop"))
    {
        if (vol->is_mounted && xset_get_b("dev_show_file"))
            return TRUE;
        if (!vol->is_mountable && !vol->is_mounted)
            return FALSE;
        // fall through
    }

    // ramfs CONFIG_BLK_DEV_RAM causes multiple entries of /dev/ram*
    if (!vol->is_mounted && g_str_has_prefix(vol->device_file, "/dev/ram") && vol->device_file[8] &&
        g_ascii_isdigit(vol->device_file[8]))
        return FALSE;

    // internal?
    if (!vol->is_removable && !xset_get_b("dev_show_internal_drives"))
        return FALSE;

    // table?
    if (vol->is_table && !xset_get_b("dev_show_partition_tables"))
        return FALSE;

    // udisks hide?
    if (!vol->is_user_visible && !xset_get_b("dev_ignore_udisks_hide"))
        return FALSE;

    // has media?
    if (!vol->is_mountable && !vol->is_mounted && !xset_get_b("dev_show_empty"))
        return FALSE;

    return TRUE;
}

void ptk_location_view_on_action(GtkWidget* view, XSet* set)
{
    char* xname;
    // g_printf("ptk_location_view_on_action\n");
    if (!view)
        return;
    VFSVolume* vol = ptk_location_view_get_selected_vol(GTK_TREE_VIEW(view));

    if (!strcmp(set->name, "dev_show_internal_drives") || !strcmp(set->name, "dev_show_empty") ||
        !strcmp(set->name, "dev_show_partition_tables") || !strcmp(set->name, "dev_show_net") ||
        !strcmp(set->name, "dev_show_file") || !strcmp(set->name, "dev_ignore_udisks_hide") ||
        !strcmp(set->name, "dev_show_hide_volumes")
#ifdef DEPRECATED_HW
        || !strcmp(set->name, "dev_automount_optical")
#endif
        || !strcmp(set->name, "dev_automount_removable") || !strcmp(set->name, "dev_ignore_udisks_nopolicy"))
        update_all();
    else if (!strcmp(set->name, "dev_automount_volumes"))
        on_automountlist(NULL, vol, view);
    else if (!strcmp(set->name, "dev_root_fstab"))
        on_root_fstab(NULL, view);
    else if (!strcmp(set->name, "dev_root_udevil"))
        on_root_udevil(NULL, view);
    else if (g_str_has_prefix(set->name, "dev_icon_"))
        update_volume_icons();
    else if (!strcmp(set->name, "dev_dispname"))
        update_names();
    else if (!strcmp(set->name, "dev_menu_sync"))
        on_sync(NULL, vol, view);
    else if (!strcmp(set->name, "dev_fs_cnf"))
        on_handler_show_config(NULL, view, set);
    else if (!strcmp(set->name, "dev_net_cnf"))
        on_handler_show_config(NULL, view, set);
    else if (!strcmp(set->name, "dev_change"))
        update_change_detection();
    else if (!vol)
        return;
    else
    {
        // require vol != NULL
        if (g_str_has_prefix(set->name, "dev_menu_"))
        {
            xname = set->name + 9;
            if (!strcmp(xname, "remove"))
                on_eject(NULL, vol, view);
            else if (!strcmp(xname, "unmount"))
                on_umount(NULL, vol, view);
            else if (!strcmp(xname, "reload"))
                on_reload(NULL, vol, view);
            else if (!strcmp(xname, "open"))
                on_open(NULL, vol, view);
            else if (!strcmp(xname, "tab"))
                on_open_tab(NULL, vol, view);
            else if (!strcmp(xname, "mount"))
                on_mount(NULL, vol, view);
            else if (!strcmp(xname, "remount"))
                on_remount(NULL, vol, view);
        }
        else if (g_str_has_prefix(set->name, "dev_root_"))
        {
            xname = set->name + 9;
            if (!strcmp(xname, "mount"))
                on_mount_root(NULL, vol, view);
            else if (!strcmp(xname, "unmount"))
                on_umount_root(NULL, vol, view);
        }
        else if (!strcmp(set->name, "dev_prop"))
            on_prop(NULL, vol, view);
    }
}

static void show_devices_menu(GtkTreeView* view, VFSVolume* vol, PtkFileBrowser* file_browser, uint button,
                              guint32 time)
{
    XSet* set;
    char* str;
    GtkWidget* popup = gtk_menu_new();
    GtkAccelGroup* accel_group = gtk_accel_group_new();
    XSetContext* context = xset_context_new();
    main_context_fill(file_browser, context);

    set = xset_set_cb("dev_menu_remove", on_eject, vol);
    xset_set_ob1(set, "view", view);
    set->disable = !vol;
    set = xset_set_cb("dev_menu_unmount", on_umount, vol);
    xset_set_ob1(set, "view", view);
    set->disable = !vol; //!( vol && vol->is_mounted );
    set = xset_set_cb("dev_menu_reload", on_reload, vol);
    xset_set_ob1(set, "view", view);
    set->disable = !(vol && vol->device_type == DEVICE_TYPE_BLOCK);
    set = xset_set_cb("dev_menu_sync", on_sync, vol);
    xset_set_ob1(set, "view", view);
    set = xset_set_cb("dev_menu_open", on_open, vol);
    xset_set_ob1(set, "view", view);
    set->disable = !vol;
    set = xset_set_cb("dev_menu_tab", on_open_tab, vol);
    xset_set_ob1(set, "view", view);
    set->disable = !vol;
    set = xset_set_cb("dev_menu_mount", on_mount, vol);
    xset_set_ob1(set, "view", view);
    set->disable = !vol; // || ( vol && vol->is_mounted );
    set = xset_set_cb("dev_menu_remount", on_remount, vol);
    xset_set_ob1(set, "view", view);
    set->disable = !vol;
    set = xset_set_cb("dev_root_mount", on_mount_root, vol);
    xset_set_ob1(set, "view", view);
    set->disable = !vol;
    set = xset_set_cb("dev_root_unmount", on_umount_root, vol);
    xset_set_ob1(set, "view", view);
    set->disable = !vol;
    xset_set_cb("dev_root_fstab", on_root_fstab, view);
    xset_set_cb("dev_root_udevil", on_root_udevil, view);

    set = xset_set_cb("dev_menu_mark", on_bookmark_device, vol);
    xset_set_ob1(set, "view", view);

    xset_set_cb("dev_show_internal_drives", update_all, NULL);
    xset_set_cb("dev_show_empty", update_all, NULL);
    xset_set_cb("dev_show_partition_tables", update_all, NULL);
    xset_set_cb("dev_show_net", update_all, NULL);
    set = xset_set_cb("dev_show_file", update_all, NULL);
    //    set->disable = xset_get_b( "dev_show_internal_drives" );
    xset_set_cb("dev_ignore_udisks_hide", update_all, NULL);
    xset_set_cb("dev_show_hide_volumes", on_showhide, vol);
#ifdef DEPRECATED_HW
    set = xset_set_cb("dev_automount_optical", update_all, NULL);
    gboolean auto_optical = set->b == XSET_B_TRUE;
#endif
    set = xset_set_cb("dev_automount_removable", update_all, NULL);
    gboolean auto_removable = set->b == XSET_B_TRUE;
    xset_set_cb("dev_ignore_udisks_nopolicy", update_all, NULL);
    set = xset_set_cb("dev_automount_volumes", on_automountlist, vol);
    xset_set_ob1(set, "view", view);

    if (vol && vol->device_type == DEVICE_TYPE_NETWORK &&
        (g_str_has_prefix(vol->device_file, "//") || strstr(vol->device_file, ":/")))
        str = " dev_menu_mark";
    else
        str = "";

    char* menu_elements = g_strdup_printf("dev_menu_remove dev_menu_reload dev_menu_unmount dev_menu_sync sep_dm1 "
                                          "dev_menu_open dev_menu_tab dev_menu_mount dev_menu_remount%s",
                                          str);
    xset_add_menu(file_browser, popup, accel_group, menu_elements);
    g_free(menu_elements);

    set = xset_set_cb("dev_prop", on_prop, vol);
    xset_set_ob1(set, "view", view);
    set->disable = !vol;

    set = xset_get("dev_menu_root");
    // set->disable = !vol;

    xset_set_cb_panel(file_browser->mypanel, "font_dev", main_update_fonts, file_browser);
#ifdef DEPRECATED_HW
    xset_set_cb("dev_icon_audiocd", update_volume_icons, NULL);
    xset_set_cb("dev_icon_optical_mounted", update_volume_icons, NULL);
    xset_set_cb("dev_icon_optical_media", update_volume_icons, NULL);
    xset_set_cb("dev_icon_optical_nomedia", update_volume_icons, NULL);
#endif
    xset_set_cb("dev_icon_remove_mounted", update_volume_icons, NULL);
    xset_set_cb("dev_icon_remove_unmounted", update_volume_icons, NULL);
    xset_set_cb("dev_icon_internal_mounted", update_volume_icons, NULL);
    xset_set_cb("dev_icon_internal_unmounted", update_volume_icons, NULL);
    xset_set_cb("dev_icon_network", update_all_icons, NULL);
    xset_set_cb("dev_dispname", update_names, NULL);
    xset_set_cb("dev_change", update_change_detection, NULL);

#ifdef DEPRECATED_HW
    set = xset_get("dev_exec_fs");
    set->disable = !auto_optical && !auto_removable;
    set = xset_get("dev_exec_audio");
    set->disable = !auto_optical;
    set = xset_get("dev_exec_video");
    set->disable = !auto_optical;
#else
    set = xset_get("dev_exec_fs");
    set->disable = !auto_removable;
#endif

    set = xset_set_cb("dev_fs_cnf", on_handler_show_config, view);
    xset_set_ob1(set, "set", set);
    set = xset_set_cb("dev_net_cnf", on_handler_show_config, view);
    xset_set_ob1(set, "set", set);

    set = xset_get("dev_menu_settings");
    menu_elements = g_strdup_printf("dev_show sep_dm4 dev_menu_auto dev_exec dev_fs_cnf dev_net_cnf dev_mount_options "
                                    "dev_change sep_dm5 dev_single dev_newtab dev_icon panel%d_font_dev",
                                    file_browser->mypanel);
    xset_set_set(set, "desc", menu_elements);
    g_free(menu_elements);

    menu_elements = g_strdup_printf("sep_dm2 dev_menu_root sep_dm3 dev_prop dev_menu_settings");
    xset_add_menu(file_browser, popup, accel_group, menu_elements);
    g_free(menu_elements);

    gtk_widget_show_all(GTK_WIDGET(popup));

    g_signal_connect(popup, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);
    g_signal_connect(popup, "key-press-event", G_CALLBACK(xset_menu_keypress), NULL);

    gtk_menu_popup(GTK_MENU(popup), NULL, NULL, NULL, NULL, button, time);
}

gboolean on_button_press_event(GtkTreeView* view, GdkEventButton* evt, gpointer user_data)
{
    if (evt->type != GDK_BUTTON_PRESS)
        return FALSE;

    GtkTreePath* tree_path = NULL;
    VFSVolume* vol = NULL;
    gboolean ret = FALSE;

    // g_printf("on_button_press_event   view = %d\n", view );
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
    ptk_file_browser_focus_me(file_browser);

    if ((evt_win_click->s || evt_win_click->ob2_data) && main_window_event(file_browser->main_window,
                                                                           evt_win_click,
                                                                           "evt_win_click",
                                                                           0,
                                                                           0,
                                                                           "devices",
                                                                           0,
                                                                           evt->button,
                                                                           evt->state,
                                                                           TRUE))
        return FALSE;

    // get selected vol
    if (gtk_tree_view_get_path_at_pos(view, evt->x, evt->y, &tree_path, NULL, NULL, NULL))
    {
        GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(view);
        GtkTreeIter it;
        if (gtk_tree_model_get_iter(model, &it, tree_path))
        {
            gtk_tree_selection_select_iter(tree_sel, &it);
            gtk_tree_model_get(model, &it, COL_DATA, &vol, -1);
        }
    }

    if (evt->button == 1) /* left button */
    {
        if (vol)
        {
            if (xset_get_b("dev_single"))
            {
                gtk_tree_view_row_activated(view, tree_path, NULL);
                ret = TRUE;
            }
        }
        else
        {
            gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(view));
            ret = TRUE;
        }
    }

    else if (vol && evt->button == 2) /* middle button */
    {
        on_eject(NULL, vol, GTK_WIDGET(view));
        ret = TRUE;
    }

    else if (evt->button == 3) /* right button */
    {
        show_devices_menu(view, vol, file_browser, evt->button, evt->time);
        ret = TRUE;
    }

    if (tree_path)
        gtk_tree_path_free(tree_path);
    return ret;
}

gboolean on_key_press_event(GtkWidget* w, GdkEventKey* event, PtkFileBrowser* file_browser)
{
    int keymod = (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK |
                                  GDK_META_MASK));

    if (event->keyval == GDK_KEY_Menu || (event->keyval == GDK_KEY_F10 && keymod == GDK_SHIFT_MASK))
    {
        // simulate right-click (menu)
        show_devices_menu(GTK_TREE_VIEW(file_browser->side_dev),
                          ptk_location_view_get_selected_vol(GTK_TREE_VIEW(file_browser->side_dev)),
                          file_browser,
                          3,
                          event->time);
        return TRUE;
    }
    return FALSE;
}

void on_dev_menu_hide(GtkWidget* widget, GtkWidget* dev_menu)
{
    gtk_widget_set_sensitive(widget, TRUE);
    gtk_menu_shell_deactivate(GTK_MENU_SHELL(dev_menu));
}

static void show_dev_design_menu(GtkWidget* menu, GtkWidget* dev_item, VFSVolume* vol, uint button, guint32 time)
{
    PtkFileBrowser* file_browser;

    // validate vol
    const GList* l;
    const GList* volumes = vfs_volume_get_all_volumes();
    for (l = volumes; l; l = l->next)
    {
        if ((VFSVolume*)l->data == vol)
            break;
    }
    if (!l)
        /////// destroy menu?
        return;

    GtkWidget* view = (GtkWidget*)g_object_get_data(G_OBJECT(menu), "parent");

    if (xset_get_b("dev_newtab"))
        file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
    else
        file_browser = NULL;

    // NOTE: file_browser may be NULL
    if (button == 1)
    {
        // left-click - mount & open

        // device opener?  note that context may be based on devices list sel
        // won't work for desktop because no DesktopWindow currently available
        if (file_browser && xset_opener(file_browser, 2))
            return;

        if (file_browser)
            on_open_tab(NULL, vol, view);
        else
            on_open(NULL, vol, view);

        return;
    }
    else if (button == 2)
    {
        // middle-click - Remove / Eject
        on_eject(NULL, vol, view);
        return;
    }

    // create menu
    XSet* set;
    GtkWidget* item;
    GtkWidget* popup = gtk_menu_new();

    set = xset_get("dev_menu_remove");
#if (GTK_MAJOR_VERSION == 3)
    item = gtk_menu_item_new_with_mnemonic(set->menu_label);
#elif (GTK_MAJOR_VERSION == 2)
    item = gtk_image_menu_item_new_with_mnemonic(set->menu_label);
#endif

    g_object_set_data(G_OBJECT(item), "view", view);
    g_signal_connect(item, "activate", G_CALLBACK(on_eject), vol);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), item);

    set = xset_get("dev_menu_unmount");
#if (GTK_MAJOR_VERSION == 3)
    item = gtk_menu_item_new_with_mnemonic(set->menu_label);
#elif (GTK_MAJOR_VERSION == 2)
    item = gtk_image_menu_item_new_with_mnemonic(set->menu_label);
#endif

    g_object_set_data(G_OBJECT(item), "view", view);
    g_signal_connect(item, "activate", G_CALLBACK(on_umount), vol);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), item);
    gtk_widget_set_sensitive(item, !!vol);

    gtk_menu_shell_append(GTK_MENU_SHELL(popup), gtk_separator_menu_item_new());

    set = xset_get("dev_menu_open");
#if (GTK_MAJOR_VERSION == 3)
    item = gtk_menu_item_new_with_mnemonic(set->menu_label);
#elif (GTK_MAJOR_VERSION == 2)
    item = gtk_image_menu_item_new_with_mnemonic(set->menu_label);
#endif

    g_object_set_data(G_OBJECT(item), "view", view);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), item);
    if (file_browser)
        g_signal_connect(item, "activate", G_CALLBACK(on_open_tab), vol);
    else
        g_signal_connect(item, "activate", G_CALLBACK(on_open), vol);

    set = xset_get("dev_menu_mount");
#if (GTK_MAJOR_VERSION == 3)
    item = gtk_menu_item_new_with_mnemonic(set->menu_label);
#elif (GTK_MAJOR_VERSION == 2)
    item = gtk_image_menu_item_new_with_mnemonic(set->menu_label);
#endif

    g_object_set_data(G_OBJECT(item), "view", view);
    g_signal_connect(item, "activate", G_CALLBACK(on_mount), vol);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), item);
    gtk_widget_set_sensitive(item, !!vol);

    // Bookmark Device
    if (vol && vol->device_type == DEVICE_TYPE_NETWORK &&
        (g_str_has_prefix(vol->device_file, "//") || strstr(vol->device_file, ":/")))
    {
        set = xset_get("dev_menu_mark");
#if (GTK_MAJOR_VERSION == 3)
        item = gtk_menu_item_new_with_mnemonic(set->menu_label);
#elif (GTK_MAJOR_VERSION == 2)
        item = gtk_image_menu_item_new_with_mnemonic(set->menu_label);
#endif

        g_object_set_data(G_OBJECT(item), "view", view);
        g_signal_connect(item, "activate", G_CALLBACK(on_bookmark_device), vol);
        gtk_menu_shell_append(GTK_MENU_SHELL(popup), item);
    }

    // Separator
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), gtk_separator_menu_item_new());

    set = xset_get("dev_prop");
#if (GTK_MAJOR_VERSION == 3)
    item = gtk_menu_item_new_with_mnemonic(set->menu_label);
#elif (GTK_MAJOR_VERSION == 2)
    item = gtk_image_menu_item_new_with_mnemonic(set->menu_label);
#endif

    g_object_set_data(G_OBJECT(item), "view", view);
    g_signal_connect(item, "activate", G_CALLBACK(on_prop), vol);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), item);
    if (!vol)
        gtk_widget_set_sensitive(item, FALSE);

    // show menu
    gtk_widget_show_all(GTK_WIDGET(popup));
    gtk_menu_popup(GTK_MENU(popup), GTK_WIDGET(menu), dev_item, NULL, NULL, button, time);
    gtk_widget_set_sensitive(GTK_WIDGET(menu), FALSE);
    g_signal_connect(menu, "hide", G_CALLBACK(on_dev_menu_hide), popup);
    g_signal_connect(popup, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);

    gtk_menu_shell_set_take_focus(GTK_MENU_SHELL(popup), TRUE);
    // this is required when showing the menu via F2 or Menu key for focus
    gtk_menu_shell_select_first(GTK_MENU_SHELL(popup), TRUE);
}

gboolean on_dev_menu_keypress(GtkWidget* menu, GdkEventKey* event, gpointer user_data)
{
    GtkWidget* item = gtk_menu_shell_get_selected_item(GTK_MENU_SHELL(menu));
    if (item)
    {
        VFSVolume* vol = (VFSVolume*)g_object_get_data(G_OBJECT(item), "vol");
        if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter || event->keyval == GDK_KEY_space)
        {
            // simulate left-click (mount)
            show_dev_design_menu(menu, item, vol, 1, event->time);
            return TRUE;
        }
        else if (event->keyval == GDK_KEY_Menu || event->keyval == GDK_KEY_F2)
        {
            // simulate right-click (menu)
            show_dev_design_menu(menu, item, vol, 3, event->time);
        }
    }
    return FALSE;
}

gboolean on_dev_menu_button_press(GtkWidget* item, GdkEventButton* event, VFSVolume* vol)
{
    GtkWidget* menu = (GtkWidget*)g_object_get_data(G_OBJECT(item), "menu");
    int keymod = (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK |
                                  GDK_META_MASK));

    if (event->type == GDK_BUTTON_RELEASE)
    {
        if (event->button == 1 && keymod == 0)
        {
            // user released left button - due to an apparent gtk bug, activate
            // doesn't always fire on this event so handle it ourselves
            // see also settings.c xset_design_cb()
            // test: gtk2 Crux theme with touchpad on Edit|Copy To|Location
            // https://github.com/IgnorantGuru/spacefm/issues/31
            // https://github.com/IgnorantGuru/spacefm/issues/228
            if (menu)
                gtk_menu_shell_deactivate(GTK_MENU_SHELL(menu));

            /*
                        GtkWidget* view = (GtkWidget*)g_object_get_data( G_OBJECT(menu), "parent" );
                        PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data( G_OBJECT(view),
                                                                        "file_browser" );
            printf("xxxxxxxxxxxxxxxxxxxx %p\n", file_browser);
                        if ( file_browser && xset_opener( file_browser, 2 ) )
                        {
                            g_printf("    TRUE\n");
                            return TRUE;
                        }
            */
            gtk_menu_item_activate(GTK_MENU_ITEM(item));
            return TRUE;
        }
        return FALSE;
    }
    else if (event->type != GDK_BUTTON_PRESS)
        return FALSE;

    show_dev_design_menu(menu, item, vol, event->button, event->time);
    return TRUE;
}

int cmp_dev_name(VFSVolume* a, VFSVolume* b)
{
    return g_strcmp0(vfs_volume_get_disp_name(a), vfs_volume_get_disp_name(b));
}

void ptk_location_view_dev_menu(GtkWidget* parent, PtkFileBrowser* file_browser, GtkWidget* menu)
{ // add currently visible devices to menu with dev design mode callback
    const GList* v;
    VFSVolume* vol;
    GtkWidget* item;
    XSet* set;
    GList* names = NULL;
    GList* l;

    g_object_set_data(G_OBJECT(menu), "parent", parent);
    // file_browser may be NULL
    g_object_set_data(G_OBJECT(parent), "file_browser", file_browser);

    const GList* volumes = vfs_volume_get_all_volumes();
    for (v = volumes; v; v = v->next)
    {
        vol = (VFSVolume*)v->data;
        if (vol && volume_is_visible(vol))
            names = g_list_prepend(names, vol);
    }

    names = g_list_sort(names, (GCompareFunc)cmp_dev_name);
    for (l = names; l; l = l->next)
    {
        vol = (VFSVolume*)l->data;
#if (GTK_MAJOR_VERSION == 3)
        item = gtk_menu_item_new_with_label(vfs_volume_get_disp_name(vol));
#elif (GTK_MAJOR_VERSION == 2)
        item = gtk_image_menu_item_new_with_label(vfs_volume_get_disp_name(vol));
#endif

        g_object_set_data(G_OBJECT(item), "menu", menu);
        g_object_set_data(G_OBJECT(item), "vol", vol);
        g_signal_connect(item, "button-press-event", G_CALLBACK(on_dev_menu_button_press), vol);
        g_signal_connect(item, "button-release-event", G_CALLBACK(on_dev_menu_button_press), vol);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    g_list_free(names);
    g_signal_connect(menu, "key_press_event", G_CALLBACK(on_dev_menu_keypress), NULL);

    xset_set_cb("dev_show_internal_drives", update_all, NULL);
    xset_set_cb("dev_show_empty", update_all, NULL);
    xset_set_cb("dev_show_partition_tables", update_all, NULL);
    xset_set_cb("dev_show_net", update_all, NULL);
    set = xset_set_cb("dev_show_file", update_all, NULL);
    //    set->disable = xset_get_b( "dev_show_internal_drives" );
    xset_set_cb("dev_ignore_udisks_hide", update_all, NULL);
    xset_set_cb("dev_show_hide_volumes", on_showhide, vol);
#ifdef DEPRECATED_HW
    set = xset_set_cb("dev_automount_optical", update_all, NULL);
    gboolean auto_optical = set->b == XSET_B_TRUE;
#endif
    set = xset_set_cb("dev_automount_removable", update_all, NULL);
    // gboolean auto_removable = set->b == XSET_B_TRUE;
    xset_set_cb("dev_ignore_udisks_nopolicy", update_all, NULL);
    xset_set_cb("dev_automount_volumes", on_automountlist, vol);
    xset_set_cb("dev_change", update_change_detection, NULL);

    set = xset_set_cb("dev_fs_cnf", on_handler_show_config, parent);
    xset_set_ob1(set, "set", set);
    set = xset_set_cb("dev_net_cnf", on_handler_show_config, parent);
    xset_set_ob1(set, "set", set);

    set = xset_get("dev_menu_settings");
    char* desc =
        g_strdup_printf("dev_show sep_dm4 dev_menu_auto dev_exec dev_fs_cnf dev_net_cnf dev_mount_options dev_change%s",
                        file_browser ? " dev_newtab" : "");
    xset_set_set(set, "desc", desc);
    g_free(desc);
}

VFSVolume* ptk_location_view_get_volume(GtkTreeView* location_view, GtkTreeIter* it)
{
    VFSVolume* vol = NULL;
    gtk_tree_model_get(model, it, COL_DATA, &vol, -1);
    return vol;
}

//===============================================================================
// BOOKMARK LIST

/*
gboolean update_drag_dest_row( GtkWidget *widget, GdkDragContext *drag_context,
                               int x, int y, uint time, gpointer user_data )
{
    GtkTreeView* view = (GtkTreeView*)widget;
    GtkTreePath* tree_path;
    GtkTreeViewDropPosition pos;
    gboolean ret = TRUE;

    if( gtk_tree_view_get_dest_row_at_pos(view, x, y, &tree_path, &pos ) )
    {
        int row = gtk_tree_path_get_indices(tree_path)[0];

        if( row <= sep_idx )
        {
            gtk_tree_path_get_indices(tree_path)[0] = sep_idx;
            gtk_tree_view_set_drag_dest_row( view, tree_path, GTK_TREE_VIEW_DROP_AFTER );
        }
        else
        {
            if( pos == GTK_TREE_VIEW_DROP_BEFORE || pos == GTK_TREE_VIEW_DROP_AFTER )
                gtk_tree_view_set_drag_dest_row( view, tree_path, pos );
            else
                ret = FALSE;
        }
    }
    else
    {
        int n = gtk_tree_model_iter_n_children( model, NULL );
        tree_path = gtk_tree_path_new_from_indices( n - 1, -1 );
        gtk_tree_view_set_drag_dest_row( view, tree_path, GTK_TREE_VIEW_DROP_AFTER );
    }
    gtk_tree_path_free( tree_path );

    if( ret )
        gdk_drag_status( drag_context, GDK_ACTION_LINK, time );

    return ret;
}
*/

/*
gboolean on_drag_motion( GtkWidget *widget, GdkDragContext *drag_context,
                         int x, int y, uint time, gpointer user_data )
{
    // stop the default handler of GtkTreeView
    g_signal_stop_emission_by_name( widget, "drag-motion" );
    return update_drag_dest_row( widget, drag_context, x, y, time, user_data );
}

gboolean on_drag_drop( GtkWidget *widget, GdkDragContext *drag_context,
                       int x, int y, uint time, gpointer user_data )
{
    GdkAtom target = gdk_atom_intern( "text/uri-list", FALSE );
    update_drag_dest_row( widget, drag_context, x, y, time, user_data );
    gtk_drag_get_data( widget, drag_context, target, time );
    gtk_tree_view_set_drag_dest_row( (GtkTreeView*)widget, NULL, 0 );
    return TRUE;
}

void on_drag_data_received( GtkWidget *widget, GdkDragContext *drag_context,
                            int x, int y, GtkSelectionData *data, uint info,
                            uint time, gpointer user_data)
{
    char** uris, **uri, *file, *name;
    GtkTreeView* view;
    GtkTreePath* tree_path;
    GtkTreeViewDropPosition pos;
    int idx;

    if ((data->length >= 0) && (data->format == 8))
    {
        if( uris = gtk_selection_data_get_uris(data) )
        {
            view = (GtkTreeView*)widget;
            gtk_tree_view_get_drag_dest_row( view, &tree_path, &pos );

            if( tree_path )
            {
                idx = gtk_tree_path_get_indices(tree_path)[0];
                idx -= sep_idx;

                if( pos == GTK_TREE_VIEW_DROP_BEFORE )
                    --idx;

                for( uri = uris; *uri; ++uri, ++idx )
                {
                    file = g_filename_from_uri( *uri, NULL, NULL );
                    if( g_file_test( file, G_FILE_TEST_IS_DIR ) )
                    {
                        name = g_filename_display_basename( file );
                        ptk_bookmarks_insert( name, file, idx );
                        g_free( name );
                    }
                    g_free( file );
                }
            }
            g_strfreev( uris );
        }
    }
    gtk_drag_finish (drag_context, FALSE, FALSE, time);
}
*/

static XSet* get_selected_bookmark_set(GtkTreeView* view)
{
    GtkListStore* list = GTK_LIST_STORE(gtk_tree_view_get_model(view));
    if (!list)
        return NULL;

    GtkTreeIter it;
    GtkTreeSelection* tree_sel;
    char* name = NULL;

    tree_sel = gtk_tree_view_get_selection(view);
    if (gtk_tree_selection_get_selected(tree_sel, NULL, &it))
        gtk_tree_model_get(GTK_TREE_MODEL(list), &it, COL_PATH, &name, -1);
    XSet* set = xset_is(name);
    g_free(name);
    return set;
}

static void select_bookmark(GtkTreeView* view, XSet* set)
{
    GtkListStore* list = GTK_LIST_STORE(gtk_tree_view_get_model(view));
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(view);
    if (!set || !list)
    {
        if (tree_sel)
            gtk_tree_selection_unselect_all(tree_sel);
        return;
    }

    // Scan list for changed
    GtkTreeIter it;
    char* set_name;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list), &it))
    {
        do
        {
            gtk_tree_model_get(GTK_TREE_MODEL(list), &it, COL_PATH, &set_name, -1);
            if (set_name && !strcmp(set->name, set_name))
            {
                // found in list
                gtk_tree_selection_select_iter(tree_sel, &it);
                GtkTreePath* tree_path = gtk_tree_model_get_path(GTK_TREE_MODEL(list), &it);
                if (tree_path)
                {
                    gtk_tree_view_scroll_to_cell(view, tree_path, NULL, TRUE, .25, 0);
                    gtk_tree_path_free(tree_path);
                }
                g_free(set_name);
                return;
            }
            g_free(set_name);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(list), &it));
    }
    gtk_tree_selection_unselect_all(tree_sel);
}

static void update_bookmark_list_item(GtkListStore* list, GtkTreeIter* it, XSet* set)
{
    char* name;
    char* menu_label = NULL;
    gboolean is_sep = FALSE;

    if (set->menu_style == XSET_MENU_SEP)
        is_sep = TRUE;
    else
    {
        int cmd_type = set->x ? atoi(set->x) : -1;
        if (!set->lock && cmd_type == XSET_CMD_BOOKMARK)
        {
            // Bookmark
            if (!(set->menu_label && set->menu_label[0]))
                menu_label = g_strdup(set->z);
        }
    }

    // add label and xset name
    name = clean_label(menu_label ? menu_label : set->menu_label, FALSE, FALSE);
    gtk_list_store_set(list, it, COL_NAME, name, -1);
    gtk_list_store_set(list, it, COL_PATH, set->name, -1);
    gtk_list_store_set(list, it, COL_DATA, is_sep, -1);
    g_free(name);
    g_free(menu_label);
    gtk_list_store_set(list, it, COL_ICON, NULL, -1);
}

static void ptk_bookmark_view_reload_list(GtkTreeView* view, XSet* book_set)
{
    GtkTreeIter it;
    int pos = 0;
    XSet* set;

    if (!view)
        return;
    GtkListStore* list = GTK_LIST_STORE(gtk_tree_view_get_model(view));
    if (!list)
        return;
    gtk_list_store_clear(list);
    if (!book_set || !book_set->child)
        return;

    g_signal_handlers_block_matched(list, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, on_bookmark_row_inserted, NULL);

    // Add top item
    gtk_list_store_insert(list, &it, ++pos);
    char* name = clean_label(book_set->menu_label, FALSE, FALSE);
    // char* name = g_strdup_printf( "[ %s ]", str );
    gtk_list_store_set(list, &it, COL_NAME, name, -1);
    gtk_list_store_set(list, &it, COL_PATH, book_set->name, -1);
    g_free(name);

    // Add items
    set = xset_get(book_set->child);
    while (set)
    {
        // add new list row
        gtk_list_store_insert(list, &it, ++pos);
        update_bookmark_list_item(list, &it, set);

        // next
        if (set->next)
            set = xset_is(set->next);
        else
            break;
    }

    g_signal_handlers_unblock_matched(list, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, on_bookmark_row_inserted, NULL);
}

static void on_bookmark_device(GtkMenuItem* item, VFSVolume* vol)
{
    XSet* set;
    XSet* newset;
    XSet* sel_set;

    GtkWidget* view = (GtkWidget*)g_object_get_data(G_OBJECT(item), "view");
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(view), "file_browser");
    if (!file_browser)
        return;

    // udi is the original user-entered URL, if available, else mtab url
    const char* url = vol->udi;

    if (g_str_has_prefix(url, "curlftpfs#"))
        url += 10;

    if (file_browser->side_book)
    {
        // bookmark pane is shown - add after selected or to end of list
        sel_set = get_selected_bookmark_set(GTK_TREE_VIEW(file_browser->side_book));
        if (!sel_set && file_browser->book_set_name)
        {
            // none selected - get last set in list
            set = xset_get(file_browser->book_set_name);
            sel_set = xset_get(set->child);
            while (sel_set)
            {
                if (!sel_set->next)
                    break;
                sel_set = xset_get(sel_set->next);
            }
        }
    }
    else
    {
        // bookmark pane is not shown for current browser - add to main_book
        set = xset_get("main_book");
        sel_set = xset_get(set->child);
        while (sel_set)
        {
            if (!sel_set->next)
                break;
            sel_set = xset_get(sel_set->next);
        }
    }
    if (!sel_set || !url)
        return; // failsafe

    // create new bookmark
    newset = xset_custom_new();
    newset->menu_label = g_strdup(url);
    newset->z = g_strdup(url);
    newset->x = g_strdup_printf("%d", XSET_CMD_BOOKMARK);
    newset->prev = g_strdup(sel_set->name);
    newset->next = sel_set->next; // steal string
    newset->task = newset->task_err = newset->task_out = newset->keep_terminal = XSET_B_UNSET;
    if (sel_set->next)
    {
        XSet* sel_set_next = xset_get(sel_set->next);
        g_free(sel_set_next->prev);
        sel_set_next->prev = g_strdup(newset->name);
    }
    sel_set->next = g_strdup(newset->name);

    main_window_bookmark_changed(newset->name);
}

void ptk_bookmark_view_update_icons(GtkIconTheme* icon_theme, PtkFileBrowser* file_browser)
{
    if (!(GTK_IS_WIDGET(file_browser) && file_browser->side_book))
        return;

    GtkTreeView* view = GTK_TREE_VIEW(file_browser->side_book);
    if (!view)
        return;

    if (global_icon_bookmark)
    {
        g_object_unref(global_icon_bookmark);
        global_icon_bookmark = NULL;
    }
    if (global_icon_submenu)
    {
        g_object_unref(global_icon_submenu);
        global_icon_submenu = NULL;
    }

    XSet* book_set = xset_is(file_browser->book_set_name);
    if (book_set)
        ptk_bookmark_view_reload_list(view, book_set);
}

XSet* ptk_bookmark_view_get_first_bookmark(XSet* book_set)
{
    XSet* child_set;
    if (!book_set)
        book_set = xset_get("main_book");
    if (!book_set->child)
    {
        child_set = xset_custom_new();
        child_set->menu_label = g_strdup_printf(_("Home"));
        child_set->z = g_strdup(g_get_home_dir());
        child_set->x = g_strdup_printf("%d", XSET_CMD_BOOKMARK);
        child_set->parent = g_strdup_printf("main_book");
        book_set->child = g_strdup(child_set->name);
        child_set->task = child_set->task_err = child_set->task_out = child_set->keep_terminal = XSET_B_UNSET;
    }
    else
        child_set = xset_get(book_set->child);
    return child_set;
}

static XSet* find_cwd_match_bookmark(XSet* parent_set, const char* cwd, gboolean recurse, XSet* skip_set,
                                     XSet** found_parent_set)
{ // This function must be as FAST as possible
    *found_parent_set = NULL;

    // if !no_skip, items in this parent are considered already examined, but
    // submenus are recursed if recurse
    gboolean no_skip = skip_set != parent_set;
    if (!no_skip && !recurse)
        return NULL;

    // g_printf( "    scan %s %s %s\n", parent_set->menu_label, no_skip ? "" : "skip", recurse ? "recurse" : "" );
    XSet* set = xset_is(parent_set->child);
    while (set)
    {
        if (no_skip && set->z && set->x && !set->lock && set->x[0] == '3' /* XSET_CMD_BOOKMARK */ &&
            set->menu_style < XSET_MENU_SUBMENU && g_str_has_prefix(set->z, cwd))
        {
            // found a possible match - confirm
            char* sep = strchr(set->z, ';');
            if (sep)
                sep[0] = '\0';
            char* url = g_strstrip(g_strdup(set->z));
            if (sep)
                sep[0] = ';';
            if (!g_strcmp0(cwd, url))
            {
                // found a bookmark matching cwd
                g_free(url);
                *found_parent_set = parent_set;
                return set;
            }
            g_free(url);
        }
        else if (set->menu_style == XSET_MENU_SUBMENU && recurse && set->child)
        {
            // set is a parent - recurse contents
            XSet* found_set;
            if ((found_set = find_cwd_match_bookmark(set, cwd, TRUE, skip_set, found_parent_set)))
                return found_set;
        }
        set = xset_is(set->next);
    }
    return NULL;
}

gboolean ptk_bookmark_view_chdir(GtkTreeView* view, PtkFileBrowser* file_browser, gboolean recurse)
{
    // select bookmark of cur dir if recurse and option 'Follow Dir'
    // select bookmark of cur dir if !recurse, ignoring option 'Follow Dir'
    XSet* parent_set = NULL;

    if (!file_browser || !view || (recurse && !xset_get_b_panel(file_browser->mypanel, "book_fol")))
        return FALSE;

    const char* cwd = ptk_file_browser_get_cwd(file_browser);
    // g_printf("\n\n\nchdir %s\n", cwd);

    // cur dir is already selected?
    XSet* set = get_selected_bookmark_set(view);
    if (set && !set->lock && set->z && set->menu_style < XSET_MENU_SUBMENU && set->x &&
        atoi(set->x) == XSET_CMD_BOOKMARK && g_str_has_prefix(set->z, cwd))
    {
        char* sep = strchr(set->z, ';');
        if (sep)
            sep[0] = '\0';
        char* url = g_strstrip(g_strdup(set->z));
        if (sep)
            sep[0] = ';';
        if (!strcmp(url, cwd))
        {
            g_free(url);
            return TRUE;
        }
        g_free(url);
    }

    // look in current bookmark list
    XSet* start_set = xset_is(file_browser->book_set_name);
    set = start_set ? find_cwd_match_bookmark(start_set, cwd, FALSE, NULL, &parent_set) : NULL;
    if (!set && recurse)
    {
        // look thru all of main_book, skipping start_set
        set = find_cwd_match_bookmark(xset_get("main_book"), cwd, TRUE, start_set, &parent_set);
    }

    if (set && parent_set && (!start_set || g_strcmp0(parent_set->name, start_set->name)))
    {
        g_free(file_browser->book_set_name);
        file_browser->book_set_name = g_strdup(parent_set->name);
        ptk_bookmark_view_reload_list(view, parent_set);
    }

    select_bookmark(view, set);
    return !!set;
}

char* ptk_bookmark_view_get_selected_dir(GtkTreeView* view)
{
    XSet* set = get_selected_bookmark_set(view);
    if (set)
    {
        int cmd_type = set->x ? atoi(set->x) : -1;
        if (!set->lock && cmd_type == XSET_CMD_BOOKMARK && set->z)
        {
            char* sep = strchr(set->z, ';');
            if (sep)
                sep[0] = '\0';
            char* url = g_strstrip(g_strdup(set->z));
            if (sep)
                sep[0] = ';';
            if (!url[0])
                g_free(url);
            else
                return url;
        }
    }
    return NULL;
}

void ptk_bookmark_view_add_bookmark(GtkMenuItem* menuitem, PtkFileBrowser* file_browser, const char* url)
{ // adding from file browser - bookmarks may not be shown
    XSet* set;
    XSet* newset;
    XSet* sel_set;

    if (!file_browser)
        return;

    if (file_browser->side_book && !url)
    {
        // already bookmarked
        if (ptk_bookmark_view_chdir(GTK_TREE_VIEW(file_browser->side_book), file_browser, FALSE))
            return;
    }

    if (menuitem || !url)
        url = ptk_file_browser_get_cwd(PTK_FILE_BROWSER(file_browser));

    if (file_browser->side_book)
    {
        // bookmark pane is shown - add after selected or to end of list
        sel_set = get_selected_bookmark_set(GTK_TREE_VIEW(file_browser->side_book));
        if (sel_set && file_browser->book_set_name && !g_strcmp0(sel_set->name, file_browser->book_set_name))
            // topmost "Bookmarks" selected - add to end
            sel_set = NULL;
        else if (sel_set && !g_strcmp0(sel_set->name, "main_book"))
        {
            // topmost "Bookmarks" selected - failsafe
            g_debug("topmost Bookmarks selected - failsafe !file_browser->book_set_name");
            sel_set = NULL;
        }
        if (!sel_set && file_browser->book_set_name)
        {
            // none selected - get last set in list
            set = xset_get(file_browser->book_set_name);
            sel_set = xset_get(set->child);
            while (sel_set)
            {
                if (!sel_set->next)
                    break;
                sel_set = xset_get(sel_set->next);
            }
        }
    }
    else
    {
        // bookmark pane is not shown for current browser - add to main_book
        set = xset_get("main_book");
        sel_set = xset_get(set->child);
        while (sel_set)
        {
            if (!sel_set->next)
                break;
            sel_set = xset_get(sel_set->next);
        }
    }
    if (!sel_set)
    {
        g_debug("ptk_bookmark_view_add_bookmark failsafe !sel_set");
        return; // failsafe
    }

    // create new bookmark
    newset = xset_custom_new();
    newset->menu_label = g_path_get_basename(url);
    newset->z = g_strdup(url);
    newset->x = g_strdup_printf("%d", XSET_CMD_BOOKMARK);
    newset->prev = g_strdup(sel_set->name);
    newset->next = sel_set->next; // steal string
    newset->task = newset->task_err = newset->task_out = newset->keep_terminal = XSET_B_UNSET;
    if (sel_set->next)
    {
        XSet* sel_set_next = xset_get(sel_set->next);
        g_free(sel_set_next->prev);
        sel_set_next->prev = g_strdup(newset->name);
    }
    sel_set->next = g_strdup(newset->name);

    main_window_bookmark_changed(newset->name);
    if (file_browser->side_book)
        select_bookmark(GTK_TREE_VIEW(file_browser->side_book), newset);
}

void ptk_bookmark_view_xset_changed(GtkTreeView* view, PtkFileBrowser* file_browser, const char* changed_name)
{ // a custom xset has changed - need to update view?
    XSet* changed_set;

    // g_printf( "ptk_bookmark_view_xset_changed\n");
    GtkListStore* list = GTK_LIST_STORE(gtk_tree_view_get_model(view));
    if (!(list && file_browser && file_browser->book_set_name && changed_name))
        return;

    changed_set = xset_is(changed_name);
    if (!strcmp(file_browser->book_set_name, changed_name))
    {
        // The loaded book set itself has changed - reload list
        if (changed_set)
            ptk_bookmark_view_reload_list(view, changed_set);
        else
        {
            // The loaded book set has been deleted
            g_free(file_browser->book_set_name);
            file_browser->book_set_name = g_strdup("main_book");
            ptk_bookmark_view_reload_list(view, xset_get("main_book"));
        }
        return;
    }

    // is changed_set currently a child of book set ?
    gboolean is_child = FALSE;
    if (changed_set)
    {
        XSet* set = changed_set;
        while (set->prev)
            set = xset_get(set->prev);
        if (set->parent && !strcmp(file_browser->book_set_name, set->parent))
            is_child = TRUE;
    }
    // g_printf( "    %s  %s  %s\n", changed_name, changed_set ? changed_set->menu_label : "missing", is_child ?
    // "is_child" : "NOT" );

    // Scan list for changed
    GtkTreeIter it;
    char* set_name;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list), &it))
    {
        do
        {
            gtk_tree_model_get(GTK_TREE_MODEL(list), &it, COL_PATH, &set_name, -1);
            if (set_name && !strcmp(changed_name, set_name))
            {
                // found in list
                if (changed_set)
                {
                    if (is_child)
                        update_bookmark_list_item(list, &it, changed_set);
                    else
                    {
                        // found in list but no longer a child - remove from list
                        gtk_list_store_remove(list, &it);
                    }
                }
                else
                    // found in list but xset removed - remove from list
                    gtk_list_store_remove(list, &it);
                g_free(set_name);
                return;
            }
            g_free(set_name);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(list), &it));
    }

    if (is_child)
    {
        // is a child but was not found in list - reload list
        ptk_bookmark_view_reload_list(view, xset_get(file_browser->book_set_name));
    }
}

static void activate_bookmark_item(XSet* sel_set, GtkTreeView* view, PtkFileBrowser* file_browser, gboolean reverse)
{
    XSet* set;

    if (!sel_set || !view || !file_browser)
        return;

    if (file_browser->book_set_name && !strcmp(file_browser->book_set_name, sel_set->name))
    {
        // top item - go up
        if (!strcmp(sel_set->name, "main_book"))
            return; // already is top
        set = sel_set;
        while (set->prev)
            set = xset_get(set->prev);
        if ((set = xset_is(set->parent)))
        {
            g_free(file_browser->book_set_name);
            file_browser->book_set_name = g_strdup(set->name);
            ptk_bookmark_view_reload_list(view, set);
            if (xset_get_b_panel(file_browser->mypanel, "book_fol"))
                ptk_bookmark_view_chdir(view, file_browser, FALSE);
        }
    }
    else if (sel_set->menu_style == XSET_MENU_SUBMENU)
    {
        // enter submenu
        g_free(file_browser->book_set_name);
        file_browser->book_set_name = g_strdup(sel_set->name);
        ptk_bookmark_view_reload_list(view, sel_set);
    }
    else
    {
        // activate bookmark
        sel_set->browser = file_browser;
        if (reverse)
        {
            // temporarily reverse the New Tab setting
            set = xset_get("book_newtab");
            set->b = set->b == XSET_B_TRUE ? XSET_B_UNSET : XSET_B_TRUE;
        }
        xset_menu_cb(NULL, sel_set); // activate
        if (reverse)
        {
            // restore the New Tab setting
            set->b = set->b == XSET_B_TRUE ? XSET_B_UNSET : XSET_B_TRUE;
        }
        if (sel_set->menu_style == XSET_MENU_CHECK)
            main_window_bookmark_changed(sel_set->name);
    }
}

void ptk_bookmark_view_on_open_reverse(GtkMenuItem* item, PtkFileBrowser* file_browser)
{
    if (!(file_browser && file_browser->side_book))
        return;
    XSet* sel_set = get_selected_bookmark_set(GTK_TREE_VIEW(file_browser->side_book));
    activate_bookmark_item(sel_set, GTK_TREE_VIEW(file_browser->side_book), file_browser, TRUE);
}

static void on_bookmark_model_destroy(gpointer data, GObject* object)
{
    g_signal_handlers_disconnect_matched(gtk_icon_theme_get_default(),
                                         G_SIGNAL_MATCH_DATA,
                                         0,
                                         0,
                                         NULL,
                                         NULL,
                                         data /* file_browser */);
}

static void on_bookmark_row_inserted(GtkTreeModel* list, GtkTreePath* tree_path, GtkTreeIter* iter,
                                     PtkFileBrowser* file_browser)
{
    if (!file_browser)
        return;

    // For auto DND handler- bookmark moved
    // The list row is not yet filled with data so just store the
    // iter for use in on_bookmark_drag_end
    file_browser->book_iter_inserted = *iter;
    return;
}

static void on_bookmark_drag_begin(GtkWidget* widget, GdkDragContext* drag_context, PtkFileBrowser* file_browser)
{
    if (!file_browser)
        return;

    // don't activate row if drag was begun
    file_browser->bookmark_button_press = FALSE;

    // reset tracking inserted/deleted row (for auto DND handler moved a bookmark)
    file_browser->book_iter_inserted.stamp = 0;
    file_browser->book_iter_inserted.user_data = NULL;
    file_browser->book_iter_inserted.user_data2 = NULL;
    file_browser->book_iter_inserted.user_data3 = NULL;
}

static void on_bookmark_drag_end(GtkWidget* widget, GdkDragContext* drag_context, PtkFileBrowser* file_browser)
{
    GtkTreeIter it;
    GtkTreeIter it_prev;
    char* set_name;
    char* prev_name;
    char* inserted_name = NULL;

    GtkListStore* list = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));

    if (!(list && file_browser && file_browser->book_set_name && file_browser->book_iter_inserted.stamp))
        return;

    /* Because stamp != 0, auto DND handler has moved a bookmark, so update
     * bookmarks.

     * GTK docs says to use row-deleted event for this, but placing this code
     * there caused crashes due to the auto handler not being done with the
     * list store? */

    // get inserted xset name
    gtk_tree_model_get(GTK_TREE_MODEL(list), &file_browser->book_iter_inserted, COL_PATH, &inserted_name, -1);
    file_browser->book_iter_inserted.stamp = 0;
    file_browser->book_iter_inserted.user_data = NULL;
    file_browser->book_iter_inserted.user_data2 = NULL;
    file_browser->book_iter_inserted.user_data3 = NULL;

    GtkTreeView* view = GTK_TREE_VIEW(file_browser->side_book);
    if (!view || !inserted_name)
        return;

    // Did user drag first item?
    if (!strcmp(file_browser->book_set_name, inserted_name))
    {
        ptk_bookmark_view_reload_list(view, xset_get(file_browser->book_set_name));
        g_free(inserted_name);
        return;
    }

    // Get previous iter
    int pos = 0;
    gboolean found = FALSE;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list), &it))
    {
        do
        {
            gtk_tree_model_get(GTK_TREE_MODEL(list), &it, COL_PATH, &set_name, -1);
            if (set_name && !strcmp(inserted_name, set_name))
            {
                // found in list
                g_free(set_name);
                found = TRUE;
                break;
            }
            g_free(set_name);
            it_prev = it;
            pos++;
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(list), &it));
    }

    // get previous iter xset name
    if (!found || pos == 0)
    {
        // pos == 0 user dropped item in first location - reload list
        ptk_bookmark_view_reload_list(view, xset_get(file_browser->book_set_name));
        return;
    }
    else if (pos == 1)
        prev_name = NULL;
    else
    {
        gtk_tree_model_get(GTK_TREE_MODEL(list), &it_prev, COL_PATH, &prev_name, -1);
        if (!prev_name)
        {
            ptk_bookmark_view_reload_list(view, xset_get(file_browser->book_set_name));
            return;
        }
    }

    // Move xset - this is like a cut paste except may be inserted in top of menu
    XSet* set_next;
    XSet* set_clipboard1 = xset_get(inserted_name);
    XSet* set = xset_is(prev_name); // pasted here, will be NULL if top
    if (set_clipboard1->lock || (set && set->lock))
        return; // failsafe
    xset_custom_remove(set_clipboard1);
    if (set)
    {
        // is NOT at top
        g_free(set_clipboard1->prev);
        g_free(set_clipboard1->next);
        set_clipboard1->prev = g_strdup(set->name);
        set_clipboard1->next = set->next; // swap string
        if (set->next)
        {
            set_next = xset_get(set->next);
            if (set_next->prev)
                g_free(set_next->prev);
            set_next->prev = g_strdup(set_clipboard1->name);
        }
        set->next = g_strdup(set_clipboard1->name);
    }
    else
    {
        // has been moved to top - no previous, need to change parent/child
        XSet* book_set = xset_get(file_browser->book_set_name);
        g_free(set_clipboard1->prev);
        g_free(set_clipboard1->next);
        g_free(set_clipboard1->parent);
        set_clipboard1->parent = g_strdup(book_set->name);
        set_clipboard1->prev = NULL;
        set_clipboard1->next = book_set->child; // swap string
        book_set->child = g_strdup(set_clipboard1->name);

        if (set_clipboard1->next)
        {
            set_next = xset_get(set_clipboard1->next);
            g_free(set_next->parent);
            set_next->parent = NULL;
            g_free(set_next->prev);
            set_next->prev = g_strdup(set_clipboard1->name);
        }
    }
    main_window_bookmark_changed(file_browser->book_set_name);
    select_bookmark(view, set_clipboard1);
    g_free(prev_name);
    g_free(inserted_name);
}

static void on_bookmark_row_activated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* column,
                                      PtkFileBrowser* file_browser)
{
    activate_bookmark_item(get_selected_bookmark_set(view), view, file_browser, FALSE);
}

static void show_bookmarks_menu(GtkTreeView* view, PtkFileBrowser* file_browser, uint button, guint32 time)
{
    GtkWidget* popup;
    XSet* set;
    XSet* insert_set = NULL;
    gboolean bookmark_selected = TRUE;

    set = get_selected_bookmark_set(view);
    if (!set)
    {
        // No bookmark selected so use menu set
        if (!(set = xset_is(file_browser->book_set_name)))
            set = xset_get("main_book");
        insert_set = xset_is(set->child);
        bookmark_selected = FALSE;
    }
    else if (file_browser->book_set_name && !strcmp(set->name, file_browser->book_set_name))
        // user right-click on top item
        insert_set = xset_is(set->child);
    // for inserts, get last child
    while (insert_set && insert_set->next)
        insert_set = xset_is(insert_set->next);

    set->browser = file_browser;
    if (insert_set)
        insert_set->browser = file_browser;

    // build menu
    XSetContext* context = xset_context_new();
    main_context_fill(file_browser, context);

    xset_set_cb_panel(file_browser->mypanel, "font_book", main_update_fonts, file_browser);
    xset_set_cb("book_icon", main_window_update_all_bookmark_views, NULL);
    xset_set_cb("book_menu_icon", main_window_update_all_bookmark_views, NULL);
    popup = xset_design_show_menu(NULL, set, insert_set ? insert_set : set, button, time);

    // Add Settings submenu
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), gtk_separator_menu_item_new());
    set = xset_get("book_settings");
    if (set->desc)
        g_free(set->desc);
    set->desc = g_strdup_printf("book_single book_newtab panel%d_book_fol book_icon book_menu_icon panel%d_font_book",
                                file_browser->mypanel,
                                file_browser->mypanel);
    GtkAccelGroup* accel_group = gtk_accel_group_new();
    xset_add_menuitem(file_browser, popup, accel_group, set);
    gtk_menu_shell_prepend(GTK_MENU_SHELL(popup), gtk_separator_menu_item_new());
    set = xset_set_cb("book_open", ptk_bookmark_view_on_open_reverse, file_browser);
    set->disable = !bookmark_selected;
    GtkWidget* item = xset_add_menuitem(file_browser, popup, accel_group, set);
    gtk_menu_reorder_child(GTK_MENU(popup), item, 0);
    gtk_widget_show_all(popup);

    gtk_menu_shell_set_take_focus(GTK_MENU_SHELL(popup), TRUE);
    // this is required when showing the menu via F2 or Menu key for focus
    gtk_menu_shell_select_first(GTK_MENU_SHELL(popup), TRUE);
}

static gboolean on_bookmark_button_press_event(GtkTreeView* view, GdkEventButton* evt, PtkFileBrowser* file_browser)
{
    if (evt->type != GDK_BUTTON_PRESS)
        return FALSE;

    ptk_file_browser_focus_me(file_browser);

    if ((evt_win_click->s || evt_win_click->ob2_data) && main_window_event(file_browser->main_window,
                                                                           evt_win_click,
                                                                           "evt_win_click",
                                                                           0,
                                                                           0,
                                                                           "bookmarks",
                                                                           0,
                                                                           evt->button,
                                                                           evt->state,
                                                                           TRUE))
        return FALSE;

    if (evt->button == 1) // left
    {
        file_browser->bookmark_button_press = TRUE;
        return FALSE;
    }

    GtkTreeSelection* tree_sel;
    GtkTreePath* tree_path;

    tree_sel = gtk_tree_view_get_selection(view);
    gtk_tree_view_get_path_at_pos(view, evt->x, evt->y, &tree_path, NULL, NULL, NULL);
    if (tree_path)
    {
        if (!gtk_tree_selection_path_is_selected(tree_sel, tree_path))
            gtk_tree_selection_select_path(tree_sel, tree_path);
        gtk_tree_path_free(tree_path);
    }
    else
        gtk_tree_selection_unselect_all(tree_sel);

    if (evt->button == 2) // middle
    {
        activate_bookmark_item(get_selected_bookmark_set(view), view, file_browser, TRUE);
        return TRUE;
    }
    else if (evt->button == 3) // right
    {
        show_bookmarks_menu(view, file_browser, evt->button, evt->time);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_bookmark_button_release_event(GtkTreeView* view, GdkEventButton* evt, PtkFileBrowser* file_browser)
{
    GtkTreeIter it;

    // don't activate row if drag was begun
    if (evt->type != GDK_BUTTON_RELEASE || !file_browser->bookmark_button_press)
        return FALSE;
    file_browser->bookmark_button_press = FALSE;

    if (evt->button == 1) // left
    {
        GtkTreePath* tree_path;
        gtk_tree_view_get_path_at_pos(view, evt->x, evt->y, &tree_path, NULL, NULL, NULL);
        if (!tree_path)
        {
            gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(view));
            return TRUE;
        }

        if (!xset_get_b("book_single"))
            return FALSE;

        GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(view);
        gtk_tree_model_get_iter(gtk_tree_view_get_model(view), &it, tree_path);
        gtk_tree_selection_select_iter(tree_sel, &it);

        gtk_tree_view_row_activated(view, tree_path, NULL);

        gtk_tree_path_free(tree_path);
    }

    return FALSE;
}

gboolean on_bookmark_key_press_event(GtkWidget* w, GdkEventKey* event, PtkFileBrowser* file_browser)
{
    int keymod = (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK |
                                  GDK_META_MASK));

    if (event->keyval == GDK_KEY_Menu || (event->keyval == GDK_KEY_F10 && keymod == GDK_SHIFT_MASK))
    {
        // simulate right-click (menu)
        show_bookmarks_menu(GTK_TREE_VIEW(file_browser->side_book), file_browser, 3, event->time);
        return TRUE;
    }
    return FALSE;
}

static gboolean is_row_separator(GtkTreeModel* model, GtkTreeIter* it, PtkFileBrowser* file_browser)
{
    gboolean is_sep = FALSE;
    gtk_tree_model_get(model, it, COL_DATA, &is_sep, -1);
    return is_sep;
}

GtkWidget* ptk_bookmark_view_new(PtkFileBrowser* file_browser)
{
    GtkWidget* view;
    GtkTreeViewColumn* col;
    GtkCellRenderer* renderer;
    GtkListStore* list;
    GtkIconTheme* icon_theme;

    list = gtk_list_store_new(N_COLS, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    g_object_weak_ref(G_OBJECT(list), on_bookmark_model_destroy, file_browser);

    view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list));

    /* gtk_tree_view_new_with_model adds a ref so we don't need original ref
     * Otherwise on_bookmark_model_destroy was not running - list model
     * wasn't being freed? */
    g_object_unref(list);

    icon_theme = gtk_icon_theme_get_default();
    if (icon_theme)
        g_signal_connect(icon_theme, "changed", G_CALLBACK(ptk_bookmark_view_update_icons), file_browser);

    // no dnd if using auto-reorderable unless you code reorder dnd manually
    //    gtk_tree_view_enable_model_drag_dest (
    //        GTK_TREE_VIEW( view ),
    //        drag_targets, G_N_ELEMENTS( drag_targets ), GDK_ACTION_LINK );
    //    g_signal_connect( view, "drag-motion", G_CALLBACK( on_bookmark_drag_motion ), NULL );
    //    g_signal_connect( view, "drag-drop", G_CALLBACK( on_bookmark_drag_drop ), NULL );
    //    g_signal_connect( view, "drag-data-received", G_CALLBACK( on_bookmark_drag_data_received ), NULL );

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_sort_indicator(col, FALSE);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, renderer, FALSE);
    gtk_tree_view_column_set_attributes(col, renderer, "pixbuf", COL_ICON, NULL);

    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
    col = gtk_tree_view_column_new();

    renderer = gtk_cell_renderer_text_new();
    // g_signal_connect( renderer, "edited", G_CALLBACK(on_bookmark_edited), view );
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_set_attributes(col, renderer, "text", COL_NAME, NULL);

    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

    gtk_tree_view_set_row_separator_func(GTK_TREE_VIEW(view),
                                         (GtkTreeViewRowSeparatorFunc)is_row_separator,
                                         file_browser,
                                         NULL);
    gtk_tree_view_set_reorderable(GTK_TREE_VIEW(view), TRUE);

    g_object_set_data(G_OBJECT(view), "file_browser", file_browser);

    // g_signal_connect( view, "row-activated", G_CALLBACK( on_bookmark_row_activated ), NULL );
    // g_signal_connect( GTK_TREE_MODEL( list ), "row-deleted",
    //                                    G_CALLBACK( on_bookmark_row_deleted ),
    //                                    file_browser );
    g_signal_connect(GTK_TREE_MODEL(list), "row-inserted", G_CALLBACK(on_bookmark_row_inserted), file_browser);

    // handle single-clicks in addition to auto-reorderable dnd
    g_signal_connect(view, "drag-begin", G_CALLBACK(on_bookmark_drag_begin), file_browser);
    g_signal_connect(view, "drag-end", G_CALLBACK(on_bookmark_drag_end), file_browser);
    g_signal_connect(view, "button-press-event", G_CALLBACK(on_bookmark_button_press_event), file_browser);
    g_signal_connect(view, "button-release-event", G_CALLBACK(on_bookmark_button_release_event), file_browser);
    g_signal_connect(view, "key-press-event", G_CALLBACK(on_bookmark_key_press_event), file_browser);
    g_signal_connect(view, "row-activated", G_CALLBACK(on_bookmark_row_activated), file_browser);

    file_browser->bookmark_button_press = FALSE;
    file_browser->book_iter_inserted.stamp = 0;

    // set font
    if (xset_get_s_panel(file_browser->mypanel, "font_book"))
    {
        PangoFontDescription* font_desc =
            pango_font_description_from_string(xset_get_s_panel(file_browser->mypanel, "font_book"));
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_override_font(view, font_desc);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_widget_modify_font(view, font_desc);
#endif
        pango_font_description_free(font_desc);
    }

    // fill list
    if (!file_browser->book_set_name)
        file_browser->book_set_name = g_strdup("main_book");
    XSet* set = xset_is(file_browser->book_set_name);
    if (!set)
    {
        set = xset_get("main_book");
        g_free(file_browser->book_set_name);
        file_browser->book_set_name = g_strdup("main_book");
    }
    ptk_bookmark_view_reload_list(GTK_TREE_VIEW(view), set);

    return view;
}

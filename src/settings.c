/*
 * SpaceFM settings.c
 *
 * Copyright (C) 2015 IgnorantGuru <ignorantguru@gmx.com>
 * Copyright (C) 2006 Hong Jen Yee (PCMan) <pcman.tw (AT) gmail.com>
 *
 * License: See COPYING file
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include "gtk2-compat.h"

#include <gdk/gdkkeysyms.h>
#include <errno.h>
#include <fcntl.h>

#include <glib/gprintf.h>
#include <gmodule.h>

#include "settings.h"
#include "utils.h"
#include "main-window.h"
#include "item-prop.h"

#include "vfs/vfs-app-desktop.h"
#include "vfs/vfs-utils.h" /* for vfs_load_icon */

#include "ptk/ptk-utils.h"
#include "ptk/ptk-app-chooser.h"
#include "ptk/ptk-handler.h"
#include "ptk/ptk-file-menu.h"
#include "ptk/ptk-location-view.h"

#include "exo/exo-icon-chooser-dialog.h" /* for exo_icon_chooser_dialog_new */

#include "extern.h"
#include "utils.h"

#define CONFIG_VERSION "38" // 1.0.6

AppSettings app_settings = {0};
const gboolean show_hidden_files_default = FALSE;
const gboolean show_thumbnail_default = FALSE;
const int max_thumb_size_default = 8 << 20;
const int big_icon_size_default = 48;
const int max_icon_size = 512;
const int small_icon_size_default = 22;
const int tool_icon_size_default = 0;
const gboolean single_click_default = FALSE;
const gboolean no_single_hover_default = FALSE;

const int view_mode_default = PTK_FB_ICON_VIEW;
const int sort_order_default = PTK_FB_SORT_BY_NAME;
const int sort_type_default = GTK_SORT_ASCENDING;

/* Default values of interface settings */
const gboolean always_show_tabs_default = TRUE;
const gboolean hide_close_tab_buttons_default = FALSE;

// MOD settings
void xset_write(GString* buf);
void xset_parse(char* line);
void xset_defaults();
const gboolean use_si_prefix_default = FALSE;
GList* xsets = NULL;
GList* keysets = NULL;
XSet* set_clipboard = NULL;
gboolean clipboard_is_cut;
XSet* set_last;
char* settings_config_dir = NULL;
const char* settings_tmp_dir = NULL;
char* settings_user_tmp_dir = NULL;
XSetContext* xset_context = NULL;
XSet* book_icon_set_cached = NULL;

XSet* evt_win_focus = NULL;
XSet* evt_win_move = NULL;
XSet* evt_win_click = NULL;
XSet* evt_win_key = NULL;
XSet* evt_win_close = NULL;
XSet* evt_pnl_show = NULL;
XSet* evt_pnl_focus = NULL;
XSet* evt_pnl_sel = NULL;
XSet* evt_tab_new = NULL;
XSet* evt_tab_chdir = NULL;
XSet* evt_tab_focus = NULL;
XSet* evt_tab_close = NULL;
XSet* evt_device = NULL;
GList* xset_cmd_history = NULL;

// delayed session saving
uint xset_autosave_timer = 0;
gboolean xset_autosave_request = FALSE;

typedef void (*SettingsParseFunc)(char* line);

void xset_free_all();
void xset_custom_delete(XSet* set, gboolean delete_next);
void xset_default_keys();
char* xset_color_dialog(GtkWidget* parent, char* title, char* defcolor);
GtkWidget* xset_design_additem(GtkWidget* menu, const char* label, const char* stock_icon, int job, XSet* set);
gboolean xset_design_cb(GtkWidget* item, GdkEventButton* event, XSet* set);
gboolean on_autosave_timer(gpointer main_window);
const char* icon_stock_to_id(const char* name);
void xset_builtin_tool_activate(char tool_type, XSet* set, GdkEventButton* event);
XSet* xset_new_builtin_toolitem(char tool_type);
void xset_custom_insert_after(XSet* target, XSet* set);
XSet* xset_custom_copy(XSet* set, gboolean copy_next, gboolean delete_set);
void xset_free(XSet* set);

const char* user_manual_url = "http://ignorantguru.github.io/spacefm/spacefm-manual-en.html";

const char* enter_command_line =
    N_("Enter program or bash command line:\n\nUse:\n\t%%F\tselected files  or  %%f first selected "
       "file\n\t%%N\tselected filenames  or  %%n first selected filename\n\t%%d\tcurrent directory\n\t%%v\tselected "
       "device (eg /dev/sda1)\n\t%%m\tdevice mount point (eg /media/dvd);  %%l device label\n\t%%b\tselected "
       "bookmark\n\t%%t\tselected task directory;  %%p task pid\n\t%%a\tmenu item value\n\t$fm_panel, $fm_tab, etc");

const char* icon_desc = N_("Enter an icon name, icon file path, or stock item name:\n\nOr click Choose to select an "
                           "icon.  Not all icons may work properly due to various issues.");

const char* enter_menu_name = N_("Enter item name:\n\nPrecede a character with an underscore (_) to underline that "
                                 "character as a shortcut key if desired.");
const char* enter_menu_name_new =
    N_("Enter new item name:\n\nPrecede a character with an underscore (_) to underline that character as a shortcut "
       "key if desired.\n\nTIP: To change this item later, right-click on the item to open the Design Menu.");

static const char* builtin_tool_name[] = { // must match XSET_TOOL_ enum
    NULL,
    NULL,
    N_("Show Devices"),
    N_("Show Bookmarks"),
    N_("Show Tree"),
    N_("Home"),
    N_("Default"),
    N_("Up"),
    N_("Back"),
    N_("Back History"),
    N_("Forward"),
    N_("Forward History"),
    N_("Refresh"),
    N_("New Tab"),
    N_("New Tab Here"),
    N_("Show Hidden"),
    N_("Show Thumbnails"),
    N_("Large Icons")};

static const char* builtin_tool_icon[] = { // must match XSET_TOOL_ enum
    NULL,
    NULL,
    "gtk-harddisk",
    "gtk-jump-to",
    "gtk-directory",
    "gtk-home",
    "gtk-home",
    "gtk-go-up",
    "gtk-go-back",
    "gtk-go-back",
    "gtk-go-forward",
    "gtk-go-forward",
    "gtk-refresh",
    "gtk-add",
    "gtk-add",
    "gtk-apply",
    GTK_STOCK_SELECT_COLOR,
    GTK_STOCK_ZOOM_IN};

static const char* builtin_tool_shared_key[] = { // must match XSET_TOOL_ enum
    NULL,
    NULL,
    "panel1_show_devmon",
    "panel1_show_book",
    "panel1_show_dirtree",
    "go_home",
    "go_default",
    "go_up",
    "go_back",
    "go_back",
    "go_forward",
    "go_forward",
    "view_refresh",
    "tab_new",
    "tab_new_here",
    "panel1_show_hidden",
    "view_thumb",
    "panel1_list_large"};

static void parse_general_settings(char* line)
{
    char* sep = strstr(line, "=");
    char* name;
    char* value;
    if (!sep)
        return;
    name = line;
    value = sep + 1;
    *sep = '\0';
    if (0 == strcmp(name, "show_thumbnail"))
        app_settings.show_thumbnail = atoi(value);
    else if (0 == strcmp(name, "max_thumb_size"))
        app_settings.max_thumb_size = atoi(value) << 10;
    else if (0 == strcmp(name, "big_icon_size"))
    {
        app_settings.big_icon_size = atoi(value);
        if (app_settings.big_icon_size <= 0 || app_settings.big_icon_size > max_icon_size)
            app_settings.big_icon_size = big_icon_size_default;
    }
    else if (0 == strcmp(name, "small_icon_size"))
    {
        app_settings.small_icon_size = atoi(value);
        if (app_settings.small_icon_size <= 0 || app_settings.small_icon_size > max_icon_size)
            app_settings.small_icon_size = small_icon_size_default;
    }
    else if (0 == strcmp(name, "tool_icon_size"))
    {
        app_settings.tool_icon_size = atoi(value);
        if (app_settings.tool_icon_size < 0 || app_settings.tool_icon_size > GTK_ICON_SIZE_DIALOG)
            app_settings.tool_icon_size = tool_icon_size_default;
    }
    else if (0 == strcmp(name, "single_click"))
        app_settings.single_click = atoi(value);
    else if (0 == strcmp(name, "no_single_hover"))
        app_settings.no_single_hover = atoi(value);
    else if (0 == strcmp(name, "sort_order"))
        app_settings.sort_order = atoi(value);
    else if (0 == strcmp(name, "sort_type"))
        app_settings.sort_type = atoi(value);
    else if (0 == strcmp(name, "use_si_prefix"))
        app_settings.use_si_prefix = atoi(value);
    else if (0 == strcmp(name, "no_execute"))
        app_settings.no_execute = atoi(value); // MOD
    else if (0 == strcmp(name, "no_confirm"))
        app_settings.no_confirm = atoi(value); // MOD
}

static void parse_window_state(char* line)
{
    char* sep = strstr(line, "=");
    char* name;
    char* value;
    int v;
    if (!sep)
        return;
    name = line;
    value = sep + 1;
    *sep = '\0';
    if (0 == strcmp(name, "width"))
    {
        v = atoi(value);
        app_settings.width = (v > 0 ? v : 640);
    }
    if (0 == strcmp(name, "height"))
    {
        v = atoi(value);
        app_settings.height = (v > 0 ? v : 480);
    }
    if (0 == strcmp(name, "maximized"))
    {
        app_settings.maximized = atoi(value);
    }
}

static void parse_interface_settings(char* line)
{
    char* sep = strstr(line, "=");
    char* name;
    char* value;
    if (!sep)
        return;
    name = line;
    value = sep + 1;
    *sep = '\0';
    if (0 == strcmp(name, "always_show_tabs"))
        app_settings.always_show_tabs = atoi(value);
    else if (0 == strcmp(name, "show_close_tab_buttons"))
        app_settings.hide_close_tab_buttons = !atoi(value);
}

void swap_menu_label(const char* set_name, const char* old_name, const char* new_name)
{ // changes default menu label for older config files
    XSet* set;

    if ((set = xset_is(set_name)))
    {
        if (set->menu_label && !strcmp(set->menu_label, old_name))
        {
            // menu label has not been changed by user - change default
            g_free(set->menu_label);
            set->menu_label = g_strdup(new_name);
            set->in_terminal = XSET_B_UNSET;
        }
    }
}

void load_settings(char* config_dir)
{
    FILE* file;
    char* path = NULL;
    gboolean git_backed_settings = TRUE;

    xset_cmd_history = NULL;
    app_settings.load_saved_tabs = TRUE;

    settings_tmp_dir = g_get_user_cache_dir();

    if (G_UNLIKELY(!(g_file_test("/usr/bin/git", G_FILE_TEST_IS_EXECUTABLE))))
        git_backed_settings = FALSE;

    if (config_dir)
        settings_config_dir = config_dir;
    else
        settings_config_dir = g_build_filename(g_get_user_config_dir(), "spacefm", NULL);

    /* General */
    app_settings.show_thumbnail = show_thumbnail_default;
    app_settings.max_thumb_size = max_thumb_size_default;
    app_settings.big_icon_size = big_icon_size_default;
    app_settings.small_icon_size = small_icon_size_default;
    app_settings.tool_icon_size = tool_icon_size_default;
    app_settings.use_si_prefix = use_si_prefix_default;
    app_settings.no_execute = TRUE;  // MOD
    app_settings.no_confirm = FALSE; // MOD
    app_settings.date_format = NULL; // MOD

    /* Interface */
    app_settings.always_show_tabs = always_show_tabs_default;
    app_settings.hide_close_tab_buttons = hide_close_tab_buttons_default;

    /* Window State */
    app_settings.width = 640;
    app_settings.height = 480;

    // MOD extra settings
    xset_defaults();

    if (!g_file_test(settings_config_dir, G_FILE_TEST_EXISTS))
    {
        // copy /etc/xdg/spacefm
        char* xdg_path = g_build_filename(SYSCONFDIR, "xdg", "spacefm", NULL);
        if (g_file_test(xdg_path, G_FILE_TEST_IS_DIR))
        {
            char* command = g_strdup_printf("cp -r %s '%s'", xdg_path, settings_config_dir);
            print_command(command);
            g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
            g_free(command);
            chmod(settings_config_dir, S_IRWXU);
        }
        g_free(xdg_path);
    }

    if (!g_file_test(settings_config_dir, G_FILE_TEST_EXISTS))
        g_mkdir_with_parents(settings_config_dir, 0700);

    // check if .git exists
    if (git_backed_settings)
    {
        path = g_build_filename(settings_config_dir, ".git", NULL);
        if (!G_LIKELY(g_file_test(path, G_FILE_TEST_EXISTS)))
        {
            char* command = g_strdup_printf("%s -c \"cd %s && git init && git config commit.gpgsign false\"",
                                            BASHPATH,
                                            settings_config_dir);
            print_command(command);
            g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
            g_free(command);
        }
    }

    // load session
    path = g_build_filename(settings_config_dir, "session", NULL);
    if (G_LIKELY(g_file_test(path, G_FILE_TEST_EXISTS)))
    {
        if (git_backed_settings)
        {
            char* command = g_strdup_printf("%s -c \"cd %s && git add session && "
                                            "git commit -m 'Session File' 1>/dev/null\"",
                                            BASHPATH,
                                            settings_config_dir);
            print_command(command);
            g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
            g_free(command);
        }
        else
        {
            // copy session to session-old
            char* old = g_build_filename(settings_config_dir, "session-old", NULL);
            char* command = g_strdup_printf("cp -a  %s %s", path, old);
            if (g_file_test(old, G_FILE_TEST_EXISTS))
                unlink(old);
            print_command(command);
            g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
            g_free(command);
            g_free(old);
        }
    }
    else
    {
        if (git_backed_settings)
        {
            char* command = g_strdup_printf("%s -c \"cd %s && git checkout session\"", BASHPATH, settings_config_dir);
            print_command(command);
            g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
            g_free(command);
            path = g_build_filename(settings_config_dir, "session", NULL);
        }
        else
        {
            path = g_build_filename(settings_config_dir, "session-old", NULL);
        }
        if (!g_file_test(path, G_FILE_TEST_EXISTS))
            path = NULL;
    }

    if (path)
    {
        file = fopen(path, "r");
        g_free(path);
    }
    else
        file = NULL;

    if (file)
    {
        SettingsParseFunc func = NULL;
        char line[2048];
        while (fgets(line, sizeof(line), file))
        {
            strtok(line, "\r\n");
            if (!line[0])
                continue;
            if (line[0] == '[')
            {
                strtok(line, "]");
                if (0 == strcmp(line + 1, "General"))
                    func = &parse_general_settings;
                else if (0 == strcmp(line + 1, "Window"))
                    func = &parse_window_state;
                else if (0 == strcmp(line + 1, "Interface"))
                    func = &parse_interface_settings;
                else if (0 == strcmp(line + 1, "MOD")) // MOD
                    func = &xset_parse;
                else
                    func = NULL;
                continue;
            }
            if (func)
                (*func)(line);
        }
        fclose(file);
    }

    // MOD turn off fullscreen
    xset_set_b("main_full", FALSE);

    // MOD date_format
    app_settings.date_format = g_strdup(xset_get_s("date_format"));
    if (!app_settings.date_format || app_settings.date_format[0] == '\0')
    {
        if (app_settings.date_format)
            g_free(app_settings.date_format);
        app_settings.date_format = g_strdup_printf("%%Y-%%m-%%d %%H:%%M");
        xset_set("date_format", "s", "%Y-%m-%d %H:%M");
    }

    // MOD su command discovery (sets default)
    char* set_su = get_valid_su();
    if (set_su)
        g_free(set_su);

    // MOD terminal discovery
    char* terminal = xset_get_s("main_terminal");
    if (!terminal || terminal[0] == '\0')
    {
        int i;
        for (i = 0; i < G_N_ELEMENTS(terminal_programs); i++)
        {
            char* term;
            if ((term = g_find_program_in_path(terminal_programs[i])))
            {
                xset_set("main_terminal", "s", terminal_programs[i]);
                xset_set_b("main_terminal", TRUE); // discovery
                g_free(term);
                break;
            }
        }
    }

    // MOD editor discovery
    char* app_name = xset_get_s("editor");
    if (!app_name || app_name[0] == '\0')
    {
        VFSMimeType* mime_type = vfs_mime_type_get_from_type("text/plain");
        if (mime_type)
        {
            app_name = vfs_mime_type_get_default_action(mime_type);
            vfs_mime_type_unref(mime_type);
            if (app_name)
            {
                VFSAppDesktop* app = vfs_app_desktop_new(app_name);
                if (app)
                {
                    if (app->exec)
                        xset_set("editor", "s", app->exec);
                    vfs_app_desktop_unref(app);
                }
            }
        }
    }

    // add default handlers
    ptk_handler_add_defaults(HANDLER_MODE_ARC, FALSE, FALSE);
    ptk_handler_add_defaults(HANDLER_MODE_FS, FALSE, FALSE);
    ptk_handler_add_defaults(HANDLER_MODE_NET, FALSE, FALSE);
    ptk_handler_add_defaults(HANDLER_MODE_FILE, FALSE, FALSE);

    // set default keys
    xset_default_keys();

    // cache event handlers
    evt_win_focus = xset_get("evt_win_focus");
    evt_win_move = xset_get("evt_win_move");
    evt_win_click = xset_get("evt_win_click");
    evt_win_key = xset_get("evt_win_key");
    evt_win_close = xset_get("evt_win_close");
    evt_pnl_show = xset_get("evt_pnl_show");
    evt_pnl_focus = xset_get("evt_pnl_focus");
    evt_pnl_sel = xset_get("evt_pnl_sel");
    evt_tab_new = xset_get("evt_tab_new");
    evt_tab_chdir = xset_get("evt_tab_chdir");
    evt_tab_focus = xset_get("evt_tab_focus");
    evt_tab_close = xset_get("evt_tab_close");
    evt_device = xset_get("evt_device");

    /*
        // config conversions
        int ver = xset_get_int( "config_version", "s" );

        if ( ver < 38 && !xset_is( "hand_net_+fuse" ) ) // < 1.0.6
        {
            // add missing fuse handler to bottom of list
            ptk_handler_add_new_default( HANDLER_MODE_NET, "hand_net_+fuse", FALSE );
        }
    */

    // add default bookmarks
    ptk_bookmark_view_get_first_bookmark(NULL);
}

void save_settings(gpointer main_window_ptr)
{
    int p;
    XSet* set;
    FMMainWindow* main_window;
    // g_printf("save_settings\n");

    xset_set("config_version", "s", CONFIG_VERSION);

    // save tabs
    gboolean save_tabs = xset_get_b("main_save_tabs");
    if (main_window_ptr)
        main_window = (FMMainWindow*)main_window_ptr;
    else
        main_window = fm_main_window_get_last_active();

    if (GTK_IS_WIDGET(main_window) && save_tabs)
    {
        for (p = 1; p < 5; p++)
        {
            set = xset_get_panel(p, "show");
            if (GTK_IS_NOTEBOOK(main_window->panel[p - 1]))
            {
                int pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_window->panel[p - 1]));
                if (pages) // panel was shown
                {
                    if (set->s)
                    {
                        g_free(set->s);
                        set->s = NULL;
                    }
                    char* tabs = g_strdup("");
                    int g;
                    for (g = 0; g < pages; g++)
                    {
                        PtkFileBrowser* file_browser =
                            PTK_FILE_BROWSER(gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_window->panel[p - 1]), g));
                        char* old_tabs = tabs;
                        tabs = g_strdup_printf("%s///%s", old_tabs, ptk_file_browser_get_cwd(file_browser));
                        g_free(old_tabs);
                    }
                    if (tabs[0] != '\0')
                        set->s = tabs;
                    else
                        g_free(tabs);

                    // save current tab
                    if (set->x)
                        g_free(set->x);
                    set->x =
                        g_strdup_printf("%d", gtk_notebook_get_current_page(GTK_NOTEBOOK(main_window->panel[p - 1])));
                }
            }
        }
    }
    else if (!save_tabs)
    {
        // clear saved tabs
        for (p = 1; p < 5; p++)
        {
            set = xset_get_panel(p, "show");
            if (set->s)
            {
                g_free(set->s);
                set->s = NULL;
            }
            if (set->x)
            {
                g_free(set->x);
                set->x = NULL;
            }
        }
    }

    /* save settings */
    if (G_UNLIKELY(!g_file_test(settings_config_dir, G_FILE_TEST_EXISTS)))
        g_mkdir_with_parents(settings_config_dir, 0700);

    GString* buf = g_string_sized_new(4096);

    g_string_append(buf, "[General]\n");
    g_string_append_printf(buf, "show_thumbnail=%d\n", !!app_settings.show_thumbnail);
    g_string_append_printf(buf, "max_thumb_size=%d\n", app_settings.max_thumb_size >> 10);
    g_string_append_printf(buf, "big_icon_size=%d\n", app_settings.big_icon_size);
    g_string_append_printf(buf, "small_icon_size=%d\n", app_settings.small_icon_size);
    g_string_append_printf(buf, "tool_icon_size=%d\n", app_settings.tool_icon_size);
    g_string_append_printf(buf, "single_click=%d\n", app_settings.single_click);
    g_string_append_printf(buf, "no_single_hover=%d\n", app_settings.no_single_hover);
    g_string_append_printf(buf, "sort_order=%d\n", app_settings.sort_order);
    g_string_append_printf(buf, "sort_type=%d\n", app_settings.sort_type);
    g_string_append_printf(buf, "use_si_prefix=%d\n", !!app_settings.use_si_prefix);
    g_string_append_printf(buf, "no_execute=%d\n", !!app_settings.no_execute);
    g_string_append_printf(buf, "no_confirm=%d\n", !!app_settings.no_confirm);

    g_string_append(buf, "\n[Window]\n");
    g_string_append_printf(buf, "width=%d\n", app_settings.width);
    g_string_append_printf(buf, "height=%d\n", app_settings.height);
    g_string_append_printf(buf, "maximized=%d\n", app_settings.maximized);

    g_string_append(buf, "\n[Interface]\n");
    g_string_append_printf(buf, "always_show_tabs=%d\n", app_settings.always_show_tabs);
    g_string_append_printf(buf, "show_close_tab_buttons=%d\n", !app_settings.hide_close_tab_buttons);

    g_string_append(buf, "\n[MOD]\n");
    xset_write(buf);

    // move
    char* path = g_build_filename(settings_config_dir, "session", NULL);
    if (!g_file_set_contents(path, buf->str, buf->len, NULL))
        g_fprintf(stderr, "spacefm: saving session file failed\n");
    g_free(path);
    g_string_free(buf, TRUE);
}

void free_settings()
{
    if (xset_cmd_history)
    {
        g_list_foreach(xset_cmd_history, (GFunc)g_free, NULL);
        g_list_free(xset_cmd_history);
        xset_cmd_history = NULL;
    }

    xset_free_all();
}

const char* xset_get_config_dir()
{
    return settings_config_dir;
}

const char* xset_get_tmp_dir()
{
    return settings_tmp_dir;
}

const char* xset_get_user_tmp_dir()
{
    if (settings_user_tmp_dir && g_file_test(settings_user_tmp_dir, G_FILE_TEST_EXISTS))
        return settings_user_tmp_dir;

    settings_user_tmp_dir = g_build_filename(settings_tmp_dir, "spacefm", NULL);
    g_mkdir_with_parents(settings_user_tmp_dir, 0700);

    return settings_user_tmp_dir;
}

static gboolean idle_save_settings(gpointer ptr)
{
    // g_printf("AUTOSAVE *** idle_save_settings\n" );
    save_settings(NULL);
    return FALSE;
}

static void auto_save_start(gboolean delay)
{
    // g_printf("AUTOSAVE auto_save_start\n" );
    if (!delay)
    {
        g_idle_add((GSourceFunc)idle_save_settings, NULL);
        xset_autosave_request = FALSE;
    }
    else
        xset_autosave_request = TRUE;
    if (!xset_autosave_timer)
    {
        xset_autosave_timer = g_timeout_add_seconds(10, (GSourceFunc)on_autosave_timer, NULL);
        // g_printf("AUTOSAVE timer started\n" );
    }
}

gboolean on_autosave_timer(gpointer ptr)
{
    // g_printf("AUTOSAVE timeout\n" );
    if (xset_autosave_timer)
    {
        g_source_remove(xset_autosave_timer);
        xset_autosave_timer = 0;
    }
    if (xset_autosave_request)
        auto_save_start(FALSE);
    return FALSE;
}

void xset_autosave(gboolean force, gboolean delay)
{
    if (xset_autosave_timer && !force)
    {
        // autosave timer is running, so request save on timeout to prevent
        // saving too frequently, unless force
        xset_autosave_request = TRUE;
        // g_printf("AUTOSAVE request\n" );
    }
    else
    {
        if (xset_autosave_timer && force)
        {
            g_source_remove(xset_autosave_timer);
            xset_autosave_timer = 0;
        }
        /* if ( force )
            g_printf("AUTOSAVE force\n" );
        else if ( delay )
            g_printf("AUTOSAVE delay\n" );
        else
            g_printf("AUTOSAVE normal\n" ); */
        auto_save_start(!force && delay);
    }
}

void xset_autosave_cancel()
{
    // g_printf("AUTOSAVE cancel\n" );
    xset_autosave_request = FALSE;
    if (xset_autosave_timer)
    {
        g_source_remove(xset_autosave_timer);
        xset_autosave_timer = 0;
    }
}

void xset_free_all()
{
    GList* l;

    for (l = xsets; l; l = l->next)
    {
        XSet* set = l->data;
        if (set->ob2_data && g_str_has_prefix(set->name, "evt_"))
        {
            g_list_foreach((GList*)set->ob2_data, (GFunc)g_free, NULL);
            g_list_free((GList*)set->ob2_data);
        }
        if (set->name)
            g_free(set->name);
        if (set->s)
            g_free(set->s);
        if (set->x)
            g_free(set->x);
        if (set->y)
            g_free(set->y);
        if (set->z)
            g_free(set->z);
        if (set->menu_label)
            g_free(set->menu_label);
        if (set->shared_key)
            g_free(set->shared_key);
        if (set->icon)
            g_free(set->icon);
        if (set->desc)
            g_free(set->desc);
        if (set->title)
            g_free(set->title);
        if (set->next)
            g_free(set->next);
        if (set->parent)
            g_free(set->parent);
        if (set->child)
            g_free(set->child);
        if (set->prev)
            g_free(set->prev);
        if (set->line)
            g_free(set->line);
        if (set->context)
            g_free(set->context);
        if (set->plugin)
        {
            if (set->plug_dir)
                g_free(set->plug_dir);
            if (set->plug_name)
                g_free(set->plug_name);
        }
        g_slice_free(XSet, set);
    }
    g_list_free(xsets);
    xsets = NULL;
    set_last = NULL;

    if (xset_context)
    {
        xset_context_new();
        g_slice_free(XSetContext, xset_context);
        xset_context = NULL;
    }
}

void xset_free(XSet* set)
{
    if (set->name)
        g_free(set->name);
    if (set->s)
        g_free(set->s);
    if (set->x)
        g_free(set->x);
    if (set->y)
        g_free(set->y);
    if (set->z)
        g_free(set->z);
    if (set->menu_label)
        g_free(set->menu_label);
    if (set->shared_key)
        g_free(set->shared_key);
    if (set->icon)
        g_free(set->icon);
    if (set->desc)
        g_free(set->desc);
    if (set->title)
        g_free(set->title);
    if (set->next)
        g_free(set->next);
    if (set->parent)
        g_free(set->parent);
    if (set->child)
        g_free(set->child);
    if (set->prev)
        g_free(set->prev);
    if (set->line)
        g_free(set->line);
    if (set->context)
        g_free(set->context);
    if (set->plugin)
    {
        if (set->plug_dir)
            g_free(set->plug_dir);
        if (set->plug_name)
            g_free(set->plug_name);
    }
    xsets = g_list_remove(xsets, set);
    g_slice_free(XSet, set);
    set_last = NULL;
}

XSet* xset_new(const char* name)
{
    XSet* set = g_slice_new(XSet);
    set->name = g_strdup(name);

    set->b = XSET_B_UNSET;
    set->s = NULL;
    set->x = NULL;
    set->y = NULL;
    set->z = NULL;
    set->disable = FALSE;
    set->menu_label = NULL;
    set->menu_style = XSET_MENU_NORMAL;
    set->cb_func = NULL;
    set->cb_data = NULL;
    set->ob1 = NULL;
    set->ob1_data = NULL;
    set->ob2 = NULL;
    set->ob2_data = NULL;
    set->key = 0;
    set->keymod = 0;
    set->shared_key = NULL;
    set->icon = NULL;
    set->desc = NULL;
    set->title = NULL;
    set->next = NULL;
    set->context = NULL;
    set->tool = XSET_TOOL_NOT;
    set->lock = TRUE;
    set->plugin = FALSE;

    // custom ( !lock )
    set->prev = NULL;
    set->parent = NULL;
    set->child = NULL;
    set->line = NULL;
    set->task = XSET_B_UNSET;
    set->task_pop = XSET_B_UNSET;
    set->task_err = XSET_B_UNSET;
    set->task_out = XSET_B_UNSET;
    set->in_terminal = XSET_B_UNSET;
    set->keep_terminal = XSET_B_UNSET;
    set->scroll_lock = XSET_B_UNSET;
    set->opener = 0;
    return set;
}

XSet* xset_get(const char* name)
{
    if (!name)
        return NULL;

    GList* l;
    for (l = xsets; l; l = l->next)
    {
        if (!strcmp(name, ((XSet*)l->data)->name))
        {
            // existing xset
            return l->data;
        }
    }

    // add new
    xsets = g_list_prepend(xsets, xset_new(name));
    return xsets->data;
}

XSet* xset_get_panel(int panel, const char* name)
{
    XSet* set;
    char* fullname = g_strdup_printf("panel%d_%s", panel, name);
    set = xset_get(fullname);
    g_free(fullname);
    return set;
}

XSet* xset_get_panel_mode(int panel, const char* name, char mode)
{
    XSet* set;
    char* fullname = g_strdup_printf("panel%d_%s%d", panel, name, mode);
    set = xset_get(fullname);
    g_free(fullname);
    return set;
}

char* xset_get_s(const char* name)
{
    XSet* set = xset_get(name);
    if (set)
        return set->s;
    else
        return NULL;
}

char* xset_get_s_panel(int panel, const char* name)
{
    char* fullname = g_strdup_printf("panel%d_%s", panel, name);
    char* s = xset_get_s(fullname);
    g_free(fullname);
    return s;
}

gboolean xset_get_b(const char* name)
{
    XSet* set = xset_get(name);
    return (set->b == XSET_B_TRUE);
}

gboolean xset_get_b_panel(int panel, const char* name)
{
    char* fullname = g_strdup_printf("panel%d_%s", panel, name);
    gboolean b = xset_get_b(fullname);
    g_free(fullname);
    return b;
}

gboolean xset_get_b_panel_mode(int panel, const char* name, char mode)
{
    char* fullname = g_strdup_printf("panel%d_%s%d", panel, name, mode);
    gboolean b = xset_get_b(fullname);
    g_free(fullname);
    return b;
}

gboolean xset_get_b_set(XSet* set)
{
    return (set->b == XSET_B_TRUE);
}

XSet* xset_is(const char* name)
{
    if (!name)
        return NULL;

    GList* l;
    for (l = xsets; l; l = l->next)
    {
        if (!strcmp(name, ((XSet*)l->data)->name))
        {
            // existing xset
            return l->data;
        }
    }
    return NULL;
}

XSet* xset_set_b(const char* name, gboolean bval)
{
    XSet* set = xset_get(name);

    if (bval)
        set->b = XSET_B_TRUE;
    else
        set->b = XSET_B_FALSE;
    return set;
}

XSet* xset_set_b_panel(int panel, const char* name, gboolean bval)
{
    char* fullname = g_strdup_printf("panel%d_%s", panel, name);
    XSet* set = xset_set_b(fullname, bval);
    g_free(fullname);
    return set;
}

XSet* xset_set_b_panel_mode(int panel, const char* name, char mode, gboolean bval)
{
    char* fullname = g_strdup_printf("panel%d_%s%d", panel, name, mode);
    XSet* set = xset_set_b(fullname, bval);
    g_free(fullname);
    return set;
}

gboolean xset_get_bool(const char* name, const char* var)
{
    XSet* set = xset_get(name);
    if (!strcmp(var, "b"))
        return (set->b == XSET_B_TRUE);
    if (!strcmp(var, "disable"))
        return set->disable;
    if (!strcmp(var, "style"))
        return !!set->menu_style;

    char* varstring = NULL;
    if (!strcmp(var, "x"))
        varstring = set->x;
    else if (!strcmp(var, "y"))
        varstring = set->y;
    else if (!strcmp(var, "z"))
        varstring = set->z;
    else if (!strcmp(var, "s"))
        varstring = set->s;
    else if (!strcmp(var, "desc"))
        varstring = set->desc;
    else if (!strcmp(var, "title"))
        varstring = set->title;
    else if (!strcmp(var, "lbl"))
        varstring = set->menu_label;
    else if (!set->lock)
    {
        if (!strcmp(var, "task"))
            return (set->task == XSET_B_TRUE);
        if (!strcmp(var, "task_pop"))
            return (set->task_pop == XSET_B_TRUE);
        if (!strcmp(var, "task-err"))
            return (set->task_err == XSET_B_TRUE);
        if (!strcmp(var, "task_out"))
            return (set->task_out == XSET_B_TRUE);
    }
    if (!varstring)
        return FALSE;
    return !!atoi(varstring);
}

gboolean xset_get_bool_panel(int panel, const char* name, const char* var)
{
    char* fullname = g_strdup_printf("panel%d_%s", panel, name);
    gboolean bool = xset_get_bool(fullname, var);
    g_free(fullname);
    return bool;
}

int xset_get_int_set(XSet* set, const char* var)
{
    if (!set || !var)
        return -1;
    const char* varstring = NULL;
    if (!strcmp(var, "x"))
        varstring = set->x;
    else if (!strcmp(var, "y"))
        varstring = set->y;
    else if (!strcmp(var, "z"))
        varstring = set->z;
    else if (!strcmp(var, "s"))
        varstring = set->s;
    else if (!strcmp(var, "key"))
        return set->key;
    else if (!strcmp(var, "keymod"))
        return set->keymod;
    if (!varstring)
        return 0;
    return atoi(varstring);
}

int xset_get_int(const char* name, const char* var)
{
    XSet* set = xset_get(name);
    return xset_get_int_set(set, var);
}

int xset_get_int_panel(int panel, const char* name, const char* var)
{
    char* fullname = g_strdup_printf("panel%d_%s", panel, name);
    int i = xset_get_int(fullname, var);
    g_free(fullname);
    return i;
}

XSet* xset_is_main_bookmark(XSet* set)
{
    // is this xset in main_book ?  returns immediate parent set
    XSet* set_prev = set;
    while (set_prev)
    {
        if (set_prev->prev)
            set_prev = xset_is(set_prev->prev);
        else if (set_prev->parent)
        {
            XSet* set_parent = NULL;
            set_prev = xset_is(set_prev->parent);
            if (!set_parent)
                set_parent = set_prev;
            if (set_prev && !g_strcmp0(set_prev->name, "main_book"))
            {
                // found bookmark in main_book tree
                return set_parent;
            }
        }
        else
            break;
    }
    return NULL;
}

static void xset_write_set(GString* buf, XSet* set)
{
    if (set->plugin)
        return;
    if (set->s)
        g_string_append_printf(buf, "%s-s=%s\n", set->name, set->s);
    if (set->x)
        g_string_append_printf(buf, "%s-x=%s\n", set->name, set->x);
    if (set->y)
        g_string_append_printf(buf, "%s-y=%s\n", set->name, set->y);
    if (set->z)
        g_string_append_printf(buf, "%s-z=%s\n", set->name, set->z);
    if (set->key)
        g_string_append_printf(buf, "%s-key=%d\n", set->name, set->key);
    if (set->keymod)
        g_string_append_printf(buf, "%s-keymod=%d\n", set->name, set->keymod);
    // menu label
    if (set->menu_label)
    {
        if (set->lock)
        {
            // built-in
            if (set->in_terminal == XSET_B_TRUE && set->menu_label && set->menu_label[0])
                // only save lbl if menu_label was customized
                g_string_append_printf(buf, "%s-lbl=%s\n", set->name, set->menu_label);
        }
        else
            // custom
            g_string_append_printf(buf, "%s-label=%s\n", set->name, set->menu_label);
    }
    // icon
    if (set->lock)
    {
        // built-in
        if (set->keep_terminal == XSET_B_TRUE)
            // only save icn if icon was customized
            g_string_append_printf(buf, "%s-icn=%s\n", set->name, set->icon ? set->icon : "");
    }
    else if (set->icon)
        // custom
        g_string_append_printf(buf, "%s-icon=%s\n", set->name, set->icon);
    if (set->next)
        g_string_append_printf(buf, "%s-next=%s\n", set->name, set->next);
    if (set->child)
        g_string_append_printf(buf, "%s-child=%s\n", set->name, set->child);
    if (set->context)
        g_string_append_printf(buf, "%s-cxt=%s\n", set->name, set->context);
    if (set->b != XSET_B_UNSET)
        g_string_append_printf(buf, "%s-b=%d\n", set->name, set->b);
    if (set->tool != XSET_TOOL_NOT)
        g_string_append_printf(buf, "%s-tool=%d\n", set->name, set->tool);
    if (!set->lock)
    {
        if (set->menu_style)
            g_string_append_printf(buf, "%s-style=%d\n", set->name, set->menu_style);
        if (set->desc)
            g_string_append_printf(buf, "%s-desc=%s\n", set->name, set->desc);
        if (set->title)
            g_string_append_printf(buf, "%s-title=%s\n", set->name, set->title);
        if (set->prev)
            g_string_append_printf(buf, "%s-prev=%s\n", set->name, set->prev);
        if (set->parent)
            g_string_append_printf(buf, "%s-parent=%s\n", set->name, set->parent);
        if (set->line)
            g_string_append_printf(buf, "%s-line=%s\n", set->name, set->line);
        if (set->task != XSET_B_UNSET)
            g_string_append_printf(buf, "%s-task=%d\n", set->name, set->task);
        if (set->task_pop != XSET_B_UNSET)
            g_string_append_printf(buf, "%s-task_pop=%d\n", set->name, set->task_pop);
        if (set->task_err != XSET_B_UNSET)
            g_string_append_printf(buf, "%s-task_err=%d\n", set->name, set->task_err);
        if (set->task_out != XSET_B_UNSET)
            g_string_append_printf(buf, "%s-task_out=%d\n", set->name, set->task_out);
        if (set->in_terminal != XSET_B_UNSET)
            g_string_append_printf(buf, "%s-term=%d\n", set->name, set->in_terminal);
        if (set->keep_terminal != XSET_B_UNSET)
            g_string_append_printf(buf, "%s-keep=%d\n", set->name, set->keep_terminal);
        if (set->scroll_lock != XSET_B_UNSET)
            g_string_append_printf(buf, "%s-scroll=%d\n", set->name, set->scroll_lock);
        if (set->opener != 0)
            g_string_append_printf(buf, "%s-op=%d\n", set->name, set->opener);
    }
}

void xset_write(GString* buf)
{
    GList* l;

    for (l = g_list_last(xsets); l; l = l->prev)
    {
        // hack to not save default handlers - this allows default handlers
        // to be updated more easily
        if ((gboolean)((XSet*)l->data)->disable && (char)((XSet*)l->data)->name[0] == 'h' &&
            g_str_has_prefix((char*)((XSet*)l->data)->name, "hand"))
            continue;
        xset_write_set(buf, (XSet*)l->data);
    }
}

void xset_parse(char* line)
{
    char* sep = strchr(line, '=');
    char* name;
    char* value;
    if (!sep)
        return;
    name = line;
    value = sep + 1;
    *sep = '\0';
    sep = strchr(name, '-');
    if (!sep)
        return;
    char* var = sep + 1;
    *sep = '\0';

    if (!strncmp(name, "cstm_", 5) || !strncmp(name, "hand_", 5))
    {
        // custom
        if (!strcmp(set_last->name, name))
            xset_set_set(set_last, var, value);
        else
        {
            set_last = xset_get(name);
            if (set_last->lock)
                set_last->lock = FALSE;
            xset_set_set(set_last, var, value);
        }
    }
    else
    {
        // normal (lock)
        if (!strcmp(set_last->name, name))
            xset_set_set(set_last, var, value);
        else
            set_last = xset_set(name, var, value);
    }
}

XSet* xset_set_cb(const char* name, void (*cb_func)(), gpointer cb_data)
{
    XSet* set = xset_get(name);
    set->cb_func = cb_func;
    set->cb_data = cb_data;
    return set;
}

XSet* xset_set_cb_panel(int panel, const char* name, void (*cb_func)(), gpointer cb_data)
{
    char* fullname = g_strdup_printf("panel%d_%s", panel, name);
    XSet* set = xset_set_cb(fullname, cb_func, cb_data);
    g_free(fullname);
    return set;
}

XSet* xset_set_ob1_int(XSet* set, const char* ob1, int ob1_int)
{
    if (set->ob1)
        g_free(set->ob1);
    set->ob1 = g_strdup(ob1);
    set->ob1_data = GINT_TO_POINTER(ob1_int);
    return set;
}

XSet* xset_set_ob1(XSet* set, const char* ob1, gpointer ob1_data)
{
    if (set->ob1)
        g_free(set->ob1);
    set->ob1 = g_strdup(ob1);
    set->ob1_data = ob1_data;
    return set;
}

XSet* xset_set_ob2(XSet* set, const char* ob2, gpointer ob2_data)
{
    if (set->ob2)
        g_free(set->ob2);
    set->ob2 = g_strdup(ob2);
    set->ob2_data = ob2_data;
    return set;
}

XSet* xset_set_set(XSet* set, const char* var, const char* value)
{
    if (!set)
        return NULL;

    if (!strcmp(var, "s"))
    {
        if (set->s)
            g_free(set->s);
        set->s = g_strdup(value);
    }
    else if (!strcmp(var, "b"))
    {
        if (!strcmp(value, "1"))
            set->b = XSET_B_TRUE;
        else
            set->b = XSET_B_FALSE;
    }
    else if (!strcmp(var, "x"))
    {
        if (set->x)
            g_free(set->x);
        set->x = g_strdup(value);
    }
    else if (!strcmp(var, "y"))
    {
        if (set->y)
            g_free(set->y);
        set->y = g_strdup(value);
    }
    else if (!strcmp(var, "z"))
    {
        if (set->z)
            g_free(set->z);
        set->z = g_strdup(value);
    }
    else if (!strcmp(var, "key"))
        set->key = atoi(value);
    else if (!strcmp(var, "keymod"))
        set->keymod = atoi(value);
    else if (!strcmp(var, "style"))
    {
        set->menu_style = atoi(value);
    }
    else if (!strcmp(var, "desc"))
    {
        if (set->desc)
            g_free(set->desc);
        set->desc = g_strdup(value);
    }
    else if (!strcmp(var, "title"))
    {
        if (set->title)
            g_free(set->title);
        set->title = g_strdup(value);
    }
    else if (!strcmp(var, "lbl"))
    {
        // lbl is only used >= 0.9.0 for changed lock default menu_label
        if (set->menu_label)
            g_free(set->menu_label);
        set->menu_label = g_strdup(value);
        if (set->lock)
            // indicate that menu label is not default and should be saved
            set->in_terminal = XSET_B_TRUE;
    }
    else if (!strcmp(var, "icn"))
    {
        // icn is only used >= 0.9.0 for changed lock default icon
        if (set->icon)
            g_free(set->icon);
        set->icon = g_strdup(value);
        if (set->lock)
            // indicate that icon is not default and should be saved
            set->keep_terminal = XSET_B_TRUE;
    }
    else if (!strcmp(var, "label"))
    {
        // pre-0.9.0 menu_label or >= 0.9.0 custom item label
        // only save if custom or not default label
        if (!set->lock || g_strcmp0(set->menu_label, value))
        {
            if (set->menu_label)
                g_free(set->menu_label);
            set->menu_label = g_strdup(value);
            if (set->lock)
                // indicate that menu label is not default and should be saved
                set->in_terminal = XSET_B_TRUE;
        }
    }
    else if (!strcmp(var, "icon"))
    {
        // pre-0.9.0 icon or >= 0.9.0 custom item icon
        // only save if custom or not default icon
        // also check that stock name doesn't match
        if (!set->lock ||
            (g_strcmp0(set->icon, value) && (!icon_stock_to_id(value) || !icon_stock_to_id(set->icon) ||
                                             g_strcmp0(icon_stock_to_id(value), icon_stock_to_id(set->icon)))))
        {
            if (set->icon)
                g_free(set->icon);
            set->icon = g_strdup(value);
            if (set->lock)
            {
                // indicate that icon is not default and should be saved
                set->keep_terminal = XSET_B_TRUE;
            }
        }
    }
    else if (!strcmp(var, "shared_key"))
    {
        if (set->shared_key)
            g_free(set->shared_key);
        set->shared_key = g_strdup(value);
    }
    else if (!strcmp(var, "next"))
    {
        if (set->next)
            g_free(set->next);
        set->next = g_strdup(value);
    }
    else if (!strcmp(var, "prev"))
    {
        if (set->prev)
            g_free(set->prev);
        set->prev = g_strdup(value);
    }
    else if (!strcmp(var, "parent"))
    {
        if (set->parent)
            g_free(set->parent);
        set->parent = g_strdup(value);
    }
    else if (!strcmp(var, "child"))
    {
        if (set->child)
            g_free(set->child);
        set->child = g_strdup(value);
    }
    else if (!strcmp(var, "cxt"))
    {
        if (set->context)
            g_free(set->context);
        set->context = g_strdup(value);
    }
    else if (!strcmp(var, "line"))
    {
        if (set->line)
            g_free(set->line);
        set->line = g_strdup(value);
    }
    else if (!strcmp(var, "tool"))
    {
        set->tool = atoi(value);
    }
    else if (!strcmp(var, "task"))
    {
        if (!strcmp(value, "1"))
            set->task = XSET_B_TRUE;
        else
            set->task = XSET_B_UNSET;
    }
    else if (!strcmp(var, "task_pop"))
    {
        if (!strcmp(value, "1"))
            set->task_pop = XSET_B_TRUE;
        else
            set->task_pop = XSET_B_UNSET;
    }
    else if (!strcmp(var, "task_err"))
    {
        if (!strcmp(value, "1"))
            set->task_err = XSET_B_TRUE;
        else
            set->task_err = XSET_B_UNSET;
    }
    else if (!strcmp(var, "task_out"))
    {
        if (!strcmp(value, "1"))
            set->task_out = XSET_B_TRUE;
        else
            set->task_out = XSET_B_UNSET;
    }
    else if (!strcmp(var, "term"))
    {
        if (!strcmp(value, "1"))
            set->in_terminal = XSET_B_TRUE;
        else
            set->in_terminal = XSET_B_UNSET;
    }
    else if (!strcmp(var, "keep"))
    {
        if (!strcmp(value, "1"))
            set->keep_terminal = XSET_B_TRUE;
        else
            set->keep_terminal = XSET_B_UNSET;
    }
    else if (!strcmp(var, "scroll"))
    {
        if (!strcmp(value, "1"))
            set->scroll_lock = XSET_B_TRUE;
        else
            set->scroll_lock = XSET_B_UNSET;
    }
    else if (!strcmp(var, "disable"))
    {
        if (!strcmp(value, "1"))
            set->disable = TRUE;
        else
            set->disable = FALSE;
    }
    else if (!strcmp(var, "op"))
        set->opener = atoi(value);
    return set;
}

XSet* xset_set(const char* name, const char* var, const char* value)
{
    XSet* set = xset_get(name);
    if (!set->lock ||
        (strcmp(var, "style") && strcmp(var, "desc") && strcmp(var, "title") && strcmp(var, "shared_key")))
        return xset_set_set(set, var, value);
    else
        return set;
}

XSet* xset_set_panel(int panel, const char* name, const char* var, const char* value)
{
    XSet* set;
    char* fullname = g_strdup_printf("panel%d_%s", panel, name);
    set = xset_set(fullname, var, value);
    g_free(fullname);
    return set;
}

XSet* xset_set_panel_mode(int panel, const char* name, char mode, const char* var, const char* value)
{
    XSet* set;
    char* fullname = g_strdup_printf("panel%d_%s%d", panel, name, mode);
    set = xset_set(fullname, var, value);
    g_free(fullname);
    return set;
}

XSet* xset_find_custom(const char* search)
{
    // find a custom command or submenu by label or xset name
    GList* l;
    char* str;

    char* label = clean_label(search, TRUE, FALSE);
    for (l = xsets; l; l = l->next)
    {
        XSet* set = l->data;
        if (!set->lock && ((set->menu_style == XSET_MENU_SUBMENU && set->child) ||
                           (set->menu_style < XSET_MENU_SUBMENU && xset_get_int_set(set, "x") <= XSET_CMD_BOOKMARK)))
        {
            // custom submenu or custom command - label or name matches?
            str = clean_label(set->menu_label, TRUE, FALSE);
            if (!g_strcmp0(set->name, search) || !g_strcmp0(str, label))
            {
                // match
                g_free(str);
                g_free(label);
                return set;
            }
            g_free(str);
        }
    }
    g_free(label);
    return NULL;
}

gboolean xset_opener(PtkFileBrowser* file_browser, char job)
{ // find an opener for job
    GList* l;
    gboolean found = FALSE;

    for (l = xsets; l; l = l->next)
    {
        if (!((XSet*)l->data)->lock && ((XSet*)l->data)->opener == job && !((XSet*)l->data)->tool &&
            ((XSet*)l->data)->menu_style != XSET_MENU_SUBMENU && ((XSet*)l->data)->menu_style != XSET_MENU_SEP)
        {
            XSet* set;
            XSet* mset;
            XSet* open_all_set;
            if (((XSet*)l->data)->desc && !strcmp(((XSet*)l->data)->desc, "@plugin@mirror@"))
            {
                // is a plugin mirror
                mset = (XSet*)l->data;
                set = xset_is(mset->shared_key);
                if (!set)
                    continue;
            }
            else if (((XSet*)l->data)->plugin && ((XSet*)l->data)->shared_key)
            {
                // plugin with mirror - ignore to use mirror's context only
                continue;
            }
            else
                set = mset = (XSet*)l->data;

            XSetContext* context = NULL;
            if (!context)
            {
                if (!(context = xset_context_new()))
                    return FALSE;
                if (file_browser)
                    main_context_fill(file_browser, context);
                else
                    return FALSE;

                if (!context->valid)
                    return FALSE;

                // get mime type open_all_type set
                char* open_all_name = g_strdup(context->var[CONTEXT_MIME]);
                if (!open_all_name)
                    open_all_name = g_strdup("");
                char* str = open_all_name;
                open_all_name = replace_string(str, "-", "_", FALSE);
                g_free(str);
                str = replace_string(open_all_name, " ", "", FALSE);
                g_free(open_all_name);
                open_all_name = g_strdup_printf("open_all_type_%s", str);
                g_free(str);
                open_all_set = xset_is(open_all_name);
                g_free(open_all_name);
            }

            // test context
            if (mset->context)
            {
                int context_action = xset_context_test(context, mset->context, FALSE);
                if (context_action == CONTEXT_HIDE || context_action == CONTEXT_DISABLE)
                    continue;
            }

            // valid custom type?
            int cmd_type = xset_get_int_set(set, "x");
            if (cmd_type != XSET_CMD_APP && cmd_type != XSET_CMD_LINE && cmd_type != XSET_CMD_SCRIPT)
                continue;

            // is set pinned to open_all_type for pre-context?
            char pinned = 0;
            GList* ll;
            for (ll = xsets; ll && !pinned; ll = ll->next)
            {
                if (((XSet*)ll->data)->next && g_str_has_prefix(((XSet*)ll->data)->name, "open_all_type_"))
                {
                    XSet* open_all_tset;
                    XSet* tset = open_all_tset = (XSet*)ll->data;
                    while (tset->next)
                    {
                        if (!strcmp(set->name, tset->next))
                        {
                            // found pinned to open_all_type
                            if (open_all_tset == open_all_set)
                                // correct mime type
                                pinned = 2;
                            else
                                // wrong mime type
                                pinned = 1;
                            break;
                        }
                        tset = xset_is(tset->next);
                    }
                }
            }
            if (pinned == 1)
                continue;

            // valid
            found = TRUE;
            set->browser = file_browser;
            char* clean = clean_label(set->menu_label, FALSE, FALSE);
            g_printf(_("\nSelected Menu Item '%s' As Handler\n"), clean);
            g_free(clean);
            xset_menu_cb(NULL, set); // also does custom activate
        }
    }
    return found;
}

XSetContext* xset_context_new()
{
    int i;
    if (!xset_context)
    {
        xset_context = g_slice_new0(XSetContext);
        xset_context->valid = FALSE;
        for (i = 0; i < G_N_ELEMENTS(xset_context->var); i++)
            xset_context->var[i] = NULL;
    }
    else
    {
        xset_context->valid = FALSE;
        for (i = 0; i < G_N_ELEMENTS(xset_context->var); i++)
        {
            if (xset_context->var[i])
                g_free(xset_context->var[i]);
            xset_context->var[i] = NULL;
        }
    }
    return xset_context;
}

const char* icon_stock_to_id(const char* name)
{
#if (GTK_MAJOR_VERSION == 2)
    if (!name)
        return NULL;
    else if (!strncmp(name, "gtk-", 4))
        return name;
    else if (!strncmp(name, "GTK_STOCK_", 10))
    {
        const char* icontail = name + 10;
        const char* stockid;
        if (!strcmp(icontail, "ABOUT"))
            stockid = GTK_STOCK_ABOUT;
        else if (!strcmp(icontail, "ADD"))
            stockid = GTK_STOCK_ADD;
        else if (!strcmp(icontail, "APPLY"))
            stockid = GTK_STOCK_APPLY;
        else if (!strcmp(icontail, "BOLD"))
            stockid = GTK_STOCK_BOLD;
        else if (!strcmp(icontail, "CANCEL"))
            stockid = GTK_STOCK_CANCEL;
#ifdef DEPRECATED_HW
        else if (!strcmp(icontail, "CDROM"))
            stockid = GTK_STOCK_CDROM;
#endif
        else if (!strcmp(icontail, "CLEAR"))
            stockid = GTK_STOCK_CLEAR;
        else if (!strcmp(icontail, "CLOSE"))
            stockid = GTK_STOCK_CLOSE;
        else if (!strcmp(icontail, "CONVERT"))
            stockid = GTK_STOCK_CONVERT;
        else if (!strcmp(icontail, "CONNECT"))
            stockid = GTK_STOCK_CONNECT;
        else if (!strcmp(icontail, "COPY"))
            stockid = GTK_STOCK_COPY;
        else if (!strcmp(icontail, "CUT"))
            stockid = GTK_STOCK_CUT;
        else if (!strcmp(icontail, "DELETE"))
            stockid = GTK_STOCK_DELETE;
        else if (!strcmp(icontail, "DIALOG_ERROR"))
            stockid = GTK_STOCK_DIALOG_ERROR;
        else if (!strcmp(icontail, "DIALOG_INFO"))
            stockid = GTK_STOCK_DIALOG_INFO;
        else if (!strcmp(icontail, "DIALOG_QUESTION"))
            stockid = GTK_STOCK_DIALOG_QUESTION;
        else if (!strcmp(icontail, "DIALOG_WARNING"))
            stockid = GTK_STOCK_DIALOG_WARNING;
        else if (!strcmp(icontail, "DIRECTORY"))
            stockid = GTK_STOCK_DIRECTORY;
        else if (!strcmp(icontail, "DISCARD"))
            stockid = GTK_STOCK_DISCARD;
        else if (!strcmp(icontail, "DISCONNECT"))
            stockid = GTK_STOCK_DISCONNECT;
        else if (!strcmp(icontail, "DND"))
            stockid = GTK_STOCK_DND;
        else if (!strcmp(icontail, "DND_MULTIPLE"))
            stockid = GTK_STOCK_DND_MULTIPLE;
        else if (!strcmp(icontail, "EDIT"))
            stockid = GTK_STOCK_EDIT;
        else if (!strcmp(icontail, "EXECUTE"))
            stockid = GTK_STOCK_EXECUTE;
        else if (!strcmp(icontail, "FILE"))
            stockid = GTK_STOCK_FILE;
        else if (!strcmp(icontail, "FIND"))
            stockid = GTK_STOCK_FIND;
        else if (!strcmp(icontail, "FIND_AND_REPLACE"))
            stockid = GTK_STOCK_FIND_AND_REPLACE;
        else if (!strcmp(icontail, "FULLSCREEN"))
            stockid = GTK_STOCK_FULLSCREEN;
        else if (!strcmp(icontail, "GOTO_BOTTOM"))
            stockid = GTK_STOCK_GOTO_BOTTOM;
        else if (!strcmp(icontail, "GOTO_FIRST"))
            stockid = GTK_STOCK_GOTO_FIRST;
        else if (!strcmp(icontail, "GOTO_LAST"))
            stockid = GTK_STOCK_GOTO_LAST;
        else if (!strcmp(icontail, "GOTO_TOP"))
            stockid = GTK_STOCK_GOTO_TOP;
        else if (!strcmp(icontail, "GO_BACK"))
            stockid = GTK_STOCK_GO_BACK;
        else if (!strcmp(icontail, "GO_DOWN"))
            stockid = GTK_STOCK_GO_DOWN;
        else if (!strcmp(icontail, "GO_FORWARD"))
            stockid = GTK_STOCK_GO_FORWARD;
        else if (!strcmp(icontail, "GO_UP"))
            stockid = GTK_STOCK_GO_UP;
        else if (!strcmp(icontail, "HARDDISK"))
            stockid = GTK_STOCK_HARDDISK;
        else if (!strcmp(icontail, "HELP"))
            stockid = GTK_STOCK_HELP;
        else if (!strcmp(icontail, "HOME"))
            stockid = GTK_STOCK_HOME;
        else if (!strcmp(icontail, "INDENT"))
            stockid = GTK_STOCK_INDENT;
        else if (!strcmp(icontail, "INDEX"))
            stockid = GTK_STOCK_INDEX;
        else if (!strcmp(icontail, "INFO"))
            stockid = GTK_STOCK_INFO;
        else if (!strcmp(icontail, "ITALIC"))
            stockid = GTK_STOCK_ITALIC;
        else if (!strcmp(icontail, "JUMP_TO"))
            stockid = GTK_STOCK_JUMP_TO;
        else if (!strcmp(icontail, "MEDIA_FORWARD"))
            stockid = GTK_STOCK_MEDIA_FORWARD;
        else if (!strcmp(icontail, "MEDIA_NEXT"))
            stockid = GTK_STOCK_MEDIA_NEXT;
        else if (!strcmp(icontail, "MEDIA_PAUSE"))
            stockid = GTK_STOCK_MEDIA_PAUSE;
        else if (!strcmp(icontail, "MEDIA_PLAY"))
            stockid = GTK_STOCK_MEDIA_PLAY;
        else if (!strcmp(icontail, "MEDIA_PREVIOUS"))
            stockid = GTK_STOCK_MEDIA_PREVIOUS;
        else if (!strcmp(icontail, "MEDIA_RECORD"))
            stockid = GTK_STOCK_MEDIA_RECORD;
        else if (!strcmp(icontail, "MEDIA_REWIND"))
            stockid = GTK_STOCK_MEDIA_REWIND;
        else if (!strcmp(icontail, "MEDIA_STOP"))
            stockid = GTK_STOCK_MEDIA_STOP;
        else if (!strcmp(icontail, "NETWORK"))
            stockid = GTK_STOCK_NETWORK;
        else if (!strcmp(icontail, "NEW"))
            stockid = GTK_STOCK_NEW;
        else if (!strcmp(icontail, "NO"))
            stockid = GTK_STOCK_NO;
        else if (!strcmp(icontail, "OK"))
            stockid = GTK_STOCK_OK;
        else if (!strcmp(icontail, "OPEN"))
            stockid = GTK_STOCK_OPEN;
        else if (!strcmp(icontail, "PAGE_SETUP"))
            stockid = GTK_STOCK_PAGE_SETUP;
        else if (!strcmp(icontail, "PASTE"))
            stockid = GTK_STOCK_PASTE;
        else if (!strcmp(icontail, "PREFERENCES"))
            stockid = GTK_STOCK_PREFERENCES;
        else if (!strcmp(icontail, "PRINT"))
            stockid = GTK_STOCK_PRINT;
        else if (!strcmp(icontail, "PROPERTIES"))
            stockid = GTK_STOCK_PROPERTIES;
        else if (!strcmp(icontail, "QUIT"))
            stockid = GTK_STOCK_QUIT;
        else if (!strcmp(icontail, "REDO"))
            stockid = GTK_STOCK_REDO;
        else if (!strcmp(icontail, "REFRESH"))
            stockid = GTK_STOCK_REFRESH;
        else if (!strcmp(icontail, "REMOVE"))
            stockid = GTK_STOCK_REMOVE;
        else if (!strcmp(icontail, "REVERT_TO_SAVED"))
            stockid = GTK_STOCK_REVERT_TO_SAVED;
        else if (!strcmp(icontail, "SAVE"))
            stockid = GTK_STOCK_SAVE;
        else if (!strcmp(icontail, "SAVE_AS"))
            stockid = GTK_STOCK_SAVE_AS;
        else if (!strcmp(icontail, "SELECT_ALL"))
            stockid = GTK_STOCK_SELECT_ALL;
        else if (!strcmp(icontail, "SELECT_COLOR"))
            stockid = GTK_STOCK_SELECT_COLOR;
        else if (!strcmp(icontail, "SELECT_FONT"))
            stockid = GTK_STOCK_SELECT_FONT;
        else if (!strcmp(icontail, "SORT_ASCENDING"))
            stockid = GTK_STOCK_SORT_ASCENDING;
        else if (!strcmp(icontail, "SORT_DESCENDING"))
            stockid = GTK_STOCK_SORT_DESCENDING;
        else if (!strcmp(icontail, "SPELL_CHECK"))
            stockid = GTK_STOCK_SPELL_CHECK;
        else if (!strcmp(icontail, "STOP"))
            stockid = GTK_STOCK_STOP;
        else if (!strcmp(icontail, "STRIKETHROUGH"))
            stockid = GTK_STOCK_STRIKETHROUGH;
        else if (!strcmp(icontail, "UNDELETE"))
            stockid = GTK_STOCK_UNDELETE;
        else if (!strcmp(icontail, "UNDERLINE"))
            stockid = GTK_STOCK_UNDERLINE;
        else if (!strcmp(icontail, "UNDO"))
            stockid = GTK_STOCK_UNDO;
        else if (!strcmp(icontail, "UNINDENT"))
            stockid = GTK_STOCK_UNINDENT;
        else if (!strcmp(icontail, "YES"))
            stockid = GTK_STOCK_YES;
        else if (!strcmp(icontail, "ZOOM_100"))
            stockid = GTK_STOCK_ZOOM_100;
        else if (!strcmp(icontail, "ZOOM_FIT"))
            stockid = GTK_STOCK_ZOOM_FIT;
        else if (!strcmp(icontail, "ZOOM_IN"))
            stockid = GTK_STOCK_ZOOM_IN;
        else if (!strcmp(icontail, "ZOOM_OUT"))
            stockid = GTK_STOCK_ZOOM_OUT;
        else if (!strcmp(icontail, "DIALOG_AUTHENTICATION"))
            stockid = GTK_STOCK_DIALOG_AUTHENTICATION;
        else
            stockid = NULL;
        return stockid;
    }
#endif
    return NULL;
}

GtkWidget* xset_get_image(const char* icon, int icon_size)
{
    /*
        GTK_ICON_SIZE_MENU,
        GTK_ICON_SIZE_SMALL_TOOLBAR,
        GTK_ICON_SIZE_LARGE_TOOLBAR,
        GTK_ICON_SIZE_BUTTON,
        GTK_ICON_SIZE_DND,
        GTK_ICON_SIZE_DIALOG
    */
    GtkWidget* image = NULL;
    const char* stockid;

    if (!(icon && icon[0]))
        return NULL;
    if (!icon_size)
        icon_size = GTK_ICON_SIZE_MENU;

    if ((stockid = icon_stock_to_id(icon)))
        image = gtk_image_new_from_stock(stockid, icon_size);
    else if (icon[0] == '/')
    {
        // icon is full path to image file
        // get real icon size from gtk icon size
        int icon_w, icon_h;
        gtk_icon_size_lookup(icon_size, &icon_w, &icon_h);
        int real_icon_size = icon_w > icon_h ? icon_w : icon_h;
        GtkIconTheme* icon_theme = gtk_icon_theme_get_default();
        GdkPixbuf* pixbuf = vfs_load_icon(icon_theme, icon, real_icon_size);
        if (pixbuf)
        {
            image = gtk_image_new_from_pixbuf(pixbuf);
            g_object_unref(pixbuf);
        }
        else
            image = gtk_image_new_from_file(icon);
    }
    else
        image = gtk_image_new_from_icon_name(icon, icon_size);
    return image;
}

void xset_add_menu(PtkFileBrowser* file_browser, GtkWidget* menu, GtkAccelGroup* accel_group, char* elements)
{
    if (!elements)
        return;

    while (elements[0] == ' ')
        elements++;

    while (elements && elements[0] != '\0')
    {
        char* space = strchr(elements, ' ');
        if (space)
            space[0] = '\0';
        XSet* set = xset_get(elements);
        if (space)
            space[0] = ' ';
        elements = space;
        xset_add_menuitem(file_browser, menu, accel_group, set);
        if (elements)
        {
            while (elements[0] == ' ')
                elements++;
        }
    }
}

GtkWidget* xset_new_menuitem(const char* label, const char* icon)
{
    GtkWidget* item;
    if (label && strstr(label, "\\_"))
    {
        // allow escape of underscore
        char* str = clean_label(label, FALSE, FALSE);
#if (GTK_MAJOR_VERSION == 3)
        item = gtk_menu_item_new_with_label(str);
#elif (GTK_MAJOR_VERSION == 2)
        item = gtk_image_menu_item_new_with_label(str);
#endif
        g_free(str);
    }
    else
#if (GTK_MAJOR_VERSION == 3)
        item = gtk_menu_item_new_with_mnemonic(label);
#elif (GTK_MAJOR_VERSION == 2)
        item = gtk_image_menu_item_new_with_mnemonic(label);
#endif
    return item;
}

char* xset_custom_get_app_name_icon(XSet* set, GdkPixbuf** icon, int icon_size)
{
    char* menu_label = NULL;
    GdkPixbuf* icon_new = NULL;
    GtkIconTheme* icon_theme = gtk_icon_theme_get_default();

    if (!set->lock && xset_get_int_set(set, "x") == XSET_CMD_APP)
    {
        VFSAppDesktop* app = NULL;
        if (set->z && g_str_has_suffix(set->z, ".desktop") && (app = vfs_app_desktop_new(set->z)))
        {
            if (!(set->menu_label && set->menu_label[0]))
                menu_label = g_strdup(vfs_app_desktop_get_disp_name(app));
            if (set->icon)
                icon_new = vfs_load_icon(icon_theme, set->icon, icon_size);
            if (!icon_new)
                icon_new = vfs_app_desktop_get_icon(app, icon_size, TRUE);
            if (app)
                vfs_app_desktop_unref(app);
        }
        else
        {
            // not a desktop file - probably executable
            if (set->icon)
                icon_new = vfs_load_icon(icon_theme, set->icon, icon_size);
            if (!icon_new && set->z)
            {
                // guess icon name from executable name
                char* name = g_path_get_basename(set->z);
                if (name && name[0])
                    icon_new = vfs_load_icon(icon_theme, name, icon_size);
                g_free(name);
            }
        }

        if (!icon_new)
        {
            // fallback
            icon_new = vfs_load_icon(icon_theme, "gtk-execute", icon_size);
        }
    }
    else
        g_warning("xset_custom_get_app_name_icon set is not XSET_CMD_APP");

    if (icon)
        *icon = icon_new;
    else if (icon_new)
        g_object_unref(icon_new);

    if (!menu_label)
    {
        menu_label = set->menu_label && set->menu_label[0] ? g_strdup(set->menu_label) : g_strdup(set->z);
        if (!menu_label)
            menu_label = g_strdup("Application");
    }
    return menu_label;
}

GtkWidget* xset_add_menuitem(PtkFileBrowser* file_browser, GtkWidget* menu, GtkAccelGroup* accel_group, XSet* set)
{
    GtkWidget* item = NULL;
    XSet* set_next;
    char* icon_name = NULL;
    char* context = NULL;
    int context_action = CONTEXT_SHOW;
    char* icon_file = NULL;
    // g_printf("xset_add_menuitem %s\n", set->name );

    // plugin?
    XSet* mset = xset_get_plugin_mirror(set);
    if (set->plugin && set->shared_key)
    {
        icon_name = mset->icon;
        context = mset->context;
    }
    if (!icon_name)
        icon_name = set->icon;
    if (!icon_name)
    {
        if (set->plugin)
            icon_file = g_build_filename(set->plug_dir, set->plug_name, "icon", NULL);
        else
            icon_file = g_build_filename(settings_config_dir, "scripts", set->name, "icon", NULL);
        if (!g_file_test(icon_file, G_FILE_TEST_EXISTS))
        {
            g_free(icon_file);
            icon_file = NULL;
        }
        else
            icon_name = icon_file;
    }
    if (!context)
        context = set->context;

    // context?
    if (context && !set->tool && xset_context && xset_context->valid && !xset_get_b("context_dlg"))
        context_action = xset_context_test(xset_context, context, set->disable);

    if (context_action != CONTEXT_HIDE)
    {
        if (set->tool && set->menu_style != XSET_MENU_SUBMENU)
        {
            // item = xset_new_menuitem( set->menu_label, icon_name );
        }
        else if (set->menu_style)
        {
            if (set->menu_style == XSET_MENU_CHECK &&
                !(!set->lock && (xset_get_int_set(set, "x") > XSET_CMD_SCRIPT))) // app or book
            {
                item = gtk_check_menu_item_new_with_mnemonic(set->menu_label);
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), mset->b == XSET_B_TRUE);
            }
            else if (set->menu_style == XSET_MENU_RADIO)
            {
                XSet* set_radio;
                if (set->ob2_data)
                    set_radio = (XSet*)set->ob2_data;
                else
                    set_radio = set;
                item = gtk_radio_menu_item_new_with_mnemonic((GSList*)set_radio->ob2_data, set->menu_label);
                set_radio->ob2_data = (gpointer)gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), mset->b == XSET_B_TRUE);
            }
            else if (set->menu_style == XSET_MENU_SUBMENU)
            {
                GtkWidget* submenu = gtk_menu_new();
                item = xset_new_menuitem(set->menu_label, icon_name);
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
                g_signal_connect(submenu, "key-press-event", G_CALLBACK(xset_menu_keypress), NULL);
                if (set->lock)
                    xset_add_menu(file_browser, submenu, accel_group, set->desc);
                else if (set->child)
                {
                    set_next = xset_get(set->child);
                    xset_add_menuitem(file_browser, submenu, accel_group, set_next);
                    GList* l = gtk_container_get_children(GTK_CONTAINER(submenu));
                    if (l)
                        g_list_free(l);
                    else
                    {
                        // Nothing was added to the menu (all items likely have
                        // invisible context) so destroy (hide) - issue #215
                        gtk_widget_destroy(item);
                        goto _next_item;
                    }
                }
            }
            else if (set->menu_style == XSET_MENU_SEP)
            {
                item = gtk_separator_menu_item_new();
            }
        }
        if (!item)
        {
            // get menu icon size
            int icon_w;
            int icon_h;
            gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &icon_w, &icon_h);
            int icon_size = icon_w > icon_h ? icon_w : icon_h;

            GdkPixbuf* app_icon = NULL;
            int cmd_type = xset_get_int_set(set, "x");
            if (!set->lock && cmd_type == XSET_CMD_APP)
            {
                // Application
                char* menu_label = xset_custom_get_app_name_icon(set, &app_icon, icon_size);
                item = xset_new_menuitem(menu_label, NULL);
                g_free(menu_label);
            }
            else if (!set->lock && cmd_type == XSET_CMD_BOOKMARK)
            {
                // Bookmark
                item = xset_new_menuitem(set->menu_label && set->menu_label[0] ? set->menu_label : set->z, NULL);
            }
            else
                item = xset_new_menuitem(set->menu_label, icon_name);
        }

        set->browser = file_browser;
        g_object_set_data(G_OBJECT(item), "menu", menu);
        g_object_set_data(G_OBJECT(item), "set", set);

        if (set->ob1)
            g_object_set_data(G_OBJECT(item), set->ob1, set->ob1_data);
        if (set->menu_style != XSET_MENU_RADIO && set->ob2)
            g_object_set_data(G_OBJECT(item), set->ob2, set->ob2_data);

        if (set->menu_style < XSET_MENU_SUBMENU)
        {
            // activate callback
            if (!set->cb_func || set->menu_style)
            {
                // use xset menu callback
                // if ( !design_mode )
                //{
                g_signal_connect(item, "activate", G_CALLBACK(xset_menu_cb), set);
                g_object_set_data(G_OBJECT(item), "cb_func", set->cb_func);
                g_object_set_data(G_OBJECT(item), "cb_data", set->cb_data);
                //}
            }
            else if (set->cb_func)
            {
                // use custom callback directly
                // if ( !design_mode )
                g_signal_connect(item, "activate", G_CALLBACK(set->cb_func), set->cb_data);
            }

            // key accel
            XSet* keyset;
            if (set->shared_key)
                keyset = xset_get(set->shared_key);
            else
                keyset = set;
            if (keyset->key > 0 && accel_group)
                gtk_widget_add_accelerator(item,
                                           "activate",
                                           accel_group,
                                           keyset->key,
                                           keyset->keymod,
                                           GTK_ACCEL_VISIBLE);
        }
        // design mode callback
        g_signal_connect(item, "button-press-event", G_CALLBACK(xset_design_cb), set);
        g_signal_connect(item, "button-release-event", G_CALLBACK(xset_design_cb), set);

        gtk_widget_set_sensitive(item, context_action != CONTEXT_DISABLE && !set->disable);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

_next_item:
    if (icon_file)
        g_free(icon_file);

    // next item
    if (set->next)
    {
        set_next = xset_get(set->next);
        xset_add_menuitem(file_browser, menu, accel_group, set_next);
    }
    return item;
}

char* xset_custom_get_script(XSet* set, gboolean create)
{
    char* path;

    if ((strncmp(set->name, "cstm_", 5) && strncmp(set->name, "cust", 4) && strncmp(set->name, "hand", 4)) ||
        (create && set->plugin))
        return NULL;

    if (create)
    {
        path = g_build_filename(settings_config_dir, "scripts", set->name, NULL);
        if (!g_file_test(path, G_FILE_TEST_EXISTS))
        {
            g_mkdir_with_parents(path, 0700);
        }
        g_free(path);
    }

    if (set->plugin)
    {
        path = g_build_filename(set->plug_dir, set->plug_name, "exec.sh", NULL);
    }
    else
    {
        path = g_build_filename(settings_config_dir, "scripts", set->name, "exec.sh", NULL);
    }

    if (create && !g_file_test(path, G_FILE_TEST_EXISTS))
    {
        FILE* file;
        file = fopen(path, "w");

        if (file)
        {
            // write default script
            // head
            fputs(g_strdup_printf("#!%s\n%s\n\n#import file manager variables\n$fm_import\n\n#For all "
                                  "spacefm variables see man page: spacefm-scripts\n\n#Start script\n",
                                  BASHPATH,
                                  SHELL_SETTINGS),
                  file);
            int i;
            for (i = 0; i < 14; i++)
                fputs("\n", file);
            // tail
            fputs("#End script\nexit $?", file);
            fclose(file);
        }
        chmod(path, 0700);
    }
    return path;
}

char* xset_custom_get_help(GtkWidget* parent, XSet* set)
{
    char* dir;

    if (!set || strncmp(set->name, "cstm_", 5))
        return NULL;

    if (set->plugin)
        dir = g_build_filename(set->plug_dir, set->plug_name, NULL);
    else
    {
        dir = g_build_filename(settings_config_dir, "scripts", set->name, NULL);
        if (!g_file_test(dir, G_FILE_TEST_EXISTS))
        {
            g_mkdir_with_parents(dir, 0700);
        }
    }

    const char* readme = "README";
    char* path = g_build_filename(dir, readme, NULL);

    if (!g_file_test(path, G_FILE_TEST_EXISTS))
    {
        if (set->plugin)
        {
            xset_msg_dialog(parent,
                            0,
                            _("Help Not Available"),
                            NULL,
                            0,
                            _("This plugin does not include a README file."),
                            NULL,
                            NULL);
            g_free(dir);
            return NULL;
        }
        else if (xset_msg_dialog(parent,
                                 GTK_MESSAGE_QUESTION,
                                 _("Create README"),
                                 NULL,
                                 GTK_BUTTONS_YES_NO,
                                 _("No README file exists for this command.\n\n"
                                   "Create a default README file for you to fill in?"),
                                 NULL,
                                 NULL) != GTK_RESPONSE_YES)
        {
            g_free(dir);
            return NULL;
        }

        // create
        path = g_build_filename(dir, readme, NULL);
        FILE* file = fopen(path, "w");
        if (file)
        {
            // write default readme
            fputs("README\n------\n\nFill this text file with detailed information about this command. "
                  "For\ncontext-sensitive help within SpaceFM, this file must be named README\n\nIf you plan to "
                  "distribute this command as a plugin, the following information\nis recommended:\n\n\nCommand "
                  "Name:\n\nRelease Version and Date:\n\nPlugin Homepage or Download Link:\n\nAuthor's Contact "
                  "Information or Feedback Instructions:\n\nDependencies or "
                  "Requirements:\n\nDescription:\n\nInstructions For Use:\n\nCopyright and License Information:\n\n",
                  file);
            fclose(file);
        }
        chmod(path, 0600);
    }

    g_free(dir);
    if (g_file_test(path, G_FILE_TEST_EXISTS))
        return path;
    g_free(path);
    xset_msg_dialog(parent,
                    0,
                    _("Creation Failed"),
                    NULL,
                    0,
                    _("An error occured creating a README file for this command."),
                    NULL,
                    NULL);
    return NULL;
}

char* xset_custom_new_name()
{
    char* setname;

    do
    {
        char* hex8 = NULL;
        if (hex8)
            g_free(hex8);
        hex8 = randhex8();
        setname = g_strdup_printf("cstm_%s", hex8);
        if (xset_is(setname))
        {
            g_free(setname);
            setname = NULL;
        }
        else
        {
            char* path1 = g_build_filename(settings_config_dir, "scripts", setname, NULL);
            char* path2 = g_build_filename(settings_config_dir, "plugin-data", setname, NULL);
            if (g_file_test(path1, G_FILE_TEST_EXISTS) || g_file_test(path2, G_FILE_TEST_EXISTS))
            {
                g_free(setname);
                setname = NULL;
            }
            g_free(path1);
            g_free(path2);
        }
    } while (!setname);
    return setname;
}

void xset_custom_copy_files(XSet* src, XSet* dest)
{
    char* path_src;
    char* path_dest;
    char* command = NULL;
    char* stdout = NULL;
    char* stderr = NULL;
    char* msg;
    gboolean ret;
    int exit_status;

    // g_printf("xset_custom_copy_files( %s, %s )\n", src->name, dest->name );

    // copy command dir

    if (src->plugin)
        path_src = g_build_filename(src->plug_dir, src->plug_name, NULL);
    else
        path_src = g_build_filename(settings_config_dir, "scripts", src->name, NULL);
    // g_printf("    path_src=%s\n", path_src );

    // g_printf("    path_src EXISTS\n");
    path_dest = g_build_filename(settings_config_dir, "scripts", NULL);
    g_mkdir_with_parents(path_dest, 0700);
    g_free(path_dest);
    path_dest = g_build_filename(settings_config_dir, "scripts", dest->name, NULL);
    command = g_strdup_printf("cp -a %s %s", path_src, path_dest);

    g_free(path_src);

    if (command)
    {
        // g_printf("    path_dest=%s\n", path_dest );
        print_command(command);
        ret = g_spawn_command_line_sync(command, &stdout, &stderr, &exit_status, NULL);
        g_free(command);
        g_printf("%s%s", stdout, stderr);

        if (!ret || (exit_status && WIFEXITED(exit_status)))
        {
            msg = g_strdup_printf(_("An error occured copying command files\n\n%s"), stderr ? stderr : "");
            xset_msg_dialog(NULL, GTK_MESSAGE_ERROR, _("Copy Command Error"), NULL, 0, msg, NULL, NULL);
            g_free(msg);
        }
        if (stderr)
            g_free(stderr);
        if (stdout)
            g_free(stdout);
        stderr = stdout = NULL;
        command = g_strdup_printf("chmod -R go-rwx %s", path_dest);
        print_command(command);
        g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
        g_free(command);
        g_free(path_dest);
    }

    // copy data dir
    XSet* mset = xset_get_plugin_mirror(src);
    path_src = g_build_filename(settings_config_dir, "plugin-data", mset->name, NULL);
    if (g_file_test(path_src, G_FILE_TEST_IS_DIR))
    {
        path_dest = g_build_filename(settings_config_dir, "plugin-data", dest->name, NULL);
        command = g_strdup_printf("cp -a %s %s", path_src, path_dest);
        g_free(path_src);
        stderr = stdout = NULL;
        print_command(command);
        ret = g_spawn_command_line_sync(command, &stdout, &stderr, &exit_status, NULL);
        g_free(command);
        g_printf("%s%s", stdout, stderr);
        if (!ret || (exit_status && WIFEXITED(exit_status)))
        {
            msg = g_strdup_printf(_("An error occured copying command data files\n\n%s"), stderr ? stderr : "");
            xset_msg_dialog(NULL, GTK_MESSAGE_ERROR, _("Copy Command Error"), NULL, 0, msg, NULL, NULL);
            g_free(msg);
        }
        if (stderr)
            g_free(stderr);
        if (stdout)
            g_free(stdout);
        stderr = stdout = NULL;
        command = g_strdup_printf("chmod -R go-rwx %s", path_dest);
        g_free(path_dest);
        print_command(command);
        g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
        g_free(command);
    }
}

XSet* xset_custom_copy(XSet* set, gboolean copy_next, gboolean delete_set)
{
    // g_printf("\nxset_custom_copy( %s, %s, %s)\n", set->name, copy_next ? "TRUE" : "FALSE", delete_set ? "TRUE" :
    // "FALSE" );
    XSet* mset = set;
    // if a plugin with a mirror, get the mirror
    if (set->plugin && set->shared_key)
        mset = xset_get_plugin_mirror(set);

    XSet* newset = xset_custom_new();
    newset->menu_label = g_strdup(set->menu_label);
    newset->s = g_strdup(set->s);
    newset->x = g_strdup(set->x);
    newset->y = g_strdup(set->y);
    newset->z = g_strdup(set->z);
    newset->desc = g_strdup(set->desc);
    newset->title = g_strdup(set->title);
    newset->b = set->b;
    newset->menu_style = set->menu_style;
    newset->context = g_strdup(mset->context);
    newset->line = g_strdup(set->line);

    newset->task = mset->task;
    newset->task_pop = mset->task_pop;
    newset->task_err = mset->task_err;
    newset->task_out = mset->task_out;
    newset->in_terminal = mset->in_terminal;
    newset->keep_terminal = mset->keep_terminal;
    newset->scroll_lock = mset->scroll_lock;

    if (!mset->icon && set->plugin)
        newset->icon = g_strdup(set->icon);
    else
        newset->icon = g_strdup(mset->icon);

    xset_custom_copy_files(set, newset);
    newset->tool = set->tool;

    if (set->menu_style == XSET_MENU_SUBMENU && set->child)
    {
        XSet* set_child = xset_get(set->child);
        // g_printf("    copy submenu %s\n", set_child->name );
        XSet* newchild = xset_custom_copy(set_child, TRUE, delete_set);
        newset->child = g_strdup(newchild->name);
        newchild->parent = g_strdup(newset->name);
    }

    if (copy_next && set->next)
    {
        XSet* set_next = xset_get(set->next);
        // g_printf("    copy next %s\n", set_next->name );
        XSet* newnext = xset_custom_copy(set_next, TRUE, delete_set);
        newnext->prev = g_strdup(newset->name);
        newset->next = g_strdup(newnext->name);
    }

    // when copying imported plugin file, discard mirror xset
    if (delete_set)
        xset_custom_delete(set, FALSE);

    return newset;
}

void clean_plugin_mirrors()
{ // remove plugin mirrors for non-existent plugins
    gboolean redo = TRUE;

    while (redo)
    {
        redo = FALSE;
        GList* l;
        for (l = xsets; l; l = l->next)
        {
            if (((XSet*)l->data)->desc && !strcmp(((XSet*)l->data)->desc, "@plugin@mirror@"))
            {
                XSet* set = (XSet*)l->data;
                if (!set->shared_key || !xset_is(set->shared_key))
                {
                    xset_free(set);
                    redo = TRUE;
                    break;
                }
            }
        }
    }

    // remove plugin-data for non-existent xsets
    const char* name;
    char* command;
    char* stdout;
    char* stderr;
    GDir* dir;
    char* path = g_build_filename(settings_config_dir, "plugin-data", NULL);
_redo:
    dir = g_dir_open(path, 0, NULL);
    if (dir)
    {
        while ((name = g_dir_read_name(dir)))
        {
            if (strlen(name) == 13 && g_str_has_prefix(name, "cstm_") && !xset_is(name))
            {
                g_dir_close(dir);
                command = g_strdup_printf("rm -rf %s/%s", path, name);
                stderr = stdout = NULL;
                print_command(command);
                g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
                g_free(command);
                if (stderr)
                    g_free(stderr);
                if (stdout)
                    g_free(stdout);
                goto _redo;
            }
        }
        g_dir_close(dir);
    }
    g_free(path);
}

void xset_set_plugin_mirror(XSet* pset)
{
    GList* l;
    for (l = xsets; l; l = l->next)
    {
        if (((XSet*)l->data)->desc && !strcmp(((XSet*)l->data)->desc, "@plugin@mirror@"))
        {
            XSet* set = (XSet*)l->data;
            if (set->parent && set->child)
            {
                if (!strcmp(set->child, pset->plug_name) && !strcmp(set->parent, pset->plug_dir))
                {
                    if (set->shared_key)
                        g_free(set->shared_key);
                    set->shared_key = g_strdup(pset->name);
                    if (pset->shared_key)
                        g_free(pset->shared_key);
                    pset->shared_key = g_strdup(set->name);
                    return;
                }
            }
        }
    }
}

XSet* xset_get_plugin_mirror(XSet* set)
{
    // plugin mirrors are custom xsets that save the user's key, icon
    // and run prefs for the plugin, if any
    if (!set->plugin)
        return set;
    if (set->shared_key)
        return xset_get(set->shared_key);

    XSet* newset = xset_custom_new();
    newset->desc = g_strdup("@plugin@mirror@");
    newset->parent = g_strdup(set->plug_dir);
    newset->child = g_strdup(set->plug_name);
    newset->shared_key = g_strdup(set->name); // this will not be saved
    newset->task = set->task;
    newset->task_pop = set->task_pop;
    newset->task_err = set->task_err;
    newset->task_out = set->task_out;
    newset->in_terminal = set->in_terminal;
    newset->keep_terminal = set->keep_terminal;
    newset->scroll_lock = set->scroll_lock;
    newset->context = g_strdup(set->context);
    newset->opener = set->opener;
    newset->b = set->b;
    newset->s = g_strdup(set->s);
    set->shared_key = g_strdup(newset->name);
    return newset;
}

int compare_plugin_sets(XSet* a, XSet* b)
{
    return g_utf8_collate(a->menu_label, b->menu_label);
}

GList* xset_get_plugins(gboolean included)
{ // return list of plugin sets (included or not ) sorted by menu_label
    GList* plugins = NULL;

    GList* l;
    for (l = xsets; l; l = l->next)
    {
        if (((XSet*)l->data)->plugin && ((XSet*)l->data)->plugin_top && ((XSet*)l->data)->plug_dir)
        {
            XSet* set = (XSet*)l->data;
            if (strstr(set->plug_dir, "/included/"))
            {
                if (included)
                    plugins = g_list_prepend(plugins, l->data);
            }
            else if (!included)
                plugins = g_list_prepend(plugins, l->data);
        }
    }
    plugins = g_list_sort(plugins, (GCompareFunc)compare_plugin_sets);
    return plugins;
}

XSet* xset_get_by_plug_name(const char* plug_dir, const char* plug_name)
{
    if (!plug_name)
        return NULL;

    GList* l;
    for (l = xsets; l; l = l->next)
    {
        if (((XSet*)l->data)->plugin && !strcmp(plug_name, ((XSet*)l->data)->plug_name) &&
            !strcmp(plug_dir, ((XSet*)l->data)->plug_dir))
            return l->data;
    }

    // add new
    XSet* set = xset_new(xset_custom_new_name());
    set->plug_dir = g_strdup(plug_dir);
    set->plug_name = g_strdup(plug_name);
    set->plugin = TRUE;
    set->lock = FALSE;
    xsets = g_list_append(xsets, set);
    return set;
}

void xset_parse_plugin(const char* plug_dir, char* line, int use)
{
    char* sep = strchr(line, '=');
    char* name;
    char* value;
    const char* prefix;

    if (!sep)
        return;
    name = line;
    value = sep + 1;
    *sep = '\0';
    sep = strchr(name, '-');
    if (!sep)
        return;
    char* var = sep + 1;
    *sep = '\0';

    if (use < PLUGIN_USE_BOOKMARKS)
    {
        // handler
        const char* handler_prefix[] = {"hand_arc_", "hand_fs_", "hand_net_", "hand_f_"};
        prefix = handler_prefix[use];
    }
    else
        prefix = "cstm_";

    if (g_str_has_prefix(name, prefix))
    {
        XSet* set = xset_get_by_plug_name(plug_dir, name);
        xset_set_set(set, var, value);

        if (use >= PLUGIN_USE_BOOKMARKS)
        {
            // map plug names to new set names (does not apply to handlers)
            XSet* set2;
            if (set->prev && !strcmp(var, "prev"))
            {
                if (!strncmp(set->prev, "cstm_", 5))
                {
                    set2 = xset_get_by_plug_name(plug_dir, set->prev);
                    g_free(set->prev);
                    set->prev = g_strdup(set2->name);
                }
                else
                {
                    g_free(set->prev);
                    set->prev = NULL;
                }
            }
            else if (set->next && !strcmp(var, "next"))
            {
                if (!strncmp(set->next, "cstm_", 5))
                {
                    set2 = xset_get_by_plug_name(plug_dir, set->next);
                    g_free(set->next);
                    set->next = g_strdup(set2->name);
                }
                else
                {
                    g_free(set->next);
                    set->next = NULL;
                }
            }
            else if (set->parent && !strcmp(var, "parent"))
            {
                if (!strncmp(set->parent, "cstm_", 5))
                {
                    set2 = xset_get_by_plug_name(plug_dir, set->parent);
                    g_free(set->parent);
                    set->parent = g_strdup(set2->name);
                }
                else
                {
                    g_free(set->parent);
                    set->parent = NULL;
                }
            }
            else if (set->child && !strcmp(var, "child"))
            {
                if (!strncmp(set->child, "cstm_", 5))
                {
                    set2 = xset_get_by_plug_name(plug_dir, set->child);
                    g_free(set->child);
                    set->child = g_strdup(set2->name);
                }
                else
                {
                    g_free(set->child);
                    set->child = NULL;
                }
            }
        }
    }
}

XSet* xset_import_plugin(const char* plug_dir, int* use)
{
    char line[2048];
    GList* l;

    if (use)
        *use = PLUGIN_USE_NORMAL;

    // clear all existing plugin sets with this plug_dir
    // ( keep the mirrors to retain user prefs )
    gboolean redo = TRUE;
    while (redo)
    {
        redo = FALSE;
        for (l = xsets; l; l = l->next)
        {
            if (((XSet*)l->data)->plugin && !strcmp(plug_dir, ((XSet*)l->data)->plug_dir))
            {
                xset_free((XSet*)l->data);
                redo = TRUE; // search list from start again due to changed list
                break;
            }
        }
    }

    // read plugin file into xsets
    gboolean plugin_good = FALSE;
    char* plugin = g_build_filename(plug_dir, "plugin", NULL);
    FILE* file = fopen(plugin, "r");
    if (!file)
    {
        g_warning(_("Error reading plugin file %s"), plugin);
        return NULL;
    }

    while (fgets(line, sizeof(line), file))
    {
        gboolean func = TRUE;
        strtok(line, "\r\n");
        if (!line[0])
            continue;
        if (line[0] == '[')
        {
            strtok(line, "]");
            if (0 == strcmp(line + 1, "Plugin"))
                func = TRUE;
            else
                func = FALSE;
            continue;
        }
        if (func)
        {
            if (use && *use == PLUGIN_USE_NORMAL)
            {
                if (g_str_has_prefix(line, "main_book-child="))
                {
                    // This plugin is an export of all bookmarks
                    *use = PLUGIN_USE_BOOKMARKS;
                }
                else if (g_str_has_prefix(line, "hand_"))
                {
                    if (g_str_has_prefix(line, "hand_fs_"))
                        *use = PLUGIN_USE_HAND_FS;
                    else if (g_str_has_prefix(line, "hand_arc_"))
                        *use = PLUGIN_USE_HAND_ARC;
                    else if (g_str_has_prefix(line, "hand_net_"))
                        *use = PLUGIN_USE_HAND_NET;
                    else if (g_str_has_prefix(line, "hand_f_"))
                        *use = PLUGIN_USE_HAND_FILE;
                }
            }
            xset_parse_plugin(plug_dir, line, use ? *use : PLUGIN_USE_NORMAL);
            if (!plugin_good)
                plugin_good = TRUE;
        }
    }
    fclose(file);

    // clean plugin sets, set type
    gboolean top = TRUE;
    XSet* rset = NULL;
    for (l = xsets; l; l = l->next)
    {
        if (((XSet*)l->data)->plugin && !strcmp(plug_dir, ((XSet*)l->data)->plug_dir))
        {
            XSet* set = (XSet*)l->data;
            set->key = set->keymod = set->tool = set->opener = 0;
            xset_set_plugin_mirror(set);
            if ((set->plugin_top = top))
            {
                top = FALSE;
                rset = set;
            }
        }
    }
    return plugin_good ? rset : NULL;
}

typedef struct _PluginData
{
    FMMainWindow* main_window;
    GtkWidget* handler_dlg;
    char* plug_dir;
    XSet* set;
    int job;
} PluginData;

void on_install_plugin_cb(VFSFileTask* task, PluginData* plugin_data)
{
    // g_printf("on_install_plugin_cb\n");
    if (plugin_data->job == PLUGIN_JOB_REMOVE) // uninstall
    {
        if (!g_file_test(plugin_data->plug_dir, G_FILE_TEST_EXISTS))
        {
            xset_custom_delete(plugin_data->set, FALSE);
            clean_plugin_mirrors();
            // main_window_on_plugins_change( NULL );
        }
    }
    else
    {
        char* plugin = g_build_filename(plugin_data->plug_dir, "plugin", NULL);
        if (g_file_test(plugin, G_FILE_TEST_EXISTS))
        {
            char* msg;
            int use = PLUGIN_USE_NORMAL;
            XSet* set = xset_import_plugin(plugin_data->plug_dir, &use);
            if (!set)
            {
                msg = g_strdup_printf(_("The imported plugin folder does not contain a valid plugin.\n\n(%s/)"),
                                      plugin_data->plug_dir);
                xset_msg_dialog(GTK_WIDGET(plugin_data->main_window),
                                GTK_MESSAGE_ERROR,
                                _("Invalid Plugin"),
                                NULL,
                                0,
                                msg,
                                NULL,
                                NULL);
                g_free(msg);
            }
            else if (use == PLUGIN_USE_BOOKMARKS)
            {
                // bookmarks
                if (plugin_data->job != PLUGIN_JOB_COPY || !plugin_data->set)
                {
                    // This dialog should never be seen - failsafe
                    GDK_THREADS_ENTER(); // due to dialog run causes low level thread lock
                    xset_msg_dialog(GTK_WIDGET(plugin_data->main_window),
                                    GTK_MESSAGE_ERROR,
                                    "Bookmarks",
                                    NULL,
                                    0,
                                    "This plugin file contains exported bookmarks which cannot be installed or "
                                    "imported to the design clipboard.\n\nYou can import these directly into a menu "
                                    "(select New|Import from the Design Menu).",
                                    NULL,
                                    NULL);
                    GDK_THREADS_LEAVE();
                }
                else
                {
                    // copy all bookmarks into menu
                    // paste after insert_set (plugin_data->set)
                    XSet* newset = xset_custom_copy(set, TRUE, TRUE);
                    // get last bookmark and toolbar if needed
                    set = newset;
                    do
                    {
                        if (plugin_data->set->tool)
                            set->tool = XSET_TOOL_CUSTOM;
                        else
                            set->tool = XSET_TOOL_NOT;
                        if (!set->next)
                            break;
                    } while ((set = xset_get(set->next)));
                    // set now points to last bookmark
                    newset->prev = g_strdup(plugin_data->set->name);
                    set->next = plugin_data->set->next; // steal
                    if (plugin_data->set->next)
                    {
                        XSet* set_next = xset_get(plugin_data->set->next);
                        g_free(set_next->prev);
                        set_next->prev = g_strdup(set->name); // last bookmark
                    }
                    plugin_data->set->next = g_strdup(newset->name);
                    // find parent
                    set = newset;
                    while (set->prev)
                        set = xset_get(set->prev);
                    if (set->parent)
                        main_window_bookmark_changed(set->parent);
                }
            }
            else if (use < PLUGIN_USE_BOOKMARKS)
            {
                // handler
                set->plugin_top = FALSE; // prevent being added to Plugins menu
                if (plugin_data->job == PLUGIN_JOB_COPY)
                    ptk_handler_import(use, plugin_data->handler_dlg, set);
            }
            else if (plugin_data->job == PLUGIN_JOB_COPY)
            {
                // copy
                set->plugin_top = FALSE; // don't show tmp plugin in Plugins menu
                if (plugin_data->set)
                {
                    // paste after insert_set (plugin_data->set)
                    XSet* newset = xset_custom_copy(set, FALSE, TRUE);
                    newset->prev = g_strdup(plugin_data->set->name);
                    newset->next = plugin_data->set->next; // steal
                    if (plugin_data->set->next)
                    {
                        XSet* set_next = xset_get(plugin_data->set->next);
                        g_free(set_next->prev);
                        set_next->prev = g_strdup(newset->name);
                    }
                    plugin_data->set->next = g_strdup(newset->name);
                    if (plugin_data->set->tool)
                        newset->tool = XSET_TOOL_CUSTOM;
                    else
                        newset->tool = XSET_TOOL_NOT;
                    main_window_bookmark_changed(newset->name);
                }
                else
                {
                    // place on design clipboard
                    set_clipboard = set;
                    clipboard_is_cut = FALSE;
                    if (xset_get_b("plug_cverb") || plugin_data->handler_dlg)
                    {
                        char* label = clean_label(set->menu_label, FALSE, FALSE);
                        if (geteuid() == 0)
                            msg = g_strdup_printf(_("The '%s' plugin has been copied to the design clipboard.  Use "
                                                    "View|Design Mode to paste it into a menu.\n\nBecause it has not "
                                                    "been installed, this plugin will not appear in the Plugins menu."),
                                                  label);
                        else
                            msg = g_strdup_printf(
                                _("The '%s' plugin has been copied to the design clipboard.  Use View|Design Mode to "
                                  "paste it into a menu.\n\nBecause it has not been installed, this plugin will not "
                                  "appear in the Plugins menu, and its contents are not protected by root (once pasted "
                                  "it will be saved with normal ownership).\n\nIf this plugin contains su commands or "
                                  "will be run as root, installing it to and running it only from the Plugins menu is "
                                  "recommended to improve your system security."),
                                label);
                        g_free(label);
                        GDK_THREADS_ENTER(); // due to dialog run causes low level thread lock
                        xset_msg_dialog(GTK_WIDGET(plugin_data->main_window),
                                        0,
                                        "Copy Plugin",
                                        NULL,
                                        0,
                                        msg,
                                        NULL,
                                        NULL);
                        GDK_THREADS_LEAVE();
                        g_free(msg);
                    }
                }
            }
            clean_plugin_mirrors();
        }
        g_free(plugin);
    }
    g_free(plugin_data->plug_dir);
    g_slice_free(PluginData, plugin_data);
}

void xset_remove_plugin(GtkWidget* parent, PtkFileBrowser* file_browser, XSet* set)
{
    if (!file_browser || !set || !set->plugin_top || !set->plug_dir)
        return;

    if (strstr(set->plug_dir, "/included/"))
        return; // failsafe - don't allow removal of included

    if (!app_settings.no_confirm)
    {
        char* label = clean_label(set->menu_label, FALSE, FALSE);
        char* msg = g_strdup_printf(_("Uninstall the '%s' plugin?\n\n( %s )"), label, set->plug_dir);
        g_free(label);
        if (xset_msg_dialog(parent,
                            GTK_MESSAGE_WARNING,
                            _("Uninstall Plugin"),
                            NULL,
                            GTK_BUTTONS_YES_NO,
                            msg,
                            NULL,
                            NULL) != GTK_RESPONSE_YES)
        {
            g_free(msg);
            return;
        }
        g_free(msg);
    }
    PtkFileTask* task = ptk_file_exec_new(_("Uninstall Plugin"), NULL, parent, file_browser->task_view);

    char* plug_dir_q = bash_quote(set->plug_dir);

    task->task->exec_command = g_strdup_printf("rm -rf %s", plug_dir_q);
    g_free(plug_dir_q);
    task->task->exec_sync = TRUE;
    task->task->exec_popup = FALSE;
    task->task->exec_show_output = FALSE;
    task->task->exec_show_error = TRUE;
    task->task->exec_export = FALSE;
    task->task->exec_as_user = g_strdup("root");

    PluginData* plugin_data = g_slice_new0(PluginData);
    plugin_data->main_window = NULL;
    plugin_data->plug_dir = g_strdup(set->plug_dir);
    plugin_data->set = set;
    plugin_data->job = PLUGIN_JOB_REMOVE;
    task->complete_notify = (GFunc)on_install_plugin_cb;
    task->user_data = plugin_data;

    ptk_file_task_run(task);
}

void install_plugin_file(gpointer main_win, GtkWidget* handler_dlg, const char* path, const char* plug_dir, int type,
                         int job, XSet* insert_set)
{
    char* file_path_q;
    char* own;
    char* rem = g_strdup("");

    FMMainWindow* main_window = (FMMainWindow*)main_win;
    // task
    PtkFileTask* task = ptk_file_exec_new(_("Install Plugin"),
                                          NULL,
                                          main_win ? GTK_WIDGET(main_window) : NULL,
                                          main_win ? main_window->task_view : NULL);

    char* plug_dir_q = bash_quote(plug_dir);

    if (type == 0)
    {
        file_path_q = bash_quote(path);
    }

    if (job == PLUGIN_JOB_COPY)
    {
        // copy to clipboard or import to menu
        own = g_strdup_printf("chmod -R go+rX-w %s", plug_dir_q);
    }

    const char* book = "";
    if (insert_set && !strcmp(insert_set->name, "main_book"))
    {
        // import bookmarks to end
        XSet* set = xset_get("main_book");
        set = xset_is(set->child);
        while (set && set->next)
            set = xset_is(set->next);
        if (set)
            insert_set = set;
        else
            insert_set = NULL; // failsafe
    }
    if (!insert_set)
    {
        book = " || [ -e main_book ]";
    }

    task->task->exec_command =
        g_strdup_printf("rm -rf %s ; mkdir -p %s && cd %s && tar --exclude='/*' --keep-old-files -xJf %s ; err=$?; if "
                        "[ $err -ne 0 ] || [ ! -e plugin ]%s;then rm -rf %s ; echo 'Error installing plugin (invalid "
                        "plugin file?)'; exit 1 ; fi ; %s %s",
                        plug_dir_q,
                        plug_dir_q,
                        plug_dir_q,
                        file_path_q,
                        book,
                        plug_dir_q,
                        own,
                        rem);
    g_free(plug_dir_q);
    g_free(file_path_q);
    g_free(own);
    g_free(rem);
    task->task->exec_sync = TRUE;
    task->task->exec_popup = FALSE;
    task->task->exec_show_output = FALSE;
    task->task->exec_show_error = TRUE;
    task->task->exec_export = FALSE;

    PluginData* plugin_data = g_slice_new0(PluginData);
    plugin_data->main_window = main_window;
    plugin_data->handler_dlg = handler_dlg;
    plugin_data->plug_dir = g_strdup(plug_dir);
    plugin_data->job = job;
    plugin_data->set = insert_set;
    task->complete_notify = (GFunc)on_install_plugin_cb;
    task->user_data = plugin_data;

    ptk_file_task_run(task);
}

gboolean xset_custom_export_files(XSet* set, char* plug_dir)
{
    char* path_src;
    char* path_dest;
    char* command;
    char* stdout = NULL;
    char* stderr = NULL;

    if (set->plugin)
    {
        path_src = g_build_filename(set->plug_dir, set->plug_name, NULL);
        path_dest = g_build_filename(plug_dir, set->plug_name, NULL);
    }
    else
    {
        path_src = g_build_filename(settings_config_dir, "scripts", set->name, NULL);
        path_dest = g_build_filename(plug_dir, set->name, NULL);
    }

    if (!(g_file_test(path_src, G_FILE_TEST_EXISTS) && dir_has_files(path_src)))
    {
        if (!strcmp(set->name, "main_book"))
        {
            // exporting all bookmarks - create empty main_book dir
            g_mkdir_with_parents(path_dest, 0755);
            if (!g_file_test(path_dest, G_FILE_TEST_EXISTS))
            {
                g_free(path_src);
                g_free(path_dest);
                return FALSE;
            }
        }
        // skip empty or missing dirs
        g_free(path_src);
        g_free(path_dest);
        return TRUE;
    }
    else
    {
        command = g_strdup_printf("cp -a %s %s", path_src, path_dest);
    }
    g_free(path_src);
    g_free(path_dest);
    print_command(command);
    gboolean ret = g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
    g_free(command);
    if (stderr)
        g_free(stderr);
    if (stdout)
        g_free(stdout);

    return ret;
}

gboolean xset_custom_export_write(GString* buf, XSet* set, char* plug_dir)
{ // recursively write set, submenu sets, and next sets
    xset_write_set(buf, set);
    if (!xset_custom_export_files(set, plug_dir))
        return FALSE;
    if (set->menu_style == XSET_MENU_SUBMENU && set->child)
    {
        if (!xset_custom_export_write(buf, xset_get(set->child), plug_dir))
            return FALSE;
    }
    if (set->next)
    {
        if (!xset_custom_export_write(buf, xset_get(set->next), plug_dir))
            return FALSE;
    }
    return TRUE;
}

void xset_custom_export(GtkWidget* parent, PtkFileBrowser* file_browser, XSet* set)
{
    const char* deffolder;
    char* deffile;

    // get new plugin filename
    XSet* save = xset_get("plug_cfile");
    if (save->s) //&& g_file_test( save->s, G_FILE_TEST_IS_DIR )
        deffolder = save->s;
    else
    {
        if (!(deffolder = xset_get_s("go_set_default")))
            deffolder = "/";
    }

    if (!set->plugin)
    {
        char* s1 = clean_label(set->menu_label, TRUE, FALSE);
        char* s2 = plain_ascii_name(s1);
        if (s2[0] == '\0')
        {
            g_free(s2);
            s2 = g_strdup("Plugin");
        }
        char* type;
        if (!strcmp(set->name, "main_book"))
            type = g_strdup_printf("bookmarks");
        else if (g_str_has_prefix(set->name, "hand_arc_"))
            type = g_strdup_printf("archive-handler");
        else if (g_str_has_prefix(set->name, "hand_fs_"))
            type = g_strdup_printf("device-handler");
        else if (g_str_has_prefix(set->name, "hand_net_"))
            type = g_strdup_printf("protocol-handler");
        else if (g_str_has_prefix(set->name, "hand_f_"))
            type = g_strdup_printf("file-handler");
        else
            type = g_strdup_printf("plugin");

        deffile = g_strdup_printf("%s.spacefm-%s.tar.xz", s2, type);

        g_free(s1);
        g_free(s2);
        g_free(type);
    }
    else
    {
        char* s1 = g_path_get_basename(set->plug_dir);
        deffile = g_strdup_printf("%s.spacefm-plugin.tar.xz", s1);
        g_free(s1);
    }

    char* path = xset_file_dialog(parent, GTK_FILE_CHOOSER_ACTION_SAVE, _("Save As Plugin File"), deffolder, deffile);
    g_free(deffile);
    if (!path)
        return;
    if (save->s)
        g_free(save->s);
    save->s = g_path_get_dirname(path);

    // get or create tmp plugin dir
    char* plug_dir = NULL;
    if (!set->plugin)
    {
        char* s1 = (char*)xset_get_user_tmp_dir();
        if (!s1)
            goto _export_error;
        while (!plug_dir || g_file_test(plug_dir, G_FILE_TEST_EXISTS))
        {
            char* hex8 = randhex8();
            if (plug_dir)
                g_free(plug_dir);
            plug_dir = g_build_filename(s1, hex8, NULL);
            g_free(hex8);
        }
        g_mkdir_with_parents(plug_dir, 0700);

        // Create plugin file
        char* path = g_build_filename(plug_dir, "plugin", NULL);
        GString* buf = g_string_sized_new(4096);

        g_string_append(buf, "[Plugin]\n");
        xset_write_set(buf, xset_get("config_version"));

        char* s_prev = set->prev;
        char* s_next = set->next;
        char* s_parent = set->parent;
        set->prev = set->next = set->parent = NULL;
        xset_write_set(buf, set);
        set->prev = s_prev;
        set->next = s_next;
        set->parent = s_parent;

        if (!xset_custom_export_files(set, plug_dir))
            goto _rmtmp_error;
        if (set->menu_style == XSET_MENU_SUBMENU && set->child)
        {
            if (!xset_custom_export_write(buf, xset_get(set->child), plug_dir))
                goto _rmtmp_error;
        }
        g_string_append(buf, "\n");
        g_file_set_contents(path, buf->str, buf->len, NULL);
        g_free(path);
        g_string_free(buf, TRUE);
    }
    else
        plug_dir = g_strdup(set->plug_dir);

    // tar and delete tmp files
    // task
    PtkFileTask* task =
        ptk_file_exec_new(_("Export Plugin"), plug_dir, parent, file_browser ? file_browser->task_view : NULL);
    char* plug_dir_q = bash_quote(plug_dir);
    char* path_q = bash_quote(path);
    if (!set->plugin)
        task->task->exec_command = g_strdup_printf(
            "tar --numeric-owner -cJf %s * ; err=$? ; rm -rf %s ; if [ $err -ne 0 ];then rm -f %s; fi; exit $err",
            path_q,
            plug_dir_q,
            path_q);
    else
        task->task->exec_command =
            g_strdup_printf("tar --numeric-owner -cJf %s * ; err=$? ; if [ $err -ne 0 ];then rm -f %s; fi; exit $err",
                            path_q,
                            path_q);
    g_free(plug_dir_q);
    g_free(path_q);
    task->task->exec_sync = TRUE;
    task->task->exec_popup = FALSE;
    task->task->exec_show_output = FALSE;
    task->task->exec_show_error = TRUE;
    task->task->exec_export = FALSE;
    task->task->exec_browser = file_browser;
    ptk_file_task_run(task);

    g_free(path);
    g_free(plug_dir);
    return;

_rmtmp_error:
    if (!set->plugin)
    {
        char* command = g_strdup_printf("rm -rf %s", bash_quote(plug_dir));
        print_command(command);
        g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
        g_free(command);
    }
_export_error:
    g_free(plug_dir);
    g_free(path);
    xset_msg_dialog(parent,
                    GTK_MESSAGE_ERROR,
                    _("Export Error"),
                    NULL,
                    0,
                    _("Unable to create temporary files"),
                    NULL,
                    NULL);
}

static void open_spec(PtkFileBrowser* file_browser, const char* url, gboolean in_new_tab)
{
    char* tilde_url = NULL;
    const char* use_url;

    gboolean new_window = FALSE;
    if (!file_browser)
    {
        FMMainWindow* main_window = fm_main_window_get_on_current_desktop();
        if (!main_window)
        {
            main_window = FM_MAIN_WINDOW(fm_main_window_new());
            gtk_window_set_default_size(GTK_WINDOW(main_window), app_settings.width, app_settings.height);
            gtk_widget_show(GTK_WIDGET(main_window));
            new_window = !xset_get_b("main_save_tabs");
        }
        file_browser = PTK_FILE_BROWSER(fm_main_window_get_current_file_browser(main_window));
        gtk_window_present(GTK_WINDOW(main_window));
    }
    gboolean new_tab = !new_window && in_new_tab;

    // convert ~ to /home/user for smarter bookmarks
    if (g_str_has_prefix(url, "~/") || !g_strcmp0(url, "~"))
        use_url = tilde_url = g_strdup_printf("%s%s", g_get_home_dir(), url + 1);
    else
        use_url = url;

    if ((use_url[0] != '/' && strstr(use_url, ":/")) || g_str_has_prefix(use_url, "//"))
    {
        // network
        if (file_browser)
            ptk_location_view_mount_network(file_browser, use_url, new_tab, FALSE);
        else
            open_in_prog(use_url);
    }
    else if (g_file_test(use_url, G_FILE_TEST_IS_DIR))
    {
        // dir
        if (file_browser)
        {
            if (new_tab || g_strcmp0(ptk_file_browser_get_cwd(file_browser), use_url))
                ptk_file_browser_emit_open(file_browser, use_url, new_tab ? PTK_OPEN_NEW_TAB : PTK_OPEN_DIR);
            gtk_widget_grab_focus(GTK_WIDGET(file_browser->folder_view));
        }
        else
            open_in_prog(use_url);
    }
    else if (g_file_test(use_url, G_FILE_TEST_EXISTS))
    {
        // file - open dir and select file
        char* dir = g_path_get_dirname(use_url);
        if (dir && g_file_test(dir, G_FILE_TEST_IS_DIR))
        {
            if (file_browser)
            {
                if (!new_tab && !strcmp(dir, ptk_file_browser_get_cwd(file_browser)))
                {
                    ptk_file_browser_select_file(file_browser, use_url);
                    gtk_widget_grab_focus(GTK_WIDGET(file_browser->folder_view));
                }
                else
                {
                    file_browser->select_path = strdup(use_url);
                    ptk_file_browser_emit_open(file_browser, dir, new_tab ? PTK_OPEN_NEW_TAB : PTK_OPEN_DIR);
                    if (new_tab)
                    {
                        FMMainWindow* main_window_last = (FMMainWindow*)file_browser->main_window;
                        file_browser = main_window_last
                                           ? PTK_FILE_BROWSER(fm_main_window_get_current_file_browser(main_window_last))
                                           : NULL;
                        if (file_browser)
                        {
                            // select path in new browser
                            file_browser->select_path = strdup(use_url);
                            // usually this is not ready but try anyway
                            ptk_file_browser_select_file(file_browser, use_url);
                        }
                    }
                }
            }
            else
                open_in_prog(dir);
        }
        g_free(dir);
    }
    else
    {
        char* msg = g_strdup_printf(_("Bookmark target '%s' is missing or invalid."), use_url);
        xset_msg_dialog(file_browser ? GTK_WIDGET(file_browser) : NULL,
                        GTK_MESSAGE_ERROR,
                        _("Invalid Bookmark Target"),
                        NULL,
                        0,
                        msg,
                        NULL,
                        NULL);
        g_free(msg);
    }
    g_free(tilde_url);
}

void xset_custom_activate(GtkWidget* item, XSet* set)
{
    GtkWidget* parent;
    GtkWidget* task_view = NULL;
    const char* cwd;
    char* command;
    char* value = NULL;

    // builtin toolitem?
    if (set->tool > XSET_TOOL_CUSTOM)
    {
        xset_builtin_tool_activate(set->tool, set, NULL);
        return;
    }

    // plugin?
    XSet* mset = xset_get_plugin_mirror(set);

    if (set->browser)
    {
        parent = GTK_WIDGET(set->browser);
        task_view = set->browser->task_view;
        cwd = ptk_file_browser_get_cwd(set->browser);
    }
    else
    {
        g_warning("xset_custom_activate !browser !desktop");
        return;
    }

    // name
    if (!set->plugin && !(!set->lock && xset_get_int_set(set, "x") > XSET_CMD_SCRIPT /*app or bookmark*/))
    {
        if (!(set->menu_label && set->menu_label[0]) ||
            (set->menu_label && !strcmp(set->menu_label, _("New _Command"))))
        {
            if (!xset_text_dialog(parent,
                                  _("Change Item Name"),
                                  NULL,
                                  FALSE,
                                  _(enter_menu_name_new),
                                  NULL,
                                  set->menu_label,
                                  &set->menu_label,
                                  NULL,
                                  FALSE,
                                  "#designmode-designmenu-new"))
                return;
        }
    }

    // variable value
    if (set->menu_style == XSET_MENU_CHECK)
        value = g_strdup_printf("%d", mset->b == XSET_B_TRUE ? 1 : 0);
    else if (set->menu_style == XSET_MENU_STRING)
        value = g_strdup(mset->s);
    else
        value = g_strdup(set->menu_label);

    // is not activatable command?
    if (!(!set->lock && set->menu_style < XSET_MENU_SUBMENU))
    {
        xset_item_prop_dlg(xset_context, set, 0);
        return;
    }

    // command
    gboolean app_no_sync = FALSE;
    int cmd_type = xset_get_int_set(set, "x");
    if (cmd_type == XSET_CMD_LINE)
    {
        // line
        if (!set->line || set->line[0] == '\0')
        {
            xset_item_prop_dlg(xset_context, set, 2);
            return;
            /*
            if ( !xset_text_dialog( parent, _("Set Command Line"), NULL, TRUE,
                            _(enter_command_line), NULL, set->line, &set->line,
                                                NULL, FALSE,
                                                "#designmode-command-line" )
                             || !set->line || set->line[0] == '\0' )
                return;
            */
        }
        command = replace_line_subs(set->line);
        char* str = replace_string(command, "\\n", "\n", FALSE);
        g_free(command);
        command = replace_string(str, "\\t", "\t", FALSE);
        g_free(str);
    }
    else if (cmd_type == XSET_CMD_SCRIPT)
    {
        // script
        command = xset_custom_get_script(set, FALSE);
        if (!command)
            return;
    }
    else if (cmd_type == XSET_CMD_APP)
    {
        // app or executable
        if (!(set->z && set->z[0]))
        {
            xset_item_prop_dlg(xset_context, set, 0);
            return;
        }
        else if (g_str_has_suffix(set->z, ".desktop"))
        {
            VFSAppDesktop* app = vfs_app_desktop_new(set->z);
            if (app && app->exec && app->exec[0] != '\0')
            {
                // get file list
                GList* sel_files;
                GdkScreen* screen;
                if (set->browser)
                {
                    sel_files = ptk_file_browser_get_selected_files(set->browser);
                    screen = gtk_widget_get_screen(GTK_WIDGET(set->browser));
                }
                else
                {
                    sel_files = NULL;
                    cwd = "/";
                    screen = gdk_screen_get_default();
                }
                GList* file_paths = NULL;
                GList* l;
                for (l = sel_files; l; l = l->next)
                {
                    file_paths =
                        g_list_prepend(file_paths,
                                       g_build_filename(cwd, vfs_file_info_get_name((VFSFileInfo*)l->data), NULL));
                }
                file_paths = g_list_reverse(file_paths);

                // open in app
                GError* err = NULL;
                if (!vfs_app_desktop_open_files(screen, cwd, app, file_paths, &err))
                {
                    ptk_show_error(parent ? GTK_WINDOW(parent) : NULL, _("Error"), err->message);
                    g_error_free(err);
                }
                if (sel_files)
                {
                    g_list_foreach(sel_files, (GFunc)vfs_file_info_unref, NULL);
                    g_list_free(sel_files);
                }
                if (file_paths)
                {
                    g_list_foreach(file_paths, (GFunc)g_free, NULL);
                    g_list_free(file_paths);
                }
            }
            if (app)
                vfs_app_desktop_unref(app);
            return;
        }
        else
        {
            command = bash_quote(set->z);
            app_no_sync = TRUE;
        }
    }
    else if (cmd_type == XSET_CMD_BOOKMARK)
    {
        // Bookmark
        if (!(set->z && set->z[0]))
        {
            xset_item_prop_dlg(xset_context, set, 0);
            return;
        }
        char* specs = set->z;
        while (specs && (specs[0] == ' ' || specs[0] == ';'))
            specs++;
        if (specs && g_file_test(specs, G_FILE_TEST_EXISTS))
            open_spec(set->browser, specs, xset_get_b("book_newtab"));
        else
        {
            // parse semi-colon separated list
            while (specs && specs[0])
            {
                char* url;
                char* sep;
                if ((sep = strchr(specs, ';')))
                    sep[0] = '\0';
                url = g_strdup(specs);
                url = g_strstrip(url);
                if (url[0])
                    open_spec(set->browser, url, TRUE);
                g_free(url);
                if (sep)
                {
                    sep[0] = ';';
                    specs = sep + 1;
                }
                else
                    specs = NULL;
            }
        }
        return;
    }
    else
        return;

    // task
    char* task_name = clean_label(set->menu_label, FALSE, FALSE);
    PtkFileTask* task = ptk_file_exec_new(task_name, cwd, parent, task_view);
    g_free(task_name);
    // don't free cwd!
    task->task->exec_browser = set->browser;
    task->task->exec_command = command;
    task->task->exec_set = set;

    if (set->y && set->y[0] != '\0')
        task->task->exec_as_user = g_strdup(set->y);

    if (set->plugin && set->shared_key && mset->icon)
        task->task->exec_icon = g_strdup(mset->icon);
    if (!task->task->exec_icon && set->icon)
        task->task->exec_icon = g_strdup(set->icon);

    task->task->current_dest = value; // temp storage
    task->task->exec_terminal = (mset->in_terminal == XSET_B_TRUE);
    task->task->exec_keep_terminal = (mset->keep_terminal == XSET_B_TRUE);
    task->task->exec_sync = !app_no_sync && (mset->task == XSET_B_TRUE);
    task->task->exec_popup = (mset->task_pop == XSET_B_TRUE);
    task->task->exec_show_output = (mset->task_out == XSET_B_TRUE);
    task->task->exec_show_error = (mset->task_err == XSET_B_TRUE);
    task->task->exec_scroll_lock = (mset->scroll_lock == XSET_B_TRUE);
    task->task->exec_checksum = set->plugin;
    task->task->exec_export = TRUE;
    // task->task->exec_keep_tmp = TRUE;

    ptk_file_task_run(task);
}

void xset_custom_delete(XSet* set, gboolean delete_next)
{
    char* command;

    if (set->menu_style == XSET_MENU_SUBMENU && set->child)
    {
        XSet* set_child = xset_get(set->child);
        xset_custom_delete(set_child, TRUE);
    }

    if (delete_next && set->next)
    {
        XSet* set_next = xset_get(set->next);
        xset_custom_delete(set_next, TRUE);
    }

    if (set == set_clipboard)
        set_clipboard = NULL;

    char* path1 = g_build_filename(settings_config_dir, "scripts", set->name, NULL);
    char* path2 = g_build_filename(settings_config_dir, "plugin-data", set->name, NULL);
    if (g_file_test(path1, G_FILE_TEST_EXISTS) || g_file_test(path2, G_FILE_TEST_EXISTS))
        command = g_strdup_printf("rm -rf %s %s", path1, path2);
    else
        command = NULL;
    g_free(path1);
    g_free(path2);
    if (command)
    {
        print_command(command);
        g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);
        g_free(command);
    }
    xset_free(set);
}

XSet* xset_custom_remove(XSet* set)
{
    XSet* set_parent;

    /*
    g_printf("xset_custom_remove %s (%s)\n", set->name, set->menu_label );
    g_printf("    set->parent = %s\n", set->parent );
    g_printf("    set->prev = %s\n", set->prev );
    g_printf("    set->next = %s\n", set->next );
    */
    if (set->prev)
    {
        XSet* set_prev = xset_get(set->prev);
        // g_printf("        set->prev = %s (%s)\n", set_prev->name, set_prev->menu_label );
        if (set_prev->next)
            g_free(set_prev->next);
        if (set->next)
            set_prev->next = g_strdup(set->next);
        else
            set_prev->next = NULL;
    }
    if (set->next)
    {
        XSet* set_next = xset_get(set->next);
        if (set_next->prev)
            g_free(set_next->prev);
        if (set->prev)
            set_next->prev = g_strdup(set->prev);
        else
        {
            set_next->prev = NULL;
            if (set->parent)
            {
                set_parent = xset_get(set->parent);
                if (set_parent->child)
                    g_free(set_parent->child);
                set_parent->child = g_strdup(set_next->name);
                set_next->parent = g_strdup(set->parent);
            }
        }
    }
    if (!set->prev && !set->next && set->parent)
    {
        XSet* set_child;
        set_parent = xset_get(set->parent);
        if (set->tool)
            set_child = xset_new_builtin_toolitem(XSET_TOOL_HOME);
        else
        {
            set_child = xset_custom_new();
            set_child->menu_label = g_strdup(_("New _Command"));
        }
        if (set_parent->child)
            g_free(set_parent->child);
        set_parent->child = g_strdup(set_child->name);
        set_child->parent = g_strdup(set->parent);
        return set_child;
    }
    return NULL;
}

void xset_custom_insert_after(XSet* target, XSet* set)
{ // inserts single set 'set', no next
    if (!set)
    {
        g_warning("xset_custom_insert_after set == NULL");
        return;
    }
    if (!target)
    {
        g_warning("xset_custom_insert_after target == NULL");
        return;
    }

    if (set->parent)
    {
        g_free(set->parent);
        set->parent = NULL;
    }

    g_free(set->prev);
    g_free(set->next);
    set->prev = g_strdup(target->name);
    set->next = target->next; // steal string
    if (target->next)
    {
        XSet* target_next = xset_get(target->next);
        if (target_next->prev)
            g_free(target_next->prev);
        target_next->prev = g_strdup(set->name);
    }
    target->next = g_strdup(set->name);
    if (target->tool)
    {
        if (set->tool < XSET_TOOL_CUSTOM)
            set->tool = XSET_TOOL_CUSTOM;
    }
    else
    {
        if (set->tool > XSET_TOOL_CUSTOM)
            g_warning("xset_custom_insert_after builtin tool inserted after non-tool");
        set->tool = XSET_TOOL_NOT;
    }
}

gboolean xset_clipboard_in_set(XSet* set)
{ // look upward to see if clipboard is in set's tree
    if (!set_clipboard || set->lock)
        return FALSE;
    if (set == set_clipboard)
        return TRUE;

    if (set->parent)
    {
        XSet* set_parent = xset_get(set->parent);
        if (xset_clipboard_in_set(set_parent))
            return TRUE;
    }

    if (set->prev)
    {
        XSet* set_prev = xset_get(set->prev);
        while (set_prev)
        {
            if (set_prev->parent)
            {
                XSet* set_prev_parent = xset_get(set_prev->parent);
                if (xset_clipboard_in_set(set_prev_parent))
                    return TRUE;
                set_prev = NULL;
            }
            else if (set_prev->prev)
                set_prev = xset_get(set_prev->prev);
            else
                set_prev = NULL;
        }
    }
    return FALSE;
}

XSet* xset_custom_new()
{
    char* setname = xset_custom_new_name();

    // create set
    XSet* set = xset_get(setname);
    g_free(setname);
    set->lock = FALSE;
    set->keep_terminal = XSET_B_TRUE;
    set->task = XSET_B_TRUE;
    set->task_err = XSET_B_TRUE;
    set->task_out = XSET_B_TRUE;
    return set;
}

void xset_edit(GtkWidget* parent, const char* path, gboolean force_root, gboolean no_root)
{
    gboolean as_root = FALSE;
    gboolean terminal;
    char* editor;
    GtkWidget* dlgparent = NULL;
    if (!path)
        return;
    if (force_root && no_root)
        return;

    if (parent)
        dlgparent = gtk_widget_get_toplevel(GTK_WIDGET(parent));

    if (geteuid() != 0 && !force_root && (no_root || have_rw_access(path)))
    {
        editor = xset_get_s("editor");
        if (!editor || editor[0] == '\0')
        {
            ptk_show_error(dlgparent ? GTK_WINDOW(dlgparent) : NULL,
                           _("Editor Not Set"),
                           _("Please set your editor in View|Preferences|Advanced"));
            return;
        }
        terminal = xset_get_b("editor");
    }
    else
    {
        editor = xset_get_s("root_editor");
        if (!editor || editor[0] == '\0')
        {
            ptk_show_error(dlgparent ? GTK_WINDOW(dlgparent) : NULL,
                           _("Root Editor Not Set"),
                           _("Please set root's editor in View|Preferences|Advanced"));
            return;
        }
        as_root = TRUE;
        terminal = xset_get_b("root_editor");
    }
    // replacements
    char* quoted_path = bash_quote(path);
    if (strstr(editor, "%f"))
        editor = replace_string(editor, "%f", quoted_path, FALSE);
    else if (strstr(editor, "%F"))
        editor = replace_string(editor, "%F", quoted_path, FALSE);
    else if (strstr(editor, "%u"))
        editor = replace_string(editor, "%u", quoted_path, FALSE);
    else if (strstr(editor, "%U"))
        editor = replace_string(editor, "%U", quoted_path, FALSE);
    else
        editor = g_strdup_printf("%s %s", editor, quoted_path);
    g_free(quoted_path);

    // task
    char* task_name = g_strdup_printf(_("Edit %s"), path);
    char* cwd = g_path_get_dirname(path);
    PtkFileTask* task = ptk_file_exec_new(task_name, cwd, dlgparent, NULL);
    g_free(task_name);
    g_free(cwd);
    task->task->exec_command = editor;
    task->task->exec_sync = FALSE;
    task->task->exec_terminal = terminal;
    if (as_root)
        task->task->exec_as_user = g_strdup_printf("root");
    ptk_file_task_run(task);
}

void xset_open_url(GtkWidget* parent, const char* url)
{
    char* command = NULL;

    const char* browser = xset_get_s("main_help_browser");
    if (browser)
    {
        if (strstr(browser, "%u"))
            command = replace_string(browser, "%u", url, TRUE);
        else
            command = g_strdup_printf("%s '%s'", browser, url);
    }
    else
    {
        browser = g_getenv("BROWSER");
        if (browser && browser[0] != '\0')
            command = g_strdup_printf("%s '%s'", browser, url);
        else
        {
            int ii = 0;
            if (g_str_has_prefix(url, "file://") || g_str_has_prefix(url, "/"))
                ii = 3; // xdg,gnome,exo-open use editor for html files so skip at start
            const char* programs[] = {"xdg-open", "gnome-open", "exo-open"};
            int i;
            for (i = ii; i < G_N_ELEMENTS(programs); ++i)
            {
                char* program;
                if ((program = g_find_program_in_path(programs[i])))
                {
                    command = g_strdup_printf("%s '%s'", program, url);
                    g_free(program);
                    break;
                }
            }
        }
    }

    if (!command)
    {
        XSet* set = xset_get("main_help_browser");
        if (!xset_text_dialog(parent, set->title, NULL, TRUE, set->desc, NULL, set->s, &set->s, NULL, FALSE, NULL) ||
            !set->s)
            return;
        xset_open_url(parent, url);
        return;
    }

    // task
    PtkFileTask* task = ptk_file_exec_new("Open URL", "/", parent, NULL);
    task->task->exec_command = command;
    task->task->exec_sync = FALSE;
    task->task->exec_export = FALSE;
    ptk_file_task_run(task);
}

char* xset_get_manual_url()
{
    char* path;
    char* url = xset_get_s("main_help_url");
    if (url)
    {
        if (url[0] == '/')
            return g_strdup_printf("file://%s", url);
        return g_strdup(url);
    }

    GList* names = NULL;
    names = g_list_append(names, g_strdup("spacefm-manual-en.html"));

    // get potential locations
    GList* locations = NULL;
    if (HTMLDIR)
        locations = g_list_append(locations, g_strdup(HTMLDIR));
    if (DATADIR)
        locations = g_list_append(locations, g_build_filename(DATADIR, "spacefm", NULL));
    const char* const* dir = g_get_system_data_dirs();
    for (; *dir; ++dir)
    {
        path = g_build_filename(*dir, "spacefm", NULL);
        if (!g_list_find_custom(locations, path, (GCompareFunc)g_strcmp0))
            locations = g_list_append(locations, path);
        else
            g_free(path);
    }
    if (!g_list_find_custom(locations, "/usr/share/spacefm", (GCompareFunc)g_strcmp0))
        locations = g_list_append(locations, g_strdup("/usr/share/spacefm"));

    GList* loc;
    for (loc = locations; loc && !url; loc = loc->next)
    {
        GList* ln;
        for (ln = names; ln; ln = ln->next)
        {
            path = g_build_filename((char*)loc->data, (char*)ln->data, NULL);
            if (g_file_test(path, G_FILE_TEST_EXISTS))
            {
                url = path;
                break;
            }
            g_free(path);
        }
    }
    g_list_foreach(names, (GFunc)g_free, NULL);
    g_list_foreach(locations, (GFunc)g_free, NULL);
    g_list_free(names);
    g_list_free(locations);

    if (!url)
        return NULL;

    path = g_strdup_printf("file://%s", url);
    g_free(url);
    return path;
}

void xset_show_help(GtkWidget* parent, XSet* set, const char* anchor)
{
    GtkWidget* dlgparent = NULL;
    char* url;
    char* manual = NULL;

    if (parent)
        dlgparent = parent;
    else if (set)
        dlgparent = set->browser;

    if (!set || set->lock)
    {
        manual = xset_get_manual_url();
        if (!manual)
        {
            if (xset_msg_dialog(dlgparent,
                                GTK_MESSAGE_QUESTION,
                                _("User's Manual Not Found"),
                                NULL,
                                GTK_BUTTONS_YES_NO,
                                _("Read the user's manual online?\n\nThe local copy of the SpaceFM user's manual was "
                                  "not found.  Click Yes to read it online, or click No and then set the correct "
                                  "location in Help|Options|Manual Location."),
                                NULL,
                                NULL) != GTK_RESPONSE_YES)
                return;
            manual = g_strdup(user_manual_url);
            xset_set("main_help_url", "s", manual);
        }
    }

    if (set)
    {
        if (set->lock)
        {
            // built-in command
            if (set->line)
            {
                url = g_strdup_printf("%s%s", manual, set->line);
                xset_open_url(dlgparent, url);
                g_free(url);
            }
            else
            {
                g_free(manual);
                return;
            }
        }
        else
        {
            // custom command or plugin
            url = xset_custom_get_help(dlgparent, set);
            if (url)
                xset_edit(dlgparent, url, FALSE, TRUE);
            g_free(url);
            return;
        }
    }
    else if (anchor)
    {
        url = g_strdup_printf("%s%s", manual, anchor);
        xset_open_url(dlgparent, url);
        g_free(url);
    }
    else
        // just show the manual
        xset_open_url(dlgparent, manual);

    if (manual)
        g_free(manual);
}

char* xset_get_keyname(XSet* set, int key_val, int key_mod)
{
    int keyval;
    int keymod;
    if (set)
    {
        keyval = set->key;
        keymod = set->keymod;
    }
    else
    {
        keyval = key_val;
        keymod = key_mod;
    }
    if (keyval <= 0)
        return g_strdup(_("( none )"));
    char* mod = g_strdup(gdk_keyval_name(keyval));
    if (mod && mod[0] && !mod[1] && g_ascii_isalpha(mod[0]))
        mod[0] = g_ascii_toupper(mod[0]);
    else if (!mod)
        mod = g_strdup("NA");
    if (keymod)
    {
        char* str;
        if (keymod & GDK_SUPER_MASK)
        {
            str = mod;
            mod = g_strdup_printf("Super+%s", str);
            g_free(str);
        }
        if (keymod & GDK_HYPER_MASK)
        {
            str = mod;
            mod = g_strdup_printf("Hyper+%s", str);
            g_free(str);
        }
        if (keymod & GDK_META_MASK)
        {
            str = mod;
            mod = g_strdup_printf("Meta+%s", str);
            g_free(str);
        }
        if (keymod & GDK_MOD1_MASK)
        {
            str = mod;
            mod = g_strdup_printf("Alt+%s", str);
            g_free(str);
        }
        if (keymod & GDK_CONTROL_MASK)
        {
            str = mod;
            mod = g_strdup_printf("Ctrl+%s", str);
            g_free(str);
        }
        if (keymod & GDK_SHIFT_MASK)
        {
            str = mod;
            mod = g_strdup_printf("Shift+%s", str);
            g_free(str);
        }
    }
    return mod;
}

gboolean on_set_key_keypress(GtkWidget* widget, GdkEventKey* event, GtkWidget* dlg)
{
    int* newkey = (int*)g_object_get_data(G_OBJECT(dlg), "newkey");
    int* newkeymod = (int*)g_object_get_data(G_OBJECT(dlg), "newkeymod");
    GtkWidget* btn = (GtkWidget*)g_object_get_data(G_OBJECT(dlg), "btn");

    int keymod = (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK |
                                  GDK_META_MASK));

    if (!event->keyval) // || ( event->keyval < 1000 && !keymod ) )
    {
        *newkey = 0;
        *newkeymod = 0;
        gtk_widget_set_sensitive(btn, FALSE);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), NULL);
        return TRUE;
    }

    gtk_widget_set_sensitive(btn, TRUE);

    if (*newkey != 0 && keymod == 0)
    {
        if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
        {
            // user pressed Enter after selecting a key, so click Set
            gtk_button_clicked(GTK_BUTTON(btn));
            return TRUE;
        }
        else if (event->keyval == GDK_KEY_Escape && *newkey == GDK_KEY_Escape)
        {
            // user pressed Escape twice so click Unset
            GtkWidget* btn_unset = (GtkWidget*)g_object_get_data(G_OBJECT(dlg), "btn_unset");
            gtk_button_clicked(GTK_BUTTON(btn_unset));
            return TRUE;
        }
    }

    // need to transpose nonlatin keyboard layout ?
    uint nonlatin_key = 0;
    if (!((GDK_KEY_0 <= event->keyval && event->keyval <= GDK_KEY_9) ||
          (GDK_KEY_A <= event->keyval && event->keyval <= GDK_KEY_Z) ||
          (GDK_KEY_a <= event->keyval && event->keyval <= GDK_KEY_z)))
    {
        nonlatin_key = event->keyval;
        transpose_nonlatin_keypress(event);
    }

    *newkey = 0;
    *newkeymod = 0;
    XSet* set = (XSet*)g_object_get_data(G_OBJECT(dlg), "set");
    XSet* keyset = NULL;
    if (set->shared_key)
        keyset = xset_get(set->shared_key);

    char* keyname;
    GList* l;
    for (l = xsets; l; l = l->next)
    {
        XSet* set2 = l->data;
        if (set2 && set2 != set && set2->key > 0 && set2->key == event->keyval && set2->keymod == keymod &&
            set2 != keyset)
        {
            char* name;
            if (set2->desc && !strcmp(set2->desc, "@plugin@mirror@") && set2->shared_key)
            {
                // set2 is plugin mirror
                XSet* rset = xset_get(set2->shared_key);
                if (rset->menu_label)
                    name = clean_label(rset->menu_label, FALSE, FALSE);
                else
                    name = g_strdup("( no name )");
            }
            else if (set2->menu_label)
                name = clean_label(set2->menu_label, FALSE, FALSE);
            else
                name = g_strdup("( no name )");

            keyname = xset_get_keyname(NULL, event->keyval, keymod);
            if (nonlatin_key == 0)
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(dlg),
                    _("\t%s\n\tKeycode: %#4x  Modifier: %#x\n\n%s is already assigned to '%s'.\n\nPress a different "
                      "key or click Set to replace the current key assignment."),
                    keyname,
                    event->keyval,
                    keymod,
                    keyname,
                    name);
            else
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(dlg),
                    _("\t%s\n\tKeycode: %#4x [%#4x]  Modifier: %#x\n\n%s is already assigned to '%s'.\n\nPress a "
                      "different key or click Set to replace the current key assignment."),
                    keyname,
                    event->keyval,
                    nonlatin_key,
                    keymod,
                    keyname,
                    name);
            g_free(name);
            g_free(keyname);
            *newkey = event->keyval;
            *newkeymod = keymod;
            return TRUE;
        }
    }
    keyname = xset_get_keyname(NULL, event->keyval, keymod);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg),
                                             _("\t%s\n\tKeycode: %#4x  Modifier: %#x"),
                                             keyname,
                                             event->keyval,
                                             keymod);
    g_free(keyname);
    *newkey = event->keyval;
    *newkeymod = keymod;
    return TRUE;
}

void xset_set_key(GtkWidget* parent, XSet* set)
{
    char* name;
    XSet* keyset;
    int newkey = 0;
    int newkeymod = 0;
    GtkWidget* dlgparent = NULL;

    if (set->menu_label)
        name = clean_label(set->menu_label, FALSE, TRUE);
    else if (set->tool > XSET_TOOL_CUSTOM)
        name = g_strdup(xset_get_builtin_toolitem_label(set->tool));
    else if (g_str_has_prefix(set->name, "open_all_type_"))
    {
        keyset = xset_get("open_all");
        name = clean_label(keyset->menu_label, FALSE, TRUE);
        if (set->shared_key)
            g_free(set->shared_key);
        set->shared_key = g_strdup("open_all");
    }
    else
        name = g_strdup("( no name )");
    char* keymsg =
        g_strdup_printf(_("Press your key combination for item '%s' then click Set.  To remove the current key "
                          "assignment, click Unset."),
                        name);
    g_free(name);
    if (parent)
        dlgparent = gtk_widget_get_toplevel(parent);
    GtkWidget* dlg = gtk_message_dialog_new_with_markup(dlgparent ? GTK_WINDOW(dlgparent) : NULL,
                                                        GTK_DIALOG_MODAL,
                                                        GTK_MESSAGE_QUESTION,
                                                        GTK_BUTTONS_NONE,
                                                        keymsg,
                                                        NULL);
    xset_set_window_icon(GTK_WINDOW(dlg));

    GtkWidget* btn_cancel = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
    gtk_button_set_label(GTK_BUTTON(btn_cancel), _("Cancel"));
    gtk_button_set_image(GTK_BUTTON(btn_cancel), xset_get_image("GTK_STOCK_CANCEL", GTK_ICON_SIZE_BUTTON));
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn_cancel, GTK_RESPONSE_CANCEL);

    GtkWidget* btn_unset = gtk_button_new_from_stock(GTK_STOCK_NO);
    gtk_button_set_label(GTK_BUTTON(btn_unset), _("Unset"));
    gtk_button_set_image(GTK_BUTTON(btn_unset), xset_get_image("GTK_STOCK_REMOVE", GTK_ICON_SIZE_BUTTON));
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn_unset, GTK_RESPONSE_NO);

    if (set->shared_key)
        keyset = xset_get(set->shared_key);
    else
        keyset = set;
    if (keyset->key <= 0)
        gtk_widget_set_sensitive(btn_unset, FALSE);

    GtkWidget* btn = gtk_button_new_from_stock(GTK_STOCK_APPLY);
    gtk_button_set_label(GTK_BUTTON(btn), _("Set"));
    gtk_button_set_image(GTK_BUTTON(btn), xset_get_image("GTK_STOCK_YES", GTK_ICON_SIZE_BUTTON));
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn, GTK_RESPONSE_OK);
    gtk_widget_set_sensitive(btn, FALSE);

    g_object_set_data(G_OBJECT(dlg), "set", set);
    g_object_set_data(G_OBJECT(dlg), "newkey", &newkey);
    g_object_set_data(G_OBJECT(dlg), "newkeymod", &newkeymod);
    g_object_set_data(G_OBJECT(dlg), "btn", btn);
    g_object_set_data(G_OBJECT(dlg), "btn_unset", btn_unset);
    g_signal_connect(dlg, "key_press_event", G_CALLBACK(on_set_key_keypress), dlg);
    gtk_widget_show_all(dlg);
    gtk_window_set_title(GTK_WINDOW(dlg), _("Set Key"));

    int response = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_NO)
    {
        if (response == GTK_RESPONSE_OK && (newkey || newkeymod))
        {
            // clear duplicate key assignments
            GList* l;
            for (l = xsets; l; l = l->next)
            {
                XSet* set2 = l->data;
                if (set2 && set2->key > 0 && set2->key == newkey && set2->keymod == newkeymod)
                {
                    set2->key = 0;
                    set2->keymod = 0;
                }
            }
        }
        else if (response == GTK_RESPONSE_NO)
        {
            newkey = -1; // unset
            newkeymod = 0;
        }
        // plugin? set shared_key to mirror if not
        if (set->plugin && !set->shared_key)
            xset_get_plugin_mirror(set);
        // set new key
        if (set->shared_key)
            keyset = xset_get(set->shared_key);
        else
            keyset = set;
        keyset->key = newkey;
        keyset->keymod = newkeymod;
    }
}

void xset_design_job(GtkWidget* item, XSet* set)
{
    XSet* newset;
    XSet* mset;
    XSet* childset;
    XSet* set_next;
    char* msg;
    char* folder;
    char* file = NULL;
    char* cscript;
    char* name;
    GtkWidget* dlgparent = NULL;
    GtkClipboard* clip;
    gboolean update_toolbars = FALSE;

    GtkWidget* parent = gtk_widget_get_toplevel(set->browser);

    int job = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "job"));
    int cmd_type = xset_get_int_set(set, "x");

    // g_printf("activate job %d %s\n", job, set->name);
    switch (job)
    {
    case XSET_JOB_KEY:
        xset_set_key(parent, set);
        break;
    case XSET_JOB_ICON:
        mset = xset_get_plugin_mirror(set);
        char* old_icon = g_strdup(mset->icon);
        // Note: xset_text_dialog uses the title passed to know this is an
        // icon chooser, so it adds a Choose button.  If you change the title,
        // change xset_text_dialog.
        xset_text_dialog(parent,
                         _("Set Icon"),
                         NULL,
                         FALSE,
                         _(icon_desc),
                         NULL,
                         mset->icon,
                         &mset->icon,
                         NULL,
                         FALSE,
                         "#designmode-designmenu-icon");
        if (set->lock && set->keep_terminal == XSET_B_UNSET && g_strcmp0(old_icon, mset->icon))
        {
            // built-in icon has been changed from default, save it
            set->keep_terminal = XSET_B_TRUE;
        }
        g_free(old_icon);
        break;
    case XSET_JOB_LABEL:
        /*  unused - note that this does not accommodate in_terminal indicator
        if ( g_str_has_prefix( set->name, "open_all_type_" ) )
            keyset = xset_get( "open_all" );
        else
            keyset = set;
        xset_text_dialog( parent, _("Change Menu Name"), NULL, FALSE, _(enter_menu_name),
                                        NULL, keyset->menu_label, &keyset->menu_label,
                                        NULL, FALSE, "#designmode-designmenu-name" );
        */
        break;
    case XSET_JOB_EDIT:
        if (cmd_type == XSET_CMD_SCRIPT)
        {
            // script
            cscript = xset_custom_get_script(set, !set->plugin);
            if (!cscript)
                break;
            xset_edit(parent, cscript, FALSE, TRUE);
            g_free(cscript);
        }
        break;
    case XSET_JOB_EDIT_ROOT:
        if (cmd_type == XSET_CMD_SCRIPT)
        {
            // script
            cscript = xset_custom_get_script(set, !set->plugin);
            if (!cscript)
                break;
            xset_edit(parent, cscript, TRUE, FALSE);
            g_free(cscript);
        }
        break;
    case XSET_JOB_COPYNAME:
        clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        if (cmd_type == XSET_CMD_LINE)
        {
            // line
            gtk_clipboard_set_text(clip, set->line, -1);
        }
        else if (cmd_type == XSET_CMD_SCRIPT)
        {
            // script
            cscript = xset_custom_get_script(set, TRUE);
            if (!cscript)
                break;
            gtk_clipboard_set_text(clip, cscript, -1);
            g_free(cscript);
        }
        else if (cmd_type == XSET_CMD_APP)
        {
            // custom
            gtk_clipboard_set_text(clip, set->z, -1);
        }
        break;
    case XSET_JOB_LINE:
        if (xset_text_dialog(parent,
                             _("Edit Command Line"),
                             NULL,
                             TRUE,
                             _(enter_command_line),
                             NULL,
                             set->line,
                             &set->line,
                             NULL,
                             FALSE,
                             "#designmode-command-line"))
            xset_set_set(set, "x", "0");
        break;
    case XSET_JOB_SCRIPT:
        xset_set_set(set, "x", "1");
        cscript = xset_custom_get_script(set, TRUE);
        if (!cscript)
            break;
        xset_edit(parent, cscript, FALSE, FALSE);
        g_free(cscript);
        break;
    case XSET_JOB_CUSTOM:
        if (set->z && set->z[0] != '\0')
        {
            folder = g_path_get_dirname(set->z);
            file = g_path_get_basename(set->z);
        }
        else
        {
            folder = g_strdup_printf("/usr/bin");
            file = NULL;
        }
        char* custom_file;
        if ((custom_file =
                 xset_file_dialog(parent, GTK_FILE_CHOOSER_ACTION_OPEN, _("Choose Custom Executable"), folder, file)))
        {
            xset_set_set(set, "x", "2");
            xset_set_set(set, "z", custom_file);
            g_free(custom_file);
        }
        g_free(file);
        g_free(folder);
        break;
    case XSET_JOB_USER:
        if (!set->plugin)
            xset_text_dialog(parent,
                             _("Run As User"),
                             NULL,
                             FALSE,
                             _("Run this command as username:\n\n( Leave blank for current user )"),
                             NULL,
                             set->y,
                             &set->y,
                             NULL,
                             FALSE,
                             "#designmode-command-user");
        break;
    case XSET_JOB_BOOKMARK:
    case XSET_JOB_APP:
    case XSET_JOB_COMMAND:
        if (g_str_has_prefix(set->name, "open_all_type_"))
        {
            name = set->name + 14;
            msg = g_strdup_printf(
                _("You are adding a custom command to the Default menu item.  This item will automatically have a "
                  "pre-context - it will only appear when the MIME type of the first selected file matches the current "
                  "type '%s'.\n\nAdd commands or menus here which you only want to appear for this one MIME type."),
                name[0] == '\0' ? "(none)" : name);
            if (xset_msg_dialog(parent, 0, _("New Context Command"), NULL, GTK_BUTTONS_OK_CANCEL, msg, NULL, NULL) !=
                GTK_RESPONSE_OK)
            {
                g_free(msg);
                break;
            }
            g_free(msg);
        }
        if (job == XSET_JOB_COMMAND)
        {
            name = g_strdup_printf(_("New _Command"));
            if (!xset_text_dialog(parent,
                                  _("Set Item Name"),
                                  NULL,
                                  FALSE,
                                  _(enter_menu_name_new),
                                  NULL,
                                  name,
                                  &name,
                                  NULL,
                                  FALSE,
                                  "#designmode-designmenu-new"))
            {
                g_free(name);
                break;
            }
            file = NULL;
        }
        else if (job == XSET_JOB_APP)
        {
            VFSMimeType* mime_type = vfs_mime_type_get_from_type(xset_context && xset_context->var[CONTEXT_MIME] &&
                                                                         xset_context->var[CONTEXT_MIME][0]
                                                                     ? xset_context->var[CONTEXT_MIME]
                                                                     : XDG_MIME_TYPE_UNKNOWN);
            file = (char*)ptk_choose_app_for_mime_type(GTK_WINDOW(parent), mime_type, TRUE, FALSE, FALSE, FALSE);
            vfs_mime_type_unref(mime_type);
            if (!(file && file[0]))
            {
                g_free(file);
                break;
            }
            name = NULL;
        }
        else if (job == XSET_JOB_BOOKMARK)
        {
            if (set->browser)
                folder = (char*)ptk_file_browser_get_cwd(set->browser);
            else
                folder = NULL;
            file = xset_file_dialog(parent, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, _("Choose Folder"), folder, NULL);
            if (!(file && file[0]))
            {
                g_free(file);
                break;
            }
            name = g_path_get_basename(file);
        }

        // add new menu item
        newset = xset_custom_new();
        xset_custom_insert_after(set, newset);

        newset->z = file;
        newset->menu_label = name;
        newset->browser = set->browser;
        if (job == XSET_JOB_COMMAND)
            xset_item_prop_dlg(xset_context, newset, 2);
        else if (job == XSET_JOB_APP)
        {
            g_free(newset->x);
            newset->x = g_strdup("2"); // XSET_CMD_APP
            // unset these to save session space
            newset->task = newset->task_err = newset->task_out = newset->keep_terminal = XSET_B_UNSET;
        }
        else if (job == XSET_JOB_BOOKMARK)
        {
            g_free(newset->x);
            newset->x = g_strdup("3"); // XSET_CMD_BOOKMARK
            // unset these to save session space
            newset->task = newset->task_err = newset->task_out = newset->keep_terminal = XSET_B_UNSET;
        }
        main_window_bookmark_changed(newset->name);
        break;
    case XSET_JOB_SUBMENU:
    case XSET_JOB_SUBMENU_BOOK:
        if (g_str_has_prefix(set->name, "open_all_type_"))
        {
            name = set->name + 14;
            msg = g_strdup_printf(
                _("You are adding a custom submenu to the Default menu item.  This item will automatically have a "
                  "pre-context - it will only appear when the MIME type of the first selected file matches the current "
                  "type '%s'.\n\nAdd commands or menus here which you only want to appear for this one MIME type."),
                name[0] == '\0' ? _("(none)") : name);
            if (xset_msg_dialog(parent, 0, "New Context Submenu", NULL, GTK_BUTTONS_OK_CANCEL, msg, NULL, NULL) !=
                GTK_RESPONSE_OK)
            {
                g_free(msg);
                break;
            }
            g_free(msg);
        }
        name = NULL;
        if (!xset_text_dialog(parent,
                              _("Set Submenu Name"),
                              NULL,
                              FALSE,
                              _("Enter submenu name:\n\nPrecede a character with an underscore (_) to underline that "
                                "character as a shortcut key if desired."),
                              NULL,
                              _("New _Submenu"),
                              &name,
                              NULL,
                              FALSE,
                              "#designmode-designmenu-name") ||
            !name)
            break;

        // add new submenu
        newset = xset_custom_new();
        newset->menu_label = name;
        newset->menu_style = XSET_MENU_SUBMENU;
        xset_custom_insert_after(set, newset);

        // add submenu child
        childset = xset_custom_new();
        newset->child = g_strdup(childset->name);
        childset->parent = g_strdup(newset->name);
        if (job == XSET_JOB_SUBMENU_BOOK || xset_is_main_bookmark(set))
        {
            // adding new submenu from a bookmark - fill with bookmark
            folder = set->browser ? (char*)ptk_file_browser_get_cwd(set->browser) : (char*)vfs_get_desktop_dir();
            childset->menu_label = g_path_get_basename(folder);
            childset->z = g_strdup(folder);
            childset->x = g_strdup_printf("%d", XSET_CMD_BOOKMARK);
            childset->task = childset->task_err = childset->task_out = childset->keep_terminal = XSET_B_UNSET;
        }
        else
            childset->menu_label = g_strdup_printf(_("New _Command"));
        main_window_bookmark_changed(newset->name);
        break;
    case XSET_JOB_SEP:
        newset = xset_custom_new();
        newset->menu_style = XSET_MENU_SEP;
        xset_custom_insert_after(set, newset);
        main_window_bookmark_changed(newset->name);
        break;
    case XSET_JOB_ADD_TOOL:
        job = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "tool_type"));
        if (job < XSET_TOOL_DEVICES || job >= XSET_TOOL_INVALID || !set->tool)
            break;
        newset = xset_new_builtin_toolitem(job);
        if (newset)
            xset_custom_insert_after(set, newset);
        break;
    case XSET_JOB_IMPORT_FILE:
        if (job == XSET_JOB_IMPORT_FILE)
        {
            // get file path
            XSet* save = xset_get("plug_cfile");
            if (save->s) //&& g_file_test( save->s, G_FILE_TEST_IS_DIR )
                folder = save->s;
            else
            {
                if (!(folder = xset_get_s("go_set_default")))
                    folder = "/";
            }
            file = xset_file_dialog(GTK_WIDGET(parent),
                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                    _("Choose Plugin File"),
                                    folder,
                                    NULL);
            if (!file)
                break;
            if (save->s)
                g_free(save->s);
            save->s = g_path_get_dirname(file);
        }

        // Make Plugin Dir
        const char* user_tmp = xset_get_user_tmp_dir();
        if (!user_tmp)
        {
            xset_msg_dialog(GTK_WIDGET(parent),
                            GTK_MESSAGE_ERROR,
                            _("Error Creating Temp Directory"),
                            NULL,
                            0,
                            _("Unable to create temporary directory"),
                            NULL,
                            NULL);
            g_free(file);
            break;
        }
        folder = NULL;
        while (!folder || g_file_test(folder, G_FILE_TEST_EXISTS))
        {
            char* hex8 = randhex8();
            if (folder)
                g_free(folder);
            folder = g_build_filename(user_tmp, hex8, NULL);
            g_free(hex8);
        }
        install_plugin_file(set->browser ? set->browser->main_window : NULL,
                            NULL,
                            file,
                            folder,
                            job == XSET_JOB_IMPORT_FILE ? 0 : 1,
                            PLUGIN_JOB_COPY,
                            set);
        g_free(file);
        g_free(folder);
        break;
    case XSET_JOB_IMPORT_GTK:
        // both GTK2 and GTK3 now use new location?
        file = g_build_filename(g_get_user_config_dir(), "gtk-3.0", "bookmarks", NULL);
        if (!(file && g_file_test(file, G_FILE_TEST_EXISTS)))
            file = g_build_filename(g_get_home_dir(), ".gtk-bookmarks", NULL);
        msg = g_strdup_printf(_("GTK bookmarks (%s) will be imported into the current or selected submenu.  Note that "
                                "importing large numbers of bookmarks (eg more than 500) may impact performance."),
                              file);
        if (xset_msg_dialog(parent,
                            GTK_MESSAGE_QUESTION,
                            _("Import GTK Bookmarks"),
                            NULL,
                            GTK_BUTTONS_OK_CANCEL,
                            msg,
                            NULL,
                            NULL) != GTK_RESPONSE_OK)
        {
            g_free(msg);
            break;
        }
        g_free(msg);
        g_free(file);
        break;
    case XSET_JOB_CUT:
        set_clipboard = set;
        clipboard_is_cut = TRUE;
        break;
    case XSET_JOB_COPY:
        set_clipboard = set;
        clipboard_is_cut = FALSE;

        // if copy bookmark, put target on real clipboard
        if (!set->lock && set->z && set->menu_style < XSET_MENU_SUBMENU &&
            xset_get_int_set(set, "x") == XSET_CMD_BOOKMARK)
        {
            clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            gtk_clipboard_set_text(clip, set->z, -1);
            clip = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
            gtk_clipboard_set_text(clip, set->z, -1);
        }
        break;
    case XSET_JOB_PASTE:
        if (!set_clipboard)
            break;
        if (set_clipboard->tool > XSET_TOOL_CUSTOM && !set->tool)
            // failsafe - disallow pasting a builtin tool to a menu
            break;
        if (clipboard_is_cut)
        {
            update_toolbars = set_clipboard->tool != XSET_TOOL_NOT;
            if (!update_toolbars && set_clipboard->parent)
            {
                newset = xset_get(set_clipboard->parent);
                if (newset->tool)
                    // we are cutting the first item in a tool submenu
                    update_toolbars = TRUE;
            }
            xset_custom_remove(set_clipboard);
            xset_custom_insert_after(set, set_clipboard);

            main_window_bookmark_changed(set_clipboard->name);
            set_clipboard = NULL;

            if (!set->lock)
            {
                // update parent for bookmarks
                newset = set;
                while (newset->prev)
                    newset = xset_get(newset->prev);
                if (newset->parent)
                    main_window_bookmark_changed(newset->parent);
            }
        }
        else
        {
            newset = xset_custom_copy(set_clipboard, FALSE, FALSE);
            xset_custom_insert_after(set, newset);
            main_window_bookmark_changed(newset->name);
        }
        break;
    case XSET_JOB_REMOVE:
    case XSET_JOB_REMOVE_BOOK:
        if (set->plugin)
        {
            xset_remove_plugin(parent, set->browser, set);
            break;
        }
        if (set->menu_label && set->menu_label[0])
            name = clean_label(set->menu_label, FALSE, FALSE);
        else
        {
            if (!set->lock && set->z && set->menu_style < XSET_MENU_SUBMENU &&
                (cmd_type == XSET_CMD_BOOKMARK || cmd_type == XSET_CMD_APP))
                name = g_strdup(set->z);
            else
                name = g_strdup(_("( no name )"));
        }
        int buttons;
        if (set->child && set->menu_style == XSET_MENU_SUBMENU)
        {
            msg = g_strdup_printf(_("Permanently remove the '%s' SUBMENU AND ALL ITEMS WITHIN IT?\n\nThis action will "
                                    "delete all settings and files associated with these items."),
                                  name);
            buttons = GTK_BUTTONS_YES_NO;
        }
        else
        {
            msg = g_strdup_printf(_("Permanently remove the '%s' item?\n\nThis action will delete all settings and "
                                    "files associated with this item."),
                                  name);
            buttons = GTK_BUTTONS_OK_CANCEL;
        }
        g_free(name);
        gboolean is_bookmark_or_app = !set->lock && set->menu_style < XSET_MENU_SUBMENU &&
                                      (cmd_type == XSET_CMD_BOOKMARK || cmd_type == XSET_CMD_APP) &&
                                      set->tool <= XSET_TOOL_CUSTOM;
        if (set->menu_style != XSET_MENU_SEP && !app_settings.no_confirm && !is_bookmark_or_app &&
            set->tool <= XSET_TOOL_CUSTOM)
        {
            if (parent)
                dlgparent = gtk_widget_get_toplevel(parent);
            GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(dlgparent),
                                                    GTK_DIALOG_MODAL,
                                                    GTK_MESSAGE_WARNING,
                                                    buttons,
                                                    msg,
                                                    NULL);
            xset_set_window_icon(GTK_WINDOW(dlg));
            gtk_window_set_title(GTK_WINDOW(dlg), _("Confirm Remove"));
            gtk_widget_show_all(dlg);
            int response = gtk_dialog_run(GTK_DIALOG(dlg));
            gtk_widget_destroy(dlg);
            if (response != GTK_RESPONSE_OK && response != GTK_RESPONSE_YES)
                break;
        }
        g_free(msg);

        // remove
        name = g_strdup(set->name);
        char* prog = g_strdup(set->parent);

        if (job == XSET_JOB_REMOVE && set->parent /* maybe only item in sub */
            && xset_is_main_bookmark(set))
            job = XSET_JOB_REMOVE_BOOK;

        if (set->parent && (set_next = xset_is(set->parent)) && set_next->tool == XSET_TOOL_CUSTOM &&
            set_next->menu_style == XSET_MENU_SUBMENU)
            // this set is first item in custom toolbar submenu
            update_toolbars = TRUE;

        childset = xset_custom_remove(set);

        if (childset && job == XSET_JOB_REMOVE_BOOK)
        {
            // added a new default item to submenu from a bookmark
            folder = set->browser ? (char*)ptk_file_browser_get_cwd(set->browser) : (char*)vfs_get_desktop_dir();
            g_free(childset->menu_label);
            childset->menu_label = g_path_get_basename(folder);
            childset->z = g_strdup(folder);
            childset->x = g_strdup_printf("%d", XSET_CMD_BOOKMARK);
            childset->task = childset->task_err = childset->task_out = childset->keep_terminal = XSET_B_UNSET;
        }
        else if (set->tool)
        {
            update_toolbars = TRUE;
            g_free(name);
            g_free(prog);
            name = prog = NULL;
        }
        else
        {
            g_free(prog);
            prog = NULL;
        }

        xset_custom_delete(set, FALSE);
        set = NULL;

        if (prog)
            main_window_bookmark_changed(prog);
        else if (name)
            main_window_bookmark_changed(name);
        g_free(name);
        g_free(prog);
        break;
    case XSET_JOB_EXPORT:
        if ((!set->lock || !g_strcmp0(set->name, "main_book")) && set->tool <= XSET_TOOL_CUSTOM)
            xset_custom_export(parent, set->browser, set);
        break;
    case XSET_JOB_NORMAL:
        set->menu_style = XSET_MENU_NORMAL;
        break;
    case XSET_JOB_CHECK:
        set->menu_style = XSET_MENU_CHECK;
        break;
    case XSET_JOB_CONFIRM:
        if (!set->desc)
            set->desc = g_strdup(_("Are you sure?"));
        if (xset_text_dialog(parent,
                             _("Dialog Message"),
                             NULL,
                             TRUE,
                             _("Enter the message to be displayed in this dialog:\n\nUse:\n\t\\n\tnewline\n\t\\t\ttab"),
                             NULL,
                             set->desc,
                             &set->desc,
                             NULL,
                             FALSE,
                             "#designmode-style-input"))
            set->menu_style = XSET_MENU_CONFIRM;
        break;
    case XSET_JOB_DIALOG:
        if (xset_text_dialog(parent,
                             _("Dialog Message"),
                             NULL,
                             TRUE,
                             _("Enter the message to be displayed in this dialog:\n\nUse:\n\t\\n\tnewline\n\t\\t\ttab"),
                             NULL,
                             set->desc,
                             &set->desc,
                             NULL,
                             FALSE,
                             "#designmode-style-input"))
            set->menu_style = XSET_MENU_STRING;
        break;
    case XSET_JOB_MESSAGE:
        xset_text_dialog(parent,
                         _("Dialog Message"),
                         NULL,
                         TRUE,
                         _("Enter the message to be displayed in this dialog:\n\nUse:\n\t\\n\tnewline\n\t\\t\ttab"),
                         NULL,
                         set->desc,
                         &set->desc,
                         NULL,
                         FALSE,
                         "#designmode-style-message");
        break;
    case XSET_JOB_PROP:
        xset_item_prop_dlg(xset_context, set, 0);
        break;
    case XSET_JOB_PROP_CMD:
        xset_item_prop_dlg(xset_context, set, 2);
        break;
    case XSET_JOB_IGNORE_CONTEXT:
        xset_set_b("context_dlg", !xset_get_b("context_dlg"));
        break;
    case XSET_JOB_HELP_BOOK:
    case XSET_JOB_HELP:
        if (parent)
            dlgparent = gtk_widget_get_toplevel(parent);

        // is a bookmark or app?
        if (!set->lock && set->menu_style < XSET_MENU_SUBMENU &&
            (cmd_type == XSET_CMD_BOOKMARK || cmd_type == XSET_CMD_APP))
        {
            // is a bookmark or app so show manual
            xset_show_help(dlgparent, NULL, job == XSET_JOB_HELP_BOOK ? "#gui-book" : "#designmode");
        }
        else if (set->tool > XSET_TOOL_CUSTOM)
        {
            // is a builtin tool item so show manual
            xset_show_help(dlgparent, NULL, "#designmode-toolbars");
        }
        else
            // show set-specific help
            xset_show_help(dlgparent, set, NULL);
        break;
    case XSET_JOB_BROWSE_FILES:
        if (set->tool > XSET_TOOL_CUSTOM)
            break;
        if (set->plugin)
        {
            folder = g_build_filename(set->plug_dir, "files", NULL);
            if (!g_file_test(folder, G_FILE_TEST_EXISTS))
            {
                g_free(folder);
                folder = g_build_filename(set->plug_dir, set->plug_name, NULL);
            }
        }
        else
        {
            folder = g_build_filename(settings_config_dir, "scripts", set->name, NULL);
        }
        if (!g_file_test(folder, G_FILE_TEST_EXISTS) && !set->plugin)
        {
            g_mkdir_with_parents(folder, 0700);
        }

        if (set->browser)
        {
            ptk_file_browser_emit_open(set->browser, folder, PTK_OPEN_DIR);
        }
        break;
    case XSET_JOB_BROWSE_DATA:
        if (set->tool > XSET_TOOL_CUSTOM)
            break;
        if (set->plugin)
        {
            mset = xset_get_plugin_mirror(set);
            folder = g_build_filename(settings_config_dir, "plugin-data", mset->name, NULL);
        }
        else
            folder = g_build_filename(settings_config_dir, "plugin-data", set->name, NULL);
        if (!g_file_test(folder, G_FILE_TEST_EXISTS))
        {
            g_mkdir_with_parents(folder, 0700);
        }

        if (set->browser)
        {
            ptk_file_browser_emit_open(set->browser, folder, PTK_OPEN_DIR);
        }
        break;
    case XSET_JOB_BROWSE_PLUGIN:
        if (set->plugin && set->plug_dir)
        {
            if (set->browser)
            {
                ptk_file_browser_emit_open(set->browser, set->plug_dir, PTK_OPEN_DIR);
            }
        }
        break;
    case XSET_JOB_TERM:
        mset = xset_get_plugin_mirror(set);
        if (mset->in_terminal == XSET_B_TRUE)
            mset->in_terminal = XSET_B_UNSET;
        else
        {
            mset->in_terminal = XSET_B_TRUE;
            mset->task = XSET_B_FALSE;
        }
        break;
    case XSET_JOB_KEEP:
        mset = xset_get_plugin_mirror(set);
        if (mset->keep_terminal == XSET_B_TRUE)
            mset->keep_terminal = XSET_B_UNSET;
        else
            mset->keep_terminal = XSET_B_TRUE;
        break;
    case XSET_JOB_TASK:
        mset = xset_get_plugin_mirror(set);
        if (mset->task == XSET_B_TRUE)
            mset->task = XSET_B_UNSET;
        else
            mset->task = XSET_B_TRUE;
        break;
    case XSET_JOB_POP:
        mset = xset_get_plugin_mirror(set);
        if (mset->task_pop == XSET_B_TRUE)
            mset->task_pop = XSET_B_UNSET;
        else
            mset->task_pop = XSET_B_TRUE;
        break;
    case XSET_JOB_ERR:
        mset = xset_get_plugin_mirror(set);
        if (mset->task_err == XSET_B_TRUE)
            mset->task_err = XSET_B_UNSET;
        else
            mset->task_err = XSET_B_TRUE;
        break;
    case XSET_JOB_OUT:
        mset = xset_get_plugin_mirror(set);
        if (mset->task_out == XSET_B_TRUE)
            mset->task_out = XSET_B_UNSET;
        else
            mset->task_out = XSET_B_TRUE;
        break;
    case XSET_JOB_SCROLL:
        mset = xset_get_plugin_mirror(set);
        if (mset->scroll_lock == XSET_B_TRUE)
            mset->scroll_lock = XSET_B_UNSET;
        else
            mset->scroll_lock = XSET_B_TRUE;
        break;
    case XSET_JOB_TOOLTIPS:
        set_next = xset_get_panel(1, "tool_l");
        set_next->b = set_next->b == XSET_B_TRUE ? XSET_B_UNSET : XSET_B_TRUE;
        break;
    default:
        break;
    }

    if (set && (!set->lock || !strcmp(set->name, "main_book")))
    {
        main_window_bookmark_changed(set->name);
        if (set->parent && (set_next = xset_is(set->parent)) && set_next->tool == XSET_TOOL_CUSTOM &&
            set_next->menu_style == XSET_MENU_SUBMENU)
            // this set is first item in custom toolbar submenu
            update_toolbars = TRUE;
    }

    if ((set && !set->lock && set->tool) || update_toolbars)
        main_window_rebuild_all_toolbars(set ? set->browser : NULL);

    // autosave
    xset_autosave(FALSE, FALSE);
}

void on_design_radio_toggled(GtkCheckMenuItem* item, XSet* set)
{
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
        xset_design_job(GTK_WIDGET(item), set);
}

gboolean xset_job_is_valid(XSet* set, int job)
{
    gboolean no_remove = FALSE;
    gboolean no_paste = FALSE;
    gboolean open_all = FALSE;

    if (!set)
        return FALSE;

    if (set->plugin)
    {
        if (!set->plug_dir)
            return FALSE;
        if (!set->plugin_top || strstr(set->plug_dir, "/included/"))
            no_remove = TRUE;
    }

    // control open_all item
    if (g_str_has_prefix(set->name, "open_all_type_"))
        open_all = TRUE;

    switch (job)
    {
    case XSET_JOB_KEY:
        return set->menu_style < XSET_MENU_SUBMENU;
    case XSET_JOB_ICON:
        return ((set->menu_style == XSET_MENU_NORMAL || set->menu_style == XSET_MENU_STRING ||
                 set->menu_style == XSET_MENU_FONTDLG || set->menu_style == XSET_MENU_COLORDLG ||
                 set->menu_style == XSET_MENU_SUBMENU || set->tool) &&
                !open_all);
    case XSET_JOB_EDIT:
        return !set->lock && set->menu_style < XSET_MENU_SUBMENU;
    case XSET_JOB_COMMAND:
        return !set->plugin;
    case XSET_JOB_CUT:
        return (!set->lock && !set->plugin);
    case XSET_JOB_COPY:
        return !set->lock;
    case XSET_JOB_PASTE:
        if (!set_clipboard)
            no_paste = TRUE;
        else if (set->plugin)
            no_paste = TRUE;
        else if (set == set_clipboard && clipboard_is_cut)
            // don't allow cut paste to self
            no_paste = TRUE;
        else if (set_clipboard->tool > XSET_TOOL_CUSTOM && !set->tool)
            // don't allow paste of builtin tool item to menu
            no_paste = TRUE;
        else if (set_clipboard->menu_style == XSET_MENU_SUBMENU)
            // don't allow paste of submenu to self or below
            no_paste = xset_clipboard_in_set(set);
        return !no_paste;
    case XSET_JOB_REMOVE:
        return (!set->lock && !no_remove);
    // case XSET_JOB_CONTEXT:
    //    return ( xset_context && xset_context->valid && !open_all );
    case XSET_JOB_PROP:
    case XSET_JOB_PROP_CMD:
        return TRUE;
    case XSET_JOB_HELP:
        return (!set->lock || set->line);
    default:
        break;
    }
    return FALSE;
}

gboolean xset_design_menu_keypress(GtkWidget* widget, GdkEventKey* event, XSet* set)
{
    int job = -1;

    GtkWidget* item = gtk_menu_shell_get_selected_item(GTK_MENU_SHELL(widget));
    if (!item)
        return FALSE;

    int keymod = (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK |
                                  GDK_META_MASK));

    transpose_nonlatin_keypress(event);

    if (keymod == 0)
    {
        if (event->keyval == GDK_KEY_F1)
        {
            const char* help = NULL;
            job = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "job"));
            switch (job)
            {
            case XSET_JOB_KEY:
                help = "#designmode-designmenu-key";
                break;
            case XSET_JOB_ICON:
                help = "#designmode-designmenu-icon";
                break;
            case XSET_JOB_LABEL:
                help = "#designmode-designmenu-name";
                break;
            case XSET_JOB_EDIT: // edit script
                help = "#designmode-designmenu-edit";
                break;
            case XSET_JOB_PROP_CMD: // edit command line
                help = "#designmode-designmenu-cedit";
                break;
            case XSET_JOB_EDIT_ROOT:
                help = "#designmode-designmenu-edit";
                break;
            case XSET_JOB_COPYNAME:
                help = "#designmode-command-copy";
                break;
            case XSET_JOB_LINE:
                help = "#designmode-command-line";
                break;
            case XSET_JOB_SCRIPT:
                help = "#designmode-command-script";
                break;
            case XSET_JOB_CUSTOM:
                help = "#designmode-command-custom";
                break;
            case XSET_JOB_USER:
                help = "#designmode-command-user";
                break;
            case XSET_JOB_COMMAND:
                help = "#designmode-designmenu-new";
                break;
            case XSET_JOB_SUBMENU:
                help = "#designmode-designmenu-submenu";
                break;
            case XSET_JOB_SEP:
                help = "#designmode-designmenu-separator";
                break;
            case XSET_JOB_IMPORT_FILE:
                help = "#designmode-designmenu-import";
                break;
            case XSET_JOB_CUT:
                help = "#designmode-designmenu-cut";
                break;
            case XSET_JOB_COPY:
                help = "#designmode-designmenu-copy";
                break;
            case XSET_JOB_PASTE:
                help = "#designmode-designmenu-paste";
                break;
            case XSET_JOB_REMOVE:
                help = "#designmode-designmenu-remove";
                break;
            case XSET_JOB_EXPORT:
                help = "#designmode-designmenu-export";
                break;
            case XSET_JOB_BOOKMARK:
                help = "#designmode-designmenu-bookmark";
                break;
            case XSET_JOB_APP:
                help = "#designmode-designmenu-app";
                break;
            case XSET_JOB_NORMAL:
                help = "#designmode-style-normal";
                break;
            case XSET_JOB_CHECK:
                help = "#designmode-style-checkbox";
                break;
            case XSET_JOB_CONFIRM:
                help = "#designmode-style-confirm";
                break;
            case XSET_JOB_DIALOG:
                help = "#designmode-style-input";
                break;
            case XSET_JOB_MESSAGE:
                help = "#designmode-style-message";
                break;
            // case XSET_JOB_CONTEXT:
            //    help = "#designmode-props-context";
            //    break;
            case XSET_JOB_IGNORE_CONTEXT:
                help = "#designmode-props-ignorecontext";
                break;
            case XSET_JOB_HELP:
                help = "#designmode-designmenu-help";
                break;
            case XSET_JOB_HELP_STYLE:
                help = "#designmode-style";
                break;
            case XSET_JOB_HELP_NEW:
                help = "#designmode-designmenu-bookmark";
                break;
            case XSET_JOB_HELP_ADD:
                help = "#designmode-designmenu-add";
                break;
            case XSET_JOB_PROP:
                help = "#designmode-props";
                break;
            case XSET_JOB_HELP_BROWSE:
                help = "#designmode-command-browse";
                break;
            case XSET_JOB_BROWSE_FILES:
                help = "#designmode-command-browse-files";
                break;
            case XSET_JOB_BROWSE_DATA:
                help = "#designmode-command-browse-data";
                break;
            case XSET_JOB_BROWSE_PLUGIN:
                help = "#designmode-command-browse-plugin";
                break;
            case XSET_JOB_TERM:
                help = "#designmode-command-terminal";
                break;
            case XSET_JOB_KEEP:
                help = "#designmode-command-keep";
                break;
            case XSET_JOB_TASK:
                help = "#designmode-command-task";
                break;
            case XSET_JOB_POP:
                help = "#designmode-command-popup";
                break;
            case XSET_JOB_ERR:
                help = "#designmode-command-poperr";
                break;
            case XSET_JOB_OUT:
                help = "#designmode-command-popout";
                break;
            case XSET_JOB_SCROLL:
                help = "#designmode-command-scroll";
                break;
            case XSET_JOB_TOOLTIPS:
                help = "#designmode-designmenu-tooltips";
                break;
            default:
                break;
            }
            if (!help)
                help = "#designmode";
            gtk_menu_shell_deactivate(GTK_MENU_SHELL(widget));
            xset_show_help(NULL, NULL, help);
            return TRUE;
        }
        else if (event->keyval == GDK_KEY_F3)
            job = XSET_JOB_PROP;
        else if (event->keyval == GDK_KEY_F4)
        {
            if (xset_get_int_set(set, "x") == XSET_CMD_SCRIPT)
                job = XSET_JOB_EDIT;
            else
                job = XSET_JOB_PROP_CMD;
        }
        else if (event->keyval == GDK_KEY_Delete)
            job = XSET_JOB_REMOVE;
        else if (event->keyval == GDK_KEY_Insert)
            job = XSET_JOB_COMMAND;
    }
    else if (keymod == GDK_CONTROL_MASK)
    {
        if (event->keyval == GDK_KEY_c)
            job = XSET_JOB_COPY;
        else if (event->keyval == GDK_KEY_x)
            job = XSET_JOB_CUT;
        else if (event->keyval == GDK_KEY_v)
            job = XSET_JOB_PASTE;
        else if (event->keyval == GDK_KEY_e)
        {
            if (set->lock)
            {
                return FALSE;
            }
            else
                job = XSET_JOB_EDIT;
        }
        else if (event->keyval == GDK_KEY_k)
            job = XSET_JOB_KEY;
        else if (event->keyval == GDK_KEY_i)
            job = XSET_JOB_ICON;
    }
    if (job != -1)
    {
        if (xset_job_is_valid(set, job))
        {
            gtk_menu_shell_deactivate(GTK_MENU_SHELL(widget));
            g_object_set_data(G_OBJECT(item), "job", GINT_TO_POINTER(job));
            xset_design_job(item, set);
            return TRUE;
        }
    }
    return FALSE;
}

void xset_design_destroy(GtkWidget* item, GtkWidget* design_menu)
{
    // g_printf( "xset_design_destroy\n");
    // close design_menu if menu deactivated
    gtk_widget_set_sensitive(item, TRUE);
    gtk_menu_shell_deactivate(GTK_MENU_SHELL(design_menu));
}

void on_menu_hide(GtkWidget* widget, GtkWidget* design_menu)
{
    gtk_widget_set_sensitive(widget, TRUE);
    gtk_menu_shell_deactivate(GTK_MENU_SHELL(design_menu));
}

static void set_check_menu_item_block(GtkWidget* item)
{
    g_signal_handlers_block_matched(item, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, xset_design_job, NULL);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
    g_signal_handlers_unblock_matched(item, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, xset_design_job, NULL);
}

GtkWidget* xset_design_additem(GtkWidget* menu, const char* label, const char* stock_icon, int job, XSet* set)
{
    GtkWidget* item;
    if (stock_icon)
    {
        if (!strcmp(stock_icon, "@check"))
            item = gtk_check_menu_item_new_with_mnemonic(label);
        else
        {
#if (GTK_MAJOR_VERSION == 3)
            item = gtk_menu_item_new_with_mnemonic(label);
#elif (GTK_MAJOR_VERSION == 2)
            item = gtk_image_menu_item_new_with_mnemonic(label);
#endif
        }
    }
    else
        item = gtk_menu_item_new_with_mnemonic(label);

    g_object_set_data(G_OBJECT(item), "job", GINT_TO_POINTER(job));
    gtk_container_add(GTK_CONTAINER(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(xset_design_job), set);
    return item;
}

GtkWidget* xset_design_show_menu(GtkWidget* menu, XSet* set, XSet* book_insert, uint button, guint32 time)
{
    GtkWidget* newitem;
    GtkWidget* submenu;
    GtkWidget* submenu2;
    gboolean no_remove = FALSE;
    gboolean no_paste = FALSE;

    // book_insert is a bookmark set to be used for Paste, etc
    XSet* insert_set = book_insert ? book_insert : set;
    // to signal this is a bookmark, pass book_insert = set
    gboolean is_bookmark = !!book_insert;
    gboolean show_keys = !is_bookmark && !set->tool;

    if (set->plugin && set->shared_key)
        xset_get_plugin_mirror(set);

    if (set->plugin)
    {
        if (set->plug_dir)
        {
            if (!set->plugin_top || strstr(set->plug_dir, "/included/"))
                no_remove = TRUE;
        }
        else
            no_remove = TRUE;
    }

    if (!set_clipboard)
        no_paste = TRUE;
    else if (insert_set->plugin)
        no_paste = TRUE;
    else if (insert_set == set_clipboard && clipboard_is_cut)
        // don't allow cut paste to self
        no_paste = TRUE;
    else if (set_clipboard->tool > XSET_TOOL_CUSTOM && !insert_set->tool)
        // don't allow paste of builtin tool item to menu
        no_paste = TRUE;
    else if (set_clipboard->menu_style == XSET_MENU_SUBMENU)
        // don't allow paste of submenu to self or below
        no_paste = xset_clipboard_in_set(insert_set);

    GtkWidget* design_menu = gtk_menu_new();
    GtkAccelGroup* accel_group = gtk_accel_group_new();

    // Cut
    newitem = xset_design_additem(design_menu, _("Cu_t"), GTK_STOCK_CUT, XSET_JOB_CUT, set);
    gtk_widget_set_sensitive(newitem, !set->lock && !set->plugin);
    if (show_keys)
        gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_x, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    // Copy
    newitem = xset_design_additem(design_menu, _("_Copy"), GTK_STOCK_COPY, XSET_JOB_COPY, set);
    gtk_widget_set_sensitive(newitem, !set->lock);
    if (show_keys)
        gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    // Paste
    newitem = xset_design_additem(design_menu, _("_Paste"), GTK_STOCK_PASTE, XSET_JOB_PASTE, insert_set);
    gtk_widget_set_sensitive(newitem, !no_paste);
    if (show_keys)
        gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_v, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    // Remove
    newitem = xset_design_additem(design_menu,
                                  _("_Remove"),
                                  GTK_STOCK_REMOVE,
                                  is_bookmark ? XSET_JOB_REMOVE_BOOK : XSET_JOB_REMOVE,
                                  set);
    gtk_widget_set_sensitive(newitem, !set->lock && !no_remove);
    if (show_keys)
        gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_Delete, 0, GTK_ACCEL_VISIBLE);

    // Export
    newitem = xset_design_additem(design_menu, _("E_xport"), GTK_STOCK_SAVE, XSET_JOB_EXPORT, set);
    gtk_widget_set_sensitive(newitem,
                             (!set->lock && set->menu_style < XSET_MENU_SEP && set->tool <= XSET_TOOL_CUSTOM) ||
                                 !g_strcmp0(set->name, "main_book"));

    //// New submenu
#if (GTK_MAJOR_VERSION == 3)
    newitem = gtk_menu_item_new_with_mnemonic(_("_New"));
#elif (GTK_MAJOR_VERSION == 2)
    newitem = gtk_image_menu_item_new_with_mnemonic(_("_New"));
#endif
    submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(newitem), submenu);
    gtk_container_add(GTK_CONTAINER(design_menu), newitem);
    gtk_widget_set_sensitive(newitem, !set->plugin);
    g_object_set_data(G_OBJECT(newitem), "job", GINT_TO_POINTER(XSET_JOB_HELP_NEW));
    g_signal_connect(submenu, "key_press_event", G_CALLBACK(xset_design_menu_keypress), set);

    // New > Bookmark
    newitem = xset_design_additem(submenu, _("_Bookmark"), NULL, XSET_JOB_BOOKMARK, insert_set);

    // New > Application
    newitem = xset_design_additem(submenu, _("_Application"), NULL, XSET_JOB_APP, insert_set);

    // New > Command
    newitem = xset_design_additem(submenu, _("_Command"), NULL, XSET_JOB_COMMAND, insert_set);
    if (show_keys)
        gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_Insert, 0, GTK_ACCEL_VISIBLE);

    // New > Submenu
    newitem = xset_design_additem(submenu,
                                  _("Sub_menu"),
                                  NULL,
                                  is_bookmark ? XSET_JOB_SUBMENU_BOOK : XSET_JOB_SUBMENU,
                                  insert_set);

    // New > Separator
    newitem = xset_design_additem(submenu, _("S_eparator"), NULL, XSET_JOB_SEP, insert_set);

    // New > Import >
#if (GTK_MAJOR_VERSION == 3)
    newitem = gtk_menu_item_new_with_mnemonic(_("_Import"));
#elif (GTK_MAJOR_VERSION == 2)
    newitem = gtk_image_menu_item_new_with_mnemonic(_("_Import"));
#endif
    submenu2 = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(newitem), submenu2);
    gtk_container_add(GTK_CONTAINER(submenu), newitem);
    gtk_widget_set_sensitive(newitem, !insert_set->plugin);
    g_object_set_data(G_OBJECT(newitem), "job", GINT_TO_POINTER(XSET_JOB_IMPORT_FILE));
    g_signal_connect(submenu2, "key_press_event", G_CALLBACK(xset_design_menu_keypress), insert_set);

    newitem = xset_design_additem(submenu2, _("_File"), NULL, XSET_JOB_IMPORT_FILE, insert_set);
    if (is_bookmark)
        newitem = xset_design_additem(submenu2, _("_GTK Bookmarks"), NULL, XSET_JOB_IMPORT_GTK, set);

    if (insert_set->tool)
    {
        // "Add" submenu for builtin tool items
#if (GTK_MAJOR_VERSION == 3)
        newitem = gtk_menu_item_new_with_mnemonic(_("_Add"));
#elif (GTK_MAJOR_VERSION == 2)
        newitem = gtk_image_menu_item_new_with_mnemonic(_("_Add"));
#endif
        submenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(newitem), submenu);
        gtk_container_add(GTK_CONTAINER(design_menu), newitem);
        g_object_set_data(G_OBJECT(newitem), "job", GINT_TO_POINTER(XSET_JOB_HELP_ADD));
        g_signal_connect(submenu, "key_press_event", G_CALLBACK(xset_design_menu_keypress), set);

        int i;
        for (i = XSET_TOOL_DEVICES; i < G_N_ELEMENTS(builtin_tool_name); i++)
        {
            newitem = xset_design_additem(submenu,
                                          _(builtin_tool_name[i]),
                                          builtin_tool_icon[i],
                                          XSET_JOB_ADD_TOOL,
                                          insert_set);
            g_object_set_data(G_OBJECT(newitem), "tool_type", GINT_TO_POINTER(i));
        }
    }

    // Separator
    gtk_container_add(GTK_CONTAINER(design_menu), gtk_separator_menu_item_new());

    // Help
    newitem = xset_design_additem(design_menu,
                                  _("_Help"),
                                  GTK_STOCK_HELP,
                                  is_bookmark ? XSET_JOB_HELP_BOOK : XSET_JOB_HELP,
                                  set);
    gtk_widget_set_sensitive(newitem, !set->lock || set->line);
    if (show_keys)
        gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_F1, 0, GTK_ACCEL_VISIBLE);

    // Tooltips (toolbar)
    if (set->tool)
    {
        newitem = xset_design_additem(design_menu, _("T_ooltips"), "@check", XSET_JOB_TOOLTIPS, set);
        if (!xset_get_b_panel(1, "tool_l"))
            set_check_menu_item_block(newitem);
    }

    // Key
    newitem = xset_design_additem(design_menu, _("_Key Shortcut"), GTK_STOCK_PROPERTIES, XSET_JOB_KEY, set);
    gtk_widget_set_sensitive(newitem, (set->menu_style < XSET_MENU_SUBMENU));
    if (show_keys)
        gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_k, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    // Edit (script)
    if (!set->lock && set->menu_style < XSET_MENU_SUBMENU && set->tool <= XSET_TOOL_CUSTOM)
    {
        if (xset_get_int_set(set, "x") == XSET_CMD_SCRIPT)
        {
            char* script = xset_custom_get_script(set, FALSE);
            if (script)
            {
                if (geteuid() != 0 && have_rw_access(script))
                {
                    // edit as user
                    newitem = xset_design_additem(design_menu, _("_Edit Script"), GTK_STOCK_EDIT, XSET_JOB_EDIT, set);
                    if (show_keys)
                        gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_F4, 0, GTK_ACCEL_VISIBLE);
                }
                else
                {
                    // edit as root
                    newitem = xset_design_additem(design_menu,
                                                  _("E_dit As Root"),
                                                  GTK_STOCK_DIALOG_WARNING,
                                                  XSET_JOB_EDIT_ROOT,
                                                  set);
                    if (geteuid() == 0 && show_keys)
                        gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_F4, 0, GTK_ACCEL_VISIBLE);
                }
                g_free(script);
            }
        }
        else if (xset_get_int_set(set, "x") == XSET_CMD_LINE)
        {
            // edit command line
            newitem = xset_design_additem(design_menu, _("_Edit Command"), GTK_STOCK_EDIT, XSET_JOB_PROP_CMD, set);
            if (show_keys)
                gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_F4, 0, GTK_ACCEL_VISIBLE);
        }
    }

    // Properties
    newitem = xset_design_additem(design_menu, _("_Properties"), GTK_STOCK_PROPERTIES, XSET_JOB_PROP, set);
    if (show_keys)
        gtk_widget_add_accelerator(newitem, "activate", accel_group, GDK_KEY_F3, 0, GTK_ACCEL_VISIBLE);

    // show menu
    gtk_widget_show_all(GTK_WIDGET(design_menu));
    /* sfm 1.0.6 passing button (3) here when menu == NULL causes items in New
     * submenu to not activate with some trackpads (eg two-finger right-click)
     * to open original design menu.  Affected only bookmarks pane and toolbar
     * where menu == NULL.  So pass 0 for button if !menu. */
    gtk_menu_popup(GTK_MENU(design_menu), menu ? GTK_WIDGET(menu) : NULL, NULL, NULL, NULL, menu ? button : 0, time);
    if (menu)
    {
        gtk_widget_set_sensitive(GTK_WIDGET(menu), FALSE);
        g_signal_connect(menu, "hide", G_CALLBACK(on_menu_hide), design_menu);
    }
    g_signal_connect(design_menu, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);
    g_signal_connect(design_menu, "key_press_event", G_CALLBACK(xset_design_menu_keypress), set);

    gtk_menu_shell_set_take_focus(GTK_MENU_SHELL(design_menu), TRUE);
    // this is required when showing the menu via F2 or Menu key for focus
    gtk_menu_shell_select_first(GTK_MENU_SHELL(design_menu), TRUE);

    return design_menu;
}

gboolean xset_design_cb(GtkWidget* item, GdkEventButton* event, XSet* set)
{
    int job = -1;

    GtkWidget* menu = item ? (GtkWidget*)g_object_get_data(G_OBJECT(item), "menu") : NULL;
    int keymod = (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK |
                                  GDK_META_MASK));

    if (event->type == GDK_BUTTON_RELEASE)
    {
        if (event->button == 1 && keymod == 0)
        {
            // user released left button - due to an apparent gtk bug, activate
            // doesn't always fire on this event so handle it ourselves
            // see also ptk-file-menu.c on_app_button_press()
            // test: gtk2 Crux theme with touchpad on Edit|Copy To|Location
            // https://github.com/IgnorantGuru/spacefm/issues/31
            // https://github.com/IgnorantGuru/spacefm/issues/228
            if (menu)
                gtk_menu_shell_deactivate(GTK_MENU_SHELL(menu));
            gtk_menu_item_activate(GTK_MENU_ITEM(item));
            return TRUE;
        }
        // TRUE for issue #521 where a right-click also left-clicks the first
        // menu item in some GTK2/3 themes.
        return TRUE;
    }
    else if (event->type != GDK_BUTTON_PRESS)
        return FALSE;

    if (event->button == 1 || event->button == 3)
    {
        // left or right click
        if (keymod == 0)
        {
            // no modifier
            if (event->button == 3)
            {
                // right
                xset_design_show_menu(menu, set, NULL, event->button, event->time);
                return TRUE;
            }
            else if (event->button == 1 && set->tool && !set->lock)
            {
                // activate
                if (set->tool == XSET_TOOL_CUSTOM)
                    xset_menu_cb(NULL, set);
                else
                    xset_builtin_tool_activate(set->tool, set, event);
                return TRUE;
            }
        }
        else if (keymod == GDK_CONTROL_MASK)
        {
            // ctrl
            job = XSET_JOB_COPY;
        }
        else if (keymod == GDK_MOD1_MASK)
        {
            // alt
            job = XSET_JOB_CUT;
        }
        else if (keymod == GDK_SHIFT_MASK)
        {
            // shift
            job = XSET_JOB_PASTE;
        }
        else if (keymod == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
        {
            // ctrl + shift
            job = XSET_JOB_COMMAND;
        }
    }
    else if (event->button == 2)
    {
        // middle click
        if (keymod == 0)
        {
            // no modifier
            if (set->lock)
            {
                xset_design_show_menu(menu, set, NULL, event->button, event->time);
                return TRUE;
            }
            else
            {
                if (xset_get_int_set(set, "x") == XSET_CMD_SCRIPT)
                    job = XSET_JOB_EDIT;
                else
                    job = XSET_JOB_PROP_CMD;
            }
        }
        else if (keymod == GDK_CONTROL_MASK)
        {
            // ctrl
            job = XSET_JOB_KEY;
        }
        else if (keymod == GDK_MOD1_MASK)
        {
            // alt
            job = XSET_JOB_HELP;
        }
        else if (keymod == GDK_SHIFT_MASK)
        {
            // shift
            job = XSET_JOB_ICON;
        }
        else if (keymod == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
        {
            // ctrl + shift
            job = XSET_JOB_REMOVE;
        }
        else if (keymod == (GDK_CONTROL_MASK | GDK_MOD1_MASK))
        {
            // ctrl + alt
            job = XSET_JOB_PROP;
        }
    }

    if (job != -1)
    {
        if (xset_job_is_valid(set, job))
        {
            if (menu)
                gtk_menu_shell_deactivate(GTK_MENU_SHELL(menu));
            g_object_set_data(G_OBJECT(item), "job", GINT_TO_POINTER(job));
            xset_design_job(item, set);
        }
        else
            xset_design_show_menu(menu, set, NULL, event->button, event->time);
        return TRUE;
    }
    return FALSE; // TRUE won't stop activate on button-press (will on release)
}

gboolean xset_menu_keypress(GtkWidget* widget, GdkEventKey* event, gpointer user_data)
{
    int job = -1;
    XSet* set;

    GtkWidget* item = gtk_menu_shell_get_selected_item(GTK_MENU_SHELL(widget));
    if (item)
    {
        set = g_object_get_data(G_OBJECT(item), "set");
        if (!set)
            return FALSE;
    }
    else
        return FALSE;

    int keymod = (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK |
                                  GDK_META_MASK));

    transpose_nonlatin_keypress(event);

    if (keymod == 0)
    {
        if (event->keyval == GDK_KEY_F1)
        {
            job = XSET_JOB_HELP;
        }
        else if (event->keyval == GDK_KEY_F2 || event->keyval == GDK_KEY_Menu)
        {
            xset_design_show_menu(widget, set, NULL, 0, event->time);
            return TRUE;
        }
        else if (event->keyval == GDK_KEY_F3)
            job = XSET_JOB_PROP;
        else if (event->keyval == GDK_KEY_F4)
        {
            if (xset_get_int_set(set, "x") == XSET_CMD_SCRIPT)
                job = XSET_JOB_EDIT;
            else
                job = XSET_JOB_PROP_CMD;
        }
        else if (event->keyval == GDK_KEY_Delete)
            job = XSET_JOB_REMOVE;
        else if (event->keyval == GDK_KEY_Insert)
            job = XSET_JOB_COMMAND;
    }
    else if (keymod == GDK_CONTROL_MASK)
    {
        if (event->keyval == GDK_KEY_c)
            job = XSET_JOB_COPY;
        else if (event->keyval == GDK_KEY_x)
            job = XSET_JOB_CUT;
        else if (event->keyval == GDK_KEY_v)
            job = XSET_JOB_PASTE;
        else if (event->keyval == GDK_KEY_e)
        {
            if (set->lock)
            {
                xset_design_show_menu(widget, set, NULL, 0, event->time);
                return TRUE;
            }
            else
            {
                if (xset_get_int_set(set, "x") == XSET_CMD_SCRIPT)
                    job = XSET_JOB_EDIT;
                else
                    job = XSET_JOB_PROP_CMD;
            }
        }
        else if (event->keyval == GDK_KEY_k)
            job = XSET_JOB_KEY;
        else if (event->keyval == GDK_KEY_i)
            job = XSET_JOB_ICON;
    }
    if (job != -1)
    {
        if (xset_job_is_valid(set, job))
        {
            gtk_menu_shell_deactivate(GTK_MENU_SHELL(widget));
            g_object_set_data(G_OBJECT(item), "job", GINT_TO_POINTER(job));
            xset_design_job(item, set);
        }
        else
            xset_design_show_menu(widget, set, NULL, 0, event->time);
        return TRUE;
    }
    return FALSE;
}

void xset_menu_cb(GtkWidget* item, XSet* set)
{
    void (*cb_func)() = NULL;
    gpointer cb_data = NULL;
    char* title;
    XSet* mset; // mirror set or set
    XSet* rset; // real set

    if (item)
    {
        if (set->lock && set->menu_style == XSET_MENU_RADIO && GTK_IS_CHECK_MENU_ITEM(item) &&
            !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
            return;
        cb_func = (void*)g_object_get_data(G_OBJECT(item), "cb_func");
        cb_data = g_object_get_data(G_OBJECT(item), "cb_data");
    }

    GtkWidget* parent = set->browser;

    if (set->plugin)
    {
        // set is plugin
        mset = xset_get_plugin_mirror(set);
        rset = set;
    }
    else if (!set->lock && set->desc && !strcmp(set->desc, "@plugin@mirror@") && set->shared_key)
    {
        // set is plugin mirror
        mset = set;
        rset = xset_get(set->shared_key);
        rset->browser = set->browser;
    }
    else
    {
        mset = set;
        rset = set;
    }

    if (!rset->menu_style)
    {
        if (cb_func)
            (*cb_func)(item, cb_data);
        else if (!rset->lock)
            xset_custom_activate(item, rset);
    }
    else if (rset->menu_style == XSET_MENU_SEP)
    {
    }
    else if (rset->menu_style == XSET_MENU_CHECK)
    {
        if (mset->b == XSET_B_TRUE)
            mset->b = XSET_B_FALSE;
        else
            mset->b = XSET_B_TRUE;
        if (cb_func)
            (*cb_func)(item, cb_data);
        else if (!rset->lock)
            xset_custom_activate(item, rset);
        if (set->tool == XSET_TOOL_CUSTOM)
            ptk_file_browser_update_toolbar_widgets(set->browser, set, -1);
    }
    else if (rset->menu_style == XSET_MENU_STRING || rset->menu_style == XSET_MENU_CONFIRM)
    {
        char* msg;
        char* help;
        char* default_str = NULL;
        if (rset->title && rset->lock)
            title = g_strdup(rset->title);
        else
            title = clean_label(rset->menu_label, FALSE, FALSE);
        if (rset->lock)
        {
            msg = rset->desc;
            default_str = rset->z;
            help = set->line;
        }
        else
        {
            char* newline = g_strdup_printf("\n");
            char* tab = g_strdup_printf("\t");
            char* msg1 = replace_string(rset->desc, "\\n", newline, FALSE);
            msg = replace_string(msg1, "\\t", tab, FALSE);
            g_free(msg1);
            g_free(newline);
            g_free(tab);
            help = set->name;
        }
        if (rset->menu_style == XSET_MENU_CONFIRM)
        {
            if (xset_msg_dialog(parent, GTK_MESSAGE_QUESTION, title, NULL, GTK_BUTTONS_OK_CANCEL, msg, NULL, help) ==
                GTK_RESPONSE_OK)
            {
                if (cb_func)
                    (*cb_func)(item, cb_data);
                else if (!set->lock)
                    xset_custom_activate(item, rset);
            }
        }
        else if (xset_text_dialog(parent, title, NULL, TRUE, msg, NULL, mset->s, &mset->s, default_str, FALSE, help))
        {
            if (cb_func)
                (*cb_func)(item, cb_data);
            else if (!set->lock)
                xset_custom_activate(item, rset);
        }
        if (!rset->lock)
            g_free(msg);
        g_free(title);
    }
    else if (rset->menu_style == XSET_MENU_RADIO)
    {
        if (mset->b != XSET_B_TRUE)
            mset->b = XSET_B_TRUE;
        if (cb_func)
            (*cb_func)(item, cb_data);
        else if (!rset->lock)
            xset_custom_activate(item, rset);
    }
    else if (rset->menu_style == XSET_MENU_FONTDLG)
    {
        char* fontname = xset_font_dialog(parent, rset->title, rset->desc, rset->s);
        if (fontname)
        {
            if (fontname[0] != '\0')
            {
                xset_set_set(rset, "s", fontname);
            }
            else
            {
                if (rset->s)
                    g_free(rset->s);
                rset->s = NULL;
            }
            g_free(fontname);
            if (cb_func)
                (*cb_func)(item, cb_data);
        }
    }
    else if (rset->menu_style == XSET_MENU_FILEDLG)
    {
        // test purpose only
        // char* file
        xset_file_dialog(parent, GTK_FILE_CHOOSER_ACTION_SAVE, rset->title, rset->s, "foobar.xyz");
        // g_printf("file=%s\n", file );
    }
    else if (rset->menu_style == XSET_MENU_ICON)
    {
        // Note: xset_text_dialog uses the title passed to know this is an
        // icon chooser, so it adds a Choose button.  If you change the title,
        // change xset_text_dialog.
        if (xset_text_dialog(parent,
                             rset->title ? rset->title : _("Set Icon"),
                             NULL,
                             FALSE,
                             rset->desc ? rset->desc : _(icon_desc),
                             NULL,
                             rset->icon,
                             &rset->icon,
                             NULL,
                             FALSE,
                             NULL))
        {
            if (rset->lock)
                rset->keep_terminal = XSET_B_TRUE; // trigger save of changed icon
            if (cb_func)
                (*cb_func)(item, cb_data);
        }
    }
    else if (rset->menu_style == XSET_MENU_COLORDLG)
    {
        char* scolor = xset_color_dialog(parent, rset->title, rset->s);
        if (rset->s)
            g_free(rset->s);
        rset->s = scolor;
        if (cb_func)
            (*cb_func)(item, cb_data);
    }
    else if (cb_func)
        (*cb_func)(item, cb_data);
    else if (!set->lock)
        xset_custom_activate(item, rset);

    if (rset->menu_style)
        xset_autosave(FALSE, FALSE);
}

int xset_msg_dialog(GtkWidget* parent, int action, const char* title, GtkWidget* image, int buttons, const char* msg1,
                    const char* msg2, const char* help)
{
    /* action=
    GTK_MESSAGE_INFO,
    GTK_MESSAGE_WARNING,
    GTK_MESSAGE_QUESTION,
    GTK_MESSAGE_ERROR
    */
    GtkWidget* dlgparent = NULL;

    if (parent)
        dlgparent = gtk_widget_get_toplevel(parent);

    if (!buttons)
        buttons = GTK_BUTTONS_OK;
    if (action == 0)
        action = GTK_MESSAGE_INFO;

    GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(dlgparent),
                                            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                            action,
                                            buttons,
                                            msg1,
                                            NULL);
    if (action == GTK_MESSAGE_INFO)
        xset_set_window_icon(GTK_WINDOW(dlg));
    gtk_window_set_role(GTK_WINDOW(dlg), "msg_dialog");

    if (msg2)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), msg2, NULL);
    if (image)
        gtk_message_dialog_set_image(GTK_MESSAGE_DIALOG(dlg), image);
    if (title)
        gtk_window_set_title(GTK_WINDOW(dlg), title);

    if (help)
    {
        GtkWidget* btn_help = gtk_button_new_with_mnemonic(_("_Help"));
        gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn_help, GTK_RESPONSE_HELP);
        gtk_button_set_image(GTK_BUTTON(btn_help), xset_get_image("GTK_STOCK_HELP", GTK_ICON_SIZE_BUTTON));
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_set_focus_on_click(GTK_BUTTON(btn_help), FALSE);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_button_set_focus_on_click(GTK_BUTTON(btn_help), FALSE);
#endif
        gtk_widget_set_can_focus(btn_help, FALSE);
    }

    gtk_widget_show_all(dlg);
    int response;
    while ((response = gtk_dialog_run(GTK_DIALOG(dlg))))
    {
        if (response == GTK_RESPONSE_HELP)
        {
            // btn_help clicked
            if (help)
            {
                if (help[0] == '#')
                {
                    // as anchor
                    xset_show_help(dlg, NULL, help);
                }
                else if (xset_is(help))
                {
                    // as set name
                    xset_show_help(dlg, xset_get(help), NULL);
                }
            }
        }
        else
            break;
    }
    gtk_widget_destroy(dlg);
    return response;
}

void on_multi_input_insert(GtkTextBuffer* buf)
{ // remove linefeeds from pasted text
    GtkTextIter iter;
    GtkTextIter siter;
    // gboolean changed = FALSE;

    // buffer contains linefeeds?
    gtk_text_buffer_get_start_iter(buf, &siter);
    gtk_text_buffer_get_end_iter(buf, &iter);
    char* all = gtk_text_buffer_get_text(buf, &siter, &iter, FALSE);
    if (!strchr(all, '\n'))
    {
        g_free(all);
        return;
    }
    g_free(all);

    // delete selected text that was pasted over
    if (gtk_text_buffer_get_selection_bounds(buf, &siter, &iter))
        gtk_text_buffer_delete(buf, &siter, &iter);

    GtkTextMark* insert = gtk_text_buffer_get_insert(buf);
    gtk_text_buffer_get_iter_at_mark(buf, &iter, insert);
    gtk_text_buffer_get_start_iter(buf, &siter);
    char* b = gtk_text_buffer_get_text(buf, &siter, &iter, FALSE);
    gtk_text_buffer_get_end_iter(buf, &siter);
    char* a = gtk_text_buffer_get_text(buf, &iter, &siter, FALSE);

    int x;
    if (strchr(b, '\n'))
    {
        x = 0;
        while (b[x] != '\0')
        {
            if (b[x] == '\n')
                b[x] = ' ';
            x++;
        }
    }
    if (strchr(a, '\n'))
    {
        x = 0;
        while (a[x] != '\0')
        {
            if (a[x] == '\n')
                a[x] = ' ';
            x++;
        }
    }

    g_signal_handlers_block_matched(buf, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, on_multi_input_insert, NULL);

    gtk_text_buffer_set_text(buf, b, -1);
    gtk_text_buffer_get_end_iter(buf, &iter);
    GtkTextMark* mark = gtk_text_buffer_create_mark(buf, NULL, &iter, TRUE);
    gtk_text_buffer_get_end_iter(buf, &iter);
    gtk_text_buffer_insert(buf, &iter, a, -1);
    gtk_text_buffer_get_iter_at_mark(buf, &iter, mark);
    gtk_text_buffer_place_cursor(buf, &iter);

    g_signal_handlers_unblock_matched(buf, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, on_multi_input_insert, NULL);
    g_free(a);
    g_free(b);
}

void on_multi_input_font_change(GtkMenuItem* item, GtkTextView* input)
{
    char* fontname = xset_get_s("input_font");
    if (fontname)
    {
        PangoFontDescription* font_desc = pango_font_description_from_string(fontname);
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_override_font(GTK_WIDGET(input), font_desc);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_widget_modify_font(GTK_WIDGET(input), font_desc);
#endif
        pango_font_description_free(font_desc);
    }
    else
    {
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_override_font(GTK_WIDGET(input), NULL);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_widget_modify_font(GTK_WIDGET(input), NULL);
#endif
    }
}

void on_multi_input_popup(GtkTextView* input, GtkMenu* menu, gpointer user_data)
{
    GtkAccelGroup* accel_group = gtk_accel_group_new();
    XSet* set = xset_get("sep_multi");
    set->menu_style = XSET_MENU_SEP;
    set->browser = NULL;
    xset_add_menuitem(NULL, GTK_WIDGET(menu), accel_group, set);
    set = xset_set_cb("input_font", on_multi_input_font_change, input);
    set->browser = NULL;
    xset_add_menuitem(NULL, GTK_WIDGET(menu), accel_group, set);
    gtk_widget_show_all(GTK_WIDGET(menu));
    g_signal_connect(G_OBJECT(menu), "key-press-event", G_CALLBACK(xset_menu_keypress), NULL);
}

char* multi_input_get_text(GtkWidget* input)
{ // returns a new allocated string or NULL if input is empty
    GtkTextIter iter;
    GtkTextIter siter;

    if (!GTK_IS_TEXT_VIEW(input))
        return NULL;

    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(input));
    gtk_text_buffer_get_start_iter(buf, &siter);
    gtk_text_buffer_get_end_iter(buf, &iter);
    char* ret = gtk_text_buffer_get_text(buf, &siter, &iter, FALSE);
    if (ret && ret[0] == '\0')
    {
        g_free(ret);
        ret = NULL;
    }
    return ret;
}

void multi_input_select_region(GtkWidget* input, int start, int end)
{
    GtkTextIter iter;
    GtkTextIter siter;

    if (start < 0 || !GTK_IS_TEXT_VIEW(input))
        return;

    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(input));

    gtk_text_buffer_get_iter_at_offset(buf, &siter, start);

    if (end < 0)
        gtk_text_buffer_get_end_iter(buf, &iter);
    else
        gtk_text_buffer_get_iter_at_offset(buf, &iter, end);

    gtk_text_buffer_select_range(buf, &iter, &siter);
}

GtkTextView* multi_input_new(GtkScrolledWindow* scrolled, const char* text, gboolean def_font)
{
    GtkTextIter iter;

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    GtkTextView* input = GTK_TEXT_VIEW(gtk_text_view_new());
    // ubuntu shows input too small so use mininum height
    gtk_widget_set_size_request(GTK_WIDGET(input), -1, 50);
    gtk_widget_set_size_request(GTK_WIDGET(scrolled), -1, 50);

    gtk_container_add(GTK_CONTAINER(scrolled), GTK_WIDGET(input));
    GtkTextBuffer* buf = gtk_text_view_get_buffer(input);
    gtk_text_view_set_wrap_mode(input, GTK_WRAP_CHAR); // GTK_WRAP_WORD_CHAR

    if (text)
        gtk_text_buffer_set_text(buf, text, -1);
    gtk_text_buffer_get_end_iter(buf, &iter);
    gtk_text_buffer_place_cursor(buf, &iter);
    GtkTextMark* insert = gtk_text_buffer_get_insert(buf);
    gtk_text_view_scroll_to_mark(input, insert, 0.0, FALSE, 0, 0);
    gtk_text_view_set_accepts_tab(input, FALSE);

    g_signal_connect_after(G_OBJECT(buf), "insert-text", G_CALLBACK(on_multi_input_insert), NULL);
    if (def_font)
    {
        on_multi_input_font_change(NULL, input);
        g_signal_connect_after(G_OBJECT(input), "populate-popup", G_CALLBACK(on_multi_input_popup), NULL);
    }
    return input;
}

static gboolean on_input_keypress(GtkWidget* widget, GdkEventKey* event, GtkWidget* dlg)
{
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
    {
        gtk_dialog_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
        return TRUE;
    }
    return FALSE;
}

static void on_icon_buffer_changed(GtkTextBuffer* buf, GtkWidget* button)
{
    GtkTextIter iter;
    GtkTextIter siter;
    gtk_text_buffer_get_start_iter(buf, &siter);
    gtk_text_buffer_get_end_iter(buf, &iter);
    char* icon = gtk_text_buffer_get_text(buf, &siter, &iter, FALSE);
    gtk_button_set_image(GTK_BUTTON(button),
                         xset_get_image(icon && icon[0] ? icon : GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON));
    g_free(icon);
}

char* xset_icon_chooser_dialog(GtkWindow* parent, const char* def_icon)
{
    GtkAllocation allocation;
    char* icon = NULL;

    // set busy cursor
    GdkCursor* cursor = gdk_cursor_new_for_display(gtk_widget_get_display(GTK_WIDGET(parent)), GDK_WATCH);
    if (cursor)
    {
        gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(parent)), cursor);
#if (GTK_MAJOR_VERSION == 3)
        g_object_unref(cursor);
#elif (GTK_MAJOR_VERSION == 2)
        gdk_cursor_unref(cursor);
#endif
        while (gtk_events_pending())
            gtk_main_iteration();
    }

    // btn_icon_choose clicked - preparing the exo icon chooser dialog
    GtkWidget* icon_chooser = exo_icon_chooser_dialog_new(_("Choose Icon"),
                                                          GTK_WINDOW(parent),
                                                          GTK_STOCK_CANCEL,
                                                          GTK_RESPONSE_CANCEL,
                                                          GTK_STOCK_OK,
                                                          GTK_RESPONSE_ACCEPT,
                                                          NULL);
    // Set icon chooser dialog size
    int width = xset_get_int("main_icon", "x");
    int height = xset_get_int("main_icon", "y");
    if (width && height)
        gtk_window_set_default_size(GTK_WINDOW(icon_chooser), width, height);

    // Load current icon
    if (def_icon && def_icon[0])
        exo_icon_chooser_dialog_set_icon(EXO_ICON_CHOOSER_DIALOG(icon_chooser), def_icon);

    // Prompting user to pick icon
    int response_icon_chooser = gtk_dialog_run(GTK_DIALOG(icon_chooser));
    if (response_icon_chooser == GTK_RESPONSE_ACCEPT)
    {
        /* Fetching selected icon */
        icon = exo_icon_chooser_dialog_get_icon(EXO_ICON_CHOOSER_DIALOG(icon_chooser));
    }

    // Save icon chooser dialog size
    gtk_widget_get_allocation(GTK_WIDGET(icon_chooser), &allocation);
    if (allocation.width && allocation.height)
    {
        char* str = g_strdup_printf("%d", allocation.width);
        xset_set("main_icon", "x", str);
        g_free(str);
        str = g_strdup_printf("%d", allocation.height);
        xset_set("main_icon", "y", str);
        g_free(str);
    }
    gtk_widget_destroy(icon_chooser);

    // remove busy cursor
    gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(parent)), NULL);

    return icon;
}

gboolean xset_text_dialog(GtkWidget* parent, const char* title, GtkWidget* image, gboolean large, const char* msg1,
                          const char* msg2, const char* defstring, char** answer, const char* defreset,
                          gboolean edit_care, const char* help)
{
    GtkAllocation allocation;
    int width;
    int height;
    GtkWidget* dlgparent = NULL;

    if (parent)
        dlgparent = gtk_widget_get_toplevel(parent);
    GtkWidget* dlg = gtk_message_dialog_new(dlgparent ? GTK_WINDOW(dlgparent) : NULL,
                                            GTK_DIALOG_MODAL,
                                            GTK_MESSAGE_QUESTION,
                                            GTK_BUTTONS_NONE,
                                            msg1,
                                            NULL);
    xset_set_window_icon(GTK_WINDOW(dlg));
    gtk_window_set_role(GTK_WINDOW(dlg), "text_dialog");

    if (large)
    {
        width = xset_get_int("text_dlg", "s");
        height = xset_get_int("text_dlg", "z");
        if (width && height)
            gtk_window_set_default_size(GTK_WINDOW(dlg), width, height);
        else
            gtk_window_set_default_size(GTK_WINDOW(dlg), 600, 400);
        // gtk_widget_set_size_request( GTK_WIDGET( dlg ), 600, 400 );
    }
    else
    {
        width = xset_get_int("text_dlg", "x");
        height = xset_get_int("text_dlg", "y");
        if (width && height)
            gtk_window_set_default_size(GTK_WINDOW(dlg), width, -1);
        else
            gtk_window_set_default_size(GTK_WINDOW(dlg), 500, -1);
        // gtk_widget_set_size_request( GTK_WIDGET( dlg ), 500, 300 );
    }

    gtk_window_set_resizable(GTK_WINDOW(dlg), TRUE);

    if (msg2)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), msg2, NULL);
    if (image)
        gtk_message_dialog_set_image(GTK_MESSAGE_DIALOG(dlg), image);

    // input view
    GtkScrolledWindow* scroll_input = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
    GtkTextView* input = multi_input_new(scroll_input, defstring, TRUE);
    GtkTextBuffer* buf = gtk_text_view_get_buffer(input);

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), GTK_WIDGET(scroll_input), TRUE, TRUE, 4);

    g_signal_connect(G_OBJECT(input), "key-press-event", G_CALLBACK(on_input_keypress), dlg);

    // buttons
    GtkWidget* btn_edit;
    GtkWidget* btn_help = NULL;
    GtkWidget* btn_default = NULL;
    GtkWidget* btn_icon_choose = NULL;
    if (help)
    {
        btn_help = gtk_button_new_with_mnemonic(_("_Help"));
        gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn_help, GTK_RESPONSE_HELP);
        gtk_button_set_image(GTK_BUTTON(btn_help), xset_get_image("GTK_STOCK_HELP", GTK_ICON_SIZE_BUTTON));
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_set_focus_on_click(GTK_BUTTON(btn_help), FALSE);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_button_set_focus_on_click(GTK_BUTTON(btn_help), FALSE);
#endif
    }

    if (edit_care)
    {
        btn_edit = gtk_toggle_button_new_with_mnemonic(_("_Edit"));
        gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn_edit, GTK_RESPONSE_YES);
        gtk_button_set_image(GTK_BUTTON(btn_edit), xset_get_image("GTK_STOCK_DIALOG_WARNING", GTK_ICON_SIZE_BUTTON));
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_set_focus_on_click(GTK_BUTTON(btn_edit), FALSE);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_button_set_focus_on_click(GTK_BUTTON(btn_edit), FALSE);
#endif
        gtk_text_view_set_editable(input, FALSE);
    }

    /* Special hack to add an icon chooser button when this dialog is called
     * to set icons - see xset_menu_cb() and set init "main_icon"
     * and xset_design_job */
    if (!g_strcmp0(title, _("Set Icon")) || !g_strcmp0(title, _("Set Window Icon")))
    {
        btn_icon_choose = gtk_button_new_with_mnemonic(_("C_hoose"));
        gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn_icon_choose, GTK_RESPONSE_ACCEPT);
        gtk_button_set_image(
            GTK_BUTTON(btn_icon_choose),
            xset_get_image(defstring && defstring[0] ? defstring : GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON));
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_set_focus_on_click(GTK_BUTTON(btn_icon_choose), FALSE);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_button_set_focus_on_click(GTK_BUTTON(btn_icon_choose), FALSE);
#endif
        g_signal_connect(G_OBJECT(buf), "changed", G_CALLBACK(on_icon_buffer_changed), btn_icon_choose);
#if (GTK_MAJOR_VERSION == 3)
        // keep this
        gtk_button_set_always_show_image(GTK_BUTTON(btn_icon_choose), TRUE);
#endif
    }

    if (defreset)
    {
        btn_default = gtk_button_new_with_mnemonic(_("_Default"));
        gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn_default, GTK_RESPONSE_NO);
        gtk_button_set_image(GTK_BUTTON(btn_default),
                             xset_get_image("GTK_STOCK_REVERT_TO_SAVED", GTK_ICON_SIZE_BUTTON));
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_set_focus_on_click(GTK_BUTTON(btn_default), FALSE);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_button_set_focus_on_click(GTK_BUTTON(btn_default), FALSE);
#endif
    }

    GtkWidget* btn_cancel = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn_cancel, GTK_RESPONSE_CANCEL);

    GtkWidget* btn_ok = gtk_button_new_from_stock(GTK_STOCK_OK);
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn_ok, GTK_RESPONSE_OK);

    // show
    gtk_widget_show_all(dlg);

    if (title)
        gtk_window_set_title(GTK_WINDOW(dlg), title);
    if (edit_care)
    {
        gtk_widget_grab_focus(btn_ok);
        if (btn_default)
            gtk_widget_set_sensitive(btn_default, FALSE);
    }

    int response;
    gboolean ret = FALSE;
    while ((response = gtk_dialog_run(GTK_DIALOG(dlg))))
    {
        GtkTextIter iter;
        GtkTextIter siter;
        if (response == GTK_RESPONSE_OK)
        {
            gtk_text_buffer_get_start_iter(buf, &siter);
            gtk_text_buffer_get_end_iter(buf, &iter);
            char* ans = gtk_text_buffer_get_text(buf, &siter, &iter, FALSE);
            if (strchr(ans, '\n'))
            {
                ptk_show_error(GTK_WINDOW(dlgparent),
                               _("Error"),
                               _("Your input is invalid because it contains linefeeds"));
            }
            else
            {
                char* trim_ans;
                if (*answer)
                    g_free(*answer);
                trim_ans = g_strdup(ans);
                trim_ans = g_strstrip(trim_ans);
                if (ans && trim_ans[0] != '\0')
                    *answer = trim_ans;
                else
                    *answer = NULL;
                if (ans)
                {
                    g_free(trim_ans);
                    g_free(ans);
                }
                ret = TRUE;
                break;
            }
        }
        else if (response == GTK_RESPONSE_YES)
        {
            // btn_edit clicked
            gtk_text_view_set_editable(input, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_edit)));
            if (btn_default)
                gtk_widget_set_sensitive(btn_default, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_edit)));
        }
        else if (response == GTK_RESPONSE_ACCEPT)
        {
            // get current icon
            gtk_text_buffer_get_start_iter(buf, &siter);
            gtk_text_buffer_get_end_iter(buf, &iter);
            char* icon = gtk_text_buffer_get_text(buf, &siter, &iter, FALSE);

            // show icon chooser
            char* new_icon = xset_icon_chooser_dialog(GTK_WINDOW(dlg), icon);
            g_free(icon);
            if (new_icon)
            {
                gtk_text_buffer_set_text(buf, new_icon, -1);
                g_free(new_icon);
            }
        }
        else if (response == GTK_RESPONSE_NO)
        {
            // btn_default clicked
            gtk_text_buffer_set_text(buf, defreset, -1);
        }
        else if (response == GTK_RESPONSE_HELP)
        {
            // btn_help clicked
            if (help)
            {
                if (help[0] == '#')
                {
                    // as anchor
                    xset_show_help(dlg, NULL, help);
                }
                else if (xset_is(help))
                {
                    // as set name
                    xset_show_help(dlg, xset_get(help), NULL);
                }
            }
        }
        else if (response == GTK_RESPONSE_CANCEL || response == GTK_RESPONSE_DELETE_EVENT)
            break;
    }
    gtk_widget_get_allocation(GTK_WIDGET(dlg), &allocation);
    width = allocation.width;
    height = allocation.height;
    if (width && height)
    {
        char* str = g_strdup_printf("%d", width);
        if (large)
            xset_set("text_dlg", "s", str);
        else
            xset_set("text_dlg", "x", str);
        g_free(str);
        str = g_strdup_printf("%d", height);
        if (large)
            xset_set("text_dlg", "z", str);
        else
            xset_set("text_dlg", "y", str);
        g_free(str);
    }
    gtk_widget_destroy(dlg);
    return ret;
}

static gboolean on_fontdlg_keypress(GtkWidget* widget, GdkEventKey* event, GtkWidget* dlg)
{
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
    {
        gtk_dialog_response(GTK_DIALOG(dlg), GTK_RESPONSE_YES);
        return TRUE;
    }
    return FALSE;
}

char* xset_font_dialog(GtkWidget* parent, const char* title, const char* preview, const char* deffont)
{
    char* fontname = NULL;
    GtkWidget* image;

    GtkWidget* dlg = gtk_font_selection_dialog_new(title);
    xset_set_window_icon(GTK_WINDOW(dlg));
    gtk_window_set_role(GTK_WINDOW(dlg), "font_dialog");

    if (deffont)
        gtk_font_selection_dialog_set_font_name(GTK_FONT_SELECTION_DIALOG(dlg), deffont);
    if (title)
        gtk_window_set_title(GTK_WINDOW(dlg), title);
    if (preview)
        gtk_font_selection_dialog_set_preview_text(GTK_FONT_SELECTION_DIALOG(dlg), preview);

    int width = xset_get_int("font_dlg", "x");
    int height = xset_get_int("font_dlg", "y");
    if (width && height)
        gtk_window_set_default_size(GTK_WINDOW(dlg), width, height);

    // add default button, rename OK
    GtkButton* btn = GTK_BUTTON(gtk_button_new_from_stock(GTK_STOCK_YES));
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), GTK_WIDGET(btn), GTK_RESPONSE_YES);
    gtk_widget_show(GTK_WIDGET(btn));
    GtkButton* ok = GTK_BUTTON(gtk_font_selection_dialog_get_ok_button(GTK_FONT_SELECTION_DIALOG(dlg)));
    gtk_button_set_label(ok, _("_Default"));
    image = xset_get_image("GTK_STOCK_YES", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(ok, image);
    gtk_button_set_label(btn, _("_OK"));
    image = xset_get_image("GTK_STOCK_OK", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(btn, image);

    g_signal_connect(G_OBJECT(dlg), "key-press-event", G_CALLBACK(on_fontdlg_keypress), dlg);

    int response = gtk_dialog_run(GTK_DIALOG(dlg));

    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(dlg), &allocation);
    width = allocation.width;
    height = allocation.height;
    if (width && height)
    {
        char* str = g_strdup_printf("%d", width);
        xset_set("font_dlg", "x", str);
        g_free(str);
        str = g_strdup_printf("%d", height);
        xset_set("font_dlg", "y", str);
        g_free(str);
    }

    if (response == GTK_RESPONSE_YES)
    {
        char* fontname2 = gtk_font_selection_dialog_get_font_name(GTK_FONT_SELECTION_DIALOG(dlg));
        char* trim_fontname = g_strstrip(fontname2);
        fontname = g_strdup(trim_fontname);
        g_free(fontname2);
    }
    else if (response == GTK_RESPONSE_OK)
    {
        // default font
        fontname = g_strdup("");
    }

    gtk_widget_destroy(dlg);
    return fontname;
}

char* xset_file_dialog(GtkWidget* parent, GtkFileChooserAction action, const char* title, const char* deffolder,
                       const char* deffile)
{
    char* path;
    /*  Actions:
     *      GTK_FILE_CHOOSER_ACTION_OPEN
     *      GTK_FILE_CHOOSER_ACTION_SAVE
     *      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
     *      GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER  */
    GtkWidget* dlgparent = parent ? gtk_widget_get_toplevel(parent) : NULL;
    GtkWidget* dlg = gtk_file_chooser_dialog_new(title,
                                                 dlgparent ? GTK_WINDOW(dlgparent) : NULL,
                                                 action,
                                                 GTK_STOCK_CANCEL,
                                                 GTK_RESPONSE_CANCEL,
                                                 GTK_STOCK_OK,
                                                 GTK_RESPONSE_OK,
                                                 NULL);
    // gtk_file_chooser_set_action( GTK_FILE_CHOOSER(dlg), GTK_FILE_CHOOSER_ACTION_SAVE );
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    xset_set_window_icon(GTK_WINDOW(dlg));
    gtk_window_set_role(GTK_WINDOW(dlg), "file_dialog");

    if (deffolder)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), deffolder);
    else
    {
        path = xset_get_s("go_set_default");
        if (path && path[0] != '\0')
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), path);
        else
        {
            path = (char*)g_get_home_dir();
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), path);
        }
    }
    if (deffile)
    {
        if (action == GTK_FILE_CHOOSER_ACTION_SAVE || action == GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER)
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), deffile);
        else
        {
            path = g_build_filename(deffolder, deffile, NULL);
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), path);
            g_free(path);
        }
    }

    int width = xset_get_int("file_dlg", "x");
    int height = xset_get_int("file_dlg", "y");
    if (width && height)
    {
        // filechooser won't honor default size or size request ?
        gtk_widget_show_all(dlg);
        gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER_ALWAYS);
        gtk_window_resize(GTK_WINDOW(dlg), width, height);
        while (gtk_events_pending())
            gtk_main_iteration();
        gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);
    }

    int response = gtk_dialog_run(GTK_DIALOG(dlg));

    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(dlg), &allocation);
    width = allocation.width;
    height = allocation.height;
    if (width && height)
    {
        char* str = g_strdup_printf("%d", width);
        xset_set("file_dlg", "x", str);
        g_free(str);
        str = g_strdup_printf("%d", height);
        xset_set("file_dlg", "y", str);
        g_free(str);
    }

    if (response == GTK_RESPONSE_OK)
    {
        char* dest = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_widget_destroy(dlg);
        return dest;
    }
    gtk_widget_destroy(dlg);
    return NULL;
}

char* xset_color_dialog(GtkWidget* parent, char* title, char* defcolor)
{
#if (GTK_MAJOR_VERSION == 3)
    GdkRGBA color;
#elif (GTK_MAJOR_VERSION == 2)
    GdkColor color;
#endif
    char* scolor = NULL;
    // GtkWidget* dlgparent = parent ? gtk_widget_get_toplevel( parent ) : NULL;
#if (GTK_MAJOR_VERSION == 3)
    GtkWidget* dlg = gtk_color_chooser_dialog_new(title, 0);
#elif (GTK_MAJOR_VERSION == 2)
    GtkWidget* dlg = gtk_color_selection_dialog_new(title);
#endif
    GtkWidget* color_sel = NULL;
    GtkWidget* help_button;

    g_object_get(G_OBJECT(dlg), "help-button", &help_button, NULL);

    gtk_button_set_label(GTK_BUTTON(help_button), _("_Unset"));
    gtk_button_set_image(GTK_BUTTON(help_button), xset_get_image("GTK_STOCK_REMOVE", GTK_ICON_SIZE_BUTTON));

    xset_set_window_icon(GTK_WINDOW(dlg));
    gtk_window_set_role(GTK_WINDOW(dlg), "color_dialog");

    if (defcolor && defcolor[0] != '\0')
    {
#if (GTK_MAJOR_VERSION == 3)
        if (gdk_rgba_parse(defcolor, &color))
#elif (GTK_MAJOR_VERSION == 2)
        if (gdk_color_parse(defcolor, &color))
#endif
        {
#if (GTK_MAJOR_VERSION == 3)
            // g_printf( "        gdk_rgba_to_string = %s\n", gdk_rgba_to_string( &color ) );
            gtk_color_chooser_set_rgba(GTK_COLOR_SELECTION(color_sel), &color);
#elif (GTK_MAJOR_VERSION == 2)
            // g_printf( "        gdk_color_to_string = %s\n", gdk_color_to_string( &color ) );
            color_sel = gtk_color_selection_dialog_get_color_selection(GTK_COLOR_SELECTION_DIALOG(dlg));
            gtk_color_selection_set_current_color(GTK_COLOR_SELECTION(color_sel), &color);
#endif
        }
    }

    gtk_widget_show_all(dlg);
    int response = gtk_dialog_run(GTK_DIALOG(dlg));

    if (response == GTK_RESPONSE_OK)
    {
        // color_sel must be set directly before get_current_color

#if (GTK_MAJOR_VERSION == 3)
        gtk_color_chooser_get_rgba(GTK_COLOR_SELECTION(color_sel), &color);
#elif (GTK_MAJOR_VERSION == 2)
        color_sel = gtk_color_selection_dialog_get_color_selection(GTK_COLOR_SELECTION_DIALOG(dlg));
        gtk_color_selection_get_current_color(GTK_COLOR_SELECTION(color_sel), &color);
#endif

#if (GTK_MAJOR_VERSION == 3)
        scolor = gdk_rgba_to_string(&color);
#elif (GTK_MAJOR_VERSION == 2)
        scolor = gdk_color_to_string(&color);
#endif
    }
    else if (response == GTK_RESPONSE_HELP)
        scolor = NULL;
    else // cancel, delete_event
        scolor = g_strdup(defcolor);
    gtk_widget_destroy(dlg);
    return scolor;
}

void xset_builtin_tool_activate(char tool_type, XSet* set, GdkEventButton* event)
{
    XSet* set2;
    int p;
    char mode;
    PtkFileBrowser* file_browser = NULL;
    FMMainWindow* main_window = fm_main_window_get_last_active();

    // set may be a submenu that doesn't match tool_type
    if (!(set && !set->lock && tool_type > XSET_TOOL_CUSTOM))
    {
        g_warning("xset_builtin_tool_activate invalid");
        return;
    }
    // g_printf("xset_builtin_tool_activate  %s\n", set->menu_label );

    // get current browser, panel, and mode
    if (main_window)
    {
        file_browser = PTK_FILE_BROWSER(fm_main_window_get_current_file_browser(main_window));
        p = file_browser->mypanel;
        mode = main_window->panel_context[p - 1];
    }
    if (!PTK_IS_FILE_BROWSER(file_browser))
        return;

    switch (tool_type)
    {
    case XSET_TOOL_DEVICES:
        set2 = xset_get_panel_mode(p, "show_devmon", mode);
        set2->b = set2->b == XSET_B_TRUE ? XSET_B_UNSET : XSET_B_TRUE;
        update_views_all_windows(NULL, file_browser);
        break;
    case XSET_TOOL_BOOKMARKS:
        set2 = xset_get_panel_mode(p, "show_book", mode);
        set2->b = set2->b == XSET_B_TRUE ? XSET_B_UNSET : XSET_B_TRUE;
        update_views_all_windows(NULL, file_browser);
        if (file_browser->side_book)
        {
            ptk_bookmark_view_chdir(GTK_TREE_VIEW(file_browser->side_book), file_browser, TRUE);
            gtk_widget_grab_focus(GTK_WIDGET(file_browser->side_book));
        }
        break;
    case XSET_TOOL_TREE:
        set2 = xset_get_panel_mode(p, "show_dirtree", mode);
        set2->b = set2->b == XSET_B_TRUE ? XSET_B_UNSET : XSET_B_TRUE;
        update_views_all_windows(NULL, file_browser);
        break;
    case XSET_TOOL_HOME:
        ptk_file_browser_go_home(NULL, file_browser);
        break;
    case XSET_TOOL_DEFAULT:
        ptk_file_browser_go_default(NULL, file_browser);
        break;
    case XSET_TOOL_UP:
        ptk_file_browser_go_up(NULL, file_browser);
        break;
    case XSET_TOOL_BACK:
        ptk_file_browser_go_back(NULL, file_browser);
        break;
    case XSET_TOOL_BACK_MENU:
        ptk_file_browser_show_history_menu(file_browser, TRUE, event);
        break;
    case XSET_TOOL_FWD:
        ptk_file_browser_go_forward(NULL, file_browser);
        break;
    case XSET_TOOL_FWD_MENU:
        ptk_file_browser_show_history_menu(file_browser, FALSE, event);
        break;
    case XSET_TOOL_REFRESH:
        ptk_file_browser_refresh(NULL, file_browser);
        break;
    case XSET_TOOL_NEW_TAB:
        ptk_file_browser_new_tab(NULL, file_browser);
        break;
    case XSET_TOOL_NEW_TAB_HERE:
        ptk_file_browser_new_tab_here(NULL, file_browser);
        break;
    case XSET_TOOL_SHOW_HIDDEN:
        set2 = xset_get_panel(p, "show_hidden");
        set2->b = set2->b == XSET_B_TRUE ? XSET_B_UNSET : XSET_B_TRUE;
        ptk_file_browser_show_hidden_files(file_browser, set2->b);
        break;
    case XSET_TOOL_SHOW_THUMB:
        main_window_toggle_thumbnails_all_windows();
        break;
    case XSET_TOOL_LARGE_ICONS:
        if (file_browser->view_mode != PTK_FB_ICON_VIEW)
        {
            xset_set_b_panel(p, "list_large", !file_browser->large_icons);
            on_popup_list_large(NULL, file_browser);
        }
        break;
    default:
        g_warning("xset_builtin_tool_activate invalid tool_type");
    }
}

const char* xset_get_builtin_toolitem_label(unsigned char tool_type)
{
    if (tool_type < XSET_TOOL_DEVICES || tool_type >= XSET_TOOL_INVALID)
        return NULL;
    return builtin_tool_name[tool_type];
}

XSet* xset_new_builtin_toolitem(char tool_type)
{
    if (tool_type < XSET_TOOL_DEVICES || tool_type >= XSET_TOOL_INVALID)
        return NULL;

    XSet* set = xset_custom_new();
    set->tool = tool_type;
    set->task = set->task_err = set->task_out = set->keep_terminal = 0;

    return set;
}

gboolean on_tool_icon_button_press(GtkWidget* widget, GdkEventButton* event, XSet* set)
{
    int job = -1;

    // g_printf("on_tool_icon_button_press  %s   button = %d\n", set->menu_label,
    //                                                    event->button );
    if (event->type != GDK_BUTTON_PRESS)
        return FALSE;
    int keymod = (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK |
                                  GDK_META_MASK));

    // get and focus browser
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(widget), "browser");
    if (!PTK_IS_FILE_BROWSER(file_browser))
        return TRUE;
    ptk_file_browser_focus_me(file_browser);
    set->browser = file_browser;

    // get context
    XSetContext* context = xset_context_new();
    main_context_fill(file_browser, context);
    if (!context->valid)
        return TRUE;

    if (event->button == 1 || event->button == 3)
    {
        // left or right click
        if (keymod == 0)
        {
            // no modifier
            if (event->button == 1)
            {
                // left click
                if (set->tool == XSET_TOOL_CUSTOM && set->menu_style == XSET_MENU_SUBMENU)
                {
                    XSet* set_child = xset_is(set->child);
                    if (set_child)
                    {
                        // activate first item in custom submenu
                        xset_menu_cb(NULL, set_child);
                    }
                }
                else if (set->tool == XSET_TOOL_CUSTOM)
                {
                    // activate
                    xset_menu_cb(NULL, set);
                }
                else if (set->tool == XSET_TOOL_BACK_MENU)
                    xset_builtin_tool_activate(XSET_TOOL_BACK, set, event);
                else if (set->tool == XSET_TOOL_FWD_MENU)
                    xset_builtin_tool_activate(XSET_TOOL_FWD, set, event);
                else if (set->tool)
                    xset_builtin_tool_activate(set->tool, set, event);
                return TRUE;
            }
            else // if ( event->button == 3 )
            {
                // right-click show design menu for submenu set
                xset_design_cb(NULL, event, set);
                return TRUE;
            }
        }
        else if (keymod == GDK_CONTROL_MASK)
        {
            // ctrl
            job = XSET_JOB_COPY;
        }
        else if (keymod == GDK_MOD1_MASK)
        {
            // alt
            job = XSET_JOB_CUT;
        }
        else if (keymod == GDK_SHIFT_MASK)
        {
            // shift
            job = XSET_JOB_PASTE;
        }
        else if (keymod == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
        {
            // ctrl + shift
            job = XSET_JOB_COMMAND;
        }
    }
    else if (event->button == 2)
    {
        // middle click
        if (keymod == 0)
        {
            // no modifier
            if (set->tool == XSET_TOOL_CUSTOM && xset_get_int_set(set, "x") == XSET_CMD_SCRIPT)
                job = XSET_JOB_EDIT;
            else
                job = XSET_JOB_PROP_CMD;
        }
        else if (keymod == GDK_CONTROL_MASK)
        {
            // ctrl
            job = XSET_JOB_KEY;
        }
        else if (keymod == GDK_MOD1_MASK)
        {
            // alt
            job = XSET_JOB_HELP;
        }
        else if (keymod == GDK_SHIFT_MASK)
        {
            // shift
            job = XSET_JOB_ICON;
        }
        else if (keymod == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
        {
            // ctrl + shift
            job = XSET_JOB_REMOVE;
        }
        else if (keymod == (GDK_CONTROL_MASK | GDK_MOD1_MASK))
        {
            // ctrl + alt
            job = XSET_JOB_PROP;
        }
    }
    if (job != -1)
    {
        if (xset_job_is_valid(set, job))
        {
            g_object_set_data(G_OBJECT(widget), "job", GINT_TO_POINTER(job));
            xset_design_job(widget, set);
        }
        else
        {
            // right-click show design menu for submenu set
            xset_design_cb(NULL, event, set);
        }
        return TRUE;
    }
    return TRUE;
}

gboolean on_tool_menu_button_press(GtkWidget* widget, GdkEventButton* event, XSet* set)
{
    // g_printf("on_tool_menu_button_press  %s   button = %d\n", set->menu_label,
    //                                                    event->button );
    if (event->type != GDK_BUTTON_PRESS)
        return FALSE;
    int keymod = (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK |
                                  GDK_META_MASK));
    if (keymod != 0 || event->button != 1)
        return on_tool_icon_button_press(widget, event, set);

    // get and focus browser
    PtkFileBrowser* file_browser = (PtkFileBrowser*)g_object_get_data(G_OBJECT(widget), "browser");
    if (!PTK_IS_FILE_BROWSER(file_browser))
        return TRUE;
    ptk_file_browser_focus_me(file_browser);

    // get context
    XSetContext* context = xset_context_new();
    main_context_fill(file_browser, context);
    if (!context->valid)
        return TRUE;

    if (event->button == 1)
    {
        if (set->tool == XSET_TOOL_CUSTOM)
        {
            // show custom submenu
            XSet* set_child;
            if (!(set && !set->lock && set->child && set->menu_style == XSET_MENU_SUBMENU &&
                  (set_child = xset_is(set->child))))
                return TRUE;
            GtkWidget* menu = gtk_menu_new();
            GtkAccelGroup* accel_group = gtk_accel_group_new();
            xset_add_menuitem(file_browser, menu, accel_group, set_child);
            gtk_widget_show_all(GTK_WIDGET(menu));
            gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, event->button, event->time);
        }
        else
            xset_builtin_tool_activate(set->tool, set, event);
        return TRUE;
    }
    return TRUE;
}

#if (GTK_MAJOR_VERSION == 3)
static void set_gtk3_widget_padding(GtkWidget* widget, int left_right, int top_bottom)
{
    char* str = g_strdup_printf("GtkWidget { padding-left: %dpx; padding-right: %dpx; "
                                "padding-top: %dpx; padding-bottom: %dpx; }",
                                left_right,
                                left_right,
                                top_bottom,
                                top_bottom);

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(GTK_CSS_PROVIDER(provider), str, -1, NULL);
    GtkStyleContext* context = gtk_widget_get_style_context(widget);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_free(str);
}
#endif

GtkWidget* xset_add_toolitem(GtkWidget* parent, PtkFileBrowser* file_browser, GtkWidget* toolbar, int icon_size,
                             XSet* set, gboolean show_tooltips)
{
    GtkWidget* image = NULL;
    GtkWidget* item = NULL;
    GtkWidget* btn;
#if (GTK_MAJOR_VERSION == 2)
    GtkRequisition req;
#endif
    XSet* set_next;
    char* new_menu_label = NULL;
    GdkPixbuf* pixbuf = NULL;
    char* icon_file = NULL;
    char* str;
    int cmd_type;

    if (set->lock)
        return NULL;

    if (set->tool == XSET_TOOL_NOT)
    {
        g_warning("xset_add_toolitem set->tool == XSET_TOOL_NOT");
        set->tool = XSET_TOOL_CUSTOM;
    }

    // get real icon size from gtk icon size
    int icon_w, icon_h;
    gtk_icon_size_lookup(icon_size, &icon_w, &icon_h);
    int real_icon_size = icon_w > icon_h ? icon_w : icon_h;

    set->browser = file_browser;

    // builtin toolitems set shared_key on build
    if (set->tool >= XSET_TOOL_INVALID)
    {
        // looks like an unknown built-in toolitem from a future version - skip
        goto _next_toolitem;
    }
    if (set->tool > XSET_TOOL_CUSTOM && set->tool < XSET_TOOL_INVALID && !set->shared_key)
        set->shared_key = g_strdup(builtin_tool_shared_key[set->tool]);

    // builtin toolitems don't have menu_style set
    int menu_style;
    switch (set->tool)
    {
    case XSET_TOOL_DEVICES:
    case XSET_TOOL_BOOKMARKS:
    case XSET_TOOL_TREE:
    case XSET_TOOL_SHOW_HIDDEN:
    case XSET_TOOL_SHOW_THUMB:
    case XSET_TOOL_LARGE_ICONS:
        menu_style = XSET_MENU_CHECK;
        break;
    case XSET_TOOL_BACK_MENU:
    case XSET_TOOL_FWD_MENU:
        menu_style = XSET_MENU_SUBMENU;
        break;
    default:
        menu_style = set->menu_style;
    }

    const char* icon_name = set->icon;
    if (!icon_name && set->tool == XSET_TOOL_CUSTOM)
    {
        // custom 'icon' file?
        icon_file = g_build_filename(settings_config_dir, "scripts", set->name, "icon", NULL);
        if (!g_file_test(icon_file, G_FILE_TEST_EXISTS))
        {
            g_free(icon_file);
            icon_file = NULL;
        }
        else
            icon_name = icon_file;
    }

    char* menu_label = set->menu_label;
    if (!menu_label && set->tool > XSET_TOOL_CUSTOM)
        menu_label = (char*)xset_get_builtin_toolitem_label(set->tool);

    if (!menu_style || menu_style == XSET_MENU_STRING)
    {
        // normal item
        cmd_type = xset_get_int_set(set, "x");
        if (set->tool > XSET_TOOL_CUSTOM)
        {
            // builtin tool item
            if (icon_name)
                image = xset_get_image(icon_name, icon_size);
            else if (set->tool > XSET_TOOL_CUSTOM && set->tool < XSET_TOOL_INVALID)
                image = xset_get_image(builtin_tool_icon[set->tool], icon_size);
        }
        else if (!set->lock && cmd_type == XSET_CMD_APP)
        {
            // Application
            new_menu_label = xset_custom_get_app_name_icon(set, &pixbuf, real_icon_size);
        }
        else if (!set->lock && cmd_type == XSET_CMD_BOOKMARK)
        {
            // Bookmark
            if (!(set->menu_label && set->menu_label[0]))
                new_menu_label = g_strdup(set->z);
        }

        if (!image)
            image = xset_get_image(icon_name ? icon_name : "gtk-execute", icon_size);
        if (!new_menu_label)
            new_menu_label = g_strdup(menu_label);

        // can't use gtk_tool_button_new because icon doesn't obey size
        // btn = GTK_WIDGET( gtk_tool_button_new( image, new_menu_label ) );
        btn = GTK_WIDGET(gtk_button_new());
        gtk_widget_show(image);
        gtk_button_set_image(GTK_BUTTON(btn), image);
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        // These don't seem to do anything
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_set_margin_start(btn, 0);
        gtk_widget_set_margin_end(btn, 0);
        gtk_widget_set_margin_top(btn, 0);
        gtk_widget_set_margin_bottom(btn, 0);
        gtk_widget_set_hexpand(btn, FALSE);
        gtk_widget_set_vexpand(btn, FALSE);
        set_gtk3_widget_padding(btn, 0, 0);
        gtk_button_set_always_show_image(GTK_BUTTON(btn), TRUE);
        gtk_widget_set_margin_start(btn, 0);
        gtk_widget_set_margin_end(btn, 0);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_widget_size_request(btn, &req);
        gtk_widget_set_size_request(GTK_WIDGET(btn), req.width - 4, -1);
#endif

        // create tool item containing an ebox to capture click on button
        item = GTK_WIDGET(gtk_tool_item_new());
        GtkWidget* ebox = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(item), ebox);
        gtk_container_add(GTK_CONTAINER(ebox), btn);
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), FALSE);
        gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), TRUE);
        g_signal_connect(ebox, "button-press-event", G_CALLBACK(on_tool_icon_button_press), set);
        g_object_set_data(G_OBJECT(ebox), "browser", file_browser);
        ptk_file_browser_add_toolbar_widget(set, btn);

        // tooltip
        if (show_tooltips)
        {
            str = clean_label(new_menu_label, FALSE, FALSE);
            gtk_widget_set_tooltip_text(ebox, str);
            g_free(str);
        }
        g_free(new_menu_label);
    }
    else if (menu_style == XSET_MENU_CHECK)
    {
        if (!icon_name && set->tool > XSET_TOOL_CUSTOM && set->tool < XSET_TOOL_INVALID)
            // builtin tool item
            image = xset_get_image(builtin_tool_icon[set->tool], icon_size);
        else
            image = xset_get_image(icon_name ? icon_name : "gtk-execute", icon_size);

        // can't use gtk_tool_button_new because icon doesn't obey size
        // btn = GTK_WIDGET( gtk_toggle_tool_button_new() );
        // gtk_tool_button_set_icon_widget( GTK_TOOL_BUTTON( btn ), image );
        // gtk_tool_button_set_label( GTK_TOOL_BUTTON( btn ), set->menu_label );
        btn = gtk_toggle_button_new();
        gtk_widget_show(image);
        gtk_button_set_image(GTK_BUTTON(btn), image);
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), xset_get_b_set(set));
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_set_margin_start(btn, 0);
        gtk_widget_set_margin_end(btn, 0);
        gtk_widget_set_margin_top(btn, 0);
        gtk_widget_set_margin_bottom(btn, 0);
        gtk_widget_set_hexpand(btn, FALSE);
        gtk_widget_set_vexpand(btn, FALSE);
        set_gtk3_widget_padding(btn, 0, 0);
        gtk_button_set_always_show_image(GTK_BUTTON(btn), TRUE);
        gtk_widget_set_margin_start(btn, 0);
        gtk_widget_set_margin_end(btn, 0);
#endif

        // create tool item containing an ebox to capture click on button
        item = GTK_WIDGET(gtk_tool_item_new());
        GtkWidget* ebox = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(item), ebox);
        gtk_container_add(GTK_CONTAINER(ebox), btn);
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), FALSE);
        gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), TRUE);
        g_signal_connect(ebox, "button-press-event", G_CALLBACK(on_tool_icon_button_press), set);
        g_object_set_data(G_OBJECT(ebox), "browser", file_browser);
        ptk_file_browser_add_toolbar_widget(set, btn);

        // tooltip
        if (show_tooltips)
        {
            str = clean_label(menu_label, FALSE, FALSE);
            gtk_widget_set_tooltip_text(ebox, str);
            g_free(str);
        }
    }
    else if (menu_style == XSET_MENU_SUBMENU)
    {
        menu_label = NULL;
        // create a tool button
        XSet* set_child = NULL;
        if (set->tool == XSET_TOOL_CUSTOM)
            set_child = xset_is(set->child);

        if (!icon_name && set_child && set_child->icon)
            // take the user icon from the first item in the submenu
            icon_name = set_child->icon;
        else if (!icon_name && set->tool > XSET_TOOL_CUSTOM && set->tool < XSET_TOOL_INVALID)
            icon_name = builtin_tool_icon[set->tool];
        else if (!icon_name && set_child && set->tool == XSET_TOOL_CUSTOM)
        {
            // take the auto icon from the first item in the submenu
            cmd_type = xset_get_int_set(set_child, "x");
            if (cmd_type == XSET_CMD_APP)
            {
                // Application
                new_menu_label = menu_label = xset_custom_get_app_name_icon(set_child, &pixbuf, real_icon_size);
            }
            else if (cmd_type == XSET_CMD_BOOKMARK)
            {
                // Bookmark
                if (!(set_child->menu_label && set_child->menu_label[0]))
                    menu_label = set_child->z;
            }
            else
                icon_name = "gtk-execute";
        }

        if (!menu_label)
        {
            if (set->tool == XSET_TOOL_BACK_MENU)
                menu_label = _(builtin_tool_name[XSET_TOOL_BACK]);
            else if (set->tool == XSET_TOOL_FWD_MENU)
                menu_label = _(builtin_tool_name[XSET_TOOL_FWD]);
            else if (set->tool == XSET_TOOL_CUSTOM && set_child)
                menu_label = set_child->menu_label;
            else if (set->tool > XSET_TOOL_CUSTOM && !set->menu_label)
                menu_label = (char*)xset_get_builtin_toolitem_label(set->tool);
            else
                menu_label = set->menu_label;
        }

        if (!image)
            image = xset_get_image(icon_name ? icon_name : "gtk-directory", icon_size);

        // can't use gtk_tool_button_new because icon doesn't obey size
        // btn = GTK_WIDGET( gtk_tool_button_new( image, menu_label ) );
        btn = GTK_WIDGET(gtk_button_new());
        gtk_widget_show(image);
        gtk_button_set_image(GTK_BUTTON(btn), image);
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_set_margin_start(btn, 0);
        gtk_widget_set_margin_end(btn, 0);
        gtk_widget_set_margin_top(btn, 0);
        gtk_widget_set_margin_bottom(btn, 0);
        gtk_widget_set_hexpand(btn, FALSE);
        gtk_widget_set_vexpand(btn, FALSE);
        set_gtk3_widget_padding(btn, 0, 0);
        gtk_button_set_always_show_image(GTK_BUTTON(btn), TRUE);
        gtk_widget_set_margin_start(btn, 0);
        gtk_widget_set_margin_end(btn, 0);
#elif (GTK_MAJOR_VERSION == 2)
        gtk_widget_size_request(btn, &req);
        gtk_widget_set_size_request(GTK_WIDGET(btn), req.width - 4, -1);
#endif

        // create eventbox for btn
        GtkWidget* ebox = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), FALSE);
        gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), TRUE);
        gtk_container_add(GTK_CONTAINER(ebox), btn);
        g_signal_connect(G_OBJECT(ebox), "button_press_event", G_CALLBACK(on_tool_icon_button_press), set);
        g_object_set_data(G_OBJECT(ebox), "browser", file_browser);
        ptk_file_browser_add_toolbar_widget(set, btn);

        // pack into hbox
#if (GTK_MAJOR_VERSION == 3)
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
#elif (GTK_MAJOR_VERSION == 2)
        GtkWidget* hbox = gtk_hbox_new(FALSE, 0);
#endif

        gtk_box_pack_start(GTK_BOX(hbox), ebox, FALSE, FALSE, 0);
        // tooltip
        if (show_tooltips)
        {
            str = clean_label(menu_label, FALSE, FALSE);
            gtk_widget_set_tooltip_text(ebox, str);
            g_free(str);
        }
        g_free(new_menu_label);

        // reset menu_label for below
        menu_label = set->menu_label;
        if (!menu_label && set->tool > XSET_TOOL_CUSTOM)
            menu_label = (char*)xset_get_builtin_toolitem_label(set->tool);

        ///////// create a menu_tool_button to steal the button from
        ebox = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), FALSE);
        gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), TRUE);
        GtkWidget* menu_btn = GTK_WIDGET(gtk_menu_tool_button_new(NULL, NULL));
        GtkWidget* hbox_menu = gtk_bin_get_child(GTK_BIN(menu_btn));
        GList* children = gtk_container_get_children(GTK_CONTAINER(hbox_menu));
        btn = GTK_WIDGET(children->next->data);
        if (!btn || !GTK_IS_WIDGET(btn))
        {
            // failed so just create a button
            btn = GTK_WIDGET(gtk_button_new());
            gtk_button_set_label(GTK_BUTTON(btn), ".");
            gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
            gtk_container_add(GTK_CONTAINER(ebox), btn);
        }
        else
        {
            // steal the drop-down button
            gtk_widget_reparent(btn, ebox);
            gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        }
#if (GTK_MAJOR_VERSION == 3)
        gtk_widget_set_margin_start(btn, 0);
        gtk_widget_set_margin_end(btn, 0);
        gtk_widget_set_margin_top(btn, 0);
        gtk_widget_set_margin_bottom(btn, 0);
        gtk_widget_set_hexpand(btn, FALSE);
        gtk_widget_set_vexpand(btn, FALSE);
        set_gtk3_widget_padding(btn, 0, 0);
        gtk_button_set_always_show_image(GTK_BUTTON(btn), TRUE);
        gtk_widget_set_margin_start(btn, 0);
        gtk_widget_set_margin_end(btn, 0);
#endif

        g_list_free(children);
        gtk_widget_destroy(menu_btn);

        gtk_box_pack_start(GTK_BOX(hbox), ebox, FALSE, FALSE, 0);
        g_signal_connect(G_OBJECT(ebox), "button_press_event", G_CALLBACK(on_tool_menu_button_press), set);
        g_object_set_data(G_OBJECT(ebox), "browser", file_browser);
        ptk_file_browser_add_toolbar_widget(set, btn);

        item = GTK_WIDGET(gtk_tool_item_new());
        gtk_container_add(GTK_CONTAINER(item), hbox);
        gtk_widget_show_all(item);

        // tooltip
        if (show_tooltips)
        {
            str = clean_label(menu_label, FALSE, FALSE);
            gtk_widget_set_tooltip_text(ebox, str);
            g_free(str);
        }
    }
    else if (menu_style == XSET_MENU_SEP)
    {
        // create tool item containing an ebox to capture click on sep
        btn = GTK_WIDGET(gtk_separator_tool_item_new());
        gtk_separator_tool_item_set_draw(GTK_SEPARATOR_TOOL_ITEM(btn), TRUE);
        item = GTK_WIDGET(gtk_tool_item_new());
        GtkWidget* ebox = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(item), ebox);
        gtk_container_add(GTK_CONTAINER(ebox), btn);
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), FALSE);
        gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), TRUE);
        g_signal_connect(ebox, "button-press-event", G_CALLBACK(on_tool_icon_button_press), set);
        g_object_set_data(G_OBJECT(ebox), "browser", file_browser);
    }
    else
        return NULL;

    g_free(icon_file);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(item), -1);

// g_printf("    set=%s   set->next=%s\n", set->name, set->next );
// next toolitem
_next_toolitem:
    if ((set_next = xset_is(set->next)))
    {
        // g_printf("    NEXT %s\n", set_next->name );
        xset_add_toolitem(parent, file_browser, toolbar, icon_size, set_next, show_tooltips);
    }

    return item;
}

void xset_fill_toolbar(GtkWidget* parent, PtkFileBrowser* file_browser, GtkWidget* toolbar, XSet* set_parent,
                       gboolean show_tooltips)
{
    const char default_tools[] = {XSET_TOOL_BOOKMARKS,
                                  XSET_TOOL_TREE,
                                  XSET_TOOL_NEW_TAB_HERE,
                                  XSET_TOOL_BACK_MENU,
                                  XSET_TOOL_FWD_MENU,
                                  XSET_TOOL_UP,
                                  XSET_TOOL_DEFAULT};

    // g_printf("xset_fill_toolbar %s\n", set_parent->name );
    if (!(file_browser && toolbar && set_parent))
        return;

    set_parent->lock = TRUE;
    set_parent->menu_style = XSET_MENU_SUBMENU;

    GtkIconSize icon_size = gtk_toolbar_get_icon_size(GTK_TOOLBAR(toolbar));

    XSet* set_child = NULL;
    if (set_parent->child)
        set_child = xset_is(set_parent->child);
    if (!set_child)
    {
        // toolbar is empty - add default items
        set_child =
            xset_new_builtin_toolitem(strstr(set_parent->name, "tool_r") ? XSET_TOOL_REFRESH : XSET_TOOL_DEVICES);
        set_parent->child = g_strdup(set_child->name);
        set_child->parent = g_strdup(set_parent->name);
        if (!strstr(set_parent->name, "tool_r"))
        {
            int stop_b4;
            if (strstr(set_parent->name, "tool_s"))
                stop_b4 = 2;
            else
                stop_b4 = G_N_ELEMENTS(default_tools);
            XSet* set_target = set_child;
            int i;
            for (i = 0; i < stop_b4; i++)
            {
                XSet* set = xset_new_builtin_toolitem(default_tools[i]);
                xset_custom_insert_after(set_target, set);
                set_target = set;
            }
        }
    }

    xset_add_toolitem(parent, file_browser, toolbar, icon_size, set_child, show_tooltips);

    // These don't seem to do anything
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 0);
#if (GTK_MAJOR_VERSION == 3)
    gtk_widget_set_margin_start(toolbar, 0);
    gtk_widget_set_margin_end(toolbar, 0);
    gtk_widget_set_margin_top(toolbar, 0);
    gtk_widget_set_margin_bottom(toolbar, 0);

    // remove padding from GTK3 toolbar - this works
    set_gtk3_widget_padding(toolbar, 0, 2);

    gtk_widget_set_margin_start(toolbar, 0);
    gtk_widget_set_margin_end(toolbar, 0);
#endif

    gtk_widget_show_all(toolbar);
}

void xset_set_window_icon(GtkWindow* win)
{
    const char* name;
    XSet* set = xset_get("main_icon");
    if (set->icon)
        name = set->icon;
    else if (geteuid() == 0)
        name = "spacefm-root";
    else
        name = "spacefm";
    GtkIconTheme* theme = gtk_icon_theme_get_default();
    if (!theme)
        return;
    GError* error = NULL;
    GdkPixbuf* icon = gtk_icon_theme_load_icon(theme, name, 48, 0, &error);
    if (icon)
    {
        gtk_window_set_icon(GTK_WINDOW(win), icon);
        g_object_unref(icon);
    }
    else if (error)
    {
        // An error occured on loading the icon
        g_fprintf(stderr,
                  "spacefm: Unable to load the window icon "
                  "'%s' in - xset_set_window_icon - %s\n",
                  name,
                  error->message);
        g_error_free(error);
    }
}

void xset_defaults()
{
    srand((size_t)time(0) + getpid());

    // set_last must be set (to anything)
    set_last = xset_get("separator");
    set_last->menu_style = XSET_MENU_SEP;

    // dev menu
    XSet* set = xset_get("sep_dm1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_dm2");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_dm3");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_dm4");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_dm5");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("dev_menu_remove", "lbl", _("Remo_ve / Eject"));
    set->line = g_strdup("#devices-menu-remove");

    set = xset_set("dev_menu_unmount", "lbl", _("_Unmount"));
    set->line = g_strdup("#devices-menu-unmount");

    set = xset_set("dev_menu_reload", "lbl", _("Re_load"));
    set->line = g_strdup("#devices-menu-reload");

    set = xset_set("dev_menu_sync", "lbl", _("_Sync"));
    set->line = g_strdup("#devices-menu-sync");

    set = xset_set("dev_menu_open", "lbl", _("_Open"));
    set->line = g_strdup("#devices-menu-open");

    set = xset_set("dev_menu_tab", "lbl", C_("Devices|Open|", "Open In _Tab"));
    set->line = g_strdup("#devices-menu-tab");

    set = xset_set("dev_menu_mount", "lbl", _("_Mount"));
    set->line = g_strdup("#devices-menu-mount");

    set = xset_set("dev_menu_remount", "lbl", _("Re_/mount"));
    set->line = g_strdup("#devices-menu-remount");

    set = xset_set("dev_menu_mark", "lbl", _("_Bookmark"));
    set->line = g_strdup("#devices-menu-bookmark");

    set = xset_get("sep_mr1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_mr2");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_mr3");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("dev_menu_root", "lbl", _("_Root"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "dev_root_unmount dev_root_mount sep_mr1 sep_mr2 sep_mr3 dev_root_fstab dev_root_udevil");
    set->line = g_strdup("#devices-root");

    set = xset_set("dev_root_mount", "lbl", _("_Mount"));
    xset_set_set(set, "z", "/usr/bin/udevil mount -o %o %v");
    set->line = g_strdup("#devices-root-mount");

    set = xset_set("dev_root_unmount", "lbl", _("_Unmount"));
    xset_set_set(set, "z", "/usr/bin/udevil umount %v");
    set->line = g_strdup("#devices-root-unmount");

    set = xset_set("dev_root_fstab", "lbl", _("_Edit fstab"));
    set->line = g_strdup("#devices-root-fstab");

    set = xset_set("dev_root_udevil", "lbl", _("Edit u_devil.conf"));
    set->line = g_strdup("#devices-root-udevil");

    set = xset_set("dev_prop", "lbl", _("_Properties"));
    set->line = g_strdup("#devices-menu-properties");

    set = xset_set("dev_menu_settings", "lbl", _("Setti_ngs"));
    set->menu_style = XSET_MENU_SUBMENU;
    set->line = g_strdup("#devices-settings");

    // dev settings
    set = xset_set("dev_show", "lbl", _("S_how"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "dev_show_internal_drives dev_show_empty dev_show_partition_tables dev_show_net dev_show_file "
                 "dev_ignore_udisks_hide dev_show_hide_volumes dev_dispname");
    set->line = g_strdup("#devices-settings-internal");

    set = xset_set("dev_show_internal_drives", "lbl", _("_Internal Drives"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = geteuid() == 0 ? XSET_B_TRUE : XSET_B_FALSE;
    set->line = g_strdup("#devices-settings-internal");

    set = xset_set("dev_show_empty", "lbl", _("_Empty Drives"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE; // geteuid() == 0 ? XSET_B_TRUE : XSET_B_UNSET;
    set->line = g_strdup("#devices-settings-empty");

    set = xset_set("dev_show_partition_tables", "lbl", _("_Partition Tables"));
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#devices-settings-table");

    set = xset_set("dev_show_net", "lbl", _("Mounted _Networks"));
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#devices-settings-net");
    set->b = XSET_B_TRUE;

    set = xset_set("dev_show_file", "lbl", _("Mounted _Other"));
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#devices-settings-files");
    set->b = XSET_B_TRUE;

    set = xset_set("dev_show_hide_volumes", "lbl", _("_Volumes..."));
    xset_set_set(set, "title", _("Show/Hide Volumes"));
    xset_set_set(set,
                 "desc",
                 _("To force showing or hiding of some volumes, overriding other settings, you can specify the "
                   "devices, volume labels, or device IDs in the space-separated list below.\n\nExample:  +/dev/sdd1 "
                   "-Label With Space +ata-OCZ-part4\nThis would cause /dev/sdd1 and the OCZ device to be shown, and "
                   "the volume with label \"Label With Space\" to be hidden.\n\nThere must be a space between entries "
                   "and a plus or minus sign directly before each item.  This list is case-sensitive.\n\n"));
    set->line = g_strdup("#devices-settings-vol");

    set = xset_set("dev_ignore_udisks_hide", "lbl", _("Ignore _Hide Policy"));
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#devices-settings-hide");

    set = xset_set("dev_dispname", "lbl", _("_Display Name"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Display Name Format"));
    xset_set_set(set,
                 "desc",
                 _("Enter device display name format:\n\nUse:\n\t%%v\tdevice filename (eg sdd1)\n\t%%s\ttotal size (eg "
                   "800G)\n\t%%t\tfstype (eg ext4)\n\t%%l\tvolume label (eg Label or [no media])\n\t%%m\tmount point "
                   "if mounted, or ---\n\t%%i\tdevice ID\n\t%%n\tmajor:minor device numbers (eg 15:3)\n"));
    xset_set_set(set, "s", "%v %s %l %m");
    xset_set_set(set, "z", "%v %s %l %m");
    set->line = g_strdup("#devices-settings-name");

#ifdef DEPRECATED_HW
    set = xset_set("dev_menu_auto", "lbl", _("_Auto Mount"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "dev_automount_optical dev_automount_removable dev_ignore_udisks_nopolicy dev_automount_volumes "
                 "dev_automount_dirs dev_auto_open dev_unmount_quit");
    set->line = g_strdup("#devices-settings-optical");
#else
    set = xset_set("dev_menu_auto", "lbl", _("_Auto Mount"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "dev_automount_removable dev_ignore_udisks_nopolicy dev_automount_volumes dev_automount_dirs "
                 "dev_auto_open dev_unmount_quit");
#endif

#ifdef DEPRECATED_HW
    set = xset_set("dev_automount_optical", "lbl", _("Mount _Optical"));
    set->b = geteuid() == 0 ? XSET_B_FALSE : XSET_B_TRUE;
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#devices-settings-optical");
#endif

    set = xset_set("dev_automount_removable", "lbl", _("_Mount Removable"));
    set->b = geteuid() == 0 ? XSET_B_FALSE : XSET_B_TRUE;
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#devices-settings-remove");

    set = xset_set("dev_automount_volumes", "lbl", _("Mount _Volumes..."));
    xset_set_set(set, "title", _("Auto-Mount Volumes"));
    xset_set_set(
        set,
        "desc",
        _("To force or prevent automounting of some volumes, overriding other settings, you can specify the devices, "
          "volume labels, or device IDs in the space-separated list below.\n\nExample:  +/dev/sdd1 -Label With Space "
          "+ata-OCZ-part4\nThis would cause /dev/sdd1 and the OCZ device to be auto-mounted when detected, and the "
          "volume with label \"Label With Space\" to be ignored.\n\nThere must be a space between entries and a plus "
          "or minus sign directly before each item.  This list is case-sensitive.\n\n"));
    set->line = g_strdup("#devices-settings-mvol");

    set = xset_set("dev_automount_dirs", "lbl", _("Mount _Dirs..."));
    xset_set_set(set, "title", _("Automatic Mount Point Dirs"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(
        set,
        "desc",
        _("Enter the directory where SpaceFM should automatically create mount point directories for fuse and similar "
          "filesystems (%%a in handler commands).  This directory must be user-writable (do NOT use /media), and empty "
          "subdirectories will be removed.  If left blank, ~/.cache/spacefm/ (or $XDG_CACHE_HOME/spacefm/) is used.  "
          "The following variables are recognized: $USER $UID $HOME $XDG_RUNTIME_DIR $XDG_CACHE_HOME\n\nNote that some "
          "handlers or mount programs may not obey this setting.\n"));
    set->line = g_strdup("#devices-settings-mdirs");

    set = xset_set("dev_auto_open", "lbl", _("Open _Tab"));
    set->b = XSET_B_TRUE;
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#devices-settings-tab");

    set = xset_set("dev_unmount_quit", "lbl", _("_Unmount On Exit"));
    set->b = XSET_B_UNSET;
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#devices-settings-exit");

    set = xset_get("sep_ar1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("dev_exec", "lbl", _("Auto _Run"));
    set->menu_style = XSET_MENU_SUBMENU;
#ifdef DEPRECATED_HW
    xset_set_set(set,
                 "desc",
                 "dev_exec_fs dev_exec_audio dev_exec_video sep_ar1 dev_exec_insert dev_exec_unmount dev_exec_remove");
#else
    xset_set_set(set, "desc", "dev_exec_fs sep_ar1 dev_exec_insert dev_exec_unmount dev_exec_remove");
#endif
    set->line = g_strdup("#devices-settings-runm");

    set = xset_set("dev_exec_fs", "lbl", _("On _Mount"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Auto Run On Mount"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically after a removable drive or data disc is "
                   "auto-mounted:\n\nUse:\n\t%%v\tdevice (eg /dev/sda1)\n\t%%l\tdevice label\n\t%%m\tdevice mount "
                   "point (eg /media/disk)"));
    set->line = g_strdup("#devices-settings-runm");

#ifdef DEPRECATED_HW
    set = xset_set("dev_exec_audio", "lbl", _("On _Audio CD"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Auto Run On Audio CD"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically when an audio CD is inserted in a "
                   "qualified device:\n\nUse:\n\t%%v\tdevice (eg /dev/sda1)\n\t%%l\tdevice label\n\t%%m\tdevice mount "
                   "point (eg /media/disk)"));
    set->line = g_strdup("#devices-settings-runa");

    set = xset_set("dev_exec_video", "lbl", _("On _Video DVD"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Auto Run On Video DVD"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically when a video DVD is "
                   "auto-mounted:\n\nUse:\n\t%%v\tdevice (eg /dev/sda1)\n\t%%l\tdevice label\n\t%%m\tdevice mount "
                   "point (eg /media/disk)"));
    set->line = g_strdup("#devices-settings-runv");
#endif

    set = xset_set("dev_exec_insert", "lbl", _("On _Insert"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Auto Run On Insert"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically when any device is "
                   "inserted:\n\nUse:\n\t%%v\tdevice added (eg /dev/sda1)\n\t%%l\tdevice label\n\t%%m\tdevice mount "
                   "point (eg /media/disk)"));
    set->line = g_strdup("#devices-settings-runi");

    set = xset_set("dev_exec_unmount", "lbl", _("On _Unmount"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Auto Run On Unmount"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically when any device is unmounted by any "
                   "means:\n\nUse:\n\t%%v\tdevice unmounted (eg /dev/sda1)\n\t%%l\tdevice label\n\t%%m\tdevice mount "
                   "point (eg /media/disk)"));
    set->line = g_strdup("#devices-settings-runu");

    set = xset_set("dev_exec_remove", "lbl", _("On _Remove"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Auto Run On Remove"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically when any device is removed (ejection of "
                   "media does not qualify):\n\nUse:\n\t%%v\tdevice removed (eg /dev/sda1)\n\t%%l\tdevice "
                   "label\n\t%%m\tdevice mount point (eg /media/disk)"));
    set->line = g_strdup("#devices-settings-runr");

    set = xset_set("dev_ignore_udisks_nopolicy", "lbl", _("Ignore _No Policy"));
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#devices-settings-nopolicy");

    set = xset_set("dev_mount_options", "lbl", _("_Mount Options"));
    xset_set_set(set,
                 "desc",
                 _("Enter your comma- or space-separated list of default mount options below (%%o in handlers).\n\nIn "
                   "addition to regular options, you can also specify options to be added or removed for a specific "
                   "filesystem type by using the form OPTION+FSTYPE or OPTION-FSTYPE.\n\nExample:  nosuid, sync+vfat, "
                   "sync+ntfs, noatime, noatime-ext4\nThis will add nosuid and noatime for all filesystem types, add "
                   "sync for vfat and ntfs only, and remove noatime for ext4.\n\nNote: Some options, such as nosuid, "
                   "may be added by the mount program even if you don't include them.  Options in fstab take "
                   "precedence.  pmount and some handlers may ignore options set here."));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Default Mount Options"));
    xset_set_set(set, "s", "noexec, nosuid, noatime");
    xset_set_set(set, "z", "noexec, nosuid, noatime");
    set->line = g_strdup("#devices-settings-opts");

    set = xset_set("dev_remount_options", "z", "noexec, nosuid, noatime");
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Re/mount With Options"));
    xset_set_set(
        set,
        "desc",
        _("Device will be (re)mounted using the options below.\n\nIn addition to regular options, you can also specify "
          "options to be added or removed for a specific filesystem type by using the form OPTION+FSTYPE or "
          "OPTION-FSTYPE.\n\nExample:  nosuid, sync+vfat, sync+ntfs, noatime, noatime-ext4\nThis will add nosuid and "
          "noatime for all filesystem types, add sync for vfat and ntfs only, and remove noatime for ext4.\n\nNote: "
          "Some options, such as nosuid, may be added by the mount program even if you don't include them.  Options in "
          "fstab take precedence.  pmount ignores options set here."));
    xset_set_set(set, "s", "noexec, nosuid, noatime");
    set->line = g_strdup("#devices-menu-remount");

    set = xset_set("dev_change", "lbl", _("_Change Detection"));
    xset_set_set(set,
                 "desc",
                 _("Enter your comma- or space-separated list of filesystems which should NOT be monitored for file "
                   "changes.  This setting only affects non-block devices (such as nfs or fuse), and is usually used "
                   "to prevent SpaceFM becoming unresponsive with network filesystems.  Loading of thumbnails and "
                   "subdirectory sizes will also be disabled."));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Change Detection Blacklist"));
    set->line = g_strdup("#devices-settings-chdet");
    set->s = g_strdup("cifs curlftpfs ftpfs fuse.sshfs nfs smbfs");
    set->z = g_strdup(set->s);

    set = xset_set("dev_fs_cnf", "label", _("_Device Handlers"));
    xset_set_set(set, "icon", "gtk-preferences");
    set->line = g_strdup("#handlers-dev");

    set = xset_set("dev_net_cnf", "label", _("_Protocol Handlers"));
    xset_set_set(set, "icon", "gtk-preferences");
    set->line = g_strdup("#handlers-pro");

    // dev icons
    set = xset_get("sep_i1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_i2");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_i3");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_i4");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("dev_icon", "lbl", _("_Icon"));
    set->menu_style = XSET_MENU_SUBMENU;
#ifdef DEPRECATED_HW
    xset_set_set(set,
                 "desc",
                 "dev_icon_internal_mounted dev_icon_internal_unmounted sep_i1 dev_icon_remove_mounted "
                 "dev_icon_remove_unmounted sep_i2 dev_icon_optical_mounted dev_icon_optical_media "
                 "dev_icon_optical_nomedia dev_icon_audiocd sep_i3 sep_i4 dev_icon_network dev_icon_file");
#else
    xset_set_set(set,
                 "desc",
                 "dev_icon_internal_mounted dev_icon_internal_unmounted sep_i1 dev_icon_remove_mounted "
                 "dev_icon_remove_unmounted sep_i2 sep_i3 sep_i4 dev_icon_network dev_icon_file");
    set->line = g_strdup("#devices-settings-icon");
#endif

#ifdef DEPRECATED_HW
    set = xset_set("dev_icon_audiocd", "lbl", _("Audio CD"));
    set->line = g_strdup("");
    set->line = g_strdup("#devices-settings-icon");

    set = xset_set("dev_icon_optical_mounted", "lbl", _("Optical Mounted"));
    set->line = g_strdup("#devices-settings-icon");

    set = xset_set("dev_icon_optical_media", "lbl", _("Optical Has Media"));
    set->line = g_strdup("#devices-settings-icon");

    set = xset_set("dev_icon_optical_nomedia", "lbl", _("Optical No Media"));
    set->line = g_strdup("#devices-settings-icon");
#endif

    set = xset_set("dev_icon_remove_mounted", "lbl", _("Removable Mounted"));
    set->line = g_strdup("#devices-settings-icon");

    set = xset_set("dev_icon_remove_unmounted", "lbl", _("Removable Unmounted"));
    set->line = g_strdup("#devices-settings-icon");

    set = xset_set("dev_icon_internal_mounted", "lbl", _("Internal Mounted"));
    set->line = g_strdup("#devices-settings-icon");

    set = xset_set("dev_icon_internal_unmounted", "lbl", _("Internal Unmounted"));
    set->line = g_strdup("#devices-settings-icon");

    set = xset_set("dev_icon_network", "lbl", _("Mounted Network"));
    set->line = g_strdup("#devices-settings-icon");

    set = xset_set("dev_icon_file", "lbl", _("Mounted Other"));
    set->line = g_strdup("#devices-settings-icon");

    set = xset_set("book_open", "lbl", _("_Open"));
    set->line = g_strdup("#gui-book-side");

    set = xset_set("book_settings", "lbl", _("_Settings"));
    set->menu_style = XSET_MENU_SUBMENU;
    set->line = g_strdup("#gui-book-side");

    set = xset_set("book_icon", "lbl", _("Bookmark _Icon"));
    // do not set a default icon for book_icon
    set->line = g_strdup("#gui-book-side");

    set = xset_set("book_menu_icon", "lbl", _("Sub_menu Icon"));
    // do not set a default icon for book_menu_icon
    set->line = g_strdup("#gui-book-side");

    set = xset_set("book_show", "lbl", _("_Show Bookmarks"));
    set->menu_style = XSET_MENU_CHECK;
    xset_set_set(set, "shared_key", "panel1_show_book");
    set->line = g_strdup("#gui-book-side");

    set = xset_set("book_add", "lbl", _("New _Bookmark"));
    set->line = g_strdup("#gui-book-add");

    set = xset_set("main_book", "lbl", _("_Bookmarks"));
    set->menu_style = XSET_MENU_SUBMENU;
    set->line = g_strdup("#gui-book");

    // Rename/Move Dialog
    set = xset_set("move_name", "lbl", _("_Name"));
    set->menu_style = XSET_MENU_CHECK;

    set = xset_set("move_filename", "lbl", _("F_ilename"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    set = xset_set("move_parent", "lbl", _("_Parent"));
    set->menu_style = XSET_MENU_CHECK;

    set = xset_set("move_path", "lbl", _("P_ath"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    set = xset_set("move_type", "lbl", _("Typ_e"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    set = xset_set("move_target", "lbl", _("Ta_rget"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    set = xset_set("move_template", "lbl", _("Te_mplate"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    set = xset_set("move_option", "lbl", _("_Option"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "move_copy move_link move_copyt move_linkt move_as_root");

    set = xset_set("move_copy", "lbl", _("_Copy"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    set = xset_set("move_link", "lbl", _("_Link"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    set = xset_set("move_copyt", "lbl", _("Copy _Target"));
    set->menu_style = XSET_MENU_CHECK;

    set = xset_set("move_linkt", "lbl", _("Lin_k Target"));
    set->menu_style = XSET_MENU_CHECK;

    set = xset_set("move_as_root", "lbl", _("_As Root"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    set = xset_set("move_dlg_font", "lbl", _("_Font"));
    set->menu_style = XSET_MENU_FONTDLG;
    xset_set_set(set, "title", _("Move Dialog Font"));
    xset_set_set(set, "desc", _("/home/user/Example Filename.ext"));

    set = xset_set("move_dlg_help", "lbl", _("_Help"));

    set = xset_set("move_dlg_confirm_create", "lbl", _("_Confirm Create"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    // status bar
    set = xset_get("sep_bar1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("status_border", "lbl", _("Highlight _Bar"));
    xset_set_set(set, "title", _("Status Bar Highlight Color"));
    set->menu_style = XSET_MENU_COLORDLG;

    set = xset_set("status_text", "lbl", _("Highlight _Text"));
    xset_set_set(set, "title", _("Status Bar Text Highlight Color"));
    set->menu_style = XSET_MENU_COLORDLG;

    set = xset_set("status_middle", "lbl", _("_Middle Click"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "status_name status_path status_info status_hide");

    set = xset_set("status_name", "lbl", _("Copy _Name"));
    set->menu_style = XSET_MENU_RADIO;

    set = xset_set("status_path", "lbl", _("Copy _Path"));
    set->menu_style = XSET_MENU_RADIO;

    set = xset_set("status_info", "lbl", _("File _Info"));
    set->menu_style = XSET_MENU_RADIO;
    set->b = XSET_B_TRUE;

    set = xset_set("status_hide", "lbl", _("_Hide Panel"));
    set->menu_style = XSET_MENU_RADIO;

    // MAIN WINDOW MENUS

    // File
    set = xset_get("sep_f1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_f2");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_f3");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("main_new_window", "lbl", _("New _Window"));

    set = xset_set("main_root_window", "lbl", _("R_oot Window"));

    set = xset_set("main_search", "lbl", _("_File Search"));

    set = xset_set("main_terminal", "lbl", _("_Terminal"));
    set->b = XSET_B_UNSET; // discovery notification

    set = xset_set("main_root_terminal", "lbl", _("_Root Terminal"));

    // was previously used for 'Save Session' < 0.9.4 as XSET_MENU_NORMAL
    set = xset_set("main_save_session", "lbl", _("Open _URL"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Open URL"));
    xset_set_set(
        set,
        "desc",
        _("Enter URL in the "
          "format:\n\tPROTOCOL://USERNAME:PASSWORD@HOST:PORT/SHARE\n\nExamples:\n\tftp://mirrors.kernel.org\n\tsmb://"
          "user:pass@10.0.0.1:50/docs\n\tssh://user@sys.domain\n\tmtp://\n\nIncluding a password is unsafe.  To "
          "bookmark a URL, right-click on the mounted network in Devices and select Bookmark.\n"));
    set->line = NULL;

    set = xset_set("main_save_tabs", "lbl", _("Save Ta_bs"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    set = xset_set("main_exit", "lbl", _("E_xit"));

    // View
    set = xset_get("sep_v1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_v2");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_v3");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_v4");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_v5");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_v6");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_v7");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_v8");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_v9");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("panel1_show", "lbl", _("Panel _1"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#gui-pan");

    set = xset_set("panel2_show", "lbl", _("Panel _2"));
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#gui-pan");

    set = xset_set("panel3_show", "lbl", _("Panel _3"));
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#gui-pan");

    set = xset_set("panel4_show", "lbl", _("Panel _4"));
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#gui-pan");

    set = xset_set("main_pbar", "lbl", _("Panel _Bar"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#gui-pan");

    set = xset_set("main_focus_panel", "lbl", _("F_ocus"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "panel_prev panel_next panel_hide panel_1 panel_2 panel_3 panel_4");
    set->line = g_strdup("#gui-pan");

    set = xset_set("panel_prev", "lbl", _("_Prev"));
    set->line = g_strdup("#gui-pan");
    xset_set("panel_next", "lbl", _("_Next"));
    /*
    xset_set( "panel_left", "lbl", _("_Left") );
    xset_set( "panel_right", "lbl", _("_Right") );
    xset_set( "panel_top", "lbl", _("_Top") );
    xset_set( "panel_bottom", "lbl", _("_Bottom") );
    */
    xset_set("panel_hide", "lbl", _("_Hide"));
    set = xset_set("panel_1", "lbl", _("Panel _1"));
    set->line = g_strdup("#gui-pan");
    xset_set("panel_2", "lbl", _("Panel _2"));
    xset_set("panel_3", "lbl", _("Panel _3"));
    xset_set("panel_4", "lbl", _("Panel _4"));

    set = xset_set("main_auto", "lbl", _("_Event Manager"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "auto_inst auto_win auto_pnl auto_tab evt_device");
    set->line = g_strdup("#sockets-menu");

    set = xset_set("auto_inst", "lbl", _("_Instance"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "evt_start evt_exit");
    set->line = g_strdup("#sockets-menu");

    set = xset_set("evt_start", "lbl", _("_Startup"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Instance Startup Command"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically when a SpaceFM instance "
                   "starts:\n\nUse:\n\t%%e\tevent type  (evt_start)\n"));
    set->line = g_strdup("#sockets-events-start");

    set = xset_set("evt_exit", "lbl", _("_Exit"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Instance Exit Command"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically when a SpaceFM instance "
                   "exits:\n\nUse:\n\t%%e\tevent type  (evt_exit)\n"));
    set->line = g_strdup("#sockets-events-exit");

    set = xset_set("auto_win", "lbl", _("_Window"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "evt_win_new evt_win_focus evt_win_move evt_win_click evt_win_key evt_win_close");
    set->line = g_strdup("#sockets-menu");

    set = xset_set("evt_win_new", "lbl", C_("View|Events|Window|", "_New"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set New Window Command"));
    xset_set_set(
        set,
        "desc",
        _("Enter program or bash command line to be run automatically whenever a new SpaceFM window is "
          "opened:\n\nUse:\n\t%%e\tevent type  (evt_win_new)\n\t%%w\twindow id  (see spacefm -s "
          "help)\n\t%%p\tpanel\n\t%%t\ttab\n\nExported bash variables (eg $fm_pwd, etc) can be used in this command."));
    set->line = g_strdup("#sockets-events-winnew");

    set = xset_set("evt_win_focus", "lbl", C_("View|Events|Window|", "_Focus"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Window Focus Command"));
    xset_set_set(
        set,
        "desc",
        _("Enter program or bash command line to be run automatically whenever a SpaceFM window gets "
          "focus:\n\nUse:\n\t%%e\tevent type  (evt_win_focus)\n\t%%w\twindow id  (see spacefm -s "
          "help)\n\t%%p\tpanel\n\t%%t\ttab\n\nExported bash variables (eg $fm_pwd, etc) can be used in this command."));
    set->line = g_strdup("#sockets-events-winfoc");

    set = xset_set("evt_win_move", "lbl", _("_Move/Resize"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Window Move/Resize Command"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically whenever a SpaceFM window is moved or "
                   "resized:\n\nUse:\n\t%%e\tevent type  (evt_win_move)\n\t%%w\twindow id  (see spacefm -s "
                   "help)\n\t%%p\tpanel\n\t%%t\ttab\n\nExported bash variables (eg $fm_pwd, etc) can be used in this "
                   "command.\n\nNote: This command may be run multiple times during resize."));
    set->line = g_strdup("#sockets-events-winmov");

    set = xset_set("evt_win_click", "lbl", C_("View|Events|Window|", "_Click"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Click Command"));
    xset_set_set(
        set,
        "desc",
        _("Enter program or bash command line to be run automatically whenever the mouse is "
          "clicked:\n\nUse:\n\t%%e\tevent type  (evt_win_click)\n\t%%w\twindow id  (see spacefm -s "
          "help)\n\t%%p\tpanel\n\t%%t\ttab\n\t%%b\tbutton  (mouse button pressed)\n\t%%m\tmodifier  (modifier "
          "keys)\n\t%%f\tfocus  (element which received the click)\n\nExported bash variables (eg $fm_pwd, etc) can be "
          "used in this command when no asterisk prefix is used.\n\nPrefix your command with an asterisk (*) and "
          "conditionally return exit status 0 to inhibit the default handler.  For example:\n*if [ \"%%b\" != \"2\" ]; "
          "then exit 1; fi; spacefm -g --label \"\\nMiddle button was clicked in %%f\" --button ok &"));
    set->line = g_strdup("#sockets-events-winclk");

    set = xset_set("evt_win_key", "lbl", _("_Keypress"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Window Keypress Command"));
    xset_set_set(
        set,
        "desc",
        _("Enter program or bash command line to be run automatically whenever a key is pressed:\n\nUse:\n\t%%e\tevent "
          "type  (evt_win_key)\n\t%%w\twindow id  (see spacefm -s help)\n\t%%p\tpanel\n\t%%t\ttab\n\t%%k\tkey code  "
          "(key pressed)\n\t%%m\tmodifier  (modifier keys)\n\nExported bash variables (eg $fm_pwd, etc) can be used in "
          "this command when no asterisk prefix is used.\n\nPrefix your command with an asterisk (*) and conditionally "
          "return exit status 0 to inhibit the default handler.  For example:\n*if [ \"%%k\" != \"0xffc5\" ]; then "
          "exit 1; fi; spacefm -g --label \"\\nKey F8 was pressed.\" --button ok &"));
    set->line = g_strdup("#sockets-events-winkey");

    set = xset_set("evt_win_close", "lbl", C_("View|Events|Window|", "Cl_ose"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Window Close Command"));
    xset_set_set(
        set,
        "desc",
        _("Enter program or bash command line to be run automatically whenever a SpaceFM window is "
          "closed:\n\nUse:\n\t%%e\tevent type  (evt_win_close)\n\t%%w\twindow id  (see spacefm -s "
          "help)\n\t%%p\tpanel\n\t%%t\ttab\n\nExported bash variables (eg $fm_pwd, etc) can be used in this command."));
    set->line = g_strdup("#sockets-events-wincls");

    set = xset_set("auto_pnl", "lbl", _("_Panel"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "evt_pnl_focus evt_pnl_show evt_pnl_sel");
    set->line = g_strdup("#sockets-menu");

    set = xset_set("evt_pnl_focus", "lbl", C_("View|Events|Panel|", "_Focus"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Panel Focus Command"));
    xset_set_set(
        set,
        "desc",
        _("Enter program or bash command line to be run automatically whenever a panel gets "
          "focus:\n\nUse:\n\t%%e\tevent type  (evt_pnl_focus)\n\t%%w\twindow id  (see spacefm -s "
          "help)\n\t%%p\tpanel\n\t%%t\ttab\n\nExported bash variables (eg $fm_pwd, etc) can be used in this command."));
    set->line = g_strdup("#sockets-events-pnlfoc");

    set = xset_set("evt_pnl_show", "lbl", C_("View|Events|Panel|", "_Show"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Panel Show Command"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically whenever a panel or panel element is "
                   "shown or hidden:\n\nUse:\n\t%%e\tevent type  (evt_pnl_show)\n\t%%w\twindow id  (see spacefm -s "
                   "help)\n\t%%p\tpanel\n\t%%t\ttab\n\t%%f\tfocus  (element shown or hidden)\n\t%%v\tvisible  (1 or "
                   "0)\n\nExported bash variables (eg $fm_pwd, etc) can be used in this command."));
    set->line = g_strdup("#sockets-events-pnlshw");

    set = xset_set("evt_pnl_sel", "lbl", _("S_elect"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Panel Select Command"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically whenever the file selection "
                   "changes:\n\nUse:\n\t%%e\tevent type  (evt_pnl_sel)\n\t%%w\twindow id  (see spacefm -s "
                   "help)\n\t%%p\tpanel\n\t%%t\ttab\n\nExported bash variables (eg $fm_pwd, etc) can be used in this "
                   "command.\n\nPrefix your command with an asterisk (*) and conditionally return exit status 0 to "
                   "inhibit the default handler."));
    set->line = g_strdup("#sockets-events-pnlsel");

    set = xset_set("auto_tab", "lbl", C_("View|Events|", "_Tab"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "evt_tab_new evt_tab_chdir evt_tab_focus evt_tab_close");
    set->line = g_strdup("#sockets-menu");

    set = xset_set("evt_tab_new", "lbl", C_("View|Events|Tab|", "_New"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set New Tab Command"));
    xset_set_set(
        set,
        "desc",
        _("Enter program or bash command line to be run automatically whenever a new tab is "
          "opened:\n\nUse:\n\t%%e\tevent type  (evt_tab_new)\n\t%%w\twindow id  (see spacefm -s "
          "help)\n\t%%p\tpanel\n\t%%t\ttab\n\nExported bash variables (eg $fm_pwd, etc) can be used in this command."));
    set->line = g_strdup("#sockets-events-tabnew");

    set = xset_set("evt_tab_chdir", "lbl", _("_Change Dir"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Tab Change Dir Command"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically whenever a tab changes to a different "
                   "directory:\n\nUse:\n\t%%e\tevent type  (evt_tab_chdir)\n\t%%w\twindow id  (see spacefm -s "
                   "help)\n\t%%p\tpanel\n\t%%t\ttab\n\t%%d\tnew directory\n\nExported bash variables (eg $fm_pwd, etc) "
                   "can be used in this command."));
    set->line = g_strdup("#sockets-events-tabchdir");

    set = xset_set("evt_tab_focus", "lbl", C_("View|Events|Tab|", "_Focus"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Tab Focus Command"));
    xset_set_set(
        set,
        "desc",
        _("Enter program or bash command line to be run automatically whenever a tab gets focus:\n\nUse:\n\t%%e\tevent "
          "type  (evt_tab_focus)\n\t%%w\twindow id  (see spacefm -s help)\n\t%%p\tpanel\n\t%%t\ttab\n\nExported bash "
          "variables (eg $fm_pwd, etc) can be used in this command."));
    set->line = g_strdup("#sockets-events-tabfoc");

    set = xset_set("evt_tab_close", "lbl", C_("View|Events|Tab|", "_Close"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Tab Close Command"));
    xset_set_set(
        set,
        "desc",
        _("Enter program or bash command line to be run automatically whenever a tab is closed:\n\nUse:\n\t%%e\tevent "
          "type  (evt_tab_close)\n\t%%w\twindow id  (see spacefm -s help)\n\t%%p\tpanel\n\t%%t\tclosed tab"));
    set->line = g_strdup("#sockets-events-tabcls");

    set = xset_set("evt_device", "lbl", C_("View|Events|", "_Device"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Device Command"));
    xset_set_set(set,
                 "desc",
                 _("Enter program or bash command line to be run automatically whenever a device state "
                   "changes:\n\nUse:\n\t%%e\tevent type  (evt_device)\n\t%%f\tdevice file\n\t%%v\tchange  "
                   "(added|removed|changed)\n"));
    set->line = g_strdup("#sockets-events-device");

    set = xset_set("main_title", "lbl", _("Wi_ndow Title"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Set Window Title Format"));
    xset_set_set(
        set,
        "desc",
        _("Set window title format:\n\nUse:\n\t%%n\tcurrent folder name (eg bin)\n\t%%d\tcurrent folder path (eg "
          "/usr/bin)\n\t%%p\tcurrent panel number (1-4)\n\t%%t\tcurrent tab number\n\t%%P\ttotal number of panels "
          "visible\n\t%%T\ttotal number of tabs in current panel\n\t*\tasterisk shown if tasks running in window"));
    xset_set_set(set, "s", "%d");
    xset_set_set(set, "z", "%d");

    set = xset_set("main_icon", "lbl", _("_Window Icon"));
    // Note: xset_text_dialog uses the title passed to know this is an
    // icon chooser, so it adds a Choose button.  If you change the title,
    // change xset_text_dialog.
    set->title = g_strdup(_("Set Window Icon"));
    set->desc =
        g_strdup(_("Enter an icon name, icon file path, or stock item name:\n\nOr click Choose to select an icon.  Not "
                   "all icons may work properly due to various issues.\n\nProvided alternate SpaceFM "
                   "icons:\n\tspacefm-[48|128]-[cube|pyramid]-[blue|green|red]\n\tspacefm-48-folder-[blue|red]\n\nFor "
                   "example: spacefm-48-pyramid-green"));
    // x and y store global icon chooser dialog size

    set = xset_set("main_full", "lbl", _("_Fullscreen"));
    set->menu_style = XSET_MENU_CHECK;

    set = xset_set("main_design_mode", "lbl", _("_Design Mode"));

    set = xset_set("main_prefs", "lbl", _("_Preferences"));

    set = xset_set("main_tool", "lbl", _("_Tool"));
    set->menu_style = XSET_MENU_SUBMENU;

    set = xset_get("root_bar"); // in Preferences
    set->b = XSET_B_TRUE;

    set = xset_set("view_thumb", "lbl", _("_Thumbnails (global)")); // in View|Panel View|Style
    set->menu_style = XSET_MENU_CHECK;

    // Plugins
    set = xset_set("plug_copy", "lbl", _("_Import"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "plug_cfile plug_cverb");
    set->line = g_strdup("#plugins-import");

    set = xset_set("plug_cfile", "lbl", _("_File"));
    set->line = g_strdup("#plugins-import");
    set = xset_set("plug_cverb", "lbl", _("_Verbose"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#plugins-import");

    // Help
    set = xset_get("sep_h1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_h2");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_h3");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("main_help", "lbl", _("_User's Manual"));

    set = xset_set("main_getplug", "lbl", _("_Get Plugins"));

    set = xset_set("main_help_opt", "lbl", _("_Options"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "main_help_browser main_help_url");

    set = xset_set("main_help_browser", "lbl", _("_Browser"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Choose HTML Browser"));
    xset_set_set(set,
                 "desc",
                 _("Enter browser name or bash command line to be used to display HTML files and "
                   "URLs:\n\nUse:\n\t%%u\turl\n\n(Leave blank for automatic browser detection)"));

    set = xset_set("main_help_url", "lbl", _("_Manual Location"));
    set->menu_style = XSET_MENU_STRING;
    xset_set_set(set, "title", _("Choose User's Manual Location"));
    xset_set_set(
        set,
        "desc",
        _("Enter local file path or remote URL for the SpaceFM User's Manual:\n\n(Leave blank for default)\n"));

    set = xset_set("main_about", "lbl", _("_About"));

    set = xset_set("main_dev", "lbl", _("_Show Devices"));
    xset_set_set(set, "shared_key", "panel1_show_devmon");
    set->menu_style = XSET_MENU_CHECK;
    set = xset_get("main_dev_sep");
    set->menu_style = XSET_MENU_SEP;

    // Tasks
    set = xset_get("sep_t1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_t2");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_t3");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_t4");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_t5");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_t6");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("main_tasks", "lbl", _("_Task Manager"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "task_show_manager task_hide_manager sep_t1 task_columns task_popups task_errors task_queue");
    set->line = g_strdup("#tasks");

    set = xset_set("task_col_status", "lbl", _("_Status"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->x = g_strdup("0");   // column position
    set->y = g_strdup("130"); // column width

    set = xset_set("task_col_count", "lbl", _("_Count"));
    set->menu_style = XSET_MENU_CHECK;
    set->x = g_strdup_printf("%d", 1);
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_path", "lbl", _("_Folder"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->x = g_strdup_printf("%d", 2);
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_file", "lbl", _("_Item"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->x = g_strdup_printf("%d", 3);
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_to", "lbl", _("_To"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->x = g_strdup_printf("%d", 4);
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_progress", "lbl", _("_Progress"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->x = g_strdup_printf("%d", 5);
    set->y = g_strdup("100");
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_total", "lbl", _("T_otal"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->x = g_strdup_printf("%d", 6);
    set->y = g_strdup("120");
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_started", "lbl", _("Sta_rted"));
    set->menu_style = XSET_MENU_CHECK;
    set->x = g_strdup_printf("%d", 7);
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_elapsed", "lbl", _("_Elapsed"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->x = g_strdup_printf("%d", 8);
    set->y = g_strdup("70");
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_curspeed", "lbl", _("C_urrent Speed"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->x = g_strdup_printf("%d", 9);
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_curest", "lbl", _("Current Re_main"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->x = g_strdup_printf("%d", 10);
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_avgspeed", "lbl", _("_Average Speed"));
    set->menu_style = XSET_MENU_CHECK;
    set->x = g_strdup_printf("%d", 11);
    set->y = g_strdup("60");
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_avgest", "lbl", _("A_verage Remain"));
    set->menu_style = XSET_MENU_CHECK;
    set->x = g_strdup_printf("%d", 12);
    set->y = g_strdup("65");
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_col_reorder", "lbl", _("Reor_der"));
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_stop", "lbl", _("_Stop"));
    set->line = g_strdup("#tasks-menu-stop");
    set = xset_set("task_pause", "lbl", _("Pa_use"));
    set->line = g_strdup("#tasks-menu-pause");
    set = xset_set("task_que", "lbl", _("_Queue"));
    set->line = g_strdup("#tasks-menu-queue");
    set = xset_set("task_resume", "lbl", _("_Resume"));
    set->line = g_strdup("#tasks-menu-resume");
    set = xset_set("task_showout", "lbl", _("Sho_w Output"));
    set->line = g_strdup("#tasks-menu-showout");

    set = xset_set("task_all", "lbl", _("_All Tasks"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "task_stop_all task_pause_all task_que_all task_resume_all");
    set->line = g_strdup("#tasks-menu-all");

    set = xset_set("task_stop_all", "lbl", _("_Stop"));
    set->line = g_strdup("#tasks-menu-all");
    set = xset_set("task_pause_all", "lbl", _("Pa_use"));
    set->line = g_strdup("#tasks-menu-all");
    set = xset_set("task_que_all", "lbl", _("_Queue"));
    set->line = g_strdup("#tasks-menu-all");
    set = xset_set("task_resume_all", "lbl", _("_Resume"));
    set->line = g_strdup("#tasks-menu-all");

    set = xset_set("task_show_manager", "lbl", _("Show _Manager"));
    set->menu_style = XSET_MENU_RADIO;
    set->b = XSET_B_FALSE;
    set->line = g_strdup("#tasks-menu-show");
    // set->x  used for task man height >=0.9.4

    set = xset_set("task_hide_manager", "lbl", _("Auto-_Hide Manager"));
    set->menu_style = XSET_MENU_RADIO;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#tasks-menu-auto");

    set = xset_set("font_task", "lbl", _("_Font"));
    set->menu_style = XSET_MENU_FONTDLG;
    xset_set_set(set, "title", _("Task Manager Font"));
    xset_set_set(set, "desc", _("copying  File  1:15  65.2 M  30.2 M/s"));
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_columns", "lbl", _("_Columns"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "task_col_count task_col_path task_col_file task_col_to task_col_progress task_col_total "
                 "task_col_started task_col_elapsed task_col_curspeed task_col_curest task_col_avgspeed "
                 "task_col_avgest sep_t2 task_col_reorder font_task");
    set->line = g_strdup("#tasks-menu-col");

    set = xset_set("task_popups", "lbl", _("_Popups"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "task_pop_all task_pop_top task_pop_above task_pop_stick sep_t6 task_pop_detail task_pop_over "
                 "task_pop_err task_pop_font");
    set->line = g_strdup("#tasks-menu-popall");

    set = xset_set("task_pop_all", "lbl", _("Popup _All Tasks"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_FALSE;
    set->line = g_strdup("#tasks-menu-popall");

    set = xset_set("task_pop_top", "lbl", _("Stay On _Top"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_FALSE;
    set->line = g_strdup("#tasks-menu-poptop");

    set = xset_set("task_pop_above", "lbl", _("A_bove Others"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_FALSE;
    set->line = g_strdup("#tasks-menu-popabove");

    set = xset_set("task_pop_stick", "lbl", _("All _Workspaces"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_FALSE;
    set->line = g_strdup("#tasks-menu-popstick");

    set = xset_set("task_pop_detail", "lbl", _("_Detailed Stats"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_FALSE;
    set->line = g_strdup("#tasks-menu-popdet");

    set = xset_set("task_pop_over", "lbl", _("_Overwrite Option"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#tasks-menu-popover");

    set = xset_set("task_pop_err", "lbl", _("_Error Option"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#tasks-menu-poperropt");

    set = xset_set("task_pop_font", "lbl", _("_Font"));
    set->menu_style = XSET_MENU_FONTDLG;
    xset_set_set(set, "title", _("Task Popup Font (affects new tasks)"));
    xset_set_set(set, "desc", _("Example Output 0123456789"));
    set->s = g_strdup("Monospace 11");
    set->line = g_strdup("#tasks-menu-popfont");

    set = xset_set("task_errors", "lbl", _("Err_ors"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "task_err_first task_err_any task_err_cont");
    set->line = g_strdup("#tasks-menu-poperr");

    set = xset_set("task_err_first", "lbl", _("Stop If _First"));
    set->menu_style = XSET_MENU_RADIO;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#tasks-menu-poperr");

    set = xset_set("task_err_any", "lbl", _("Stop On _Any"));
    set->menu_style = XSET_MENU_RADIO;
    set->b = XSET_B_FALSE;
    set->line = g_strdup("#tasks-menu-poperr");

    set = xset_set("task_err_cont", "lbl", _("_Continue"));
    set->menu_style = XSET_MENU_RADIO;
    set->b = XSET_B_FALSE;
    set->line = g_strdup("#tasks-menu-poperr");

    set = xset_set("task_queue", "lbl", _("Qu_eue"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "task_q_new task_q_smart task_q_pause");
    set->line = g_strdup("#tasks-menu-new");

    set = xset_set("task_q_new", "lbl", _("_Queue New Tasks"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#tasks-menu-new");

    set = xset_set("task_q_smart", "lbl", _("_Smart Queue"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#tasks-menu-smart");

    set = xset_set("task_q_pause", "lbl", _("_Pause On Error"));
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#tasks-menu-qpause");

    // Menu Item Properties
    set = xset_get("sep_ctxt");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("context_dlg", "lbl", _("_Font"));
    set->menu_style = XSET_MENU_FONTDLG;
    xset_set_set(set, "title", _("Editor Font"));
    xset_set_set(set, "desc", _("Example Input 0123456789"));
    set->s = g_strdup("Monospace 11");
    // set->b reserved for Ignore Context

    // PANELS COMMON
    set = xset_get("sep_new");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_edit");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_tab");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_entry");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_mopt1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_mopt2");
    set->menu_style = XSET_MENU_SEP;

    xset_set("date_format", "s", "%Y-%m-%d %H:%M");

    set = xset_set("input_font", "lbl", _("_Font"));
    set->menu_style = XSET_MENU_FONTDLG;
    xset_set_set(set, "title", _("Input Font"));
    xset_set_set(set, "desc", _("Example Input 0123456789"));

    set = xset_set("con_open", "lbl", _("_Open"));
    set->menu_style = XSET_MENU_SUBMENU;

    set = xset_set("open_execute", "lbl", _("E_xecute"));

    set = xset_set("open_edit", "lbl", _("Edi_t"));

    set = xset_set("open_edit_root", "lbl", _("Edit As _Root"));

    set = xset_set("open_other", "lbl", _("_Choose..."));

    set = xset_set("open_hand", "lbl", _("File _Handlers..."));
    set->line = g_strdup("#handlers-fil");

    set = xset_set("open_all", "lbl", _("Open With _Default")); // virtual

    set = xset_set("open_in_tab", "lbl", _("In _Tab"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "opentab_new opentab_prev opentab_next opentab_1 opentab_2 opentab_3 opentab_4 opentab_5 opentab_6 "
                 "opentab_7 opentab_8 opentab_9 opentab_10");

    xset_set("opentab_new", "lbl", _("N_ew"));
    xset_set("opentab_prev", "lbl", _("_Prev"));
    xset_set("opentab_next", "lbl", _("_Next"));
    xset_set("opentab_1", "lbl", _("Tab _1"));
    xset_set("opentab_2", "lbl", _("Tab _2"));
    xset_set("opentab_3", "lbl", _("Tab _3"));
    xset_set("opentab_4", "lbl", _("Tab _4"));
    xset_set("opentab_5", "lbl", _("Tab _5"));
    xset_set("opentab_6", "lbl", _("Tab _6"));
    xset_set("opentab_7", "lbl", _("Tab _7"));
    xset_set("opentab_8", "lbl", _("Tab _8"));
    xset_set("opentab_9", "lbl", _("Tab _9"));
    xset_set("opentab_10", "lbl", _("Tab 1_0"));

    set = xset_set("open_in_panel", "lbl", _("In _Panel"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "open_in_panelprev open_in_panelnext open_in_panel1 open_in_panel2 open_in_panel3 open_in_panel4");

    xset_set("open_in_panelprev", "lbl", _("_Prev"));
    xset_set("open_in_panelnext", "lbl", _("_Next"));
    xset_set("open_in_panel1", "lbl", _("Panel _1"));
    xset_set("open_in_panel2", "lbl", _("Panel _2"));
    xset_set("open_in_panel3", "lbl", _("Panel _3"));
    xset_set("open_in_panel4", "lbl", _("Panel _4"));

    set = xset_set("arc_extract", "lbl", _("_Extract"));
    set->line = g_strdup("#handlers-arc");

    set = xset_set("arc_extractto", "lbl", _("Extract _To"));
    set->line = g_strdup("#handlers-arc");

    set = xset_set("arc_list", "lbl", _("_List Contents"));
    set->line = g_strdup("#handlers-arc");

    set = xset_get("sep_arc1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_arc2");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("arc_default", "lbl", _("_Archive Defaults"));
    set->line = g_strdup("#handlers-arc-arcdef");
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(
        set,
        "desc",
        "arc_conf2 sep_arc2 arc_def_open arc_def_ex arc_def_exto arc_def_list sep_arc1 arc_def_parent arc_def_write");

    set = xset_set("arc_def_open", "lbl", _("_Open With App"));
    set->menu_style = XSET_MENU_RADIO;
    set->line = g_strdup("#handlers-arc-arcdef");

    set = xset_set("arc_def_ex", "lbl", _("_Extract"));
    set->menu_style = XSET_MENU_RADIO;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#handlers-arc-arcdef");

    set = xset_set("arc_def_exto", "lbl", _("Extract _To"));
    set->menu_style = XSET_MENU_RADIO;
    set->line = g_strdup("#handlers-arc-arcdef");

    set = xset_set("arc_def_list", "lbl", _("_List Contents"));
    set->menu_style = XSET_MENU_RADIO;
    set->line = g_strdup("#handlers-arc-arcdef");

    set = xset_set("arc_def_parent", "lbl", _("_Create Subfolder"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#handlers-arc-arcdef");

    set = xset_set("arc_def_write", "lbl", _("_Write Access"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#handlers-arc-arcdef");

    set = xset_set("arc_conf2", "label", _("Archive _Handlers"));
    xset_set_set(set, "icon", "gtk-preferences");
    set->line = g_strdup("#handlers-arc");

    set = xset_get("sep_o1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("open_new", "lbl", _("_New"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "new_file new_folder new_link new_archive sep_o1 tab_new tab_new_here new_bookmark");

    set = xset_set("new_file", "lbl", _("_File"));
    set->line = g_strdup("#gui-newf");

    set = xset_set("new_folder", "lbl", _("Fol_der"));
    set->line = g_strdup("#gui-newf");

    set = xset_set("new_link", "lbl", _("_Link"));
    set->line = g_strdup("#gui-newf");

    set = xset_set("new_bookmark", "lbl", C_("New|", "_Bookmark"));
    xset_set_set(set, "shared_key", "book_add");

    set = xset_set("new_archive", "lbl", _("_Archive"));

    set = xset_get("arc_dlg");
    set->b = XSET_B_TRUE;   // Extract To - Create Subfolder
    set->z = g_strdup("1"); // Extract To - Write Access

    set = xset_set("tab_new", "lbl", C_("New|", "_Tab"));
    set = xset_set("tab_new_here", "lbl", _("Tab _Here"));

    set = xset_set("new_app", "lbl", _("_Desktop Application"));

    set = xset_get("sep_g1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("con_go", "lbl", _("_Go"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "go_back go_forward go_up go_home go_default go_set_default edit_canon sep_g1 go_tab go_focus");

    set = xset_set("go_back", "lbl", _("_Back"));
    set = xset_set("go_forward", "lbl", _("_Forward"));
    set = xset_set("go_up", "lbl", _("_Up"));
    set = xset_set("go_home", "lbl", _("_Home"));
    set = xset_set("go_default", "lbl", _("_Default"));

    set = xset_set("go_set_default", "lbl", _("_Set Default"));

    set = xset_set("edit_canon", "lbl", _("Re_al Path"));

    set = xset_set("go_focus", "lbl", _("Fo_cus"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "focus_path_bar focus_filelist focus_dirtree focus_book focus_device");

    set = xset_set("focus_path_bar", "lbl", _("_Path Bar"));
    set = xset_set("focus_filelist", "lbl", _("_File List"));
    set = xset_set("focus_dirtree", "lbl", _("_Tree"));
    set = xset_set("focus_book", "lbl", _("_Bookmarks"));
    set = xset_set("focus_device", "lbl", _("De_vices"));

    set = xset_set("go_tab", "lbl", C_("Go|", "_Tab"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "tab_prev tab_next tab_close tab_1 tab_2 tab_3 tab_4 tab_5 tab_6 tab_7 tab_8 tab_9 tab_10");

    xset_set("tab_prev", "lbl", _("_Prev"));
    xset_set("tab_next", "lbl", _("_Next"));
    set = xset_set("tab_close", "lbl", _("_Close"));
    xset_set("tab_1", "lbl", _("Tab _1"));
    xset_set("tab_2", "lbl", _("Tab _2"));
    xset_set("tab_3", "lbl", _("Tab _3"));
    xset_set("tab_4", "lbl", _("Tab _4"));
    xset_set("tab_5", "lbl", _("Tab _5"));
    xset_set("tab_6", "lbl", _("Tab _6"));
    xset_set("tab_7", "lbl", _("Tab _7"));
    xset_set("tab_8", "lbl", _("Tab _8"));
    xset_set("tab_9", "lbl", _("Tab _9"));
    xset_set("tab_10", "lbl", _("Tab 1_0"));

    set = xset_set("con_view", "lbl", _("_View"));
    set->menu_style = XSET_MENU_SUBMENU;

    set = xset_set("view_list_style", "lbl", _("Styl_e"));
    set->menu_style = XSET_MENU_SUBMENU;

    set = xset_set("view_columns", "lbl", _("C_olumns"));
    set->menu_style = XSET_MENU_SUBMENU;

    set = xset_set("view_reorder_col", "lbl", _("_Reorder"));

    set = xset_set("rubberband", "lbl", _("_Rubberband Select"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;

    set = xset_get("sep_s1");
    set->menu_style = XSET_MENU_SEP;
    set = xset_get("sep_s2");
    set->menu_style = XSET_MENU_SEP;
    set = xset_get("sep_s3");
    set->menu_style = XSET_MENU_SEP;
    set = xset_get("sep_s4");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("view_sortby", "lbl", _("_Sort"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "sortby_name sortby_size sortby_type sortby_perm sortby_owner sortby_date sep_s1 sortby_ascend "
                 "sortby_descend sep_s2 sortx_natural sortx_case sep_s3 sortx_folders sortx_files sortx_mix sep_s4 "
                 "sortx_hidfirst sortx_hidlast");

    set = xset_set("sortby_name", "lbl", _("_Name"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortby_size", "lbl", _("_Size"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortby_type", "lbl", _("_Type"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortby_perm", "lbl", _("_Permission"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortby_owner", "lbl", _("_Owner"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortby_date", "lbl", _("_Modified"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortby_ascend", "lbl", _("_Ascending"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortby_descend", "lbl", _("_Descending"));
    set->menu_style = XSET_MENU_RADIO;

    set = xset_set("sortx_natural", "lbl", _("Nat_ural"));
    set->menu_style = XSET_MENU_CHECK;
    set = xset_set("sortx_case", "lbl", _("_Case Sensitive"));
    set->menu_style = XSET_MENU_CHECK;
    set = xset_set("sortx_folders", "lbl", _("Folders Fi_rst"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortx_files", "lbl", _("F_iles First"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortx_mix", "lbl", _("Mi_xed"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortx_hidfirst", "lbl", _("_Hidden First"));
    set->menu_style = XSET_MENU_RADIO;
    set = xset_set("sortx_hidlast", "lbl", _("Hidden _Last"));
    set->menu_style = XSET_MENU_RADIO;

    set = xset_set("view_refresh", "lbl", _("Re_fresh"));

    set = xset_set("path_seek", "lbl", _("Auto See_k"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#gui-pathbar-seek");

    set = xset_set("path_hand", "lbl", _("_Protocol Handlers"));
    set->line = g_strdup("#handlers-pro");
    xset_set_set(set, "shared_key", "dev_net_cnf");
    // set->s was custom protocol handler in sfm<=0.9.3 - retained

    set = xset_set("path_help", "lbl", _("Path Bar _Help"));

    // EDIT
    set = xset_set("edit_cut", "lbl", _("Cu_t"));

    set = xset_set("edit_copy", "lbl", _("_Copy"));

    set = xset_set("edit_paste", "lbl", _("_Paste"));

    set = xset_set("edit_rename", "lbl", _("_Rename"));
    set->line = g_strdup("#gui-rename");

    set = xset_set("edit_delete", "lbl", _("_Delete"));

    set = xset_get("sep_e1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_e2");
    set->menu_style = XSET_MENU_SEP;

    set = xset_get("sep_e3");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("edit_submenu", "lbl", _("_Actions"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "copy_name copy_parent copy_path sep_e1 paste_link paste_target paste_as sep_e2 copy_to move_to "
                 "edit_root sep_e3 select_all select_patt select_invert select_un");

    set = xset_set("copy_name", "lbl", _("Copy _Name"));

    set = xset_set("copy_path", "lbl", _("Copy _Path"));

    set = xset_set("copy_parent", "lbl", _("Copy Pa_rent"));

    set = xset_set("paste_link", "lbl", _("Paste _Link"));

    set = xset_set("paste_target", "lbl", _("Paste _Target"));

    set = xset_set("paste_as", "lbl", _("Paste _As"));

    set = xset_get("sep_c1");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("copy_to", "lbl", _("_Copy To"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "copy_loc copy_loc_last sep_c1 copy_tab copy_panel");

    set = xset_set("copy_loc", "lbl", _("L_ocation"));
    set = xset_set("copy_loc_last", "lbl", _("L_ast Location"));

    set = xset_set("copy_tab", "lbl", C_("Edit|CopyTo|", "_Tab"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "copy_tab_prev copy_tab_next copy_tab_1 copy_tab_2 copy_tab_3 copy_tab_4 copy_tab_5 copy_tab_6 "
                 "copy_tab_7 copy_tab_8 copy_tab_9 copy_tab_10");

    xset_set("copy_tab_prev", "lbl", _("_Prev"));
    xset_set("copy_tab_next", "lbl", _("_Next"));
    xset_set("copy_tab_1", "lbl", _("Tab _1"));
    xset_set("copy_tab_2", "lbl", _("Tab _2"));
    xset_set("copy_tab_3", "lbl", _("Tab _3"));
    xset_set("copy_tab_4", "lbl", _("Tab _4"));
    xset_set("copy_tab_5", "lbl", _("Tab _5"));
    xset_set("copy_tab_6", "lbl", _("Tab _6"));
    xset_set("copy_tab_7", "lbl", _("Tab _7"));
    xset_set("copy_tab_8", "lbl", _("Tab _8"));
    xset_set("copy_tab_9", "lbl", _("Tab _9"));
    xset_set("copy_tab_10", "lbl", _("Tab 1_0"));

    set = xset_set("copy_panel", "lbl", C_("Edit|CopyTo|", "_Panel"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "copy_panel_prev copy_panel_next copy_panel_1 copy_panel_2 copy_panel_3 copy_panel_4");

    xset_set("copy_panel_prev", "lbl", _("_Prev"));
    xset_set("copy_panel_next", "lbl", _("_Next"));
    xset_set("copy_panel_1", "lbl", _("Panel _1"));
    xset_set("copy_panel_2", "lbl", _("Panel _2"));
    xset_set("copy_panel_3", "lbl", _("Panel _3"));
    xset_set("copy_panel_4", "lbl", _("Panel _4"));

    set = xset_get("sep_c2");
    set->menu_style = XSET_MENU_SEP;

    set = xset_set("move_to", "lbl", _("_Move To"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "move_loc move_loc_last sep_c2 move_tab move_panel");

    set = xset_set("move_loc", "lbl", _("_Location"));
    set = xset_set("move_loc_last", "lbl", _("L_ast Location"));
    set = xset_set("move_tab", "lbl", C_("Edit|MoveTo|", "_Tab"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "move_tab_prev move_tab_next move_tab_1 move_tab_2 move_tab_3 move_tab_4 move_tab_5 move_tab_6 "
                 "move_tab_7 move_tab_8 move_tab_9 move_tab_10");

    xset_set("move_tab_prev", "lbl", _("_Prev"));
    xset_set("move_tab_next", "lbl", _("_Next"));
    xset_set("move_tab_1", "lbl", _("Tab _1"));
    xset_set("move_tab_2", "lbl", _("Tab _2"));
    xset_set("move_tab_3", "lbl", _("Tab _3"));
    xset_set("move_tab_4", "lbl", _("Tab _4"));
    xset_set("move_tab_5", "lbl", _("Tab _5"));
    xset_set("move_tab_6", "lbl", _("Tab _6"));
    xset_set("move_tab_7", "lbl", _("Tab _7"));
    xset_set("move_tab_8", "lbl", _("Tab _8"));
    xset_set("move_tab_9", "lbl", _("Tab _9"));
    xset_set("move_tab_10", "lbl", _("Tab 1_0"));

    set = xset_set("move_panel", "lbl", C_("Edit|MoveTo|", "_Panel"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "move_panel_prev move_panel_next move_panel_1 move_panel_2 move_panel_3 move_panel_4");

    xset_set("move_panel_prev", "lbl", _("_Prev"));
    xset_set("move_panel_next", "lbl", _("_Next"));
    xset_set("move_panel_1", "lbl", _("Panel _1"));
    xset_set("move_panel_2", "lbl", _("Panel _2"));
    xset_set("move_panel_3", "lbl", _("Panel _3"));
    xset_set("move_panel_4", "lbl", _("Panel _4"));

    set = xset_set("select_all", "lbl", _("_Select All"));

    set = xset_set("select_un", "lbl", _("_Unselect All"));

    set = xset_set("select_invert", "lbl", _("_Invert Selection"));

    set = xset_set("select_patt", "lbl", _("S_elect By Pattern"));

    set = xset_set("edit_root", "lbl", _("R_oot"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "root_copy_loc root_move2 root_delete");

    set = xset_set("root_copy_loc", "lbl", _("_Copy To"));
    set = xset_set("root_move2", "lbl", _("Move _To"));
    set = xset_set("root_delete", "lbl", _("_Delete"));

    // Properties
    set = xset_set("con_prop", "lbl", _("Propert_ies"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "");

    set = xset_set("prop_info", "lbl", _("_Info"));

    set = xset_set("prop_perm", "lbl", _("_Permissions"));

    set = xset_set("prop_quick", "lbl", _("_Quick"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(
        set,
        "desc",
        "perm_r perm_rw perm_rwx perm_r_r perm_rw_r perm_rw_rw perm_rwxr_x perm_rwxrwx perm_r_r_r perm_rw_r_r "
        "perm_rw_rw_rw perm_rwxr_r perm_rwxr_xr_x perm_rwxrwxrwx perm_rwxrwxrwt perm_unstick perm_stick perm_recurs");

    xset_set("perm_r", "lbl", "r--------");
    xset_set("perm_rw", "lbl", "rw-------");
    xset_set("perm_rwx", "lbl", "rwx------");
    xset_set("perm_r_r", "lbl", "r--r-----");
    xset_set("perm_rw_r", "lbl", "rw-r-----");
    xset_set("perm_rw_rw", "lbl", "rw-rw----");
    xset_set("perm_rwxr_x", "lbl", "rwxr-x---");
    xset_set("perm_rwxrwx", "lbl", "rwxrwx---");
    xset_set("perm_r_r_r", "lbl", "r--r--r--");
    xset_set("perm_rw_r_r", "lbl", "rw-r--r--");
    xset_set("perm_rw_rw_rw", "lbl", "rw-rw-rw-");
    xset_set("perm_rwxr_r", "lbl", "rwxr--r--");
    xset_set("perm_rwxr_xr_x", "lbl", "rwxr-xr-x");
    xset_set("perm_rwxrwxrwx", "lbl", "rwxrwxrwx");
    xset_set("perm_rwxrwxrwt", "lbl", "rwxrwxrwt");
    xset_set("perm_unstick", "lbl", "-t");
    xset_set("perm_stick", "lbl", "+t");

    set = xset_set("perm_recurs", "lbl", _("_Recursive"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "perm_go_w perm_go_rwx perm_ugo_w perm_ugo_rx perm_ugo_rwx");

    xset_set("perm_go_w", "lbl", "go-w");
    xset_set("perm_go_rwx", "lbl", "go-rwx");
    xset_set("perm_ugo_w", "lbl", "ugo+w");
    xset_set("perm_ugo_rx", "lbl", "ugo+rX");
    xset_set("perm_ugo_rwx", "lbl", "ugo+rwX");

    set = xset_set("prop_root", "lbl", _("_Root"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(
        set,
        "desc",
        "rperm_rw rperm_rwx rperm_rw_r rperm_rw_rw rperm_rwxr_x rperm_rwxrwx rperm_rw_r_r rperm_rw_rw_rw rperm_rwxr_r "
        "rperm_rwxr_xr_x rperm_rwxrwxrwx rperm_rwxrwxrwt rperm_unstick rperm_stick rperm_recurs rperm_own");

    xset_set("rperm_rw", "lbl", "rw-------");
    xset_set("rperm_rwx", "lbl", "rwx------");
    xset_set("rperm_rw_r", "lbl", "rw-r-----");
    xset_set("rperm_rw_rw", "lbl", "rw-rw----");
    xset_set("rperm_rwxr_x", "lbl", "rwxr-x---");
    xset_set("rperm_rwxrwx", "lbl", "rwxrwx---");
    xset_set("rperm_rw_r_r", "lbl", "rw-r--r--");
    xset_set("rperm_rw_rw_rw", "lbl", "rw-rw-rw-");
    xset_set("rperm_rwxr_r", "lbl", "rwxr--r--");
    xset_set("rperm_rwxr_xr_x", "lbl", "rwxr-xr-x");
    xset_set("rperm_rwxrwxrwx", "lbl", "rwxrwxrwx");
    xset_set("rperm_rwxrwxrwt", "lbl", "rwxrwxrwt");
    xset_set("rperm_unstick", "lbl", "-t");
    xset_set("rperm_stick", "lbl", "+t");

    set = xset_set("rperm_recurs", "lbl", _("_Recursive"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set, "desc", "rperm_go_w rperm_go_rwx rperm_ugo_w rperm_ugo_rx rperm_ugo_rwx");

    xset_set("rperm_go_w", "lbl", "go-w");
    xset_set("rperm_go_rwx", "lbl", "go-rwx");
    xset_set("rperm_ugo_w", "lbl", "ugo+w");
    xset_set("rperm_ugo_rx", "lbl", "ugo+rX");
    xset_set("rperm_ugo_rwx", "lbl", "ugo+rwX");

    set = xset_set("rperm_own", "lbl", _("_Owner"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "own_myuser own_myuser_users own_user1 own_user1_users own_user2 own_user2_users own_root "
                 "own_root_users own_root_myuser own_root_user1 own_root_user2 own_recurs");

    xset_set("own_myuser", "lbl", "myuser");
    xset_set("own_myuser_users", "lbl", "myuser:users");
    xset_set("own_user1", "lbl", "user1");
    xset_set("own_user1_users", "lbl", "user1:users");
    xset_set("own_user2", "lbl", "user2");
    xset_set("own_user2_users", "lbl", "user2:users");
    xset_set("own_root", "lbl", "root");
    xset_set("own_root_users", "lbl", "root:users");
    xset_set("own_root_myuser", "lbl", "root:myuser");
    xset_set("own_root_user1", "lbl", "root:user1");
    xset_set("own_root_user2", "lbl", "root:user2");

    set = xset_set("own_recurs", "lbl", _("_Recursive"));
    set->menu_style = XSET_MENU_SUBMENU;
    xset_set_set(set,
                 "desc",
                 "rown_myuser rown_myuser_users rown_user1 rown_user1_users rown_user2 rown_user2_users rown_root "
                 "rown_root_users rown_root_myuser rown_root_user1 rown_root_user2");

    xset_set("rown_myuser", "lbl", "myuser");
    xset_set("rown_myuser_users", "lbl", "myuser:users");
    xset_set("rown_user1", "lbl", "user1");
    xset_set("rown_user1_users", "lbl", "user1:users");
    xset_set("rown_user2", "lbl", "user2");
    xset_set("rown_user2_users", "lbl", "user2:users");
    xset_set("rown_root", "lbl", "root");
    xset_set("rown_root_users", "lbl", "root:users");
    xset_set("rown_root_myuser", "lbl", "root:myuser");
    xset_set("rown_root_user1", "lbl", "root:user1");
    xset_set("rown_root_user2", "lbl", "root:user2");

    // PANELS
    int p;
    for (p = 1; p < 5; p++)
    {
        set = xset_set_panel(p, "show_toolbox", "lbl", _("_Toolbar"));
        set->menu_style = XSET_MENU_CHECK;
        set->b = XSET_B_TRUE;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_show_toolbox");

        set = xset_set_panel(p, "show_devmon", "lbl", _("_Devices"));
        set->menu_style = XSET_MENU_CHECK;
        set->b = XSET_B_UNSET;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_show_devmon");

        set = xset_set_panel(p, "show_dirtree", "lbl", _("T_ree"));
        set->menu_style = XSET_MENU_CHECK;
        set->b = XSET_B_TRUE;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_show_dirtree");

        set = xset_set_panel(p, "show_book", "lbl", _("_Bookmarks"));
        set->menu_style = XSET_MENU_CHECK;
        set->b = XSET_B_UNSET;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_show_book");

        set = xset_set_panel(p, "show_sidebar", "lbl", _("_Side Toolbar"));
        set->menu_style = XSET_MENU_CHECK;
        set->b = XSET_B_UNSET;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_show_sidebar");

        set = xset_set_panel(p, "list_detailed", "lbl", _("_Detailed"));
        set->menu_style = XSET_MENU_RADIO;
        set->b = XSET_B_TRUE;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_list_detailed");

        set = xset_set_panel(p, "list_icons", "lbl", _("_Icons"));
        set->menu_style = XSET_MENU_RADIO;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_list_icons");

        set = xset_set_panel(p, "list_compact", "lbl", _("_Compact"));
        set->menu_style = XSET_MENU_RADIO;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_list_compact");

        set = xset_set_panel(p, "list_large", "lbl", _("_Large Icons"));
        set->menu_style = XSET_MENU_CHECK;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_list_large");

        set = xset_set_panel(p, "show_hidden", "lbl", _("_Hidden Files"));
        set->menu_style = XSET_MENU_CHECK;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_show_hidden");

        set = xset_set_panel(p, "font_file", "lbl", _("_Font"));
        set->menu_style = XSET_MENU_FONTDLG;
        set->title = g_strdup_printf(_("File List Font (Panel %d)"), p);
        xset_set_set(set, "desc", _("Example  1.1 M  file  -rwxr--r--  user:group  2011-01-01 01:11"));

        set = xset_set_panel(p, "font_dev", "lbl", _("_Font"));
        set->menu_style = XSET_MENU_FONTDLG;
        set->title = g_strdup_printf(_("Devices Font (Panel %d)"), p);
        xset_set_set(set, "desc", _("sr0 [no media] :EXAMPLE"));
        set->line = g_strdup("#devices-settings-font");

        set = xset_set_panel(p, "font_book", "lbl", _("_Font"));
        set->menu_style = XSET_MENU_FONTDLG;
        set->title = g_strdup_printf(_("Bookmarks Font (Panel %d)"), p);
        xset_set_set(set, "desc", _("Example Bookmark Name"));
        set->line = g_strdup("#gui-book-side");

        set = xset_set_panel(p, "font_path", "lbl", _("_Font"));
        set->menu_style = XSET_MENU_FONTDLG;
        set->title = g_strdup_printf(_("Path Bar Font (Panel %d)"), p);
        xset_set_set(set, "desc", _("$ cat /home/user/example"));
        set->line = g_strdup("#gui-pathbar-font");

        set = xset_set_panel(p, "font_tab", "lbl", _("_Font"));
        set->menu_style = XSET_MENU_FONTDLG;
        set->title = g_strdup_printf(_("Tab Font (Panel %d)"), p);
        xset_set_set(set, "desc", "/usr/bin");

        set = xset_set_panel(p, "icon_tab", "lbl", _("_Icon"));
        set->menu_style = XSET_MENU_ICON;
        xset_set_set(set, "icn", "gtk-directory");

        set = xset_set_panel(p, "font_status", "lbl", _("_Font"));
        set->menu_style = XSET_MENU_FONTDLG;
        set->title = g_strdup_printf(_("Status Bar Font (Panel %d)"), p);
        xset_set_set(set, "desc", _("12 G free / 200 G   52 items"));
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_font_status");

        set = xset_set_panel(p, "icon_status", "lbl", _("_Icon"));
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_icon_status");

        set = xset_set_panel(p, "detcol_name", "lbl", _("_Name"));
        set->menu_style = XSET_MENU_CHECK;
        set->b = XSET_B_TRUE;              // visible
        set->x = g_strdup_printf("%d", 0); // position

        set = xset_set_panel(p, "detcol_size", "lbl", _("_Size"));
        set->menu_style = XSET_MENU_CHECK;
        set->b = XSET_B_TRUE;
        set->x = g_strdup_printf("%d", 1);
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_detcol_size");

        set = xset_set_panel(p, "detcol_type", "lbl", _("_Type"));
        set->menu_style = XSET_MENU_CHECK;
        set->x = g_strdup_printf("%d", 2);
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_detcol_type");

        set = xset_set_panel(p, "detcol_perm", "lbl", _("_Permission"));
        set->menu_style = XSET_MENU_CHECK;
        set->x = g_strdup_printf("%d", 3);
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_detcol_perm");

        set = xset_set_panel(p, "detcol_owner", "lbl", _("_Owner"));
        set->menu_style = XSET_MENU_CHECK;
        set->x = g_strdup_printf("%d", 4);
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_detcol_owner");

        set = xset_set_panel(p, "detcol_date", "lbl", _("_Modified"));
        set->menu_style = XSET_MENU_CHECK;
        set->x = g_strdup_printf("%d", 5);
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_detcol_date");

        set = xset_get_panel(p, "sort_extra");
        set->b = XSET_B_TRUE;                         // sort_natural
        set->x = g_strdup_printf("%d", XSET_B_FALSE); // sort_case
        set->y = g_strdup("1");                       // PTK_LIST_SORT_DIR_FIRST from ptk-file-list.h
        set->z = g_strdup_printf("%d", XSET_B_TRUE);  // sort_hidden_first

        set = xset_set_panel(p, "book_fol", "lbl", _("Follow _Dir"));
        set->menu_style = XSET_MENU_CHECK;
        set->b = XSET_B_TRUE;
        if (p != 1)
            xset_set_set(set, "shared_key", "panel1_book_fol");
        set->line = g_strdup("#gui-book-side");
    }

    // speed
    set = xset_set("book_newtab", "lbl", _("_New Tab"));
    set->menu_style = XSET_MENU_CHECK;
    set->line = g_strdup("#gui-book-side");

    set = xset_set("book_single", "lbl", _("_Single Click"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#gui-book-side");

    set = xset_set("dev_newtab", "lbl", _("_New Tab"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#devices-settings-newtab");

    set = xset_set("dev_single", "lbl", _("_Single Click"));
    set->menu_style = XSET_MENU_CHECK;
    set->b = XSET_B_TRUE;
    set->line = g_strdup("#devices-settings-single");

    // mark all labels and icons as default
    GList* l;
    for (l = xsets; l; l = l->next)
    {
        if (((XSet*)l->data)->lock)
        {
            if (((XSet*)l->data)->in_terminal == XSET_B_TRUE)
                ((XSet*)l->data)->in_terminal = XSET_B_UNSET;
            if (((XSet*)l->data)->keep_terminal == XSET_B_TRUE)
                ((XSet*)l->data)->keep_terminal = XSET_B_UNSET;
        }
    }
}

void def_key(const char* name, int key, int keymod)
{
    XSet* set = xset_get(name);

    // key already set or unset?
    if (set->key != 0 || key == 0)
        return;

    // key combo already in use?
    GList* l;
    for (l = keysets; l; l = l->next)
    {
        if (((XSet*)l->data)->key == key && ((XSet*)l->data)->keymod == keymod)
            return;
    }
    set->key = key;
    set->keymod = keymod;
}

void xset_default_keys()
{
    GList* l;

    // read all currently set or unset keys
    keysets = NULL;
    for (l = xsets; l; l = l->next)
    {
        if (((XSet*)l->data)->key)
            keysets = g_list_prepend(keysets, (XSet*)l->data);
    }

    def_key("tab_prev", 65056, 5); // ctrl-tab  or use ctrl-pgdn??
    def_key("tab_next", 65289, 4);
    def_key("tab_close", 119, 4);
    def_key("tab_new", 116, 4);
    def_key("tab_1", 0x31, 8); // Alt-1
    def_key("tab_2", 0x32, 8);
    def_key("tab_3", 0x33, 8);
    def_key("tab_4", 0x34, 8);
    def_key("tab_5", 0x35, 8);
    def_key("tab_6", 0x36, 8);
    def_key("tab_7", 0x37, 8);
    def_key("tab_8", 0x38, 8);
    def_key("tab_9", 0x39, 8);  // Alt-9
    def_key("tab_10", 0x30, 8); // Alt-0
    def_key("edit_cut", 120, 4);
    def_key("edit_copy", 99, 4);
    def_key("edit_paste", 118, 4);
    def_key("edit_rename", 65471, 0);
    def_key("edit_delete", 65535, 0);
    def_key("copy_name", 67, 9);
    def_key("copy_path", 67, 5);
    def_key("paste_link", 86, 5);
    def_key("paste_as", 65, 5);
    def_key("select_all", 97, 4);
    def_key("main_terminal", 65473, 0); // F4
    def_key("go_default", 65307, 0);
    def_key("go_back", 65361, 8);
    def_key("go_forward", 65363, 8);
    def_key("go_up", 65362, 8);
    def_key("focus_path_bar", 0x6c, 4); // Ctrl-L
    def_key("view_refresh", 65474, 0);
    def_key("prop_info", 0xff0d, 8);
    def_key("prop_perm", 112, 4);
    def_key("panel1_show_hidden", 104, 4);
    def_key("book_new", 100, 4);
    def_key("new_folder", 102, 4);
    def_key("new_file", 70, 5);
    def_key("main_new_window", 110, 4);
    def_key("open_all", 65475, 0);   // F6
    def_key("main_full", 0xffc8, 0); // F11
    def_key("panel1_show", 0x31, 4);
    def_key("panel2_show", 0x32, 4);
    def_key("panel3_show", 0x33, 4);
    def_key("panel4_show", 0x34, 4);
    def_key("main_help", 0xffbe, 0); // F1
    def_key("main_exit", 0x71, 4);   // Ctrl-Q
    def_key("book_add", 0x64, 4);    // Ctrl-D

    if (keysets)
        g_list_free(keysets);
}

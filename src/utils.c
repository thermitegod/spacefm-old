/*
 *
 * License: See COPYING file
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#include <glib/gprintf.h>

#include "settings.h"
#include "extern.h"
#include "utils.h"


char* SHELL_SETTINGS = "set -o pipefail\nshopt -s failglob";
char* BASHPATH = "/bin/bash";
char* VERSION = "0.0.0";

char* HTMLDIR = "/usr/share";
char* DATADIR = "/usr/share";

char* SYSCONFDIR = "/etc";
char* PACKAGE_UI_DIR = "/usr/share/spacefm/ui";


void print_command(const char* cmd)
{
    g_printf("COMMAND=%s\n", cmd);
}

void print_task_command(char* ptask, const char* cmd)
{
    g_printf("\nTASK_COMMAND(%p)=%s\n", ptask, cmd);
}

void print_task_command_spawn(char* argv[], int pid)
{
    g_printf("SPAWN=");
    int i = 0;
    while (argv[i])
    {
        g_printf("%s%s", i == 0 ? "" : "  ", argv[i]);
        i++;
    }

    g_printf("\n");
    g_printf("    pid = %d\n", pid);
}

char* randhex8()
{
    char hex[9];
    uint n = rand();
    g_snprintf(hex, sizeof(hex), "%08x", n);
    return g_strdup(hex);
}

void check_for_errno()
{
    char* err_msg = NULL;

    if (errno)
    {
        err_msg = (char*)g_strerror(errno);
        if (err_msg)
            err_msg = g_strdup(err_msg);
    }
    if (!err_msg)
        err_msg = g_strdup_printf("write() failure");
}

char* replace_line_subs(const char* line)
{
    const char* perc[] = {"%f", "%F", "%n", "%N", "%d", "%D", "%v", "%l", "%m", "%y", "%b", "%t", "%p", "%a"};
    const char* var[] = {"\"${fm_file}\"",
                         "\"${fm_files[@]}\"",
                         "\"${fm_filename}\"",
                         "\"${fm_filenames[@]}\"",
                         "\"${fm_pwd}\"",
                         "\"${fm_pwd}\"",
                         "\"${fm_device}\"",
                         "\"${fm_device_label}\"",
                         "\"${fm_device_mount_point}\"",
                         "\"${fm_device_fstype}\"",
                         "\"${fm_bookmark}\"",
                         "\"${fm_task_pwd}\"",
                         "\"${fm_task_pid}\"",
                         "\"${fm_value}\""};

    char* s = g_strdup(line);
    int num = G_N_ELEMENTS(perc);
    int i;
    for (i = 0; i < num; i++)
    {
        if (strstr(line, perc[i]))
        {
            char* old_s = s;
            s = replace_string(old_s, perc[i], var[i], FALSE);
            g_free(old_s);
        }
    }
    return s;
}

gboolean have_x_access(const char* path)
{
    return (access(path, R_OK | X_OK) == 0);
}

gboolean have_rw_access(const char* path)
{
    if (!path)
        return FALSE;
    return (access(path, R_OK | W_OK) == 0);
}

gboolean dir_has_files(const char* path)
{
    gboolean ret = FALSE;

    if (!(path && g_file_test(path, G_FILE_TEST_IS_DIR)))
        return FALSE;

    GDir* dir = g_dir_open(path, 0, NULL);
    if (dir)
    {
        if (g_dir_read_name(dir))
            ret = TRUE;
        g_dir_close(dir);
    }
    return ret;
}

char* get_name_extension(char* full_name, gboolean is_dir, char** ext)
{
    char* str;
    char* full = g_strdup(full_name);
    // get last dot
    char* dot;
    if (is_dir || !(dot = strrchr(full, '.')) || dot == full)
    {
        // dir or no dots or one dot first
        *ext = NULL;
        return full;
    }
    dot[0] = '\0';
    char*  final_ext = dot + 1;
    // get previous dot
    dot = strrchr(full, '.');
    uint final_ext_len = strlen(final_ext);
    if (dot && !strcmp(dot + 1, "tar") && final_ext_len < 11 && final_ext_len)
    {
        // double extension
        final_ext[-1] = '.';
        *ext = g_strdup(dot + 1);
        dot[0] = '\0';
        str = g_strdup(full);
        g_free(full);
        return str;
    }
    // single extension, one or more dots
    if (final_ext_len < 11 && final_ext[0])
    {
        *ext = g_strdup(final_ext);
        str = g_strdup(full);
        g_free(full);
        return str;
    }
    else
    {
        // extension too long, probably part of name
        final_ext[-1] = '.';
        *ext = NULL;
        return full;
    }
}

void open_in_prog(const char* path)
{
    char* prog = g_find_program_in_path(g_get_prgname());
    if (!prog)
        prog = g_strdup(g_get_prgname());
    if (!prog)
        prog = g_strdup("spacefm");
    char* qpath = bash_quote(path);
    char* command = g_strdup_printf("%s %s", prog, qpath);
    print_command(command);
    g_spawn_command_line_async(command, NULL);
    g_free(qpath);
    g_free(command);
    g_free(prog);
}

char* replace_string(const char* orig, const char* str, const char* replace, gboolean quote)
{ // replace all occurrences of str in orig with replace, optionally quoting
    char* rep;
    char* result = NULL;
    char* old_result;
    char* s;

    if (!orig || !(s = strstr(orig, str)))
        return g_strdup(orig); // str not in orig

    if (!replace)
    {
        if (quote)
            rep = g_strdup("''");
        else
            rep = g_strdup("");
    }
    else if (quote)
        rep = g_strdup_printf("'%s'", replace);
    else
        rep = g_strdup(replace);

    const char* cur = orig;
    do
    {
        if (result)
        {
            old_result = result;
            result = g_strdup_printf("%s%s%s", old_result, g_strndup(cur, s - cur), rep);
            g_free(old_result);
        }
        else
            result = g_strdup_printf("%s%s", g_strndup(cur, s - cur), rep);
        cur = s + strlen(str);
        s = strstr(cur, str);
    } while (s);
    old_result = result;
    result = g_strdup_printf("%s%s", old_result, cur);
    g_free(old_result);
    g_free(rep);
    return result;
}

char* bash_quote(const char* str)
{
    if (!str)
        return g_strdup("\"\"");
    char* s1 = replace_string(str, "\"", "\\\"", FALSE);
    char* s2 = g_strdup_printf("\"%s\"", s1);
    g_free(s1);
    return s2;
}

char* plain_ascii_name(const char* orig_name)
{
    if (!orig_name)
        return g_strdup("");
    char* orig = g_strdup(orig_name);
    g_strcanon(orig, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", ' ');
    char* s = replace_string(orig, " ", "", FALSE);
    g_free(orig);
    return s;
}

char* clean_label(const char* menu_label, gboolean kill_special, gboolean escape)
{
    char* s1;
    char* s2;
    if (menu_label && strstr(menu_label, "\\_"))
    {
        s1 = replace_string(menu_label, "\\_", "@UNDERSCORE@", FALSE);
        s2 = replace_string(s1, "_", "", FALSE);
        g_free(s1);
        s1 = replace_string(s2, "@UNDERSCORE@", "_", FALSE);
        g_free(s2);
    }
    else
        s1 = replace_string(menu_label, "_", "", FALSE);
    if (kill_special)
    {
        s2 = replace_string(s1, "&", "", FALSE);
        g_free(s1);
        s1 = replace_string(s2, " ", "-", FALSE);
        g_free(s2);
    }
    else if (escape)
    {
        s2 = g_markup_escape_text(s1, -1);
        g_free(s1);
        s1 = s2;
    }
    return s1;
}

void string_copy_free(char** s, const char* src)
{
    char* discard = *s;
    *s = g_strdup(src);
    g_free(discard);
}

char* unescape(const char* t)
{
    if (!t)
        return NULL;

    char* s = g_strdup(t);

    int i = 0, j = 0;
    while (t[i])
    {
        switch (t[i])
        {
        case '\\':
            switch (t[++i])
            {
            case 'n':
                s[j] = '\n';
                break;
            case 't':
                s[j] = '\t';
                break;
            case '\\':
                s[j] = '\\';
                break;
            case '\"':
                s[j] = '\"';
                break;
            default:
                // copy
                s[j++] = '\\';
                s[j] = t[i];
            }
            break;
        default:
            s[j] = t[i];
        }
        ++i;
        ++j;
    }
    s[j] = t[i]; // null char
    return s;
}

char* get_valid_su() // may return NULL
{
    int i;
    char* use_su = NULL;

    use_su = g_strdup(xset_get_s("su_command"));

    if (use_su)
    {
        // is Prefs use_su in list of valid su commands?
        for (i = 0; i < G_N_ELEMENTS(su_commands); i++)
        {
            if (!strcmp(su_commands[i], use_su))
                break;
        }
        if (i == G_N_ELEMENTS(su_commands))
        {
            // not in list - invalid
            g_free(use_su);
            use_su = NULL;
        }
    }
    if (!use_su)
    {
        // discovery
        for (i = 0; i < G_N_ELEMENTS(su_commands); i++)
        {
            if ((use_su = g_find_program_in_path(su_commands[i])))
                break;
        }
        if (!use_su)
            use_su = g_strdup(su_commands[0]);
        xset_set("su_command", "s", use_su);
    }
    char* su_path = g_find_program_in_path(use_su);
    g_free(use_su);
    return su_path;
}

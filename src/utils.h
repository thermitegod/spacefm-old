/*
 *
 * License: See COPYING file
 *
 */

#ifndef _UTILS_H_
#define _UTILS_H_

//-u is too strict
//#define SHELL_SETTINGS = "set -o pipefail\nshopt -s failglob"
//#define BASHPATH = "/bin/bash"
//#define VERSION = "0.0.0"

extern char* SHELL_SETTINGS;
extern char* BASHPATH;
extern char* VERSION;

extern char* HTMLDIR;
extern char* DATADIR;

extern char* SYSCONFDIR;
extern char* PACKAGE_UI_DIR;

void print_command(const char* cmd);
void print_task_command(char* ptask, const char* cmd);
void print_task_command_spawn(char* argv[], int pid);

void check_for_errno();

char* randhex8();
gboolean have_rw_access(const char* path);
gboolean have_x_access(const char* path);
gboolean dir_has_files(const char* path);
char* replace_line_subs(const char* line);
char* get_name_extension(char* full_name, gboolean is_dir, char** ext);
void open_in_prog(const char* path);

char* replace_string(const char* orig, const char* str, const char* replace, gboolean quote);
char* bash_quote(const char* str);
char* plain_ascii_name(const char* orig_name);
char* clean_label(const char* menu_label, gboolean kill_special, gboolean convert_amp);
void string_copy_free(char** s, const char* src);
char* unescape(const char* t);

char* get_valid_su();

#endif

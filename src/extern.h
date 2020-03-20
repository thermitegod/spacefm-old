/*
 *
 * License: See COPYING file
 *
 */

#ifndef _EXTERN_H_
#define _EXTERN_H_

static const char* terminal_programs[] = // for pref-dialog.c
    {"roxterm",
     "terminal",
     "xfce4-terminal",
     "aterm",
     "Eterm",
     "mlterm",
     "mrxvt",
     "rxvt",
     "sakura",
     "terminator",
     "urxvt",
     "xterm",
     "x-terminal-emulator",
     "lilyterm",
     "qterminal"};

// clang-format off
static const char* su_commands[] = // order and contents must match prefdlg.ui
    {"/bin/su",
     "/usr/bin/sudo",
     "/usr/bin/doas",
     "/usr/bin/su-to-root"};
// clang-format on

static const char* gsu_commands[] = // order and contents must match prefdlg.ui
    {"/usr/bin/kdesu",
     "/usr/bin/ktsuss",
     "/usr/bin/lxqt-sudo",
     "/usr/bin/su-to-root",
     "/bin/su",
     "/usr/bin/sudo",
     "/usr/bin/doas"};

#endif

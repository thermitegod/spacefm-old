/*
 *  C Interface: vfs-monitor
 *
 * Description: File alteration monitor
 *
 *
 * Author: Hong Jen Yee (PCMan) <pcman.tw (AT) gmail.com>, (C) 2006
 *
 * Copyright: See COPYING file that comes with this distribution
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _VFS_FILE_MONITOR_H_
#define _VFS_FILE_MONITOR_H_

#include <glib.h>

#include <unistd.h>
#include <sys/inotify.h>

G_BEGIN_DECLS

typedef enum
{
    VFS_FILE_MONITOR_CREATE,
    VFS_FILE_MONITOR_DELETE,
    VFS_FILE_MONITOR_CHANGE
} VFSFileMonitorEvent;

typedef struct _VFSFileMonitor VFSFileMonitor;

struct _VFSFileMonitor
{
    char* path;
    /*<private>*/
    int n_ref;
    int wd;
    GArray* callbacks;
};

/* Callback function which will be called when monitored events happen
 *  NOTE: GDK_THREADS_ENTER and GDK_THREADS_LEAVE might be needed
 *  if gtk+ APIs are called in this callback, since the callback is called from
 *  IO channel handler.
 */
typedef void (*VFSFileMonitorCallback)(VFSFileMonitor* fm, VFSFileMonitorEvent event, const char* file_name,
                                       gpointer user_data);

/*
 * Init monitor:
 * connect to inotify
 */
gboolean vfs_file_monitor_init();

/*
 * Monitor changes of a file or directory.
 *
 * Parameters:
 * path: the file/dir to be monitored
 * cb: callback function to be called when file event happens.
 * user_data: user data to be passed to callback function.
 */
VFSFileMonitor* vfs_file_monitor_add(char* path, gboolean is_dir, VFSFileMonitorCallback cb, gpointer user_data);

/*
 * Monitor changes of a file.
 *
 * Parameters:
 * path: the file/dir to be monitored
 * cb: callback function to be called when file event happens.
 * user_data: user data to be passed to callback function.
 */
#define vfs_file_monitor_add_file(path, cb, user_data) vfs_file_monitor_add(path, FALSE, cb, user_data)

/*
 * Monitor changes of a directory.
 *
 * Parameters:
 * path: the file/dir to be monitored
 * cb: callback function to be called when file event happens.
 * user_data: user data to be passed to callback function.
 */
#define vfs_file_monitor_add_dir(path, cb, user_data) vfs_file_monitor_add(path, TRUE, cb, user_data)

/*
 * Remove previously installed monitor.
 */
void vfs_file_monitor_remove(VFSFileMonitor* fm, VFSFileMonitorCallback cb, gpointer user_data);

/*
 * Clearn up and shutdown file alteration monitor.
 */
void vfs_file_monitor_clean();

G_END_DECLS

#endif

/*
*  C Implementation: vfs-monitor
*
* Description: File alteration monitor
*
*
* Copyright (C) 2014 IgnorantGuru <ignorantguru@gmx.com>
* Original Author: Hong Jen Yee (PCMan) <pcman.tw (AT) gmail.com>, (C) 2006
*
* Copyright: See COPYING file that comes with this distribution
*
* Most of the inotify parts are taken from "menu-monitor-inotify.c" of
* gnome-menus, which is licensed under GNU Lesser General Public License.
*
* Copyright (C) 2005 Red Hat, Inc.
* Copyright (C) 2006 Mark McLoughlin
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vfs-file-monitor.h"
#include "vfs-file-info.h"
#include <sys/types.h>  /* for stat */
#include <sys/stat.h>
#include <errno.h>

#include <stdlib.h>
#include <string.h>

#include <linux/limits.h> //PATH_MAX

typedef struct
{
    VFSFileMonitorCallback callback;
    gpointer user_data;
}
VFSFileMonitorCallbackEntry;

static GHashTable* monitor_hash = NULL;
static GIOChannel* inotify_io_channel = NULL;
static guint inotify_io_watch = 0;
static int inotify_fd = -1;

/* event handler of all inotify events */
static gboolean on_inotify_event( GIOChannel *channel,
                              GIOCondition cond,
                              gpointer user_data );

static gboolean connect_to_inotify()
{
    inotify_fd = inotify_init ();
    if ( inotify_fd < 0 )
    {
        inotify_io_channel = NULL;
        g_warning( "failed to initialize inotify." );
        return FALSE;
    }
    inotify_io_channel = g_io_channel_unix_new( inotify_fd );

    g_io_channel_set_encoding( inotify_io_channel, NULL, NULL );
    g_io_channel_set_buffered( inotify_io_channel, FALSE );
    g_io_channel_set_flags( inotify_io_channel, G_IO_FLAG_NONBLOCK, NULL );

    inotify_io_watch = g_io_add_watch( inotify_io_channel,
                                   G_IO_IN | G_IO_PRI | G_IO_HUP|G_IO_ERR,
                                   on_inotify_event,
                                   NULL );
    return TRUE;
}

static void disconnect_from_inotify()
{
    if ( inotify_io_channel )
    {
        g_io_channel_unref( inotify_io_channel );
        inotify_io_channel = NULL;
        g_source_remove( inotify_io_watch );

        close( inotify_fd );
        inotify_fd = -1;
    }
}

/* final cleanup */
void vfs_file_monitor_clean()
{
    disconnect_from_inotify();
    if ( monitor_hash )
    {
        g_hash_table_destroy( monitor_hash );
        monitor_hash = NULL;
    }
}

/*
* Init monitor:
* connect to inotify
*/
gboolean vfs_file_monitor_init()
{
    monitor_hash = g_hash_table_new( g_str_hash, g_str_equal );
    if ( ! connect_to_inotify() )
        return FALSE;
    return TRUE;
}

VFSFileMonitor* vfs_file_monitor_add( char* path,
                                      gboolean is_dir,
                                      VFSFileMonitorCallback cb,
                                      gpointer user_data )
{
    VFSFileMonitor * monitor;
    VFSFileMonitorCallbackEntry cb_ent;
    struct stat file_stat;   // skip stat64
    char resolved_path[PATH_MAX];
    char* real_path;

//printf( "vfs_file_monitor_add  %s\n", path );

    if ( ! monitor_hash )
        return NULL;

    // Since inotify does not follow symlinks, need to get real path
    if ( strlen( path ) > PATH_MAX - 1 )
    {
        g_warning ( "PATH_MAX exceeded on %s", path );
        real_path = path;  //fallback
    }
    else if ( realpath( path, resolved_path ) == NULL )
    {
        g_warning ( "realpath failed on %s", path );
        real_path = path;  //fallback
    }
    else
        real_path = resolved_path;

    monitor = ( VFSFileMonitor* ) g_hash_table_lookup ( monitor_hash, real_path );
    if ( ! monitor )
    {
        monitor = g_slice_new0( VFSFileMonitor );
        monitor->path = g_strdup( real_path );

        monitor->callbacks = g_array_new ( FALSE, FALSE, sizeof( VFSFileMonitorCallbackEntry ) );
        g_hash_table_insert ( monitor_hash,
                              monitor->path,
                              monitor );

        monitor->wd = inotify_add_watch ( inotify_fd, real_path,
                                          IN_MODIFY | IN_CREATE | IN_DELETE |
                                          IN_DELETE_SELF | IN_MOVE |
                                          IN_MOVE_SELF | IN_UNMOUNT | IN_ATTRIB );
        if ( monitor->wd < 0 )
        {
            const char* msg;
            switch ( errno )
            {
            case EACCES:
                msg = "EACCES Read access to the given directory is not permitted.";
                break;
            case EBADF:
                msg = "EBADF The given file descriptor is not valid.";
                break;
            case EFAULT:
                msg = "EFAULT Pathname points outside of the process's accessible address space.";
                break;
            case EINVAL:
                msg = "EINVAL The given event mask contains no valid events; or fd is not an inotify file descriptor.";
                break;
            case ENOENT:
                msg = "ENOENT A directory component in pathname does not exist or is a dangling symbolic link.";
                break;
            case ENOMEM:
                msg = "ENOMEM Insufficient kernel memory was available.";
                break;
            case ENOSPC:
                msg = "ENOSPC The user limit on the total number of inotify watches (cat /proc/sys/fs/inotify/max_user_watches) was reached or the kernel failed to allocate a needed resource.";
                break;
            default:
                msg = "??? Unknown error.";
                break;
            }
            g_warning ( "Failed to add watch on '%s' ('%s'): inotify_add_watch errno %d %s",
                        real_path, path, errno, msg );
            return NULL;
        }
//printf("vfs_file_monitor_add  %s (%s) %d\n", real_path, path, monitor->wd );
    }

    if( G_LIKELY(monitor) )
    {
        /* g_debug( "monitor installed: %s, %p", path, monitor ); */
        if ( cb )
        { /* Install a callback */
            cb_ent.callback = cb;
            cb_ent.user_data = user_data;
            monitor->callbacks = g_array_append_val( monitor->callbacks, cb_ent );
        }
        g_atomic_int_inc( &monitor->n_ref );
    }
    return monitor;
}

void vfs_file_monitor_remove( VFSFileMonitor * fm,
                              VFSFileMonitorCallback cb,
                              gpointer user_data )
{
    int i;
    VFSFileMonitorCallbackEntry* callbacks;

//printf( "vfs_file_monitor_remove\n" );
    if ( cb && fm && fm->callbacks )
    {
        callbacks = ( VFSFileMonitorCallbackEntry* ) fm->callbacks->data;
        for ( i = 0; i < fm->callbacks->len; ++i )
        {
            if ( callbacks[ i ].callback == cb && callbacks[ i ].user_data == user_data )
            {
                fm->callbacks = g_array_remove_index_fast ( fm->callbacks, i );
                break;
            }
        }
    }

    if ( fm && g_atomic_int_dec_and_test( &fm->n_ref ) )  //MOD added "fm &&"
    {
//printf( "vfs_file_monitor_remove  %d\n", fm->wd );
        inotify_rm_watch ( inotify_fd, fm->wd );

        g_hash_table_remove( monitor_hash, fm->path );
        g_free( fm->path );
        g_array_free( fm->callbacks, TRUE );
        g_slice_free( VFSFileMonitor, fm );
    }
//printf( "vfs_file_monitor_remove   DONE\n" );
}

static void reconnect_inotify( gpointer key,
                           gpointer value,
                           gpointer user_data )
{
    struct stat file_stat;   // skip stat64
    VFSFileMonitor* monitor = ( VFSFileMonitor* ) value;
    const char* path = ( const char* ) key;
    if ( lstat( path, &file_stat ) != -1 )
    {
        monitor->wd = inotify_add_watch ( inotify_fd, path,
                                          IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVE );
        if ( monitor->wd < 0 )
        {
            /*
                  * FIXME: add monitor to an ancestor which does actually exist,
                  *        or do the equivalent of inotify-missing.c by maintaining
                  *        a list of monitors on non-existent files/directories
                  *        which you retry in a timeout.
            */
            g_warning ( "Failed to add monitor on '%s': %s",
                        path,
                        g_strerror ( errno ) );
            return ;
        }
    }
}

static gboolean find_monitor( gpointer key,
                              gpointer value,
                              gpointer user_data )
{
    int wd = GPOINTER_TO_INT( user_data );
    VFSFileMonitor* monitor = ( VFSFileMonitor* ) value;
    return ( monitor->wd == wd );
}

static VFSFileMonitorEvent translate_inotify_event( int inotify_mask )
{
    if ( inotify_mask & ( IN_CREATE | IN_MOVED_TO ) )
        return VFS_FILE_MONITOR_CREATE;
    else if ( inotify_mask & ( IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_UNMOUNT ) )
        return VFS_FILE_MONITOR_DELETE;
    else  if ( inotify_mask & ( IN_MODIFY | IN_ATTRIB ) )
        return VFS_FILE_MONITOR_CHANGE;
    else
    {
        //IN_IGNORED not handled
        //g_warning( "translate_inotify_event mask not handled %d", inotify_mask );
        return VFS_FILE_MONITOR_CHANGE;
    }
}

static void dispatch_event( VFSFileMonitor * monitor,
                            VFSFileMonitorEvent evt,
                            const char * file_name )
{
    VFSFileMonitorCallbackEntry * cb;
    VFSFileMonitorCallback func;
    int i;
    /* Call the callback functions */
    if ( monitor->callbacks && monitor->callbacks->len )
    {
        cb = ( VFSFileMonitorCallbackEntry* ) monitor->callbacks->data;
        for ( i = 0; i < monitor->callbacks->len; ++i )
        {
            func = cb[ i ].callback;
            func( monitor, evt, file_name, cb[ i ].user_data );
        }
    }
}

/* event handler of all inotify events */
static gboolean on_inotify_event( GIOChannel * channel,
                              GIOCondition cond,
                              gpointer user_data )
{
#define BUF_LEN (1024 * (sizeof (struct inotify_event) + 16))
    char buf[ BUF_LEN ];
    int i, len;

    VFSFileMonitor* monitor = NULL;

    if ( cond & (G_IO_HUP | G_IO_ERR) )
    {
        disconnect_from_inotify();
        if ( g_hash_table_size ( monitor_hash ) > 0 )
        {
            /*
              Disconnected from inotify server, but there are still monitors.
              This may be caused by crash of inotify server.
              So we have to reconnect to inotify server.
            */
            if ( connect_to_inotify() )
                g_hash_table_foreach( monitor_hash, ( GHFunc ) reconnect_inotify,
                                                                        NULL );
        }
        return TRUE; /* don't need to remove the event source since
                                    it has been removed by disconnect_from_inotify(). */
    }

    while ( ( len = read ( inotify_fd, buf, BUF_LEN ) ) < 0
            && errno == EINTR );
    if ( len < 0 )
    {
        g_warning ( "Error reading inotify event: %s",
                    g_strerror ( errno ) );
        /* goto error_cancel; */
        return FALSE;
    }

    if ( len == 0 )
    {
        /*
        * FIXME: handle this better?
        */
        g_warning ( "Error reading inotify event: supplied buffer was too small" );
        /* goto error_cancel; */
        return FALSE;
    }
    i = 0;
    while ( i < len )
    {
        struct inotify_event * ievent = ( struct inotify_event * ) & buf [ i ];
        /* FIXME: 2 different paths can have the same wd because of link
         *        This was fixed in spacefm 0.8.7 ?? */
        monitor = ( VFSFileMonitor* ) g_hash_table_find(
                      monitor_hash,
                      find_monitor,
                      GINT_TO_POINTER( ievent->wd ) );
        if( G_LIKELY(monitor) )
        {
            const char* file_name;
            file_name = ievent->len > 0 ? ievent->name : monitor->path;
/*
//MOD for debug output only
char* desc;
if ( ievent->mask & ( IN_CREATE | IN_MOVED_TO ) )
    desc = "CREATE";
else if ( ievent->mask & ( IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_UNMOUNT ) )
    desc = "DELETE";
else if ( ievent->mask & ( IN_MODIFY | IN_ATTRIB ) )
    desc = "CHANGE";
if ( !strcmp( monitor->path, "/tmp" ) && g_str_has_prefix( file_name, "vte" ) )
{ } // due to current vte scroll problems creating and deleting massive numbers of
// /tmp/vte8CBO7V types of files, ignore these (creates feedback loop when
// spacefm is run in terminal because each printf triggers a scroll,
// which triggers another printf below, which triggers another file change)
// https://bugs.launchpad.net/ubuntu/+source/vte/+bug/778872
else
    printf("inotify-event %s: %s///%s\n", desc, monitor->path, file_name);
//g_debug("inotify (%d) :%s", ievent->mask, file_name);
*/
            dispatch_event( monitor,
                            translate_inotify_event( ievent->mask ),
                            file_name );
        }
        i += sizeof ( struct inotify_event ) + ievent->len;
    }
    return TRUE;
}


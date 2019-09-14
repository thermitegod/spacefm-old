/*
*  C Implementation: vfs-dir
*
* Description: Object used to present a directory
*
*
*
* Copyright: See COPYING file that comes with this distribution
*
*/

#include "vfs-dir.h"
#include "vfs-thumbnail-loader.h"

#include <glib/gi18n.h>
#include <string.h>

#include <fcntl.h>  /* for open() */

#if defined (__GLIBC__)
#include <malloc.h> /* for malloc_trim */
#endif

#include <unistd.h> /* for read */
#include "vfs-volume.h"

#include <glib/gprintf.h>


static void vfs_dir_class_init( VFSDirClass* klass );
static void vfs_dir_init( VFSDir* dir );
static void vfs_dir_finalize( GObject *obj );
static void vfs_dir_set_property ( GObject *obj,
                                   guint prop_id,
                                   const GValue *value,
                                   GParamSpec *pspec );
static void vfs_dir_get_property ( GObject *obj,
                                   guint prop_id,
                                   GValue *value,
                                   GParamSpec *pspec );

/* constructor is private */
static VFSDir* vfs_dir_new( const char* path );

static void vfs_dir_load( VFSDir* dir );
static gpointer vfs_dir_load_thread( VFSAsyncTask* task, VFSDir* dir );

static void vfs_dir_monitor_callback( VFSFileMonitor* fm,
                                      VFSFileMonitorEvent event,
                                      const char* file_name,
                                      gpointer user_data );

#if 0
static gpointer load_thumbnail_thread( gpointer user_data );
#endif

static void on_mime_type_reload( gpointer user_data );


static void update_changed_files( gpointer key, gpointer data,
                                  gpointer user_data );
static gboolean notify_file_change( gpointer user_data );
static gboolean update_file_info( VFSDir* dir, VFSFileInfo* file );

#if 0
static gboolean is_dir_desktop( const char* path );
#endif

static void on_list_task_finished( VFSAsyncTask* task, gboolean is_cancelled, VFSDir* dir );

enum {
    FILE_CREATED_SIGNAL = 0,
    FILE_DELETED_SIGNAL,
    FILE_CHANGED_SIGNAL,
    THUMBNAIL_LOADED_SIGNAL,
    FILE_LISTED_SIGNAL,
    N_SIGNALS
};

static guint signals[ N_SIGNALS ] = { 0 };
static GObjectClass *parent_class = NULL;

static GHashTable* dir_hash = NULL;
static GList* mime_cb = NULL;
static guint change_notify_timeout = 0;
static guint theme_change_notify = 0;

static gboolean is_desktop_set = FALSE;

GType vfs_dir_get_type()
{
    static GType type = G_TYPE_INVALID;
    if ( G_UNLIKELY ( type == G_TYPE_INVALID ) )
    {
        static const GTypeInfo info =
            {
                sizeof ( VFSDirClass ),
                NULL,
                NULL,
                ( GClassInitFunc ) vfs_dir_class_init,
                NULL,
                NULL,
                sizeof ( VFSDir ),
                0,
                ( GInstanceInitFunc ) vfs_dir_init,
                NULL,
            };
        type = g_type_register_static ( G_TYPE_OBJECT, "VFSDir", &info, 0 );
    }
    return type;
}

void vfs_dir_class_init( VFSDirClass* klass )
{
    GObjectClass * object_class;

    object_class = ( GObjectClass * ) klass;
    parent_class = g_type_class_peek_parent ( klass );

    object_class->set_property = vfs_dir_set_property;
    object_class->get_property = vfs_dir_get_property;
    object_class->finalize = vfs_dir_finalize;

    /* signals */
//    klass->file_created = on_vfs_dir_file_created;
//    klass->file_deleted = on_vfs_dir_file_deleted;
//    klass->file_changed = on_vfs_dir_file_changed;

    /*
    * file-created is emitted when there is a new file created in the dir.
    * The param is VFSFileInfo of the newly created file.
    */
    signals[ FILE_CREATED_SIGNAL ] =
        g_signal_new ( "file-created",
                       G_TYPE_FROM_CLASS ( klass ),
                       G_SIGNAL_RUN_FIRST,
                       G_STRUCT_OFFSET ( VFSDirClass, file_created ),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__POINTER,
                       G_TYPE_NONE, 1, G_TYPE_POINTER );

    /*
    * file-deleted is emitted when there is a file deleted in the dir.
    * The param is VFSFileInfo of the newly created file.
    */
    signals[ FILE_DELETED_SIGNAL ] =
        g_signal_new ( "file-deleted",
                       G_TYPE_FROM_CLASS ( klass ),
                       G_SIGNAL_RUN_FIRST,
                       G_STRUCT_OFFSET ( VFSDirClass, file_deleted ),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__POINTER,
                       G_TYPE_NONE, 1, G_TYPE_POINTER );

    /*
    * file-changed is emitted when there is a file changed in the dir.
    * The param is VFSFileInfo of the newly created file.
    */
    signals[ FILE_CHANGED_SIGNAL ] =
        g_signal_new ( "file-changed",
                       G_TYPE_FROM_CLASS ( klass ),
                       G_SIGNAL_RUN_FIRST,
                       G_STRUCT_OFFSET ( VFSDirClass, file_changed ),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__POINTER,
                       G_TYPE_NONE, 1, G_TYPE_POINTER );

    signals[ THUMBNAIL_LOADED_SIGNAL ] =
        g_signal_new ( "thumbnail-loaded",
                       G_TYPE_FROM_CLASS ( klass ),
                       G_SIGNAL_RUN_FIRST,
                       G_STRUCT_OFFSET ( VFSDirClass, thumbnail_loaded ),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__POINTER,
                       G_TYPE_NONE, 1, G_TYPE_POINTER );

    signals[ FILE_LISTED_SIGNAL ] =
        g_signal_new ( "file-listed",
                       G_TYPE_FROM_CLASS ( klass ),
                       G_SIGNAL_RUN_FIRST,
                       G_STRUCT_OFFSET ( VFSDirClass, file_listed ),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__BOOLEAN,
                       G_TYPE_NONE, 1, G_TYPE_BOOLEAN );

    /* FIXME: Is there better way to do this? */
    if( G_UNLIKELY( ! is_desktop_set ) )
        vfs_get_desktop_dir();
}

/* constructor */
void vfs_dir_init( VFSDir* dir )
{
    g_mutex_init(&dir->mutex);
}

/* destructor */

void vfs_dir_finalize( GObject *obj )
{
    VFSDir * dir = VFS_DIR( obj );
//g_printf("vfs_dir_finalize  %s\n", dir->path );
    do{}
    while( g_source_remove_by_user_data( dir ) );

    if( G_UNLIKELY( dir->task ) )
    {
        g_signal_handlers_disconnect_by_func( dir->task, on_list_task_finished, dir );
        /* FIXME: should we generate a "file-list" signal to indicate the dir loading was cancelled? */
//g_printf("spacefm: vfs_dir_finalize -> vfs_async_task_cancel\n");
        vfs_async_task_cancel( dir->task );
        g_object_unref( dir->task );
        dir->task = NULL;
    }
    if ( dir->monitor )
    {
        vfs_file_monitor_remove( dir->monitor,
                                 vfs_dir_monitor_callback,
                                 dir );
    }
    if ( dir->path )
    {
        if( G_LIKELY( dir_hash ) )
        {
            g_hash_table_remove( dir_hash, dir->path );

            /* There is no VFSDir instance */
            if ( 0 == g_hash_table_size( dir_hash ) )
            {
                g_hash_table_destroy( dir_hash );
                dir_hash = NULL;

                vfs_mime_type_remove_reload_cb( mime_cb );
                mime_cb = NULL;

                if( change_notify_timeout )
                {
                    g_source_remove( change_notify_timeout );
                    change_notify_timeout = 0;
                }

                g_signal_handler_disconnect( gtk_icon_theme_get_default(),
                                                                theme_change_notify );
                theme_change_notify = 0;
            }
        }
        g_free( dir->path );
        g_free( dir->disp_path );
        dir->path = dir->disp_path = NULL;
    }
    /* g_debug( "dir->thumbnail_loader: %p", dir->thumbnail_loader ); */
    if( G_UNLIKELY( dir->thumbnail_loader ) )
    {
        /* g_debug( "FREE THUMBNAIL LOADER IN VFSDIR" ); */
        vfs_thumbnail_loader_free( dir->thumbnail_loader );
        dir->thumbnail_loader = NULL;
    }

    if ( dir->file_list )
    {
        g_list_foreach( dir->file_list, ( GFunc ) vfs_file_info_unref, NULL );
        g_list_free( dir->file_list );
        dir->file_list = NULL;
        dir->n_files = 0;
    }

    if( dir->changed_files )
    {
        g_slist_foreach( dir->changed_files, (GFunc)vfs_file_info_unref, NULL );
        g_slist_free( dir->changed_files );
        dir->changed_files = NULL;
    }

    if( dir->created_files )
    {
        g_slist_foreach( dir->created_files, (GFunc)g_free, NULL );
        g_slist_free( dir->created_files );
        dir->created_files = NULL;
    }

    g_mutex_clear(&dir->mutex);
    G_OBJECT_CLASS( parent_class ) ->finalize( obj );
}

void vfs_dir_get_property ( GObject *obj,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec )
{}

void vfs_dir_set_property ( GObject *obj,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec )
{}

static GList* vfs_dir_find_file( VFSDir* dir, const char* file_name, VFSFileInfo* file )
{
    GList * l;
    VFSFileInfo* file2;
    for ( l = dir->file_list; l; l = l->next )
    {
        file2 = ( VFSFileInfo* ) l->data;
        if( G_UNLIKELY( file == file2 ) )
            return l;
        if ( file2->name && 0 == strcmp( file2->name, file_name ) )
        {
            return l;
        }
    }
    return NULL;
}

/* signal handlers */
void vfs_dir_emit_file_created( VFSDir* dir, const char* file_name, gboolean force )
{
    // Ignore avoid_changes for creation of files
    //if ( !force && dir->avoid_changes )
    //    return;

    if ( G_UNLIKELY( 0 == strcmp(file_name, dir->path) ) )
    {
        // Special Case: The directory itself was created?
        return;
    }

    dir->created_files = g_slist_append( dir->created_files, g_strdup( file_name ) );
    if ( 0 == change_notify_timeout )
    {
        change_notify_timeout = g_timeout_add_full( G_PRIORITY_LOW,
                                                    200,
                                                    notify_file_change,
                                                    NULL, NULL );
    }
}

void vfs_dir_emit_file_deleted( VFSDir* dir, const char* file_name, VFSFileInfo* file )
{
    GList* l;
    VFSFileInfo* file_found;

    if( G_UNLIKELY( 0 == strcmp(file_name, dir->path) ) )
    {
        /* Special Case: The directory itself was deleted... */
        file = NULL;
        /* clear the whole list */
        g_mutex_lock( dir->mutex );
        g_list_foreach( dir->file_list, (GFunc)vfs_file_info_unref, NULL );
        g_list_free( dir->file_list );
        dir->file_list = NULL;
        g_mutex_unlock( dir->mutex );

        g_signal_emit( dir, signals[ FILE_DELETED_SIGNAL ], 0, file );
        return;
    }

    l = vfs_dir_find_file( dir, file_name, file );
    if ( G_LIKELY( l ) )
    {
        file_found = vfs_file_info_ref( ( VFSFileInfo* ) l->data );
        if( !g_slist_find( dir->changed_files, file_found ) )
        {
            dir->changed_files = g_slist_prepend( dir->changed_files, file_found );
            if ( 0 == change_notify_timeout )
            {
                change_notify_timeout = g_timeout_add_full( G_PRIORITY_LOW,
                                                            200,
                                                            notify_file_change,
                                                            NULL, NULL );
            }
        }
        else
            vfs_file_info_unref( file_found );
    }
}

void vfs_dir_emit_file_changed( VFSDir* dir, const char* file_name,
                                        VFSFileInfo* file, gboolean force )
{
    GList* l;
//g_printf("vfs_dir_emit_file_changed dir=%s file_name=%s avoid=%s\n", dir->path, file_name, dir->avoid_changes ? "TRUE" : "FALSE" );

    if ( !force && dir->avoid_changes )
        return;

    if ( G_UNLIKELY( 0 == strcmp(file_name, dir->path) ) )
    {
        // Special Case: The directory itself was changed
        g_signal_emit( dir, signals[ FILE_CHANGED_SIGNAL ], 0, NULL );
        return;
    }

    g_mutex_lock( dir->mutex );

    l = vfs_dir_find_file( dir, file_name, file );
    if ( G_LIKELY( l ) )
    {
        file = vfs_file_info_ref( ( VFSFileInfo* ) l->data );
        if( !g_slist_find( dir->changed_files, file ) )
        {
            if ( force )
            {
                dir->changed_files = g_slist_prepend( dir->changed_files, file );
                if ( 0 == change_notify_timeout )
                {
                    change_notify_timeout = g_timeout_add_full( G_PRIORITY_LOW,
                                                                100,
                                                                notify_file_change,
                                                                NULL, NULL );
                }
            }
            else if( G_LIKELY( update_file_info( dir, file ) ) ) // update file info the first time
            {
                dir->changed_files = g_slist_prepend( dir->changed_files, file );
                if ( 0 == change_notify_timeout )
                {
                    change_notify_timeout = g_timeout_add_full( G_PRIORITY_LOW,
                                                                500,
                                                                notify_file_change,
                                                                NULL, NULL );
                }
                g_signal_emit( dir, signals[ FILE_CHANGED_SIGNAL ], 0, file );
            }
        }
        else
            vfs_file_info_unref( file );
    }

    g_mutex_unlock( dir->mutex );
}

void vfs_dir_emit_thumbnail_loaded( VFSDir* dir, VFSFileInfo* file )
{
    GList* l;
    g_mutex_lock( dir->mutex );
    l = vfs_dir_find_file( dir, file->name, file );
    if( l )
    {
        g_assert( file == (VFSFileInfo*)l->data );
        file = vfs_file_info_ref( (VFSFileInfo*)l->data );
    }
    else
        file = NULL;
    g_mutex_unlock( dir->mutex );

    if ( G_LIKELY( file ) )
    {
        g_signal_emit( dir, signals[ THUMBNAIL_LOADED_SIGNAL ], 0, file );
        vfs_file_info_unref( file );
    }
}

/* methods */

VFSDir* vfs_dir_new( const char* path )
{
    VFSDir * dir;
    dir = ( VFSDir* ) g_object_new( VFS_TYPE_DIR, NULL );
    dir->path = g_strdup( path );

    dir->avoid_changes = vfs_volume_dir_avoid_changes( path );
//g_printf("vfs_dir_new %s  avoid_changes=%s\n", dir->path, dir->avoid_changes ? "TRUE" : "FALSE" );
    return dir;
}

void on_list_task_finished( VFSAsyncTask* task, gboolean is_cancelled, VFSDir* dir )
{
    g_object_unref( dir->task );
    dir->task = NULL;
    g_signal_emit( dir, signals[FILE_LISTED_SIGNAL], 0, is_cancelled );
    dir->file_listed = 1;
    dir->load_complete = 1;
}

void vfs_dir_load( VFSDir* dir )
{
    if ( G_LIKELY(dir->path) )
    {
        dir->disp_path = g_filename_display_name( dir->path );
        dir->flags = 0;

        /* FIXME: We should check access here! */

        if( G_UNLIKELY( strcmp( dir->path, vfs_get_desktop_dir() ) == 0 ) )
            dir->is_desktop = TRUE;
        else if( G_UNLIKELY( strcmp( dir->path, g_get_home_dir() ) == 0 ) )
            dir->is_home = TRUE;

        dir->task = vfs_async_task_new( (VFSAsyncFunc)vfs_dir_load_thread, dir );
        g_signal_connect( dir->task, "finish", G_CALLBACK(on_list_task_finished), dir );
        vfs_async_task_execute( dir->task );
    }
}

#if 0
gboolean is_dir_desktop( const char* path )
{
    return (desktop_dir && 0 == strcmp(path, desktop_dir));
}
#endif

gpointer vfs_dir_load_thread(  VFSAsyncTask* task, VFSDir* dir )
{
    const gchar * file_name;
    char* full_path;
    GDir* dir_content;
    VFSFileInfo* file;

    dir->file_listed = 0;
    dir->load_complete = 0;
    if ( dir->path )
    {
        /* Install file alteration monitor */
        dir->monitor = vfs_file_monitor_add_dir( dir->path,
                                             vfs_dir_monitor_callback,
                                             dir );

        dir_content = g_dir_open( dir->path, 0, NULL );

        if ( dir_content )
        {
            while ( ! vfs_async_task_is_cancelled( dir->task )
                        && ( file_name = g_dir_read_name( dir_content ) ) )
            {
                full_path = g_build_filename( dir->path, file_name, NULL );
                if ( !full_path )
                    continue;

                /* FIXME: Is locking GDK needed here? */
                /* GDK_THREADS_ENTER(); */
                file = vfs_file_info_new();
                if ( G_LIKELY( vfs_file_info_get( file, full_path, file_name ) ) )
                {
                    g_mutex_lock( dir->mutex );

                    /* Special processing for desktop folder */
                    vfs_file_info_load_special_info( file, full_path );
                    dir->file_list = g_list_prepend( dir->file_list, file );
                    g_mutex_unlock( dir->mutex );
                    ++dir->n_files;
                }
                else
                {
                    vfs_file_info_unref( file );
                }
                /* GDK_THREADS_LEAVE(); */
                g_free( full_path );
            }
            g_dir_close( dir_content );
        }
    }
    return NULL;
}

gboolean vfs_dir_is_loading( VFSDir* dir )
{
    return dir->task ? TRUE : FALSE;
}

gboolean vfs_dir_is_file_listed( VFSDir* dir )
{
    return dir->file_listed;
}

void vfs_cancel_load( VFSDir* dir )
{
    dir->cancel = TRUE;
    if ( dir->task )
    {
//g_printf("spacefm: vfs_cancel_load -> vfs_async_task_cancel\n");
        vfs_async_task_cancel( dir->task );
        /* don't do g_object_unref on task here since this is done in the handler of "finish" signal. */
        dir->task = NULL;
    }
}

gboolean update_file_info( VFSDir* dir, VFSFileInfo* file )
{
    char* full_path;
    char* file_name;
    gboolean ret = FALSE;
    /* gboolean is_desktop = is_dir_desktop(dir->path); */

    /* FIXME: Dirty hack: steal the string to prevent memory allocation */
    file_name = file->name;
    if( file->name == file->disp_name )
        file->disp_name = NULL;
    file->name = NULL;

    full_path = g_build_filename( dir->path, file_name, NULL );
    if ( G_LIKELY( full_path ) )
    {
        if( G_LIKELY( vfs_file_info_get( file, full_path, file_name ) ) )
        {
            ret = TRUE;
            /* if( G_UNLIKELY(is_desktop) ) */
            vfs_file_info_load_special_info( file, full_path );
        }
        else /* The file doesn't exist */
        {
            GList* l;
            l = g_list_find( dir->file_list, file );
            if( G_UNLIKELY(l) )
            {
                dir->file_list = g_list_delete_link( dir->file_list, l );
                --dir->n_files;
                if ( file )
                {
                    g_signal_emit( dir, signals[ FILE_DELETED_SIGNAL ], 0, file );
                    vfs_file_info_unref( file );
                }
            }
            ret = FALSE;
        }
        g_free( full_path );
    }
    g_free( file_name );
    return ret;
}


void update_changed_files( gpointer key, gpointer data, gpointer user_data )
{
    VFSDir* dir = (VFSDir*)data;
    GSList* l;
    VFSFileInfo* file;

    if ( dir->changed_files )
    {
        g_mutex_lock( dir->mutex );
        for ( l = dir->changed_files; l; l = l->next )
        {
            file = vfs_file_info_ref( ( VFSFileInfo* ) l->data );  ///
            if ( update_file_info( dir, file ) )
            {
                g_signal_emit( dir, signals[ FILE_CHANGED_SIGNAL ], 0, file );
                vfs_file_info_unref( file );
            }
            // else was deleted, signaled, and unrefed in update_file_info
        }
        g_slist_free( dir->changed_files );
        dir->changed_files = NULL;
        g_mutex_unlock( dir->mutex );
    }
}

void update_created_files( gpointer key, gpointer data, gpointer user_data )
{
    VFSDir* dir = (VFSDir*)data;
    GSList* l;
    char* full_path;
    VFSFileInfo* file;
    GList* ll;

    if ( dir->created_files )
    {
        g_mutex_lock( dir->mutex );
        for ( l = dir->created_files; l; l = l->next )
        {
            if ( !( ll = vfs_dir_find_file( dir, (char*)l->data, NULL ) ) )
            {
                // file is not in dir file_list
                full_path = g_build_filename( dir->path, (char*)l->data, NULL );
                file = vfs_file_info_new();
                if ( vfs_file_info_get( file, full_path, NULL ) )
                {
                    // add new file to dir file_list
                    vfs_file_info_load_special_info( file, full_path );
                    dir->file_list = g_list_prepend( dir->file_list,
                                                    vfs_file_info_ref( file ) );
                    ++dir->n_files;
                    g_signal_emit( dir, signals[ FILE_CREATED_SIGNAL ], 0, file );
                }
                // else file doesn't exist in filesystem
                vfs_file_info_unref( file );
                g_free( full_path );
            }
            else
            {
                // file already exists in dir file_list
                file = vfs_file_info_ref( (VFSFileInfo*)ll->data );
                if ( update_file_info( dir, file ) )
                {
                    g_signal_emit( dir, signals[ FILE_CHANGED_SIGNAL ], 0, file );
                    vfs_file_info_unref( file );
                }
                // else was deleted, signaled, and unrefed in update_file_info
            }
            g_free( (char*)l->data );  // free file_name string
        }
        g_slist_free( dir->created_files );
        dir->created_files = NULL;
        g_mutex_unlock( dir->mutex );
    }
}

gboolean notify_file_change( gpointer user_data )
{
    //GDK_THREADS_ENTER();  //sfm not needed because in main thread?
    g_hash_table_foreach( dir_hash, update_changed_files, NULL );
    g_hash_table_foreach( dir_hash, update_created_files, NULL );
    /* remove the timeout */
    change_notify_timeout = 0;
    //GDK_THREADS_LEAVE();
    return FALSE;
}

void vfs_dir_flush_notify_cache()
{
    if ( change_notify_timeout )
        g_source_remove( change_notify_timeout );
    change_notify_timeout = 0;
    g_hash_table_foreach( dir_hash, update_changed_files, NULL );
    g_hash_table_foreach( dir_hash, update_created_files, NULL );
}

/* Callback function which will be called when monitored events happen */
void vfs_dir_monitor_callback( VFSFileMonitor* fm,
                               VFSFileMonitorEvent event,
                               const char* file_name,
                               gpointer user_data )
{
    VFSDir* dir = ( VFSDir* ) user_data;
    GDK_THREADS_ENTER();

    switch ( event )
    {
    case VFS_FILE_MONITOR_CREATE:
        vfs_dir_emit_file_created( dir, file_name, FALSE );
        break;
    case VFS_FILE_MONITOR_DELETE:
        vfs_dir_emit_file_deleted( dir, file_name, NULL );
        break;
    case VFS_FILE_MONITOR_CHANGE:
        vfs_dir_emit_file_changed( dir, file_name, NULL, FALSE );
        break;
    default:
        g_warning("Error: unrecognized file monitor signal!");
    }
    GDK_THREADS_LEAVE();
}

/* called on every VFSDir when icon theme got changed */
static void reload_icons( const char* path, VFSDir* dir, gpointer user_data )
{
    GList* l;
    for( l = dir->file_list; l; l = l->next )
    {
        VFSFileInfo* fi = (VFSFileInfo*)l->data;
        /* It's a desktop entry file */
        if( fi->flags & VFS_FILE_INFO_DESKTOP_ENTRY )
        {
            char* file_path = g_build_filename( path, fi->name,NULL );
            if( fi->big_thumbnail )
            {
                g_object_unref( fi->big_thumbnail );
                fi->big_thumbnail = NULL;
            }
            if( fi->small_thumbnail )
            {
                g_object_unref( fi->small_thumbnail );
                fi->small_thumbnail = NULL;
            }
            vfs_file_info_load_special_info( fi, file_path );
            g_free( file_path );
        }
    }
}

static void on_theme_changed( GtkIconTheme *icon_theme, gpointer user_data )
{
    g_hash_table_foreach( dir_hash, (GHFunc)reload_icons, NULL );
}

VFSDir* vfs_dir_get_by_path_soft( const char* path )
{
    if ( G_UNLIKELY( !dir_hash || !path ) )
        return NULL;

    VFSDir * dir = g_hash_table_lookup( dir_hash, path );
    if ( dir )
        g_object_ref( dir );
    return dir;
}

VFSDir* vfs_dir_get_by_path( const char* path )
{
    VFSDir * dir = NULL;

    g_return_val_if_fail( G_UNLIKELY(path), NULL );

    if ( G_UNLIKELY( ! dir_hash ) )
    {
        dir_hash = g_hash_table_new_full( g_str_hash, g_str_equal, NULL, NULL );
        if( 0 == theme_change_notify )
            theme_change_notify = g_signal_connect( gtk_icon_theme_get_default(), "changed",
                                                                        G_CALLBACK( on_theme_changed ), NULL );
    }
    else
    {
        dir = g_hash_table_lookup( dir_hash, path );
    }

    if( G_UNLIKELY( !mime_cb ) )
        mime_cb = vfs_mime_type_add_reload_cb( on_mime_type_reload, NULL );

    if ( dir )
    {
        g_object_ref( dir );
    }
    else
    {
        dir = vfs_dir_new( path );
        vfs_dir_load( dir );  /* asynchronous operation */
        g_hash_table_insert( dir_hash, (gpointer)dir->path, (gpointer)dir );
    }
    return dir;
}

static void reload_mime_type( char* key, VFSDir* dir, gpointer user_data )
{
    GList* l;
    VFSFileInfo* file;
    char* full_path;

    if( G_UNLIKELY( ! dir || ! dir->file_list ) )
        return;
    g_mutex_lock( dir->mutex );
    for( l = dir->file_list; l; l = l->next )
    {
        file = (VFSFileInfo*)l->data;
        full_path = g_build_filename( dir->path,
                                      vfs_file_info_get_name( file ), NULL );
        vfs_file_info_reload_mime_type( file, full_path );
        /* g_debug( "reload %s", full_path ); */
        g_free( full_path );
    }

    for( l = dir->file_list; l; l = l->next )
    {
        file = (VFSFileInfo*)l->data;
        g_signal_emit( dir, signals[FILE_CHANGED_SIGNAL], 0, file );
    }
    g_mutex_unlock( dir->mutex );
}

static void on_mime_type_reload( gpointer user_data )
{
    if( ! dir_hash )
        return;
    /* g_debug( "reload mime-type" ); */
    g_hash_table_foreach( dir_hash, (GHFunc)reload_mime_type, NULL );
}

/* Thanks to the freedesktop.org, things are much more complicated now... */
const char* vfs_get_desktop_dir()
{
    static const char* desktop_dir;

    if( G_LIKELY(is_desktop_set) )
        return desktop_dir;

    desktop_dir = g_get_user_special_dir( G_USER_DIRECTORY_DESKTOP );

    is_desktop_set = TRUE;

    return desktop_dir;
}

void vfs_dir_foreach( GHFunc func, gpointer user_data )
{
    if( ! dir_hash )
        return;
    /* g_debug( "reload mime-type" ); */
    g_hash_table_foreach( dir_hash, (GHFunc)func, user_data );
}

void vfs_dir_unload_thumbnails( VFSDir* dir, gboolean is_big )
{
    GList* l;
    VFSFileInfo* file;
    char* file_path;

    g_mutex_lock( dir->mutex );
    if( is_big )
    {
        for( l = dir->file_list; l; l = l->next )
        {
            file = (VFSFileInfo*)l->data;
            if( file->big_thumbnail )
            {
                g_object_unref( file->big_thumbnail );
                file->big_thumbnail = NULL;
            }
            /* This is a desktop entry file, so the icon needs reload
                 FIXME: This is not a good way to do things, but there is no better way now.  */
            if( file->flags & VFS_FILE_INFO_DESKTOP_ENTRY )
            {
                file_path = g_build_filename( dir->path, file->name, NULL );
                vfs_file_info_load_special_info( file, file_path );
                g_free( file_path );
            }
        }
    }
    else
    {
        for( l = dir->file_list; l; l = l->next )
        {
            file = (VFSFileInfo*)l->data;
            if( file->small_thumbnail )
            {
                g_object_unref( file->small_thumbnail );
                file->small_thumbnail = NULL;
            }
            /* This is a desktop entry file, so the icon needs reload
                 FIXME: This is not a good way to do things, but there is no better way now.  */
            if( file->flags & VFS_FILE_INFO_DESKTOP_ENTRY )
            {
                file_path = g_build_filename( dir->path, file->name, NULL );
                vfs_file_info_load_special_info( file, file_path );
                g_free( file_path );
            }
        }
    }
    g_mutex_unlock( dir->mutex );

    /* Ensuring free space at the end of the heap is freed to the OS,
     * mainly to deal with the possibility thousands of large thumbnails
     * have been freed but the memory not actually released by SpaceFM */
#if defined (__GLIBC__)
	malloc_trim(0);
#endif
}

//sfm added mime change timer
guint mime_change_timer = 0;
VFSDir* mime_dir = NULL;

gboolean on_mime_change_timer( gpointer user_data )
{
    //g_printf("MIME-UPDATE on_timer\n" );
    char* cmd = g_strdup_printf("update-mime-database %s/mime", g_get_user_data_dir());
    g_printf("COMMAND=%s\n", cmd);
    g_spawn_command_line_async( cmd, NULL );
    g_free( cmd );

    cmd = g_strdup_printf("update-desktop-database %s/applications", g_get_user_data_dir());
    g_printf("COMMAND=%s\n", cmd);
    g_spawn_command_line_async( cmd, NULL );
    g_free( cmd );

    g_source_remove( mime_change_timer );
    mime_change_timer = 0;
    return FALSE;
}

void mime_change( gpointer user_data )
{
    if ( mime_change_timer )
    {
        // timer is already running, so ignore request
        //g_printf("MIME-UPDATE already set\n" );
        return;
    }
    if ( mime_dir )
    {
        // update mime database in 2 seconds
        mime_change_timer = g_timeout_add_seconds( 2,
                                    ( GSourceFunc ) on_mime_change_timer, NULL );
        //g_printf("MIME-UPDATE timer started\n" );
    }
}

void vfs_dir_monitor_mime()
{
    // start watching for changes
    if ( mime_dir )
        return;
    char* path = g_build_filename( g_get_user_data_dir(), "mime/packages", NULL );
    if ( g_file_test( path, G_FILE_TEST_IS_DIR ) )
    {
        mime_dir = vfs_dir_get_by_path( path );
        if ( mime_dir )
        {
            g_signal_connect( mime_dir, "file-listed", G_CALLBACK( mime_change ), NULL );
            g_signal_connect( mime_dir, "file-created", G_CALLBACK( mime_change ), NULL );
            g_signal_connect( mime_dir, "file-deleted", G_CALLBACK( mime_change ), NULL );
            g_signal_connect( mime_dir, "file-changed", G_CALLBACK( mime_change ), NULL );
        }
        //g_printf("MIME-UPDATE watch started\n" );
    }
    g_free( path );
}


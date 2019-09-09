/*
 *
 * License: See COPYING file
 *
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib/gprintf.h>

#include "settings.h"
#include "utils.h"

char* randhex8()
{
    char hex[9];
    uint n;

    n = rand();
    g_snprintf(hex, sizeof(hex), "%08x", n);
    return g_strdup( hex );
}

char* replace_line_subs( const char* line )
{
    char* old_s;
    char* s;
    int i;
    const char* perc[] = { "%f", "%F", "%n", "%N", "%d", "%D", "%v", "%l", "%m", "%y", "%b", "%t", "%p", "%a" };
    const char* var[] =
    {
        "\"${fm_file}\"",
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
        "\"${fm_value}\""
    };

    s = g_strdup( line );
    int num = G_N_ELEMENTS( perc );
    for ( i = 0; i < num; i++ )
    {
        if ( strstr( line, perc[i] ) )
        {
            old_s = s;
            s = replace_string( old_s, perc[i], var[i], FALSE );
            g_free( old_s );
        }
    }
    return s;
}

gboolean have_x_access( const char* path )
{
    return ( access( path, R_OK | X_OK ) == 0 );
}

gboolean have_rw_access( const char* path )
{
    if ( !path )
        return FALSE;
    return ( access( path, R_OK | W_OK ) == 0 );
}

gboolean dir_has_files( const char* path )
{
    GDir* dir;
    gboolean ret = FALSE;

    if ( !( path && g_file_test( path, G_FILE_TEST_IS_DIR ) ) )
        return FALSE;

    dir = g_dir_open( path, 0, NULL );
    if ( dir )
    {
        if ( g_dir_read_name( dir ) )
            ret = TRUE;
        g_dir_close( dir );
    }
    return ret;
}

char* get_name_extension( char* full_name, gboolean is_dir, char** ext )
{
    char* dot;
    char* str;
    char* final_ext;
    char* full;

    full = g_strdup( full_name );
    // get last dot
    if ( is_dir || !( dot = strrchr( full, '.' ) ) || dot == full )
    {
        // dir or no dots or one dot first
        *ext = NULL;
        return full;
    }
    dot[0] = '\0';
    final_ext = dot + 1;
    // get previous dot
    dot = strrchr( full, '.' );
    uint final_ext_len = strlen( final_ext );
    if ( dot && !strcmp( dot + 1, "tar" ) && final_ext_len < 11 && final_ext_len )
    {
        // double extension
        final_ext[-1] = '.';
        *ext = g_strdup( dot + 1 );
        dot[0] = '\0';
        str = g_strdup( full );
        g_free( full );
        return str;
    }
    // single extension, one or more dots
    if ( final_ext_len < 11 && final_ext[0] )
    {
        *ext = g_strdup( final_ext );
        str = g_strdup( full );
        g_free( full );
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

void open_in_prog( const char* path )
{
    char* prog = g_find_program_in_path( g_get_prgname() );
    if ( !prog )
        prog = g_strdup( g_get_prgname() );
    if ( !prog )
        prog = g_strdup( "spacefm" );
    char* qpath = bash_quote( path );
    char* command = g_strdup_printf("%s %s", prog, qpath);
    g_printf("COMMAND=%s\n", command);
    g_spawn_command_line_async(command, NULL);
    g_free( qpath );
    g_free(command);
    g_free( prog );
}

char *replace_string( const char* orig, const char* str, const char* replace,
                                                                gboolean quote )
{   // replace all occurrences of str in orig with replace, optionally quoting
    char* rep;
    const char* cur;
    char* result = NULL;
    char* old_result;
    char* s;

    if ( !orig || !( s = strstr( orig, str ) ) )
        return g_strdup( orig );  // str not in orig

    if ( !replace )
    {
        if ( quote )
            rep = g_strdup( "''" );
        else
            rep = g_strdup( "" );
    }
    else if ( quote )
        rep = g_strdup_printf( "'%s'", replace );
    else
        rep = g_strdup( replace );

    cur = orig;
    do
    {
        if ( result )
        {
            old_result = result;
            result = g_strdup_printf( "%s%s%s", old_result,
                                            g_strndup( cur, s - cur ), rep );
            g_free( old_result );
        }
        else
            result = g_strdup_printf( "%s%s", g_strndup( cur, s - cur ), rep );
        cur = s + strlen( str );
        s = strstr( cur, str );
    } while ( s );
    old_result = result;
    result = g_strdup_printf( "%s%s", old_result, cur );
    g_free( old_result );
    g_free( rep );
    return result;

/*
    // replace first occur of str in orig with rep
    char* buffer;
    char* buffer2;
    char *p;
    char* rep_good;

    if ( !( p = strstr( orig, str ) ) )
        return g_strdup( orig );  // str not in orig
    if ( !rep )
        rep_good = g_strdup_printf( "" );
    else
        rep_good = g_strdup( rep );
    buffer = g_strndup( orig, p - orig );
    if ( quote )
        buffer2 = g_strdup_printf( "%s'%s'%s", buffer, rep_good, p + strlen( str ) );
    else
        buffer2 = g_strdup_printf( "%s%s%s", buffer, rep_good, p + strlen( str ) );
    g_free( buffer );
    g_free( rep_good );
    return buffer2;
*/
}

char* bash_quote( const char* str )
{
    if ( !str )
        return g_strdup( "\"\"" );
    char* s1 = replace_string( str, "\"", "\\\"", FALSE );
    char* s2 = g_strdup_printf( "\"%s\"", s1 );
    g_free( s1 );
    return s2;
}

char* plain_ascii_name( const char* orig_name )
{
    if ( !orig_name )
        return g_strdup( "" );
    char* orig = g_strdup( orig_name );
    g_strcanon( orig, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", ' ' );
    char* s = replace_string( orig, " ", "", FALSE );
    g_free( orig );
    return s;
}

char* clean_label( const char* menu_label, gboolean kill_special, gboolean escape )
{
    char* s1;
    char* s2;
    if ( menu_label && strstr( menu_label, "\\_" ) )
    {
        s1 = replace_string( menu_label, "\\_", "@UNDERSCORE@", FALSE );
        s2 = replace_string( s1, "_", "", FALSE );
        g_free( s1 );
        s1 = replace_string( s2, "@UNDERSCORE@", "_", FALSE );
        g_free( s2 );
    }
    else
        s1 = replace_string( menu_label, "_", "", FALSE );
    if ( kill_special )
    {
        s2 = replace_string( s1, "&", "", FALSE );
        g_free( s1 );
        s1 = replace_string( s2, " ", "-", FALSE );
        g_free( s2 );
    }
    else if ( escape )
    {
        s2 = g_markup_escape_text( s1, -1 );
        g_free( s1 );
        s1 = s2;
    }
    return s1;
}

void string_copy_free( char** s, const char* src )
{
    char* discard = *s;
    *s = g_strdup( src );
    g_free( discard );
}

char* unescape( const char* t )
{
    if ( !t )
        return NULL;

    char* s = g_strdup( t );

    int i = 0, j = 0;
    while ( t[i] )
    {
        switch ( t[i] )
        {
        case '\\':
            switch( t[++i] )
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
    s[j] = t[i];  // null char
    return s;
}

char* get_valid_su()  // may return NULL
{
    int i;
    char* use_su = NULL;
    char* custom_su = NULL;

    use_su = g_strdup( xset_get_s( "su_command" ) );

    if ( settings_terminal_su )
        // get su from /etc/spacefm/spacefm.conf
        custom_su = g_find_program_in_path( settings_terminal_su );

    if ( custom_su && ( !use_su || use_su[0] == '\0' ) )
    {
        // no su set in Prefs, use custom
        xset_set( "su_command", "s", custom_su );
        g_free( use_su );
        use_su = g_strdup( custom_su );
    }
    if ( use_su )
    {
        if ( !custom_su || g_strcmp0( custom_su, use_su ) )
        {
            // is Prefs use_su in list of valid su commands?
            for ( i = 0; i < G_N_ELEMENTS( su_commands ); i++ )
            {
                if ( !strcmp( su_commands[i], use_su ) )
                    break;
            }
            if ( i == G_N_ELEMENTS( su_commands ) )
            {
                // not in list - invalid
                g_free( use_su );
                use_su = NULL;
            }
        }
    }
    if ( !use_su )
    {
        // discovery
        for ( i = 0; i < G_N_ELEMENTS( su_commands ); i++ )
        {
            if ((use_su = g_find_program_in_path(su_commands[i])))
                break;
        }
        if ( !use_su )
            use_su = g_strdup( su_commands[0] );
        xset_set( "su_command", "s", use_su );
    }
    char* su_path = g_find_program_in_path( use_su );
    g_free( use_su );
    g_free( custom_su );
    return su_path;
}

char* get_valid_gsu()  // may return NULL
{
    int i;
    char* use_gsu = NULL;
    char* custom_gsu = NULL;

    // get gsu set in Prefs
    use_gsu = g_strdup( xset_get_s( "gsu_command" ) );

    if ( settings_graphical_su )
        // get gsu from /etc/spacefm/spacefm.conf
        custom_gsu = g_find_program_in_path( settings_graphical_su );
#ifdef PREFERABLE_SUDO_PROG
    if ( !custom_gsu )
        // get build-time gsu
        custom_gsu = g_find_program_in_path( PREFERABLE_SUDO_PROG );
#endif
    if ( custom_gsu && ( !use_gsu || use_gsu[0] == '\0' ) )
    {
        // no gsu set in Prefs, use custom
        xset_set( "gsu_command", "s", custom_gsu );
        g_free( use_gsu );
        use_gsu = g_strdup( custom_gsu );
    }
    if ( use_gsu )
    {
        if ( !custom_gsu || g_strcmp0( custom_gsu, use_gsu ) )
        {
            // is Prefs use_gsu in list of valid gsu commands?
            for ( i = 0; i < G_N_ELEMENTS( gsu_commands ); i++ )
            {
                if ( !strcmp( gsu_commands[i], use_gsu ) )
                    break;
            }
            if ( i == G_N_ELEMENTS( gsu_commands ) )
            {
                // not in list - invalid
                g_free( use_gsu );
                use_gsu = NULL;
            }
        }
    }
    if ( !use_gsu )
    {
        // discovery
        for ( i = 0; i < G_N_ELEMENTS( gsu_commands ); i++ )
        {
            // don't automatically select gksudo
            if ( strcmp( gsu_commands[i], "/usr/bin/gksudo" ) )
            {
                if ((use_gsu = g_find_program_in_path(gsu_commands[i])))
                    break;
            }
        }
        if ( !use_gsu )
            use_gsu = g_strdup( gsu_commands[0] );
        xset_set( "gsu_command", "s", use_gsu );
    }

    char* gsu_path = g_find_program_in_path( use_gsu );
    if ( !gsu_path && !g_strcmp0( use_gsu, "/usr/bin/kdesu" ) )
    {
        // kdesu may be in libexec path
        char* stdout;
        if ( g_spawn_command_line_sync( "kde4-config --path libexec",
                                            &stdout, NULL, NULL, NULL )
                                            && stdout && stdout[0] != '\0' )
        {
            if ((gsu_path = strchr(stdout, '\n')))
               gsu_path[0] = '\0';
            gsu_path = g_build_filename( stdout, "kdesu", NULL );
            g_free( stdout );
            if ( !g_file_test( gsu_path, G_FILE_TEST_EXISTS ) )
            {
                g_free( gsu_path );
                gsu_path = NULL;
            }
        }
    }
    g_free( use_gsu );
    g_free( custom_gsu );
    return gsu_path;
}


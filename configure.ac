dnl Process this file with autoconf to produce a configure script.

AC_INIT([spacefm], [1.0.6])
AC_CONFIG_SRCDIR([src/main.c])
AM_INIT_AUTOMAKE([dist-xz] [subdir-objects] [foreign])
AM_MAINTAINER_MODE

AM_DISABLE_STATIC

AC_ISC_POSIX

AC_PROG_CC
AM_PROG_LIBTOOL
AM_PROG_CC_STDC
AC_PROG_INTLTOOL
AC_HEADER_STDC


# sysconfdir defaults to /usr/local/etc if this is not done:
test "$sysconfdir" = '${prefix}/etc' && sysconfdir=/etc

# libtool option to control which symbols are exported
# right now, symbols starting with _ are not exported
LIBTOOL_EXPORT_OPTIONS='-export-symbols-regex "^[[^_]].*"'
AC_SUBST(LIBTOOL_EXPORT_OPTIONS)

# check for mmap()
AC_FUNC_MMAP

AC_CHECK_LIB(m, pow)
AC_CHECK_LIB(X11, XClearWindow)

AC_DEFINE(G_DISABLE_ASSERT, [1], [do not enable g_asserts])
AC_DEFINE(G_LOG_DOMAIN, "SpaceFM")

AC_ARG_WITH([bash-path],
        AC_HELP_STRING([--with-bash-path=PATH], [Absolute path to GENUINE bash v5 - other shells lack features REQUIRED by SpaceFM (default: /bin/bash)]),
        AC_DEFINE_UNQUOTED(BASHPATH, "$withval", [path to bash]),
        AC_DEFINE_UNQUOTED(BASHPATH, "/bin/bash", [path to bash]))
bash_path="$withval"
# IMPORTANT: For security reasons, an absolute path must be used for genuine
# bash v4.  SpaceFM runs bash as root and you WILL open root exploits if you
# disable this check, or run bash based on $PATH.
if test ! -z "$bash_path"; then
    if test ! "${bash_path:0:1}" = "/"; then
        AC_MSG_ERROR([Fatal Error: Option --with-bash-path requires absolute path.])
    fi
    echo "Modifying spacefm-auth to use bash path..."
    bash_esc="${bash_path//\//\\/}"
    sed "s/\(\/bin\/bash\)/$bash_esc/" src/spacefm-auth >| src/spacefm-auth
fi

#-u is too strict
AC_DEFINE_UNQUOTED(SHELL_SETTINGS, "set -o pipefail\nshopt -s failglob")

# gtk2 or gtk3
PKG_PROG_PKG_CONFIG
gtk2_modules="gtk+-2.0 >= 2.24.0 \
              gthread-2.0 \
              gobject-2.0"
gtk3_modules="gtk+-3.0 >= 3.24.0 \
              gthread-2.0 \
              gobject-2.0"

AC_ARG_WITH([gtk3], AS_HELP_STRING([--with-gtk3], [Build with GTK3 interface instead of GTK2 (default: with GTK2 unless GTK2 not installed)]), with_gtk3=$withval, with_gtk3="no")

AC_ARG_WITH([gtk2], AS_HELP_STRING([--with-gtk2], [Build with GTK2 interface only (default: with GTK2 unless GTK2 not installed)]), with_gtk2=$withval, with_gtk2="no")

if test x"$with_gtk3" = x"yes"; then
    # user enabled - gtk3 required
    PKG_CHECK_MODULES(GTK, [$gtk3_modules])
    AC_DEFINE([HAVE_GTK3], [1], [Use GTK3])
elif test x"$with_gtk2" = x"yes"; then
    # user enabled - gtk2 required
    PKG_CHECK_MODULES(GTK, [$gtk2_modules])
else
    # disabled - use gtk3 only if gtk2 not installed
    PKG_CHECK_MODULES(GTK, [$gtk2_modules], with_gtk3="no", [
       PKG_CHECK_MODULES(GTK, [$gtk3_modules], with_gtk3="yes", with_gtk3="no")
    ])
    if test x"$with_gtk3" = x"yes"; then
        AC_DEFINE([HAVE_GTK3], [1], [Use GTK3])
    else
        PKG_CHECK_MODULES(GTK, [$gtk2_modules])
    fi
fi
AC_SUBST([GTK_CFLAGS])
AC_SUBST([GTK_LIBS])

# Default make options
CFLAGS="$CFLAGS -D_FORTIFY_SOURCE=2 -D_DEFAULT_SOURCE -std=c18 -Wall -Wformat-security -Wswitch-default -Wimplicit-fallthrough=3"

# start-notification is optional - will be used if installed and no --disable
AC_ARG_ENABLE(
    [startup-notification],
    AS_HELP_STRING([--disable-startup-notification],
                   [disable use of libstartup-notification (default: enable if installed)]),
    use_sn=$enableval, use_sn="yes")
if test x"$use_sn" = x"yes"; then
    sn_modules="libstartup-notification-1.0"
    PKG_CHECK_MODULES(SN, [$sn_modules], use_sn="yes", use_sn="no (not installed)")
    if test x"$use_sn" = x"yes"; then
        AC_SUBST(SN_CFLAGS)
        AC_SUBST(SN_LIBS)
        AC_DEFINE([HAVE_SN], [1], [Use SN])
    fi
fi

dnl linker optimizations
AC_MSG_CHECKING([whether $LD accepts --as-needed])
case `$LD --as-needed -v 2>&1 </dev/null` in
*GNU* | *'with BFD'*)
  LDFLAGS="$LDFLAGS -Wl,--as-needed"
  AC_MSG_RESULT([yes])
  ;;
*)
  AC_MSG_RESULT([no])
  ;;
esac
AC_MSG_CHECKING([whether $LD accepts -O1])
case `$LD -O1 -v 2>&1 </dev/null` in
*GNU* | *'with BFD'*)
  LDFLAGS="$LDFLAGS -Wl,-O1"
  AC_MSG_RESULT([yes])
  ;;
*)
  AC_MSG_RESULT([no])
  ;;
esac
AC_MSG_CHECKING([whether $LD accepts -Bsymbolic-functions])
case `$LD -Bsymbolic-functions -v 2>&1 </dev/null` in
*GNU* | *'with BFD'*)
  LDFLAGS="$LDFLAGS -Wl,-Bsymbolic-functions"
  AC_MSG_RESULT([yes])
  ;;
*)
  AC_MSG_RESULT([no])
  ;;
esac

AC_ARG_ENABLE(
    [cast-checks],
    AS_HELP_STRING([--enable-cast-checks],
                   [enable Glib casting checks (default: disable)]),
    cast_checks=$enableval, cast_checks="no")
G_CAST_CHECKS=""
if test x"$cast_checks" = "xno"; then
    G_CAST_CHECKS="-DG_DISABLE_CAST_CHECKS"
fi
AC_SUBST(G_CAST_CHECKS)

PKG_CHECK_MODULES(LIBUDEV, [libudev >= 143])
AC_SUBST(LIBUDEV_CFLAGS)
AC_SUBST(LIBUDEV_LIBS)

AC_ARG_WITH(
    [preferable-sudo],
    AS_HELP_STRING([--with-preferable-sudo=PROG],
                   [Specify custom graphical su program to run root commands (or see /etc/spacefm/spacefm.conf)]),
                   [ preferable_sudo="$withval" ])
if test ! -z "$preferable_sudo"; then
  CFLAGS="$CFLAGS -DPREFERABLE_SUDO_PROG=\\\"$preferable_sudo\\\""
fi

AC_ARG_ENABLE(
    [desktop-integration],
    AS_HELP_STRING([--disable-desktop-integration],
                   [disable desktop manager integration (default: enable)]),
    desktop_integration=$enableval, desktop_integration="no")
if test x"$desktop_integration" = "xyes"; then
  AC_DEFINE(DESKTOP_INTEGRATION, 1, [Whether to integrate desktop or not.])
fi
AM_CONDITIONAL(DESKTOP_INTEGRATION, test "$desktop_integration" = "yes")

AC_ARG_ENABLE(
    [deprecated_hw],
    AS_HELP_STRING([--enable-deprecated-hw],
                   [enable deprecated hardware support (firewire,audiocd,dvd) (default: disable)]),
    deprecated_hw=$enableval, deprecated_hw="no")
if test x"$deprecated_hw" = "xyes"; then
  AC_DEFINE(DEPRECATED_HW, 1, [Whether to build depercated hw support.])
fi
AM_CONDITIONAL(DEPRECATED_HW, test "$deprecated_hw" = "yes")

AC_ARG_ENABLE(
    [git],
    AS_HELP_STRING([--disable-git],
		   [use git to keep history of session file (default: enable)]),
    use_git=$enableval, use_git="yes")
if test x"$use_git" = "xyes"; then
  AC_DEFINE(USE_GIT, 1, [Whether to build git session support.])
fi
AM_CONDITIONAL(USE_GIT, test "$use_git" = "yes")

AC_ARG_ENABLE(
    [small_screen],
    AS_HELP_STRING([--enable-small-screen],
                   [enable dialog scaling on small screens (default: disable)]),
    small_screen=$enableval, small_screen="no")
if test x"$small_screen" = "xyes"; then
  AC_DEFINE(SMALL_SCREEN, 1, [Wheather to build dialog screen scaling.])
fi
AM_CONDITIONAL(SMALL_SCREEN, test "$small_screen" = "yes")

AC_ARG_ENABLE(
    [video-thumbnails],
    AS_HELP_STRING([--disable-video-thumbnails],
                   [disable libffmpegthumbnailer video thumbnails (default: enable)]),
    video_thumbnails=$enableval, video_thumbnails="yes")
if test x"$video_thumbnails" = "xyes"; then
    PKG_CHECK_MODULES( FFMPEG, libffmpegthumbnailer, HAVE_FFMPEG=yes)
    AC_SUBST(FFMPEG_CFLAGS)
    AC_SUBST(FFMPEG_LIBS)
    AC_DEFINE(HAVE_FFMPEG, 1, [Whether to use video thumbnails or not.])
fi

AC_ARG_ENABLE(
    [pixmaps],
    AS_HELP_STRING([--enable-pixmaps],
        [use share/pixmaps dir instead of share/icons dir to store icons (default: disable)]),
    use_pixmaps=$enableval, use_pixmaps="no")
AM_CONDITIONAL(NO_PIXMAPS, test ! x"$use_pixmaps" = x"yes")

dnl advanced compiler tweaking
CFLAGS="$CFLAGS -fstrict-aliasing -fmessage-length=0"

dnl data dirs
CFLAGS="$CFLAGS -DDATADIR=\\\"$datadir\\\" -DHTMLDIR=\\\"$htmldir\\\""

AC_CHECK_FUNC(euidaccess,[AC_DEFINE(HAVE_EUIDACCESS,[],[Define to 1 if euidaccess is available])])
AC_CHECK_FUNC(eaccess,[AC_DEFINE(HAVE_EACCESS,[],[Define to 1 if eaccess is available])])

AC_CHECK_FUNC(statvfs,[AC_DEFINE(HAVE_STATVFS,[],[Define to 1 if statvfs is available])])


# Gtk Builder
#AC_PATH_PROG([GTK_BUILDER_CONVERT],[gtk-builder-convert],[false])
#if test "$GTK_BUILDER_CONVERT" = "false"; then
#  AC_MSG_ERROR([gtk-builder-convert not found])
#fi

GETTEXT_PACKAGE=spacefm
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])

dnl Add the languages which your application supports here.
#ALL_LINGUAS="af ar da ca cs de es et eu fa fi fr gl he hr hu id it ja ko lt ml ms nb nl nn pl ps pt pt_BR ru sk sl sv tr uk ur ur_PK vi zh_CN zh_TW"
ALL_LINGUAS=""

AC_OUTPUT([
Makefile
etc/Makefile
src/Makefile
po/Makefile.in
data/Makefile
])

resolve_datadir="$(eval echo "$datadir")"
while [[ "${resolve_datadir:0:1}" == "$" ]]; do
    resolve_datadir="$(eval echo "$resolve_datadir")"
done

resolve_htmldir="$(eval echo "$htmldir")"
while [[ "${resolve_htmldir:0:1}" == "$" ]]; do
    resolve_htmldir="$(eval echo "$resolve_htmldir")"
done

echo
echo SpaceFM...................................... : Version $VERSION
echo
echo Prefix....................................... : $prefix
echo Executable................................... : $prefix/bin/spacefm
echo Configuration Dir............................ : "$sysconfdir/spacefm/"
if test x"$use_pixmaps" = x"yes"; then
echo Icon Dir..................................... : "$resolve_datadir/pixmaps/"
else
echo Icon Dir..................................... : "$resolve_datadir/icons/hicolor/"
fi
echo Documentation Dir............................ : "$resolve_htmldir/"
if test ! -z "$bash_path"; then
echo Custom path to bash v4....................... : "$bash_path"
fi
echo Linux device support......................... : libudev
echo Linux file change monitor support............ : inotify
if test ! -z "$preferable_sudo"; then
echo Additional graphical su program.............. : $preferable_sudo
fi
if test x"$with_gtk3" = x"yes"; then
echo 'GTK+ Version................................. : GTK 3'
else
echo 'GTK+ Version................................. : GTK 2'
fi
echo Desktop manager integration.................. : $desktop_integration
echo Deprecated Hardware support.................. : $deprecated_hw
echo Small Screen Scaling......................... : $small_screen
echo Git session backend.......................... : $use_git
echo Startup notification......................... : $use_sn
echo Video thumbnail support...................... : $video_thumbnails
echo
echo 'Homepage: http://ignorantguru.github.io/spacefm/'
echo
echo Please read README carefully if you are packaging SpaceFM.
echo
if test ! x"$use_pixmaps" = x"yes"; then
echo
echo 'IMPORTANT: After make and install, run:'
echo "           sudo gtk-update-icon-cache -q -t -f $resolve_datadir/icons/hicolor"
echo "           sudo gtk-update-icon-cache -q -t -f $resolve_datadir/icons/Faenza"
echo
fi
if test ! -z "$bash_path"; then
echo "IMPORTANT: SpaceFM is highly integrated with bash and uses features of"
echo "           bash v4.  Attempting to substitute another shell for bash WILL cause"
echo "           feature breakage, even if not immediately apparent.  Use genuine"
echo "           bash."
echo
fi
echo

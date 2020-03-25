project('spacefm', 'c',
  version : '0.0.0',
  license : 'GPL2 LGPL',
  default_options : [
  	  'prefix=/usr',
  	  'sysconfdir=/etc',
  ],
  meson_version : '>= 0.49'
)

result = [ 'Passed options:' ]

c = meson.get_compiler('c')
c_id = c.get_id()
cross = meson.is_cross_build()
sh = find_program('sh')
sed = find_program('sed')
echo = find_program('echo')
incdir = include_directories('src')

builddir = meson.current_build_dir()
sourcedir = meson.current_source_dir()
package = meson.project_name()
version = meson.project_version()
prefix = get_option('prefix')
bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
sysconfdir = get_option('sysconfdir')
localedir = join_paths(prefix, get_option('localedir'))
mandir = join_paths(prefix, get_option('mandir'))

docdir = get_option('docdir')
if docdir != ''
	docdir = join_paths(prefix, docdir)
else
	docdir = join_paths(prefix, datadir, 'doc', package)
endif

htmldir = get_option('htmldir')
if htmldir != ''
	htmldir = join_paths(prefix, htmldir)
else
	htmldir = join_paths(docdir, 'html')
endif

ldflags_add = []
cflags_add = []

ldflags_opt = []
cflags_opt = []

cflags_opt += [
	[ '-fomit-frame-pointer' ],
	[ '-fno-common' ],
]
ldflags_opt += [
	[ '-Wl,-O2' ],
	[ '-Wl,--as-needed' ],
]

add_global_arguments(cflags_opt, language: 'c')
add_global_link_arguments(ldflags_opt, language: 'c')

spacefm_conf = configuration_data()
# Surround the version in quotes to make it a C string
spacefm_conf.set_quoted('VERSION', version)

#spacefm_conf.set_quoted('SYSCONFDIR', sysconfdir,
#	description : 'Path to the configuration directory, usually /etc')

#subst.set('LOCALEDIR', localedir)
#spacefm_conf.set_quoted('LOCALEDIR', localedir,
#	description : 'Path to the locale directory, usually /usr/share/locale')

#############################################

math_lib = meson.get_compiler('c').find_library('libm', required : true)
libbsd_lib = meson.get_compiler('c').find_library('libbsd', required : true)
udev_lib = meson.get_compiler('c').find_library('libudev', required : true)
ffmpeg_lib = meson.get_compiler('c').find_library('libffmpegthumbnailer', required : true)

#_lib = meson.get_compiler('c').find_library('', required : true)
#_lib = meson.get_compiler('c').find_library('', required : true)

x11_dep = dependency('x11')
gtk_dep = dependency('gtk+-2.0')
glib_dep = dependency('glib-2.0')

#if gtk_dep.found():
#    spacefm_conf.set()

spacefm_conf.set('DEPRECATED_HW', false)
spacefm_conf.set('HAVE_SN', false)
spacefm_conf.set('HAVE_MMAP', true)
spacefm_conf.set('ENABLE_NLS', false)

#spacefm_conf.set(, 0)
#spacefm_conf.set(, 0)
#spacefm_conf.set(, 0)


sources = [
  'src/cust-dialog.c',
  'src/find-files.c',
  'src/go-dialog.c',
  'src/item-prop.c',
  'src/main.c',
  'src/main-window.c',
  'src/pref-dialog.c',
  'src/settings.c',
  'src/utils.c',
  'src/mime-type/mime-action.c',
  'src/mime-type/mime-cache.c',
  'src/mime-type/mime-type.c',
  'src/exo/exo-binding.c',
  'src/exo/exo-cell-renderer-ellipsized-text.c',
  'src/exo/exo-cell-renderer-icon.c',
  'src/exo/exo-gdk-pixbuf-extensions.c',
  'src/exo/exo-gtk-extensions.c',
  'src/exo/exo-icon-chooser-dialog.c',
  'src/exo/exo-icon-chooser-model.c',
  'src/exo/exo-icon-view.c',
  'src/exo/exo-marshal.c',
  'src/exo/exo-private.c',
  'src/exo/exo-string.c',
  'src/exo/exo-thumbnail-preview.c',
  'src/exo/exo-tree-view.c',
  'src/exo/exo-utils.c',
  'src/ptk/ptk-app-chooser.c',
  'src/ptk/ptk-clipboard.c',
  'src/ptk/ptk-console-output.c',
  'src/ptk/ptk-dir-tree.c',
  'src/ptk/ptk-dir-tree-view.c',
  'src/ptk/ptk-file-archiver.c',
  'src/ptk/ptk-file-browser.c',
  'src/ptk/ptk-file-icon-renderer.c',
  'src/ptk/ptk-file-list.c',
  'src/ptk/ptk-file-menu.c',
  'src/ptk/ptk-file-misc.c',
  'src/ptk/ptk-file-properties.c',
  'src/ptk/ptk-file-task.c',
  'src/ptk/ptk-handler.c',
  'src/ptk/ptk-input-dialog.c',
  'src/ptk/ptk-location-view.c',
  'src/ptk/ptk-path-entry.c',
  'src/ptk/ptk-text-renderer.c',
  'src/ptk/ptk-utils.c',
  'src/vfs/vfs-app-desktop.c',
  'src/vfs/vfs-async-task.c',
  'src/vfs/vfs-dir.c',
  'src/vfs/vfs-execute.c',
  'src/vfs/vfs-file-info.c',
  'src/vfs/vfs-file-monitor.c',
  'src/vfs/vfs-file-task.c',
  'src/vfs/vfs-mime-type.c',
  'src/vfs/vfs-thumbnail-loader.c',
  'src/vfs/vfs-utils.c',
  'src/vfs/vfs-volume.c',
]

#configure_file(output : 'config.h', configuration : spacefm_conf)

target_name = 'spacefm'
target_type = 'executable'

spacefm = build_target(
  target_name,
  sources,
  target_type: target_type,
  include_directories: incdir,
  install : true,
  install_dir : bindir,
  dependencies: [
  glib_dep,
  gtk_dep,
  x11_dep,
  math_lib,
  libbsd_lib,
  udev_lib,
  ffmpeg_lib,
  ],
)

desktop_files = [
    'data/spacefm-folder-handler.desktop',
    'data/spacefm.desktop',
    'data/spacefm-find.desktop'
    ]

#icons = [
#    '',
#]

#foreach i : icons
#    install_data(i, install_dir : datadir / 'icons')
#endforeach

install_data(desktop_files, install_dir : datadir / 'applications')
install_subdir('data/ui', install_dir : datadir / target_name)

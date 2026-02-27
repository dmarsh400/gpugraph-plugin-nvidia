project('xfce4-gpugraph-plugin-nvidia', 'c',
  version: '1.0.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 0.50.0',
  default_options: ['c_std=gnu11', 'buildtype=debugoptimized']
)

cc = meson.get_compiler('c')

glib = dependency('glib-2.0', version: '>= 2.50.0')
gtk = dependency('gtk+-3.0', version: '>= 3.22.0')
libxfce4panel = dependency('libxfce4panel-2.0', version: '>= 4.16.0')
libxfce4ui = dependency('libxfce4ui-2', version: '>= 4.16.0')
libxfce4util = dependency('libxfce4util-1.0', version: '>= 4.16.0')
xfconf = dependency('libxfconf-0', version: '>= 4.16.0')

plugin_sources = [
  'gpugraph.c',
  'plugin.c',
  'plugin.h',
]

plugin_install_subdir = 'xfce4' / 'panel' / 'plugins'

shared_module('gpugraph',
  plugin_sources,
  dependencies: [glib, gtk, libxfce4panel, libxfce4ui, libxfce4util, xfconf],
  install: true,
  install_dir: get_option('libdir') / plugin_install_subdir,
)

install_data('gpugraph.desktop.in',
  rename: 'gpugraph.desktop',
  install_dir: get_option('datadir') / plugin_install_subdir,
)
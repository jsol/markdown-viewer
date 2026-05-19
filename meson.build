project(
  'markdown-viewer',
  'c',
  meson_version: '>= 1.1.0',
  license: 'MIT',
  default_options: [
    'warning_level=1',
  ],
)

configuration_data().set('version', get_option('version'))
add_project_arguments('-DGIT_VERSION="@0@"'.format(get_option('version')), language: 'c')
add_project_arguments('-DG_LOG_USE_STRUCTURED=true', language: 'c')
add_project_arguments('-DAPP_ID="@0@"'.format(get_option('appid')), language: 'c')

if get_option('buildtype') == 'debug'
  add_project_arguments('-DPRINT_DEBUG', language: 'c')
endif

subdir('src')

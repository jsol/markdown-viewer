project('glib-test', 'c',
        meson_version : '>= 0.61.0',
        license: 'MIT',
        default_options: [
            'warning_level=1',
        ],
        )

configuration_data().set('version', get_option('version'))
add_project_arguments('-DGIT_VERSION="@0@"'.format(get_option('version')), language : 'c')
add_project_arguments('-DG_LOG_USE_STRUCTURED=true', language : 'c')


subdir('src')

# Dependencies
deps = []
deps += dependency('gio-2.0', version : '>=2.72.4')
deps += dependency('glib-2.0', version : '>=2.72.4')
deps += dependency('libcmark')
deps += dependency('libadwaita-1', version : '>=1.1.7')
deps += dependency('gtk4', version : '>=4.6.9')

# Compiler flags
extra_cflags = ['-W', '-Wformat=2', '-Winline',
                '-Wstrict-prototypes', '-Wmissing-prototypes',
                '-Wdisabled-optimization', '-Wfloat-equal', '-Wall',
                '-Werror', '-Ddebug=false', '-Dstrip=true',
                ]

sources = ([
  'html.c',
  'main.c',
  'toc.c',
])


executable('glib-test',
           sources : sources,
           dependencies : deps,
           c_args : extra_cflags
           )


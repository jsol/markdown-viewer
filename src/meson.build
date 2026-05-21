# Dependencies
deps = []
deps += dependency('gio-2.0', version: '>=2.72.4')
deps += dependency('glib-2.0', version: '>=2.72.4')
deps += dependency('libadwaita-1', version: '>=1.1.7')
deps += dependency('gtk4', version: '>=4.6.9')
deps += dependency('libcmark-gfm', static: true)

# Compiler flags
extra_cflags = [
  '-W',
  '-Wformat=2',
  '-Winline',
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Wdisabled-optimization',
  '-Wfloat-equal',
  '-Wall',
  '-Werror',
  '-Ddebug=false',
  '-Dstrip=true',
]

sources = (
  [
    'color.c',
    'html.c',
    'main.c',
    'toc.c',
    'listener.c',
    'md.c',
    'table.c',
  ]
)

gly = dependency('glycin-2', required: false)
glygtk = dependency('glycin-gtk4-2', required: false)

if not gly.found()
  gly = dependency('glycin-1', required: false)
  glygtk = dependency('glycin-gtk4-1', required: false)
endif

if gly.found() and glygtk.found()
  deps += gly
  deps += glygtk
  sources += 'img_load_glycin.c'
else
  sources += 'img_load_gtk.c'
  message(
    'Using GTK4 image loader, if building for linux, consider installing libglycin',
  )
endif

executable(
  'markdown-viewer',
  sources: sources,
  dependencies: deps,
  c_args: extra_cflags,
)

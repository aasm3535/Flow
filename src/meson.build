flow_sources = [
  'main.c',
  'flow-application.c',
  'flow-window.c',
]

flow_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
]

flow_sources += gnome.compile_resources('flow-resources',
  'flow.gresource.xml',
  c_name: 'flow'
)

executable('flow', flow_sources,
  dependencies: flow_deps,
       install: true,
)

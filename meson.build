project(
	'rawe',
	'c',
	version: '0.1',
	license: 'MIT',
)

wayland_client = dependency('wayland-client')
wayland_server = dependency('wayland-server')

subdir('protocols')

executable(
	'rawe',
	['main.c'],
	dependencies: [ client_protos, wayland_client, wayland_server ]
)

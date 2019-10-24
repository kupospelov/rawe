wayland_scanner = find_program('wayland-scanner')

if wayland_server.version().version_compare('>=1.14.91')
	code_type = 'private-code'
else
	code_type = 'code'
endif

wayland_scanner_code = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.c',
	arguments: [code_type, '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
	wayland_scanner,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

client_protos_src = wayland_scanner_code.process('idle.xml')
client_protos_headers = wayland_scanner_client.process('idle.xml')

lib_client_protos = static_library(
	'client-protos',
	[client_protos_src, client_protos_headers],
	dependencies: [wayland_client]
)

client_protos = declare_dependency(
	link_with: lib_client_protos,
	sources: client_protos_headers,
)

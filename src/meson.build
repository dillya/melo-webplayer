# Melo web player module

# Module configuration
cdata = configuration_data()
cdata.set_quoted(
	'MELO_YOUTUBE_BROWSER_API_KEY',
	get_option('youtube_api_key'),
	description : 'Youtube API key')
configure_file(output : 'config.h', configuration : cdata)

# Module sources
src = [
	'melo_youtube_browser.c',
	'melo_webplayer_player.c',
	'melo_webplayer.c'
]

# Library dependencies
libmelo_dep = dependency('melo', version : '>=1.0.0')
libmelo_proto_dep = dependency('melo_proto', version : '>=1.0.0')
libpython3_dep = dependency('python3', version : '>=3.3.0')

# Generate module
shared_library(
	'melo_webplayer',
	src,
	dependencies : [libmelo_dep, libmelo_proto_dep, libpython3_dep],
	version : meson.project_version(),
	install : true,
	install_dir : libmelo_dep.get_pkgconfig_variable('moduledir'))

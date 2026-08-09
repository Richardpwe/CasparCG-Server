cmake_minimum_required (VERSION 3.28)
project (html)

set(SOURCES
		producer/html_cg_proxy.cpp
		producer/html_cg_proxy.h
		producer/html_producer.cpp
		producer/html_producer.h

		html.cpp
		html.h
)

casparcg_add_module_project(html
	SOURCES ${SOURCES}
	INIT_FUNCTION "html::init"
)
target_link_libraries(html PRIVATE ffmpeg web CEF::CEF)

target_include_directories(html PRIVATE
    ..
	${FFMPEG_INCLUDE_PATH}
)

set_target_properties(html PROPERTIES FOLDER modules)
source_group(sources\\producer producer/*)
source_group(sources ./*)

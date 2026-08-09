cmake_minimum_required (VERSION 3.28)
project (web)

set(SOURCES
		producer/web_producer.cpp
		producer/web_producer.h

		web.cpp
		web.h
)

casparcg_add_library(web_policy
	SOURCES
		producer/request_policy.cpp
		producer/request_policy.h
)
target_include_directories(web_policy
	PUBLIC ..
	PRIVATE ${CMAKE_SOURCE_DIR}
)

casparcg_add_module_project(web
	SOURCES ${SOURCES}
	INIT_FUNCTION "web::init"
	UNINIT_FUNCTION "web::uninit"
	CLI_INTERCEPTOR "web::intercept_command_line"
)
target_link_libraries(web PRIVATE ffmpeg web_policy CEF::CEF)

target_include_directories(web PRIVATE
	..
	${FFMPEG_INCLUDE_PATH}
)

set_target_properties(web PROPERTIES FOLDER modules)
set_target_properties(web_policy PROPERTIES FOLDER modules)
source_group(sources\\producer producer/*)
source_group(sources ./*)

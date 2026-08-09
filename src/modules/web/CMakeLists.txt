cmake_minimum_required (VERSION 3.28)
project (web)

set(SOURCES
		web.cpp
		web.h
)

casparcg_add_module_project(web
	SOURCES ${SOURCES}
	INIT_FUNCTION "web::init"
	UNINIT_FUNCTION "web::uninit"
)
target_link_libraries(web PRIVATE CEF::CEF)

set_target_properties(web PROPERTIES FOLDER modules)
source_group(sources ./*)

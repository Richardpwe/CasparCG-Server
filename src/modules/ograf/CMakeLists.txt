cmake_minimum_required(VERSION 3.28)
project(ograf)

set(SCHEMA_HEADERS
	ograf_schema_graphics.h
	ograf_schema_action.h
	ograf_schema_boolean_constraint.h
	ograf_schema_number_constraint.h
	ograf_schema_string_constraint.h
	ograf_schema_gdd_basic_types.h
	ograf_schema_gdd_types.h
	ograf_schema_gdd_object.h
	ograf_schema_gdd_playout_options.h
)

bin2c("spec/v1/json-schemas/graphics/schema.json" "ograf_schema_graphics.h" "caspar::ograf::schemas" "graphics")
bin2c("spec/v1/json-schemas/lib/action.json" "ograf_schema_action.h" "caspar::ograf::schemas" "action")
bin2c("spec/v1/json-schemas/lib/constraints/boolean.json" "ograf_schema_boolean_constraint.h" "caspar::ograf::schemas" "boolean_constraint")
bin2c("spec/v1/json-schemas/lib/constraints/number.json" "ograf_schema_number_constraint.h" "caspar::ograf::schemas" "number_constraint")
bin2c("spec/v1/json-schemas/lib/constraints/string.json" "ograf_schema_string_constraint.h" "caspar::ograf::schemas" "string_constraint")
bin2c("spec/v1/json-schemas/gdd/basic-types.json" "ograf_schema_gdd_basic_types.h" "caspar::ograf::schemas" "gdd_basic_types")
bin2c("spec/v1/json-schemas/gdd/gdd-types.json" "ograf_schema_gdd_types.h" "caspar::ograf::schemas" "gdd_types")
bin2c("spec/v1/json-schemas/gdd/object.json" "ograf_schema_gdd_object.h" "caspar::ograf::schemas" "gdd_object")
bin2c("spec/v1/json-schemas/gdd/playout-options.json" "ograf_schema_gdd_playout_options.h" "caspar::ograf::schemas" "gdd_playout_options")

set(MANIFEST_SOURCES
	manifest/manifest.cpp
	manifest/manifest.h
	manifest/registry.cpp
	manifest/registry.h
	manifest/schema_validator.cpp
	manifest/schema_validator.h

	boost_json.cpp
	${SCHEMA_HEADERS}
)

set(SOURCES
	host/ograf_host.html

	ograf_host.h
	ograf.cpp
	ograf.h
	producer/ograf_cg_proxy.cpp
	producer/ograf_cg_proxy.h
	runtime/ograf_producer.cpp
	runtime/ograf_producer.h
	service/graphics_service.cpp
	service/graphics_service.h
)

bin2c("host/ograf_host.html" "ograf_host.h" "caspar::ograf::host" "document")

casparcg_add_library(ograf_manifest
	SOURCES ${MANIFEST_SOURCES}
)
target_include_directories(ograf_manifest PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
)
target_compile_definitions(ograf_manifest PRIVATE BOOST_JSON_NO_LIB)

casparcg_add_library(ograf_bridge
	SOURCES
		runtime/action_parameters.cpp
		runtime/action_parameters.h
		runtime/action_dispatcher.cpp
		runtime/action_dispatcher.h
		runtime/graphic_runtime.cpp
		runtime/graphic_runtime.h
)
target_include_directories(ograf_bridge PRIVATE
	${CMAKE_SOURCE_DIR}
)
target_compile_definitions(ograf_bridge PRIVATE BOOST_JSON_NO_LIB)
target_link_libraries(ograf_bridge PRIVATE ograf_manifest)

casparcg_add_library(ograf_api
	SOURCES
		../../protocol/ograf/http_server.cpp
		../../protocol/ograf/http_server.h
		../../protocol/ograf/router.cpp
		../../protocol/ograf/router.h
)
target_include_directories(ograf_api PRIVATE
	${CMAKE_SOURCE_DIR}
)
target_compile_definitions(ograf_api PRIVATE BOOST_JSON_NO_LIB)
target_link_libraries(ograf_api PRIVATE ograf_bridge ograf_manifest)
if (MSVC)
	target_link_libraries(ograf_api PUBLIC Ws2_32.lib)
else ()
	target_link_libraries(ograf_api PUBLIC pthread)
endif ()

casparcg_add_module_project(ograf
	SOURCES ${SOURCES}
	INIT_FUNCTION "ograf::init"
	UNINIT_FUNCTION "ograf::uninit"
)
target_include_directories(ograf PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}
	..
)
target_link_libraries(ograf PRIVATE ograf_api ograf_bridge ograf_manifest web)

set_target_properties(ograf PROPERTIES FOLDER modules)
set_target_properties(ograf_api PROPERTIES FOLDER modules)
set_target_properties(ograf_manifest PROPERTIES FOLDER modules)
set_target_properties(ograf_bridge PROPERTIES FOLDER modules)
source_group(sources\\manifest manifest/*)
source_group(sources\\producer producer/*)
source_group(sources\\runtime runtime/*)
source_group(sources\\service service/*)
source_group(sources\\protocol ../../protocol/ograf/*)
source_group(sources\\spec ${SCHEMA_HEADERS})
source_group(sources ./*)

if (BUILD_TESTING)
	add_subdirectory(tests)
endif ()

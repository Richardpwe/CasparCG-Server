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
	ograf.cpp
	ograf.h
)

casparcg_add_library(ograf_manifest
	SOURCES ${MANIFEST_SOURCES}
)
target_include_directories(ograf_manifest PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
)
target_compile_definitions(ograf_manifest PRIVATE BOOST_JSON_NO_LIB)

casparcg_add_module_project(ograf
	SOURCES ${SOURCES}
	INIT_FUNCTION "ograf::init"
)
target_link_libraries(ograf PRIVATE ograf_manifest)

set_target_properties(ograf PROPERTIES FOLDER modules)
set_target_properties(ograf_manifest PROPERTIES FOLDER modules)
source_group(sources\\manifest manifest/*)
source_group(sources\\spec ${SCHEMA_HEADERS})
source_group(sources ./*)

if (BUILD_TESTING)
	add_subdirectory(tests)
endif ()

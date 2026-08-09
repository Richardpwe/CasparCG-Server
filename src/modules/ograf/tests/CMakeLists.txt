add_executable(ograf_manifest_tests
	manifest_test.cpp
	registry_test.cpp
	action_dispatcher_test.cpp
	graphic_runtime_test.cpp
	request_policy_test.cpp
)

target_compile_features(ograf_manifest_tests PRIVATE cxx_std_20)
target_compile_definitions(ograf_manifest_tests PRIVATE
	BOOST_JSON_NO_LIB
	OGRAF_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_include_directories(ograf_manifest_tests PRIVATE
	${CMAKE_SOURCE_DIR}
)
target_include_directories(ograf_manifest_tests SYSTEM PRIVATE
	${BOOST_INCLUDE_PATH}
)
target_link_libraries(ograf_manifest_tests PRIVATE ograf_bridge ograf_manifest web_policy)
casparcg_add_build_dependencies(ograf_manifest_tests)

set_target_properties(ograf_manifest_tests PROPERTIES FOLDER tests)
add_test(NAME ograf.manifest COMMAND ograf_manifest_tests)

find_program(NODE_EXECUTABLE NAMES node nodejs)
if (NODE_EXECUTABLE)
	add_test(
		NAME ograf.host
		COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/host_runtime_test.mjs
			${CMAKE_CURRENT_SOURCE_DIR}/../host/ograf_host.html
			${CMAKE_CURRENT_SOURCE_DIR}/fixtures
	)
endif ()

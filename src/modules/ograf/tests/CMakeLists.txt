add_executable(ograf_manifest_tests
	manifest_test.cpp
	registry_test.cpp
)

target_compile_features(ograf_manifest_tests PRIVATE cxx_std_20)
target_compile_definitions(ograf_manifest_tests PRIVATE BOOST_JSON_NO_LIB)
target_include_directories(ograf_manifest_tests PRIVATE
	${CMAKE_SOURCE_DIR}
)
target_include_directories(ograf_manifest_tests SYSTEM PRIVATE
	${BOOST_INCLUDE_PATH}
)
target_link_libraries(ograf_manifest_tests PRIVATE ograf_manifest)
casparcg_add_build_dependencies(ograf_manifest_tests)

set_target_properties(ograf_manifest_tests PROPERTIES FOLDER tests)
add_test(NAME ograf.manifest COMMAND ograf_manifest_tests)

cmake_minimum_required(VERSION 3.16)

if (WEB_OPTIONS_TEST_CHILD)
    include("${WEB_OPTIONS_MODULE}")

    foreach (option IN ITEMS WEB HTML OGRAF)
        if (NOT "${ENABLE_${option}}" STREQUAL "${EXPECTED_${option}}")
            message(FATAL_ERROR
                "${CASE_NAME}: expected ENABLE_${option}=${EXPECTED_${option}}, "
                "got ${ENABLE_${option}}")
        endif ()
    endforeach ()
    return()
endif ()

function(check_web_options case_name expected_web expected_html expected_ograf)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            ${ARGN}
            "-DWEB_OPTIONS_TEST_CHILD=ON"
            "-DWEB_OPTIONS_MODULE=${WEB_OPTIONS_MODULE}"
            "-DCASE_NAME=${case_name}"
            "-DEXPECTED_WEB=${expected_web}"
            "-DEXPECTED_HTML=${expected_html}"
            "-DEXPECTED_OGRAF=${expected_ograf}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)

    if (NOT result EQUAL 0)
        message(FATAL_ERROR
            "Build option case '${case_name}' failed.\n${output}${error}")
    endif ()
endfunction()

check_web_options(default ON ON ON)
check_web_options(html-only ON ON OFF
    -DENABLE_OGRAF=OFF)
check_web_options(ograf-only ON OFF ON
    -DENABLE_HTML=OFF
    -DENABLE_WEB=ON)
check_web_options(web-disabled OFF OFF OFF
    -DENABLE_WEB=OFF)
check_web_options(legacy-html-off OFF OFF OFF
    -DENABLE_HTML=OFF)
check_web_options(web-disabled-wins OFF OFF OFF
    -DENABLE_WEB=OFF
    -DENABLE_HTML=ON
    -DENABLE_OGRAF=ON)

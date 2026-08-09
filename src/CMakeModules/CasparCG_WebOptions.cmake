option(ENABLE_HTML "Enable classic HTML templates" ON)

# ENABLE_HTML used to control both the HTML producer and the CEF dependency.
# Derive the new shared web-renderer switch from it when configuring an
# existing build tree or when only the old switch is supplied.
if (NOT DEFINED ENABLE_WEB)
    set(ENABLE_WEB ${ENABLE_HTML} CACHE BOOL "Enable the shared CEF web renderer")
endif ()
option(ENABLE_WEB "Enable the shared CEF web renderer" ON)

if (NOT DEFINED ENABLE_OGRAF)
    set(ENABLE_OGRAF ${ENABLE_WEB} CACHE BOOL "Enable native OGraf graphics")
endif ()
option(ENABLE_OGRAF "Enable native OGraf graphics" ON)

if (NOT ENABLE_WEB)
    if (ENABLE_HTML OR ENABLE_OGRAF)
        message(STATUS "ENABLE_WEB is OFF; disabling HTML and OGraf modules")
    endif ()
    set(ENABLE_HTML OFF CACHE BOOL "Enable classic HTML templates" FORCE)
    set(ENABLE_OGRAF OFF CACHE BOOL "Enable native OGraf graphics" FORCE)
endif ()

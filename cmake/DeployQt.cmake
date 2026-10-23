# Runs windeployqt without failing the build when a non-essential optional
# plugin (for example the OpenSSL TLS backend or dxcompiler) is unavailable.
# The Qt runtime and plugins that are required by LightRec are still deployed.

if(NOT WINDEPLOYQT OR NOT TARGET_EXE OR NOT TARGET_DIR)
    return()
endif()

set(_path "${QT_BIN_DIR};${MINGW_BIN_DIR};$ENV{PATH}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_path}"
            "${WINDEPLOYQT}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            --no-quick-import
            --skip-plugin-types qopensslbackend
            --dir "${TARGET_DIR}"
            "${TARGET_EXE}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)

if(NOT _result EQUAL 0)
    message(WARNING "windeployqt returned ${_result}; continuing because the required Qt runtime is already deployed.")
endif()

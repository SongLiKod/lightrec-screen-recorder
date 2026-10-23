cmake_minimum_required(VERSION 3.21)

# Single source of truth for the application version. Update version.txt only;
# CMake, the EXE resource, the About UI, the updater, and packaging all read it.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" LIGHTREC_VERSION LIMIT_COUNT 1)
string(STRIP "${LIGHTREC_VERSION}" LIGHTREC_VERSION)
if(NOT LIGHTREC_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "version.txt must contain a semantic version such as 1.2.0")
endif()

project(LightRec
    VERSION ${LIGHTREC_VERSION}
    DESCRIPTION "LightRec - lightweight Windows screen recorder"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT WIN32)
    message(FATAL_ERROR "LightRec targets Windows 10 1903+ / Windows 11 only.")
endif()

enable_language(RC)

option(LIGHTREC_WITH_FFMPEG "Enable FFmpeg based recording and post processing" ON)
option(LIGHTREC_ALLOW_NO_FFMPEG "Allow a UI-only build when FFmpeg is missing" OFF)
option(LIGHTREC_ENABLE_WGC "Enable Windows Graphics Capture backend when C++/WinRT is available" ON)
option(LIGHTREC_BUILD_TESTS "Build the headless media pipeline self-test" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia MultimediaWidgets)
qt_standard_project_setup()

find_package(Qt6 QUIET COMPONENTS HttpServer)

# ---------------------------------------------------------------------------
# FFmpeg (shared build).  Put headers in third_party/ffmpeg/include and the
# import libraries in third_party/ffmpeg/lib, or set FFMPEG_ROOT.
# ---------------------------------------------------------------------------
set(FFMPEG_FOUND FALSE)
if(LIGHTREC_WITH_FFMPEG)
    set(FFMPEG_ROOT "" CACHE PATH "Root of the FFmpeg shared SDK (bundled vendor/ffmpeg by default)")
    if(NOT FFMPEG_ROOT)
        foreach(_candidate "${CMAKE_SOURCE_DIR}/vendor/ffmpeg"
                           "${CMAKE_SOURCE_DIR}/third_party/ffmpeg")
            if(EXISTS "${_candidate}/include/libavcodec/avcodec.h")
                set(FFMPEG_ROOT "${_candidate}")
                break()
            endif()
        endforeach()
    endif()
    set(FFMPEG_DLL_DIR "${FFMPEG_ROOT}/bin" CACHE PATH
        "Directory containing FFmpeg shared DLLs and ffmpeg.exe")
    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
        HINTS "${FFMPEG_ROOT}/include")
    find_library(FFMPEG_AVCODEC_LIBRARY NAMES avcodec
        HINTS "${FFMPEG_ROOT}/lib/mingw" "${FFMPEG_ROOT}/lib")
    find_library(FFMPEG_AVFORMAT_LIBRARY NAMES avformat
        HINTS "${FFMPEG_ROOT}/lib/mingw" "${FFMPEG_ROOT}/lib")
    find_library(FFMPEG_AVUTIL_LIBRARY NAMES avutil
        HINTS "${FFMPEG_ROOT}/lib/mingw" "${FFMPEG_ROOT}/lib")
    find_library(FFMPEG_SWSCALE_LIBRARY NAMES swscale
        HINTS "${FFMPEG_ROOT}/lib/mingw" "${FFMPEG_ROOT}/lib")
    find_library(FFMPEG_SWRESAMPLE_LIBRARY NAMES swresample
        HINTS "${FFMPEG_ROOT}/lib/mingw" "${FFMPEG_ROOT}/lib")

    if(FFMPEG_INCLUDE_DIR AND FFMPEG_AVCODEC_LIBRARY AND FFMPEG_AVFORMAT_LIBRARY
       AND FFMPEG_AVUTIL_LIBRARY AND FFMPEG_SWSCALE_LIBRARY AND FFMPEG_SWRESAMPLE_LIBRARY)
        set(FFMPEG_FOUND TRUE)
    elseif(NOT LIGHTREC_ALLOW_NO_FFMPEG)
        message(FATAL_ERROR
            "FFmpeg is a required dependency. Put a shared GPL FFmpeg build in "
            "'${CMAKE_SOURCE_DIR}/vendor/ffmpeg' (include/, lib/, bin/) or configure "
            "-DFFMPEG_ROOT=<sdk> -DFFMPEG_DLL_DIR=<sdk>/bin. "
            "Set -DLIGHTREC_ALLOW_NO_FFMPEG=ON only for UI-only development.")
    else()
        message(WARNING "FFmpeg SDK not found - building a UI-only development binary.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(LIGHTREC_SOURCES
    src/main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/lightrec.rc

    src/core/AppPaths.cpp
    src/core/ConfigStore.cpp
    src/core/ThemeManager.cpp
    src/core/HotkeyManager.cpp
    src/core/WindowUtils.cpp
    src/core/Logger.cpp
    src/core/SingleInstance.cpp

    src/capture/ScreenCapture.cpp
    src/capture/DxgiCapture.cpp
    src/capture/PrintWindowCapture.cpp

    src/audio/WasapiCapture.cpp
    src/audio/AudioProcessor.cpp

    src/media/MediaPipeline.cpp
    src/media/MediaProbe.cpp
    src/media/FFmpegTool.cpp
    src/media/FFmpegInstaller.cpp

    src/update/UpdateManager.cpp

    src/overlay/AnnotationModel.cpp
    src/overlay/OverlayCompositor.cpp
    src/overlay/OverlayController.cpp
    src/overlay/AnnotationOverlay.cpp
    src/overlay/AnnotationToolbar.cpp
    src/overlay/KeyDisplayWindow.cpp
    src/overlay/CameraWindow.cpp
    src/overlay/CameraManager.cpp
    src/overlay/InputHooks.cpp

    src/record/RecordingController.cpp
    src/record/RecordingJournal.cpp
    src/record/RecordingLibrary.cpp

    src/live/LiveServer.cpp
    src/live/QrCode.cpp
    src/live/LanPreview.cpp

    src/schedule/ScheduleManager.cpp
    src/preset/PresetManager.cpp
    src/virtualcam/VirtualCamera.cpp

    src/ui/MainWindow.cpp
    src/ui/CountdownWindow.cpp
    src/ui/CaptureBorderWindow.cpp
    src/ui/FloatingToolbar.cpp
    src/ui/FloatingRecordDot.cpp
    src/ui/RegionSelector.cpp
    src/ui/RecordPage.cpp
    src/ui/FilesPage.cpp
    src/ui/ToolboxPage.cpp
    src/ui/LivePage.cpp
    src/ui/TasksPage.cpp
    src/ui/PresetsPage.cpp
    src/ui/SettingsPage.cpp
    src/ui/PreviewDialog.cpp
    src/ui/Widgets.cpp
)

set(LIGHTREC_HEADERS
    src/core/Types.h
    src/core/AppPaths.h
    src/core/ConfigStore.h
    src/core/ThemeManager.h
    src/core/HotkeyManager.h
    src/core/WindowUtils.h
    src/core/Logger.h
    src/core/SingleInstance.h

    src/capture/ScreenCapture.h
    src/capture/DxgiCapture.h
    src/capture/PrintWindowCapture.h
    src/capture/WgcCapture.h

    src/audio/WasapiCapture.h
    src/audio/AudioProcessor.h

    src/media/FFmpegHeaders.h
    src/media/MediaPipeline.h
    src/media/MediaProbe.h
    src/media/FFmpegTool.h
    src/media/FFmpegInstaller.h

    src/update/UpdateManager.h

    src/overlay/AnnotationModel.h
    src/overlay/OverlayCompositor.h
    src/overlay/OverlayController.h
    src/overlay/AnnotationOverlay.h
    src/overlay/AnnotationToolbar.h
    src/overlay/KeyDisplayWindow.h
    src/overlay/CameraWindow.h
    src/overlay/CameraManager.h
    src/overlay/InputHooks.h

    src/record/RecordingController.h
    src/record/RecordingJournal.h
    src/record/RecordingLibrary.h

    src/live/LiveServer.h
    src/live/QrCode.h
    src/live/LanPreview.h

    src/schedule/ScheduleManager.h
    src/preset/PresetManager.h
    src/virtualcam/VirtualCamera.h

    src/ui/MainWindow.h
    src/ui/CountdownWindow.h
    src/ui/CaptureBorderWindow.h
    src/ui/FloatingToolbar.h
    src/ui/FloatingRecordDot.h
    src/ui/RegionSelector.h
    src/ui/RecordPage.h
    src/ui/FilesPage.h
    src/ui/ToolboxPage.h
    src/ui/LivePage.h
    src/ui/TasksPage.h
    src/ui/PresetsPage.h
    src/ui/SettingsPage.h
    src/ui/PreviewDialog.h
    src/ui/Widgets.h
)

if(LIGHTREC_ENABLE_WGC)
    # C++/WinRT headers are required for the WGC backend.  The MinGW build
    # simply falls back to DXGI Desktop Duplication.
    include(CheckIncludeFileCXX)
    check_include_file_cxx("winrt/Windows.Graphics.Capture.h" LIGHTREC_HAVE_CPPWINRT)
    if(LIGHTREC_HAVE_CPPWINRT)
        list(APPEND LIGHTREC_SOURCES src/capture/WgcCapture.cpp)
        set(LIGHTREC_WGC_ENABLED TRUE)
    endif()
endif()

qt_add_executable(LightRec WIN32 ${LIGHTREC_SOURCES} ${LIGHTREC_HEADERS})
# windres resolves the ICON path relative to the build directory.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/lightrec.ico
    ${CMAKE_CURRENT_BINARY_DIR}/lightrec.ico
    COPYONLY)

set(LIGHTREC_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${LIGHTREC_GENERATED_DIR}")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/lightrec_version.h.in
    ${LIGHTREC_GENERATED_DIR}/lightrec_version.h
    @ONLY)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/lightrec.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/lightrec.rc
    @ONLY)

qt_add_resources(LightRec "lightrec_assets"
    PREFIX "/"
    BASE "resources"
    FILES
        resources/qss/light.qss
        resources/qss/dark.qss
        resources/html/live.html
        resources/icons/app.svg
)

target_include_directories(LightRec PRIVATE src ${LIGHTREC_GENERATED_DIR})

target_compile_definitions(LightRec PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    QT_NO_CAST_FROM_ASCII
)

target_link_libraries(LightRec PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    d3d11 dxgi ole32 oleaut32 uuid dwmapi psapi shcore propsys
)

if(TARGET Qt6::HttpServer)
    target_compile_definitions(LightRec PRIVATE LIGHTREC_HAVE_HTTPSERVER)
    target_link_libraries(LightRec PRIVATE Qt6::HttpServer)
endif()

if(FFMPEG_FOUND)
    target_compile_definitions(LightRec PRIVATE LIGHTREC_HAVE_FFMPEG)
    target_include_directories(LightRec PRIVATE ${FFMPEG_INCLUDE_DIR})
    target_link_libraries(LightRec PRIVATE
        ${FFMPEG_AVFORMAT_LIBRARY}
        ${FFMPEG_AVCODEC_LIBRARY}
        ${FFMPEG_AVUTIL_LIBRARY}
        ${FFMPEG_SWSCALE_LIBRARY}
        ${FFMPEG_SWRESAMPLE_LIBRARY}
    )
endif()

if(LIGHTREC_WGC_ENABLED)
    target_compile_definitions(LightRec PRIVATE LIGHTREC_HAVE_WGC)
    target_link_libraries(LightRec PRIVATE windowsapp)
endif()

if(LIGHTREC_BUILD_TESTS AND FFMPEG_FOUND)
    qt_add_executable(LightRecMediaTest
        tests/MediaPipelineTest.cpp
        src/media/MediaPipeline.cpp
        src/media/MediaProbe.cpp)
    target_include_directories(LightRecMediaTest PRIVATE src ${FFMPEG_INCLUDE_DIR})
    target_compile_definitions(LightRecMediaTest PRIVATE LIGHTREC_HAVE_FFMPEG)
    target_link_libraries(LightRecMediaTest PRIVATE
        Qt6::Core Qt6::Gui
        ${FFMPEG_AVFORMAT_LIBRARY}
        ${FFMPEG_AVCODEC_LIBRARY}
        ${FFMPEG_AVUTIL_LIBRARY}
        ${FFMPEG_SWSCALE_LIBRARY}
        ${FFMPEG_SWRESAMPLE_LIBRARY})

    qt_add_executable(LightRecQrTest tests/QrCodeTest.cpp src/live/QrCode.cpp)
    target_include_directories(LightRecQrTest PRIVATE src)
    target_link_libraries(LightRecQrTest PRIVATE Qt6::Core Qt6::Gui)

    qt_add_executable(LightRecFileDialogTest tests/FileDialogTest.cpp src/ui/Widgets.cpp)
    target_include_directories(LightRecFileDialogTest PRIVATE src)
    target_link_libraries(LightRecFileDialogTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)

    qt_add_executable(LightRecWatermarkTest
        tests/WatermarkTest.cpp
        src/core/AppPaths.cpp
        src/media/FFmpegTool.cpp
    )
    target_include_directories(LightRecWatermarkTest PRIVATE src)
    target_link_libraries(LightRecWatermarkTest PRIVATE Qt6::Core Qt6::Gui)

    qt_add_executable(LightRecUpdateManagerTest
        tests/UpdateManagerTest.cpp
        src/update/UpdateManager.cpp
    )
    target_include_directories(LightRecUpdateManagerTest PRIVATE src ${LIGHTREC_GENERATED_DIR})
    target_link_libraries(LightRecUpdateManagerTest PRIVATE Qt6::Core Qt6::Network)

    qt_add_executable(LightRecPreviewDialogTest
        tests/PreviewDialogTest.cpp
        src/ui/PreviewDialog.cpp
        src/ui/Widgets.cpp
    )
    target_include_directories(LightRecPreviewDialogTest PRIVATE src)
    target_link_libraries(LightRecPreviewDialogTest PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Test)

    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(LightRecAnnotationTest
        tests/AnnotationOverlayTest.cpp
        src/overlay/AnnotationOverlay.cpp
        src/overlay/AnnotationModel.cpp
        src/overlay/AnnotationToolbar.cpp
        src/overlay/OverlayCompositor.cpp
        src/core/WindowUtils.cpp)
    target_include_directories(LightRecAnnotationTest PRIVATE src)
    target_link_libraries(LightRecAnnotationTest PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
        dwmapi psapi shell32 ole32)
endif()

if(MSVC)
    target_compile_options(LightRec PRIVATE /W3 /utf-8 /permissive-)
else()
    target_compile_options(LightRec PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Make developer builds directly runnable: deploy Qt DLLs/plugins and the
# MinGW runtime next to LightRec.exe.  This avoids the common
# "找不到 Qt6Gui.dll" startup error when running the executable from the build
# directory without a Qt environment.
# ---------------------------------------------------------------------------
get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
if(_qt_qmake)
    get_filename_component(QT_BIN_DIR "${_qt_qmake}" DIRECTORY)
endif()
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET LightRec POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DWINDEPLOYQT=${WINDEPLOYQT_EXECUTABLE}"
                "-DTARGET_EXE=$<TARGET_FILE:LightRec>"
                "-DTARGET_DIR=$<TARGET_FILE_DIR:LightRec>"
                "-DQT_BIN_DIR=${QT_BIN_DIR}"
                "-DMINGW_BIN_DIR=${MINGW_BIN_DIR}"
                -P "${CMAKE_SOURCE_DIR}/cmake/DeployQt.cmake"
        COMMENT "Deploying Qt runtime with windeployqt")
endif()

if(WIN32)
    foreach(_runtime libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        if(EXISTS "${MINGW_BIN_DIR}/${_runtime}")
            add_custom_command(TARGET LightRec POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MINGW_BIN_DIR}/${_runtime}" "$<TARGET_FILE_DIR:LightRec>")
        endif()
    endforeach()
endif()

set(FFMPEG_DLL_DIR "" CACHE PATH "Directory containing FFmpeg shared DLLs and ffmpeg.exe")
if(FFMPEG_DLL_DIR)
    file(GLOB _ffmpeg_runtime_dlls
        "${FFMPEG_DLL_DIR}/avcodec*.dll"
        "${FFMPEG_DLL_DIR}/avdevice*.dll"
        "${FFMPEG_DLL_DIR}/avfilter*.dll"
        "${FFMPEG_DLL_DIR}/avformat*.dll"
        "${FFMPEG_DLL_DIR}/avutil*.dll"
        "${FFMPEG_DLL_DIR}/postproc*.dll"
        "${FFMPEG_DLL_DIR}/swscale*.dll"
        "${FFMPEG_DLL_DIR}/swresample*.dll")
    foreach(_dll ${_ffmpeg_runtime_dlls})
        add_custom_command(TARGET LightRec POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dll}" "$<TARGET_FILE_DIR:LightRec>")
    endforeach()
    if(EXISTS "${FFMPEG_DLL_DIR}/ffmpeg.exe")
        add_custom_command(TARGET LightRec POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${FFMPEG_DLL_DIR}/ffmpeg.exe" "$<TARGET_FILE_DIR:LightRec>")
    endif()
endif()

install(TARGETS LightRec RUNTIME DESTINATION .)
install(DIRECTORY resources/ DESTINATION resources)

if(FFMPEG_FOUND AND FFMPEG_DLL_DIR)
    file(GLOB _ffmpeg_install_dlls
        "${FFMPEG_DLL_DIR}/avcodec*.dll"
        "${FFMPEG_DLL_DIR}/avdevice*.dll"
        "${FFMPEG_DLL_DIR}/avfilter*.dll"
        "${FFMPEG_DLL_DIR}/avformat*.dll"
        "${FFMPEG_DLL_DIR}/avutil*.dll"
        "${FFMPEG_DLL_DIR}/postproc*.dll"
        "${FFMPEG_DLL_DIR}/swscale*.dll"
        "${FFMPEG_DLL_DIR}/swresample*.dll")
    if(_ffmpeg_install_dlls)
        install(FILES ${_ffmpeg_install_dlls} DESTINATION .)
    endif()
    if(EXISTS "${FFMPEG_DLL_DIR}/ffmpeg.exe")
        install(FILES "${FFMPEG_DLL_DIR}/ffmpeg.exe" DESTINATION .)
    endif()
endif()

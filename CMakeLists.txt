cmake_minimum_required(VERSION 3.20)

project(Vertex VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_TAG_RESULT
)

if (GIT_TAG_RESULT EQUAL 0 AND GIT_TAG MATCHES "^v?(.+)$")
    string(REGEX REPLACE "^v" "" VERTEX_VERSION "${GIT_TAG}")
else()
    set(VERTEX_VERSION "0.0.0")
endif()

message(STATUS "Vertex version: ${VERTEX_VERSION}")

add_compile_definitions(VERTEX_VERSION="${VERTEX_VERSION}")

if (MSVC)
    string(REGEX REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/EHs" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    add_compile_options(/EHa /utf-8 /FS)
endif()

if (UNIX AND NOT APPLE)
    include(FetchContent)

    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG v3.6.2
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(mbedtls)
endif()

add_library(vertex_engine STATIC
    src/codec/inflate.cpp
    src/codec/crc32.cpp
    src/codec/sha256.cpp
    src/codec/png.cpp
    src/codec/jpeg.cpp
    src/codec/webp.cpp

    src/render/rasterizer.cpp

    src/font/truetype.cpp
    src/font/font_face.cpp

    src/network/fetcher.cpp
    src/network/resource_cache.cpp
    src/network/text_decode.cpp
    src/network/url.cpp
    src/network/websocket.cpp
    src/network/socket.cpp
    src/network/arti_client.cpp
    src/network/http_client.cpp
    src/network/tls_windows.cpp
    src/network/tls_linux.cpp
    src/network/tls_macos.cpp

    src/platform/downloads.cpp
    src/platform/profile.cpp

    src/html/tokenizer.cpp
    src/html/parser.cpp
    src/html/resources.cpp
    src/html/serializer.cpp

    src/css/stylesheet.cpp

    src/layout/layout.cpp
    src/layout/layout_engine.cpp

    src/paint/display_list.cpp

    src/js/gc.cpp
    src/js/lexer.cpp
    src/js/parser.cpp
    src/js/compiler.cpp
    src/js/vm.cpp
    src/js/runtime.cpp
    src/js/dom_bridge.cpp
    src/js/canvas_bridge.cpp
    src/js/engine.cpp
)

target_include_directories(vertex_engine PUBLIC "${CMAKE_SOURCE_DIR}/src")

if (WIN32)
    target_compile_definitions(vertex_engine PUBLIC
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    target_link_libraries(vertex_engine PUBLIC
        secur32
        crypt32
        ws2_32
    )
elseif (APPLE)
    target_link_libraries(vertex_engine PUBLIC
        "-framework Security"
        "-framework CoreFoundation"
    )
elseif (UNIX)
    target_link_libraries(vertex_engine PUBLIC
        mbedtls
        mbedx509
        mbedcrypto
        ${CMAKE_DL_LIBS}
    )
endif()

option(VERTEX_BUILD_ARTI "Build the embedded Arti bridge" OFF)

find_program(CARGO_EXECUTABLE cargo)

if (VERTEX_BUILD_ARTI AND CARGO_EXECUTABLE)
    if (WIN32)
        set(VERTEX_ARTI_LIB_NAME "vertex_arti.dll")
    elseif (APPLE)
        set(VERTEX_ARTI_LIB_NAME "libvertex_arti.dylib")
    else()
        set(VERTEX_ARTI_LIB_NAME "libvertex_arti.so")
    endif()

    add_custom_target(vertex_arti
        COMMAND "${CARGO_EXECUTABLE}" build --release
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/vertex_arti/target/release/${VERTEX_ARTI_LIB_NAME}"
            "${CMAKE_BINARY_DIR}/$<CONFIG>/${VERTEX_ARTI_LIB_NAME}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/vertex_arti"
        COMMENT "Building embedded Arti bridge"
        VERBATIM
    )
elseif (VERTEX_BUILD_ARTI)
    message(WARNING
        "VERTEX_BUILD_ARTI=ON, but cargo was not found. "
        "The embedded Arti bridge will not be built."
    )
else()
    message(STATUS
        "Embedded Arti bridge disabled. "
        "Set VERTEX_BUILD_ARTI=ON to enable it."
    )
endif()

if (WIN32)
    add_executable(Vertex WIN32
        src/main.cpp
        src/platform/vertex.rc
        src/render/renderer.cpp
        src/render/box_paint.cpp
        src/render/canvas_renderer.cpp
        src/platform/media_win32.cpp
        src/platform/platform_win32.cpp
    )

    target_link_libraries(Vertex PRIVATE
        vertex_engine
        d2d1
        dwrite
        comctl32
        ole32
        strmiids
    )

    if (MSVC)
        target_link_options(Vertex PRIVATE /STACK:67108864)
    endif()
elseif (APPLE)
    set_source_files_properties(
        src/platform/vertex_icon.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
    )

    set_source_files_properties(
        src/platform/vertex_icon_light.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
    )

    add_executable(Vertex MACOSX_BUNDLE
        src/platform/main_macos.mm
        src/platform/platform_macos.mm
        src/platform/media_macos.mm
        src/render/canvas_coregraphics.cpp
        src/platform/vertex_icon.icns
        src/platform/vertex_icon_light.icns
    )

    target_link_libraries(Vertex PRIVATE
        vertex_engine
        "-framework Cocoa"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework CoreText"
    )

    set_target_properties(Vertex PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.vertex.browser"
        MACOSX_BUNDLE_BUNDLE_NAME "Vertex"
        MACOSX_BUNDLE_ICON_FILE "vertex_icon.icns"
    )
elseif (UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED xcb)

    add_executable(Vertex
        src/platform/main_linux.cpp
        src/platform/platform_linux.cpp
        src/platform/media_linux.cpp
        src/render/canvas_raster.cpp
    )

    target_include_directories(Vertex PRIVATE ${XCB_INCLUDE_DIRS})
    target_compile_options(Vertex PRIVATE ${XCB_CFLAGS_OTHER})

    target_link_libraries(Vertex PRIVATE
        vertex_engine
        ${XCB_LIBRARIES}
    )

    add_custom_command(TARGET Vertex POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/platform/vertex_icon.png"
            "$<TARGET_FILE_DIR:Vertex>/vertex_icon.png"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/platform/vertex_icon_light.png"
            "$<TARGET_FILE_DIR:Vertex>/vertex_icon_light.png"
    )
endif()

if (TARGET vertex_arti)
    add_dependencies(Vertex vertex_arti)
endif()

add_executable(VertexUpdater
    src/platform/updater_helper.cpp
)

add_executable(vertex-tests
    tests/main.cpp
    tests/fixture.cpp
    tests/html_tests.cpp
    tests/css_tests.cpp
    tests/layout_tests.cpp
    tests/paint_tests.cpp
    tests/js_tests.cpp
    tests/network_tests.cpp
    tests/codec_tests.cpp
)

target_include_directories(vertex-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/tests"
)

target_link_libraries(vertex-tests PRIVATE vertex_engine)

if (WIN32)
    target_link_libraries(vertex-tests PRIVATE ws2_32)
endif()

add_executable(vertex-layout-engine-tests
    tests/layout_engine_main.cpp
    tests/fixture.cpp
    tests/layout_engine_tests.cpp
)

target_include_directories(vertex-layout-engine-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/tests"
)

target_link_libraries(vertex-layout-engine-tests PRIVATE vertex_engine)

add_executable(check_title tools/check_title.cpp)
target_link_libraries(check_title PRIVATE vertex_engine)

add_executable(debug_title tools/debug_title.cpp)
target_link_libraries(debug_title PRIVATE vertex_engine)

add_executable(dump_js tools/dump_js.cpp)
target_link_libraries(dump_js PRIVATE vertex_engine)

add_executable(dump_layout tools/dump_layout.cpp)
target_link_libraries(dump_layout PRIVATE vertex_engine)

add_executable(render_probe tools/render_probe.cpp)
target_link_libraries(render_probe PRIVATE vertex_engine)

add_executable(snapshot_svg tools/snapshot_svg.cpp)
target_link_libraries(snapshot_svg PRIVATE vertex_engine)

add_executable(test_url tools/test_url.cpp)
target_link_libraries(test_url PRIVATE vertex_engine)

add_executable(site_probe tools/site_probe.cpp)
target_link_libraries(site_probe PRIVATE vertex_engine)

if (WIN32)
    target_link_libraries(site_probe PRIVATE dbghelp)
endif()

if (MSVC)
    target_link_options(dump_js PRIVATE /DYNAMICBASE:NO /MAP)
endif()

option(VERTEX_BUILD_FUZZERS "Build libFuzzer harnesses" OFF)

if (VERTEX_BUILD_FUZZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_executable(fuzz_js_lexer tests/fuzz/fuzz_js_lexer.cpp)
    add_executable(fuzz_html_tokenizer tests/fuzz/fuzz_html_tokenizer.cpp)
    add_executable(fuzz_css_stylesheet tests/fuzz/fuzz_css_stylesheet.cpp)
    add_executable(fuzz_url tests/fuzz/fuzz_url.cpp)

    foreach(fuzzer
        fuzz_js_lexer
        fuzz_html_tokenizer
        fuzz_css_stylesheet
        fuzz_url
    )
        target_include_directories(${fuzzer} PRIVATE
            "${CMAKE_SOURCE_DIR}/src"
        )

        target_link_libraries(${fuzzer} PRIVATE vertex_engine)

        target_compile_options(${fuzzer} PRIVATE
            -fsanitize=fuzzer,address,undefined
            -fno-omit-frame-pointer
        )

        target_link_options(${fuzzer} PRIVATE
            -fsanitize=fuzzer,address,undefined
        )
    endforeach()
endif()

set(VERTEX_RUNTIME_TARGETS
    Vertex
    VertexUpdater
    vertex-tests
    vertex-layout-engine-tests
    check_title
    debug_title
    dump_js
    dump_layout
    render_probe
    snapshot_svg
    test_url
    site_probe
)

set_target_properties(${VERTEX_RUNTIME_TARGETS} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

install(TARGETS Vertex
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)

if (VERTEX_BUILD_ARTI AND CARGO_EXECUTABLE)
    if (APPLE)
        install(CODE "
            set(_arti \"${CMAKE_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/${VERTEX_ARTI_LIB_NAME}\")
            if (EXISTS \"\${_arti}\")
                file(
                    INSTALL
                    DESTINATION \"\${CMAKE_INSTALL_PREFIX}/Vertex.app/Contents/MacOS\"
                    TYPE FILE
                    FILES \"\${_arti}\"
                )
            endif()
        ")
    else()
        install(CODE "
            set(_arti \"${CMAKE_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/${VERTEX_ARTI_LIB_NAME}\")
            if (EXISTS \"\${_arti}\")
                file(
                    INSTALL
                    DESTINATION \"\${CMAKE_INSTALL_PREFIX}\"
                    TYPE FILE
                    FILES \"\${_arti}\"
                )
            endif()
        ")
    endif()
endif()

if (APPLE)
    install(TARGETS VertexUpdater
        RUNTIME DESTINATION "Vertex.app/Contents/MacOS"
    )
else()
    install(TARGETS VertexUpdater
        RUNTIME DESTINATION .
    )
endif()

if (UNIX AND NOT APPLE)
    install(FILES
        src/platform/vertex_icon.png
        src/platform/vertex_icon_light.png
        DESTINATION .
    )
endif()

set(CPACK_PACKAGE_NAME "Vertex")
set(CPACK_PACKAGE_VENDOR "hackclubium")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "A from-scratch browser engine and native browser shell."
)
set(CPACK_PACKAGE_VERSION "${VERTEX_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Vertex")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME
    "Vertex-${VERTEX_VERSION}-${CMAKE_SYSTEM_NAME}"
)

if (WIN32)
    find_program(MAKENSIS_EXECUTABLE makensis
        HINTS
            "C:/Program Files (x86)/NSIS"
            "C:/Program Files/NSIS"
    )

    set(VERTEX_ICON_ICO "${CMAKE_SOURCE_DIR}/src/platform/vertex_icon.ico")
    set(VERTEX_LICENSE_FILE "${CMAKE_SOURCE_DIR}/LICENSE")

    configure_file(
        "${CMAKE_SOURCE_DIR}/installer/windows/vertex.nsi.in"
        "${CMAKE_BINARY_DIR}/vertex.nsi"
        @ONLY
    )

    if (MAKENSIS_EXECUTABLE)
        set(VERTEX_INSTALLER_OUTPUT
            "${CMAKE_BINARY_DIR}/Vertex-${VERTEX_VERSION}-windows-installer.exe"
        )

        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" VERTEX_BUILD_DIR_NATIVE)
        file(TO_NATIVE_PATH
            "${VERTEX_INSTALLER_OUTPUT}"
            VERTEX_INSTALLER_OUTPUT_NATIVE
        )

        set(VERTEX_NSIS_EXTRA_DEFINES)
        set(VERTEX_INSTALLER_DEPS)

        if (VERTEX_BUILD_ARTI AND CARGO_EXECUTABLE)
            list(APPEND VERTEX_NSIS_EXTRA_DEFINES
                "/DVERTEX_ARTI_LIB_PATH=${VERTEX_BUILD_DIR_NATIVE}\\$<CONFIG>\\${VERTEX_ARTI_LIB_NAME}"
            )
            list(APPEND VERTEX_INSTALLER_DEPS vertex_arti)
        endif()

        add_custom_command(
            OUTPUT "${VERTEX_INSTALLER_OUTPUT}"
            COMMAND "${MAKENSIS_EXECUTABLE}"
                "/DVERTEX_EXE_PATH=${VERTEX_BUILD_DIR_NATIVE}\\$<CONFIG>\\Vertex.exe"
                "/DVERTEX_UPDATER_EXE_PATH=${VERTEX_BUILD_DIR_NATIVE}\\$<CONFIG>\\VertexUpdater.exe"
                "/DVERTEX_INSTALLER_OUTPUT=${VERTEX_INSTALLER_OUTPUT_NATIVE}"
                ${VERTEX_NSIS_EXTRA_DEFINES}
                "${CMAKE_BINARY_DIR}/vertex.nsi"
            DEPENDS
                Vertex
                VertexUpdater
                ${VERTEX_INSTALLER_DEPS}
                "${CMAKE_BINARY_DIR}/vertex.nsi"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Building Vertex Windows installer with NSIS"
            VERBATIM
        )

        add_custom_target(windows_installer
            DEPENDS "${VERTEX_INSTALLER_OUTPUT}"
        )
    else()
        message(STATUS
            "makensis not found — the windows_installer target is unavailable."
        )
    endif()
elseif (APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

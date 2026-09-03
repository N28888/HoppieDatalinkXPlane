cmake_minimum_required(VERSION 3.24)

project(HoppieDatalinkXP VERSION 0.1.6 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(HOPPIE_BUILD_PLUGIN "Build the X-Plane plugin" ON)
set(HOPPIE_DEPENDENCY_CACHE "" CACHE PATH "Optional directory containing pinned dependency archives")
set(HOPPIE_XPLANE_SDK_ROOT "" CACHE PATH "Existing X-Plane SDK root containing CHeaders and Libraries")
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures" FORCE)
endif()

function(hoppie_dependency_url out_var filename remote_url)
    if(HOPPIE_DEPENDENCY_CACHE AND EXISTS "${HOPPIE_DEPENDENCY_CACHE}/${filename}")
        set(${out_var} "${HOPPIE_DEPENDENCY_CACHE}/${filename}" PARENT_SCOPE)
    else()
        set(${out_var} "${remote_url}" PARENT_SCOPE)
    endif()
endfunction()

hoppie_dependency_url(JSON_URL "json-v3.11.3.tar.gz"
    "https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz")
FetchContent_Declare(nlohmann_json
    URL "${JSON_URL}"
    URL_HASH SHA256=0D8EF5AF7F9794E3263480193C491549B2BA6CC74BB018906202ADA498A79406
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

add_library(hoppie_core STATIC src/core.cpp)
target_include_directories(hoppie_core PUBLIC include)
target_compile_features(hoppie_core PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(hoppie_core PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(hoppie_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(hoppie_datasources STATIC src/datasources.cpp)
target_include_directories(hoppie_datasources PUBLIC include)
target_link_libraries(hoppie_datasources PUBLIC hoppie_core nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(hoppie_datasources PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(hoppie_datasources PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(hoppie_core_tests tests/core_tests.cpp)
    target_link_libraries(hoppie_core_tests PRIVATE hoppie_core)
    if(MSVC)
        target_compile_options(hoppie_core_tests PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(hoppie_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME core COMMAND hoppie_core_tests)

    add_executable(hoppie_datasource_tests tests/datasource_tests.cpp)
    target_link_libraries(hoppie_datasource_tests PRIVATE hoppie_datasources)
    if(MSVC)
        target_compile_options(hoppie_datasource_tests PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(hoppie_datasource_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME datasources COMMAND hoppie_datasource_tests)

    add_executable(hoppie_network_tests tests/network_tests.cpp src/network.cpp)
    target_include_directories(hoppie_network_tests PRIVATE include)
    if(MSVC)
        target_compile_options(hoppie_network_tests PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(hoppie_network_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME network-policy COMMAND hoppie_network_tests)

    add_executable(hoppie_ui_geometry_tests tests/ui_geometry_tests.cpp)
    target_include_directories(hoppie_ui_geometry_tests PRIVATE include)
    if(MSVC)
        target_compile_options(hoppie_ui_geometry_tests PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(hoppie_ui_geometry_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ui-geometry COMMAND hoppie_ui_geometry_tests)

    add_executable(hoppie_application_tests tests/application_tests.cpp src/application.cpp src/network.cpp)
    target_link_libraries(hoppie_application_tests PRIVATE hoppie_datasources)
    if(MSVC)
        target_compile_options(hoppie_application_tests PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(hoppie_application_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME application COMMAND hoppie_application_tests)
endif()

if(HOPPIE_BUILD_PLUGIN)
    if(NOT WIN32 AND NOT APPLE)
        message(FATAL_ERROR "HoppieDatalinkXP intentionally supports only Windows and macOS")
    endif()

    hoppie_dependency_url(IMGUI_URL "imgui-v1.91.8.tar.gz"
        "https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.tar.gz")
    FetchContent_Declare(imgui_source
        URL "${IMGUI_URL}"
        URL_HASH SHA256=DB3A2E02BFD6C269ADF0968950573053D002F40BDFB9EF2E4A90BCE804B0F286
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(imgui_source)
    add_library(hoppie_imgui STATIC
        "${imgui_source_SOURCE_DIR}/imgui.cpp"
        "${imgui_source_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_source_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_source_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_source_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
        "${imgui_source_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp")
    target_include_directories(hoppie_imgui PUBLIC
        "${imgui_source_SOURCE_DIR}"
        "${imgui_source_SOURCE_DIR}/backends")
    set_target_properties(hoppie_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if(BUILD_TESTING)
        add_executable(hoppie_message_ui_tests tests/message_ui_tests.cpp)
        target_link_libraries(hoppie_message_ui_tests PRIVATE hoppie_core hoppie_imgui)
        if(MSVC)
            target_compile_options(hoppie_message_ui_tests PRIVATE /W4 /utf-8 /permissive-)
        else()
            target_compile_options(hoppie_message_ui_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME message-ui COMMAND hoppie_message_ui_tests
            "${CMAKE_BINARY_DIR}/resources/NotoSansCJKsc-Regular.otf")
    endif()

    if(HOPPIE_XPLANE_SDK_ROOT)
        set(XPLANE_SDK_ROOT "${HOPPIE_XPLANE_SDK_ROOT}")
    else()
        hoppie_dependency_url(XPSDK_URL "XPSDK430.zip"
            "https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK430.zip")
        FetchContent_Declare(xplane_sdk
            URL "${XPSDK_URL}"
            URL_HASH SHA256=B9875AB27B593927B4F9B3E0DDFFFE7401EE5DCE6D86B50AEA0DA65F70FF7816
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(xplane_sdk)
        set(XPLANE_SDK_ROOT "${xplane_sdk_SOURCE_DIR}")
    endif()

    set(FONT_NAME "NotoSansCJKsc-Regular.otf")
    set(FONT_SHA256 "2C76254F6FC379FDDFCE0A7E84FB5385BB135D3E399294F6EEB6680D0365B74B")
    set(FONT_OUTPUT "${CMAKE_BINARY_DIR}/resources/${FONT_NAME}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/resources")
    if(HOPPIE_DEPENDENCY_CACHE AND EXISTS "${HOPPIE_DEPENDENCY_CACHE}/${FONT_NAME}")
        file(COPY_FILE "${HOPPIE_DEPENDENCY_CACHE}/${FONT_NAME}" "${FONT_OUTPUT}" ONLY_IF_DIFFERENT)
    elseif(NOT EXISTS "${FONT_OUTPUT}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf"
            "${FONT_OUTPUT}"
            EXPECTED_HASH "SHA256=${FONT_SHA256}"
            TLS_VERIFY ON
            SHOW_PROGRESS)
    endif()
    file(SHA256 "${FONT_OUTPUT}" ACTUAL_FONT_SHA256)
    string(TOUPPER "${ACTUAL_FONT_SHA256}" ACTUAL_FONT_SHA256)
    if(NOT ACTUAL_FONT_SHA256 STREQUAL FONT_SHA256)
        message(FATAL_ERROR "Bundled font checksum mismatch")
    endif()

    set(PLUGIN_SOURCES
        src/application.cpp
        src/plugin.cpp
        src/ui.cpp
        src/network.cpp)
    if(WIN32)
        list(APPEND PLUGIN_SOURCES src/network_win.cpp src/credentials_win.cpp)
        set(PLATFORM_DIRECTORY win_x64)
    else()
        list(APPEND PLUGIN_SOURCES src/network_mac.mm src/credentials_mac.mm)
        set(PLATFORM_DIRECTORY mac_x64)
    endif()

    add_library(HoppieDatalinkXP MODULE ${PLUGIN_SOURCES})
    target_include_directories(HoppieDatalinkXP PRIVATE
        include
        "${XPLANE_SDK_ROOT}/CHeaders/XPLM")
    target_link_libraries(HoppieDatalinkXP PRIVATE
        hoppie_core hoppie_datasources hoppie_imgui nlohmann_json::nlohmann_json)
    target_compile_definitions(HoppieDatalinkXP PRIVATE
        XPLM400=1 XPLM303=1 XPLM301=1 XPLM300=1 XPLM210=1 XPLM200=1)
    target_compile_definitions(HoppieDatalinkXP PRIVATE
        HOPPIE_VERSION="${PROJECT_VERSION}"
        HOPPIE_FONT_RELATIVE_PATH="Resources/${FONT_NAME}")
    set_target_properties(HoppieDatalinkXP PROPERTIES
        PREFIX ""
        SUFFIX ".xpl"
        OUTPUT_NAME "HoppieDatalinkXP"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/HoppieDatalinkXP/${PLATFORM_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/HoppieDatalinkXP/${PLATFORM_DIRECTORY}")

    if(WIN32)
        target_compile_definitions(HoppieDatalinkXP PRIVATE IBM=1 APL=0 LIN=0 NOMINMAX)
        target_compile_options(HoppieDatalinkXP PRIVATE /W4 /utf-8 /permissive-)
        target_link_libraries(HoppieDatalinkXP PRIVATE
            "${XPLANE_SDK_ROOT}/Libraries/Win/XPLM_64.lib"
            winhttp.lib advapi32.lib opengl32.lib)
    else()
        target_compile_definitions(HoppieDatalinkXP PRIVATE IBM=0 APL=1 LIN=0 GL_SILENCE_DEPRECATION)
        target_compile_options(HoppieDatalinkXP PRIVATE -Wall -Wextra -Wpedantic
            $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        set_target_properties(HoppieDatalinkXP PROPERTIES OSX_ARCHITECTURES "x86_64;arm64")
        target_link_options(HoppieDatalinkXP PRIVATE
            "-F${XPLANE_SDK_ROOT}/Libraries/Mac")
        target_link_libraries(HoppieDatalinkXP PRIVATE
            "-framework XPLM" "-framework Foundation" "-framework Security" "-framework OpenGL")
        add_custom_command(TARGET HoppieDatalinkXP POST_BUILD
            COMMAND codesign --force --sign - "$<TARGET_FILE:HoppieDatalinkXP>"
            VERBATIM)
    endif()

    add_custom_command(TARGET HoppieDatalinkXP POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_BINARY_DIR}/HoppieDatalinkXP/Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FONT_OUTPUT}"
            "${CMAKE_BINARY_DIR}/HoppieDatalinkXP/Resources/${FONT_NAME}"
        VERBATIM)

    install(TARGETS HoppieDatalinkXP
        LIBRARY DESTINATION "HoppieDatalinkXP/${PLATFORM_DIRECTORY}"
        RUNTIME DESTINATION "HoppieDatalinkXP/${PLATFORM_DIRECTORY}")
    install(FILES "${FONT_OUTPUT}" DESTINATION "HoppieDatalinkXP/Resources")
    install(FILES LICENSE THIRD_PARTY_LICENSES.md README.md
        DESTINATION "HoppieDatalinkXP")
endif()

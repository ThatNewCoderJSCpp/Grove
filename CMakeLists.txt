cmake_minimum_required(VERSION 3.18)
project(grove VERSION 0.1.47 DESCRIPTION "Grove: HTML-style UI documents backed by Fizmo and Walnut" LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GROVE_BUILD_RUNTIME "Build grove_runtime, which opens documents in a Fizmo window" ON)
option(GROVE_WITH_WALNUT "Fetch Walnut so Grove can build <script lang=\"walnut\"> blocks" ON)
option(GROVE_BUILD_TESTS "Build the Grove tests" ON)
option(GROVE_PACKAGE "Build grove as a self-contained download (static C++ runtime) and set up CPack" OFF)
set(GROVE_FIZMO_TAG "v0.2.36" CACHE STRING "Fizmo git tag to fetch")
set(GROVE_WALNUT_TAG "v0.1.31" CACHE STRING "Walnut git tag to fetch")

include(FetchContent)
set(GROVE_FETCH_EXCLUDE "")
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
    list(APPEND GROVE_FETCH_EXCLUDE SYSTEM)
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    list(APPEND GROVE_FETCH_EXCLUDE EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(fizmo
    GIT_REPOSITORY https://github.com/ThatNewCoderJSCpp/Fizmo.git
    GIT_TAG        ${GROVE_FIZMO_TAG}
    GIT_SHALLOW    TRUE
    ${GROVE_FETCH_EXCLUDE})
FetchContent_MakeAvailable(fizmo)

if(GROVE_WITH_WALNUT)
    FetchContent_Declare(walnut
        GIT_REPOSITORY https://github.com/ThatNewCoderJSCpp/Walnut.git
        GIT_TAG        ${GROVE_WALNUT_TAG}
        GIT_SHALLOW    TRUE
        ${GROVE_FETCH_EXCLUDE})
    FetchContent_MakeAvailable(walnut)
endif()

set(GROVE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in" "${GROVE_GENERATED_DIR}/grove/version.hpp" @ONLY)

function(grove_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(grove_core STATIC
    Source/common/diagnostics.cpp
    Source/common/marks.cpp
    Source/markup/node.cpp
    Source/markup/events.cpp
    Source/markup/tags.cpp
    Source/markup/parser.cpp
    Source/bark/value.cpp
    Source/bark/stylesheet.cpp
    Source/bark/parser.cpp
    Source/bark/cascade.cpp
    Source/bark/properties.cpp
    Source/bark/actions.cpp
    Source/script/script.cpp
    Source/script/builder.cpp
    Source/script/loader.cpp
    Source/input/input.cpp
    Source/input/code_buffer.cpp
    Source/input/code_find.cpp
    Source/input/completion.cpp
    Source/input/keymap.cpp
    Source/input/code_highlight.cpp
    Source/native.cpp
    Source/math/math_block.cpp
    Source/api/grove_api.cpp
    Source/api/grove_bark_api.cpp
    Source/project.cpp
    Source/dom_tools.cpp
    Source/bundle.cpp
    Source/windowing.cpp
    Source/pages.cpp
    Source/app.cpp
    Source/highlight.cpp
    Source/terminal.cpp
    Source/canvas.cpp
    Source/math_input.cpp)
add_library(grove::core ALIAS grove_core)
target_compile_features(grove_core PUBLIC cxx_std_17)
target_include_directories(grove_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    "${GROVE_GENERATED_DIR}")
target_link_libraries(grove_core PUBLIC fizmo::headers ${CMAKE_DL_LIBS})
if(TARGET fizmo::string)
    target_link_libraries(grove_core PUBLIC fizmo::string)
else()
    target_link_libraries(grove_core PUBLIC fizmo::fizmo)
endif()
target_compile_definitions(grove_core PRIVATE "GROVE_SOURCE_INCLUDE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Include\"")
set_target_properties(grove_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
grove_warnings(grove_core)

if(GROVE_BUILD_RUNTIME)
    add_library(grove_runtime STATIC Source/runtime/runtime.cpp)
    add_library(grove::runtime ALIAS grove_runtime)
    target_link_libraries(grove_runtime PUBLIC grove_core)
    if(TARGET fizmo::graphics)
        target_link_libraries(grove_runtime PUBLIC fizmo::graphics)
    else()
        target_link_libraries(grove_runtime PUBLIC fizmo::fizmo)
    endif()
    grove_warnings(grove_runtime)
endif()

add_executable(grove Tools/grove.cpp)
target_link_libraries(grove PRIVATE grove_core)
if(GROVE_BUILD_RUNTIME)
    target_link_libraries(grove PRIVATE grove_runtime)
    target_compile_definitions(grove PRIVATE GROVE_HAS_RUNTIME)
endif()
if(GROVE_WITH_WALNUT AND TARGET walnut)
    add_dependencies(grove walnut)
    target_compile_definitions(grove PRIVATE "GROVE_WALNUT_EXECUTABLE=\"$<TARGET_FILE:walnut>\"")
    target_compile_definitions(grove_core PRIVATE "GROVE_WALNUT_EXECUTABLE=\"$<TARGET_FILE:walnut>\"")
endif()
grove_warnings(grove)

if(GROVE_BUILD_TESTS)
    enable_testing()
    add_executable(grove_tests Tests/grove_tests.cpp)
    target_link_libraries(grove_tests PRIVATE grove_core)
    target_compile_definitions(grove_tests PRIVATE "GROVE_EXAMPLES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Examples\"")
    grove_warnings(grove_tests)
    add_test(NAME grove_tests COMMAND grove_tests)
    add_test(NAME grove_check_hello COMMAND grove check "${CMAKE_CURRENT_SOURCE_DIR}/Examples/hello/hello.grove")
    add_test(NAME grove_check_inputs COMMAND grove check "${CMAKE_CURRENT_SOURCE_DIR}/Examples/inputs/inputs.grove")
    add_test(NAME grove_check_todo COMMAND grove check "${CMAKE_CURRENT_SOURCE_DIR}/Examples/todo/todo.grove")
    add_test(NAME grove_check_windows COMMAND grove check "${CMAKE_CURRENT_SOURCE_DIR}/Examples/windows/windows.grove")
    add_test(NAME grove_check_editor COMMAND grove check "${CMAKE_CURRENT_SOURCE_DIR}/Examples/editor/editor.grove")
    add_test(NAME grove_check_pages COMMAND grove check "${CMAKE_CURRENT_SOURCE_DIR}/Examples/pages/pages.grove")
    add_test(NAME grove_check_align COMMAND grove check "${CMAKE_CURRENT_SOURCE_DIR}/Examples/align/align.grove")
    add_test(NAME grove_check_bark COMMAND grove check "${CMAKE_CURRENT_SOURCE_DIR}/Examples/bark/bark.grove")
    add_test(NAME grove_exec_pages COMMAND grove exec "${CMAKE_CURRENT_SOURCE_DIR}/Examples/pages/pages.grove" --event "click #notes-tab" --event "click #starred-tab" --event "click #back-tab")
    set_tests_properties(grove_exec_pages PROPERTIES PASS_REGULAR_EXPRESSION "<page id=\"all-notes\" current")
    if(GROVE_WITH_WALNUT AND TARGET walnut)
        add_test(NAME grove_exec_todo COMMAND grove exec "${CMAKE_CURRENT_SOURCE_DIR}/Examples/todo/todo.grove" --event "click #clear-done")
        set_tests_properties(grove_exec_todo PROPERTIES
            ENVIRONMENT "GROVE_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/grove-cache"
            PASS_REGULAR_EXPRESSION "0 of 2 done")
        add_test(NAME grove_exec_editor COMMAND grove exec "${CMAKE_CURRENT_SOURCE_DIR}/Examples/editor/editor.grove" --event "click #docs")
        set_tests_properties(grove_exec_editor PROPERTIES
            ENVIRONMENT "GROVE_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/grove-cache"
            PASS_REGULAR_EXPRESSION "no host function named open_docs")
        add_test(NAME grove_exec_windows COMMAND grove exec "${CMAKE_CURRENT_SOURCE_DIR}/Examples/windows/windows.grove" --event "click #ask-age" --event "click .popup-ok")
        set_tests_properties(grove_exec_windows PROPERTIES
            ENVIRONMENT "GROVE_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/grove-cache"
            PASS_REGULAR_EXPRESSION "be 31")
        add_test(NAME grove_exec_bark COMMAND grove exec "${CMAKE_CURRENT_SOURCE_DIR}/Examples/bark/bark.grove" --event "click #greet" --event "click .popup-ok")
        set_tests_properties(grove_exec_bark PROPERTIES
            ENVIRONMENT "GROVE_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/grove-cache"
            PASS_REGULAR_EXPRESSION "hello Walnut, feeling fine; 2 lines from Grove")
        add_test(NAME grove_exec_bark_fail COMMAND grove exec "${CMAKE_CURRENT_SOURCE_DIR}/Examples/bark/bark.grove" --event "click #check")
        set_tests_properties(grove_exec_bark_fail PROPERTIES
            ENVIRONMENT "GROVE_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/grove-cache"
            PASS_REGULAR_EXPRESSION "checking everything; failed: everything is broken")
        if(GROVE_BUILD_RUNTIME)
            add_test(NAME grove_build_todo COMMAND grove build "${CMAKE_CURRENT_SOURCE_DIR}/Examples/todo/todo.grove" -o "${CMAKE_CURRENT_BINARY_DIR}/todo-app")
            set_tests_properties(grove_build_todo PROPERTIES
                ENVIRONMENT "GROVE_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/grove-cache"
                PASS_REGULAR_EXPRESSION "2 scripts")
        endif()
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS grove RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS grove_core ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(GROVE_BUILD_RUNTIME)
    install(TARGETS grove_runtime ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${GROVE_GENERATED_DIR}/grove/version.hpp" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/grove)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION docs)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Documentation/" DESTINATION docs)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Examples/" DESTINATION examples)

if(GROVE_PACKAGE)
    if(MSVC)
        message(FATAL_ERROR "GROVE_PACKAGE needs a GCC or Clang toolchain")
    endif()

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _grove_cpu)
    if(_grove_cpu MATCHES "^(x86_64|amd64|x64)$")
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(_grove_arch i686)
        else()
            set(_grove_arch x86_64)
        endif()
    elseif(_grove_cpu MATCHES "^(aarch64|arm64)$")
        set(_grove_arch arm64)
    elseif(_grove_cpu MATCHES "^(i[3-6]86|x86)$")
        set(_grove_arch i686)
    else()
        set(_grove_arch ${_grove_cpu})
    endif()

    if(MINGW)
        target_link_options(grove PRIVATE -static)
        include(CheckCXXSourceCompiles)
        check_cxx_source_compiles("#include <cstdlib>\n#ifndef _UCRT\n#error msvcrt\n#endif\nint main() { return 0; }" GROVE_MINGW_UCRT)
        if(_grove_arch STREQUAL "arm64")
            set(_grove_platform windows-clangarm64)
        elseif(_grove_arch STREQUAL "i686")
            set(_grove_platform windows-i686)
        elseif(GROVE_MINGW_UCRT)
            set(_grove_platform windows-ucrt64)
        else()
            set(_grove_platform windows-mingw64)
        endif()
        set(CPACK_GENERATOR ZIP)
    else()
        target_link_options(grove PRIVATE -static-libstdc++ -static-libgcc)
        set(_grove_platform linux-${_grove_arch})
        set(CPACK_GENERATOR TGZ)
    endif()
    set(GROVE_PACKAGE_PLATFORM ${_grove_platform} CACHE STRING "Platform name used in the package file name")

    set(CPACK_PACKAGE_NAME "Grove")
    set(CPACK_PACKAGE_VENDOR "ThatNewCoderJSCpp")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "Grove-${PROJECT_VERSION}-${GROVE_PACKAGE_PLATFORM}")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_STRIP_FILES ON)
    include(CPack)
endif()

cmake_minimum_required(VERSION 3.22.1)
project(ttrek VERSION 1.9.0 LANGUAGES C CXX)

option(USE_SYSTEM_LIBS "Link against system development libraries" OFF)
option(USE_CPPCHECK "Enable source testing with cppcheck at build time" OFF)

if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(TARGET ${PROJECT_NAME})
set(CMAKE_C_STANDARD   11)
set(CMAKE_C_STANDARD_REQUIRED true)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(THREADS_PREFER_PTHREAD_FLAG ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if (NOT APPLE)
    set(EXTRA_LIBS "-lm")
else()
    find_library(CORE_FOUNDATION CoreFoundation)
    message(STATUS "CORE_FOUNDATION: ${CORE_FOUNDATION}")
    set(EXTRA_LIBS "-framework CoreFoundation -framework SystemConfiguration")
endif()

#set(CMAKE_VERBOSE_MAKEFILE ON)

add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_definitions(TCL_THREADS PROJECT_VERSION=${PROJECT_VERSION})

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    add_compile_options(-fPIE -g -fsanitize=undefined -fsanitize=address)
    add_link_options(-fsanitize=undefined -fsanitize=address)
    set(CMAKE_C_FLAGS "-DDEBUG ${CMAKE_C_FLAGS}")
else()
    add_compile_definitions(NDEBUG)
endif ()

if(USE_CPPCHECK)
    find_program(cppcheck NAMES cppcheck)
    message(STATUS "cppcheck: ${cppcheck}")
    if(NOT (cppcheck MATCHES "NOTFOUND"))

        set (CPPCHECK_COMMON_OPTIONS
            --quiet
            --enable=all
            --inconclusive
            --inline-suppr
            --error-exitcode=0
            --suppress=missingIncludeSystem
            --suppress=constParameterPointer
            --suppress=constVariablePointer
            --suppress=unusedFunction
            --suppress=unmatchedSuppression
            --suppress=checkersReport
            -I"${CMAKE_INSTALL_PREFIX}/include")

        set (CMAKE_CXX_CPPCHECK "${cppcheck}" --language=c++ "${CPPCHECK_COMMON_OPTIONS}")
        set (CMAKE_C_CPPCHECK "${cppcheck}" --language=c "${CPPCHECK_COMMON_OPTIONS}")

    endif()
else()
    message(STATUS "cppcheck: is not enabled")
endif()

set(SATDEPSOLVER_SOURCE_DIR "" CACHE PATH "Override the vendored satdepsolver source directory")
if(NOT SATDEPSOLVER_SOURCE_DIR)
    set(SATDEPSOLVER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/satdepsolver-cpp")
endif()
if(NOT EXISTS "${SATDEPSOLVER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Missing satdepsolver sources. Use the complete release tarball or run git submodule update --init.")
endif()
add_subdirectory("${SATDEPSOLVER_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/satdepsolver" EXCLUDE_FROM_ALL)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/install_common_dynamic.sh.h
  COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/src/install_common_dynamic.sh | od -A n -v -t xC | sed 's@[^0-9A-Fa-f]@@g\;s@..@0x&, @g' >${CMAKE_CURRENT_BINARY_DIR}/install_common_dynamic.sh.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/install_common_dynamic.sh
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/script_package.sh.h
  COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/src/script_package.sh | od -A n -v -t xC | sed 's@[^0-9A-Fa-f]@@g\;s@..@0x&, @g' >${CMAKE_CURRENT_BINARY_DIR}/script_package.sh.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/script_package.sh
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/install_common_static.sh.h
  COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/src/install_common_static.sh | od -A n -v -t xC | sed 's@[^0-9A-Fa-f]@@g\;s@..@0x&, @g' >${CMAKE_CURRENT_BINARY_DIR}/install_common_static.sh.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/install_common_static.sh
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/script_header.sh.h
  COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/src/script_header.sh | od -A n -v -t xC | sed 's@[^0-9A-Fa-f]@@g\;s@..@0x&, @g' >${CMAKE_CURRENT_BINARY_DIR}/script_header.sh.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/script_header.sh
)

foreach(ACTIVATION_SCRIPT activate.sh activate.zsh)
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${ACTIVATION_SCRIPT}.h
      COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/src/${ACTIVATION_SCRIPT} | od -A n -v -t xC | sed 's@[^0-9A-Fa-f]@@g\;s@..@0x&, @g' >${CMAKE_CURRENT_BINARY_DIR}/${ACTIVATION_SCRIPT}.h
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${ACTIVATION_SCRIPT}
    )
endforeach()

file(GLOB HELP_TOPICS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/docs" "${CMAKE_CURRENT_SOURCE_DIR}/docs/help_*.txt")

set(HELP_TOPICS_H) # List of *.h files for the help topics
foreach (HELP_TOPIC ${HELP_TOPICS})
    list(APPEND HELP_TOPICS_H ${CMAKE_CURRENT_BINARY_DIR}/${HELP_TOPIC}.h)
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${HELP_TOPIC}.h
      COMMAND "${CMAKE_COMMAND}"
        "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/docs/${HELP_TOPIC}"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${HELP_TOPIC}.h"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedHelp.cmake"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/docs/${HELP_TOPIC}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedHelp.cmake"
      VERBATIM
    )
endforeach()

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/check_dependencies.sh.h
  COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/src/check_dependencies.sh | od -A n -v -t xC | sed 's@[^0-9A-Fa-f]@@g\;s@..@0x&, @g' >${CMAKE_CURRENT_BINARY_DIR}/check_dependencies.sh.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/check_dependencies.sh
)

add_executable(${TARGET}
        ${CMAKE_CURRENT_BINARY_DIR}/activate.sh.h
        ${CMAKE_CURRENT_BINARY_DIR}/activate.zsh.h
        ${CMAKE_CURRENT_BINARY_DIR}/check_dependencies.sh.h
        ${CMAKE_CURRENT_BINARY_DIR}/install_common_dynamic.sh.h
        ${CMAKE_CURRENT_BINARY_DIR}/script_package.sh.h
        ${CMAKE_CURRENT_BINARY_DIR}/install_common_static.sh.h
        ${CMAKE_CURRENT_BINARY_DIR}/script_header.sh.h
        ${HELP_TOPICS_H}
        src/ttrek_solver.cc
        src/ttrek_solver.h
        src/ttrek_git.c
        src/ttrek_git.h
        src/semver/semver.c
        src/PackageDatabase.h
        src/base64/cdecode.c
        src/base64/cencode.c
        src/semver/semver.c
        src/base64.c
        src/registry.c
        src/registrySources.cc
        src/registryHttp.c
        src/registryLocal.cc
        src/syncSubCmd.cc
        src/selfUpdateSubCmd.cc
        src/versionSubCmd.c
        src/initSubCmd.c
        src/installSubCmd.c
        src/genScriptSubCmd.cc
        src/runSubCmd.c
        src/activateSubCmd.c
        src/updateSubCmd.c
        src/ttrek.c
        src/common.c
        src/common.h
        src/base64/cdecode.h
        src/base64/cencode.h
        src/installer.h
        src/installer.c
        src/recipeDependencies.cc
        src/recipeDependencies.h
        src/fsmonitor/fsmonitor.h
        src/fsmonitor/fsmonitor.c
        src/listSubCmd.c
        src/searchSubCmd.c
        src/cloneSubCmd.cc
        src/useSubCmd.c
        src/uninstallSubCmd.c
        src/downloadSubCmd.c
        src/unpackSubCmd.c
        src/ttrek_telemetry.c
        src/ttrek_telemetry.h
        src/ttrek_genInstall.c
        src/ttrek_genInstall.h
        src/helpSubCmd.c
        src/ttrek_help.c
        src/ttrek_help.h
        src/ttrek_help_messages.h
        src/ttrek_useflags.c
        src/ttrek_buildInstructions.c
        src/ttrek_buildInstructions.h
        src/scriptsSubCmd.c
        src/ttrek_scripts.c
        src/ttrek_scripts.h
)

add_library(ttrek_dependencies INTERFACE)
target_link_libraries(ttrek_dependencies INTERFACE satdepsolver::satdepsolver)
if(USE_SYSTEM_LIBS)
    find_package(OpenSSL REQUIRED)
    find_package(CURL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(LibArchive REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIT2 REQUIRED IMPORTED_TARGET libgit2)
    pkg_check_modules(CJSON REQUIRED IMPORTED_TARGET libcjson)
    pkg_search_module(TCL REQUIRED IMPORTED_TARGET tcl>=9.0 tcl9.0 tcl9.1)
    target_link_libraries(ttrek_dependencies INTERFACE PkgConfig::TCL PkgConfig::CJSON
        PkgConfig::GIT2 LibArchive::LibArchive CURL::libcurl OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB)
else()
    set(static_libdir "${CMAKE_INSTALL_PREFIX}/lib")
    find_library(TTREK_TCL_LIBRARY NAMES libtcl9.1.a libtcl9.0.a PATHS "${static_libdir}" NO_DEFAULT_PATH REQUIRED)
    find_library(TTREK_CJSON_LIBRARY NAMES libcjson.a PATHS "${static_libdir}" NO_DEFAULT_PATH REQUIRED)
    find_library(TTREK_GIT2_LIBRARY NAMES libgit2.a PATHS "${static_libdir}" NO_DEFAULT_PATH REQUIRED)
    find_library(TTREK_ARCHIVE_LIBRARY NAMES libarchive.a PATHS "${static_libdir}" NO_DEFAULT_PATH REQUIRED)
    find_library(TTREK_CURL_LIBRARY NAMES libcurl.a PATHS "${static_libdir}" NO_DEFAULT_PATH REQUIRED)
    find_library(TTREK_SSL_LIBRARY NAMES libssl.a PATHS "${static_libdir}" "${CMAKE_INSTALL_PREFIX}/lib64" NO_DEFAULT_PATH REQUIRED)
    find_library(TTREK_CRYPTO_LIBRARY NAMES libcrypto.a PATHS "${static_libdir}" "${CMAKE_INSTALL_PREFIX}/lib64" NO_DEFAULT_PATH REQUIRED)
    find_library(TTREK_ZLIB_LIBRARY NAMES libz.a PATHS "${static_libdir}" NO_DEFAULT_PATH REQUIRED)
    target_include_directories(ttrek_dependencies INTERFACE "${CMAKE_INSTALL_PREFIX}/include")
    target_link_libraries(ttrek_dependencies INTERFACE ${TTREK_TCL_LIBRARY} ${TTREK_CJSON_LIBRARY}
        ${TTREK_GIT2_LIBRARY} ${TTREK_ARCHIVE_LIBRARY} ${TTREK_CURL_LIBRARY} ${TTREK_SSL_LIBRARY}
        ${TTREK_CRYPTO_LIBRARY} ${TTREK_ZLIB_LIBRARY})
endif()
target_link_libraries(ttrek_dependencies INTERFACE ${EXTRA_LIBS} ${CMAKE_DL_LIBS})
target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(${TARGET} PRIVATE ttrek_dependencies)
if (NOT APPLE AND NOT USE_SYSTEM_LIBS)
    if(${CMAKE_BUILD_TYPE} MATCHES "Release*")
        include(CheckSymbolExists)
        check_symbol_exists(__GLIBC__ "features.h" TTREK_USE_GLIBC)
        if(TTREK_USE_GLIBC)
            # Static glibc can dlopen incompatible host NSS modules during DNS
            # lookups. Use the host libc while keeping other dependencies static.
            target_link_options(${TARGET} PRIVATE -static-libgcc -static-libstdc++)
        else()
            target_link_options(${TARGET} PRIVATE -static)
        endif()
    endif()
endif()

install(TARGETS ${TARGET} RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_solver src/sat-solver/tests/solve.cc src/semver/semver.c)
    target_link_libraries(test_solver PRIVATE ttrek_dependencies)
    target_compile_options(test_solver PRIVATE -UNDEBUG)
    target_compile_definitions(test_solver PRIVATE SOLVER_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/sat-solver/tests/fixtures/registry.json")
    add_test(NAME package_solver COMMAND test_solver)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME cmake_logging COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake_logging.py)
    add_test(NAME release_abi COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/release_abi.py)
    add_test(NAME help COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/help.py
        $<TARGET_FILE:ttrek>)
    add_test(NAME use_flags COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/use_flags.py
        $<TARGET_FILE:ttrek>)
    add_test(NAME project_scripts COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/project_scripts.py
        $<TARGET_FILE:ttrek>)
    add_test(NAME activate COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/activate.py
        $<TARGET_FILE:ttrek>)
    set_tests_properties(activate PROPERTIES TIMEOUT 60)
    find_program(TTREK_TEST_ZSH NAMES zsh)
    if(TTREK_TEST_ZSH)
        add_test(NAME activate_zsh COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/activate.py
            $<TARGET_FILE:ttrek> "${TTREK_TEST_ZSH}")
        set_tests_properties(activate_zsh PROPERTIES TIMEOUT 60)
    endif()
    add_test(NAME unpack COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/unpack.py
        $<TARGET_FILE:ttrek>)
    # The dependency build installs curl here. Prefer it over a host curl that
    # may be sandboxed and unable to read the tests' temporary file:// archives.
    find_program(TTREK_TEST_CURL NAMES curl HINTS "${CMAKE_INSTALL_PREFIX}/bin" REQUIRED)
    get_target_property(TTREK_TEST_SOURCES ttrek SOURCES)
    list(REMOVE_ITEM TTREK_TEST_SOURCES src/registry.c)
    add_executable(ttrek_fixture ${TTREK_TEST_SOURCES} tests/registry_fixture.c)
    # Debug diagnostics must also leave generated stdout scripts intact.
    target_compile_definitions(ttrek_fixture PRIVATE DEBUG)
    target_compile_definitions(ttrek_fixture PRIVATE TTREK_TEST_RELEASES)
    target_include_directories(ttrek_fixture PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(ttrek_fixture PRIVATE ttrek_dependencies)
    add_test(NAME version COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/version.py
        $<TARGET_FILE:ttrek> $<TARGET_FILE:ttrek_fixture> $<CONFIG>)
    add_test(NAME telemetry COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/telemetry.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_program(TTREK_TEST_OPENSSL NAMES openssl HINTS "${CMAKE_INSTALL_PREFIX}/bin" REQUIRED)
        add_test(NAME https COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/https.py
            $<TARGET_FILE:ttrek> "${TTREK_TEST_OPENSSL}" $<TARGET_FILE:ttrek_fixture>)
    endif()
    add_test(NAME languages COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/languages.py
        $<TARGET_FILE:ttrek>)
    add_test(NAME search_http COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/search_http.py
        $<TARGET_FILE:ttrek>)
    add_test(NAME clone COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/clone.py
        $<TARGET_FILE:ttrek_fixture>)
    add_test(NAME gen_script COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen_script.py
        $<TARGET_FILE:ttrek_fixture> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME local_registry COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/local_registry.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME download_fallback COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/download_fallback.py
        $<TARGET_FILE:ttrek_fixture>)
    add_test(NAME download COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/download.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME shared_sources COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_sources.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME source_cleanup COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/source_cleanup.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME metapackages COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/metapackages.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME dependencies COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dependencies.py
        $<TARGET_FILE:ttrek>)
    add_test(NAME recipe_dependencies COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/recipe_dependencies.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME external_dependencies COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/external_dependencies.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME install_options COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_options.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME sync_self_update COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/sync_self_update.py
        $<TARGET_FILE:ttrek_fixture> $<TARGET_FILE:ttrek>)
    add_test(NAME registry_sources COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/registry_sources.py
        $<TARGET_FILE:ttrek>)
endif()

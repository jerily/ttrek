cmake_minimum_required(VERSION 3.22.1)
project(ttrek VERSION 1.1.0 LANGUAGES C CXX)

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
set(ZLIB_USE_STATIC_LIBS "ON")
set(BUILD_SHARED_LIBS OFF)

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

#add_library(foo STATIC IMPORTED)
#
## Set the properties of the imported library target
#set_target_properties(foo PROPERTIES
#    IMPORTED_LOCATION ${TCL_LIBRARY} # The full path to the library file
#    INTERFACE_INCLUDE_DIRECTORIES "${TCL_INCLUDE_PATH}" # The include directories for the library
#    #IMPORTED_LINK_INTERFACE_LIBRARIES "tcl" # The link libraries for the library
#    IMPORTED_NO_SONAME TRUE # Avoid linking to the library and include it in the executable
#)
set(SATDEPSOLVER_SOURCE_DIR "" CACHE PATH "Optional satdepsolver source checkout")
if(SATDEPSOLVER_SOURCE_DIR)
    add_subdirectory("${SATDEPSOLVER_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/satdepsolver")
else()
    find_package(satdepsolver 0.6 CONFIG REQUIRED)
endif()

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
        src/updateSubCmd.c
        src/ttrek.c
        src/common.c
        src/common.h
        src/base64/cdecode.h
        src/base64/cencode.h
        src/installer.h
        src/installer.c
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

message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
find_library(OPENSSL_SSL_LIBRARY NAMES libssl.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" "${CMAKE_INSTALL_PREFIX}/lib64" NO_DEFAULT_PATH)
find_library(OPENSSL_CRYPTO_LIBRARY NAMES libcrypto.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" "${CMAKE_INSTALL_PREFIX}/lib64" NO_DEFAULT_PATH)
set(OPENSSL_LIBRARIES "${OPENSSL_SSL_LIBRARY};${OPENSSL_CRYPTO_LIBRARY}")
find_library(CURL_LIBRARY NAMES libcurl.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(CJSON_LIBRARY NAMES libcjson.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(ZLIB_LIBRARY NAMES libz.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(TCL_LIBRARY NAMES libtcl9.1.a libtcl9.0.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(GIT2_LIBRARY NAMES libgit2.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(ARCHIVE_LIBRARY NAMES libarchive.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)

message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_INSTALL_PREFIX}/include")
target_link_libraries(${TARGET} PRIVATE satdepsolver::satdepsolver ${TCL_LIBRARY} ${CJSON_LIBRARY} ${GIT2_LIBRARY} ${ARCHIVE_LIBRARY} ${CURL_LIBRARY} ${OPENSSL_LIBRARIES} ${ZLIB_LIBRARY} ${EXTRA_LIBS} ${CMAKE_DL_LIBS})
find_package(ttrie 0.1 CONFIG REQUIRED)
find_package(ttrigrams 0.1 CONFIG REQUIRED)
target_link_libraries(${TARGET} PRIVATE ttrie::core ttrigrams::core)
if (NOT APPLE)
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

install(TARGETS ${TARGET}
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_solver src/sat-solver/tests/solve.cc src/semver/semver.c)
    target_include_directories(test_solver PRIVATE "${CMAKE_INSTALL_PREFIX}/include")
    target_link_libraries(test_solver PRIVATE satdepsolver::satdepsolver ${TCL_LIBRARY} ${CJSON_LIBRARY} ${ZLIB_LIBRARY} ${EXTRA_LIBS} ${CMAKE_DL_LIBS})
    target_compile_options(test_solver PRIVATE -UNDEBUG)
    target_compile_definitions(test_solver PRIVATE SOLVER_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/sat-solver/tests/fixtures/registry.json")
    add_test(NAME package_solver COMMAND test_solver)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME release_abi COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/release_abi.py)
    add_test(NAME help COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/help.py
        $<TARGET_FILE:ttrek>)
    add_test(NAME project_scripts COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/project_scripts.py
        $<TARGET_FILE:ttrek>)
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
    target_include_directories(ttrek_fixture PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_INSTALL_PREFIX}/include")
    target_link_libraries(ttrek_fixture PRIVATE satdepsolver::satdepsolver ${TCL_LIBRARY} ${CJSON_LIBRARY}
        ${GIT2_LIBRARY} ${ARCHIVE_LIBRARY} ${CURL_LIBRARY} ${OPENSSL_LIBRARIES} ${ZLIB_LIBRARY} ${EXTRA_LIBS} ${CMAKE_DL_LIBS})
    target_link_libraries(ttrek_fixture PRIVATE ttrie::core ttrigrams::core)
    add_test(NAME version COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/version.py
        $<TARGET_FILE:ttrek> $<TARGET_FILE:ttrek_fixture> $<CONFIG>)
    add_test(NAME telemetry COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/telemetry.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_program(TTREK_TEST_OPENSSL NAMES openssl HINTS "${CMAKE_INSTALL_PREFIX}/bin" REQUIRED)
        add_test(NAME https COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/https.py
            $<TARGET_FILE:ttrek> "${TTREK_TEST_OPENSSL}" $<TARGET_FILE:ttrek_fixture>)
    endif()
    add_test(NAME search COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/search.py
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
    add_test(NAME metapackages COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/metapackages.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME dependencies COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dependencies.py
        $<TARGET_FILE:ttrek>)
    add_test(NAME install_options COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_options.py
        $<TARGET_FILE:ttrek> ${CMAKE_COMMAND} "${TTREK_TEST_CURL}")
    add_test(NAME sync_self_update COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/sync_self_update.py
        $<TARGET_FILE:ttrek_fixture> $<TARGET_FILE:ttrek>)
    add_test(NAME registry_sources COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/registry_sources.py
        $<TARGET_FILE:ttrek>)
endif()

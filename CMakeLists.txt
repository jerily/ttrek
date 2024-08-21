cmake_minimum_required(VERSION 3.22.1)
project(ttrek VERSION 1.0.0 LANGUAGES C CXX)

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
            -I"${CMAKE_INSTALL_PREFIX}/include"
            -I"${CMAKE_CURRENT_SOURCE_DIR}/src/resolvo/cpp/include")

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
add_subdirectory(src/resolvo)

add_executable(test_resolvo
        src/sat-solver/tests/solve.cc
)

target_link_libraries(test_resolvo
        PRIVATE
        Resolvo::Resolvo
        stdc++
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/install_common_dynamic.sh.h
  COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/src/install_common_dynamic.sh | od -A n -v -t xC | sed 's@[^0-9A-Fa-f]@@g\;s@..@0x&, @g' >${CMAKE_CURRENT_BINARY_DIR}/install_common_dynamic.sh.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/install_common_dynamic.sh
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/install_common_static.sh.h
  COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/src/install_common_static.sh | od -A n -v -t xC | sed 's@[^0-9A-Fa-f]@@g\;s@..@0x&, @g' >${CMAKE_CURRENT_BINARY_DIR}/install_common_static.sh.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/install_common_static.sh
)

file(GLOB HELP_TOPICS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/docs" "${CMAKE_CURRENT_SOURCE_DIR}/docs/help_*.txt")

set(HELP_TOPICS_H) # List of *.h files for the help topics
foreach (HELP_TOPIC ${HELP_TOPICS})
    list(APPEND HELP_TOPICS_H ${CMAKE_CURRENT_BINARY_DIR}/${HELP_TOPIC}.h)
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${HELP_TOPIC}.h
      COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/docs/${HELP_TOPIC} | sed -e 's/"/\\\\"/g' | sed -e :a -e '/$$/N\; s/\\n/\\\\n/\; ta' | sed -e 's/.*/\\"&\\"/' >${CMAKE_CURRENT_BINARY_DIR}/${HELP_TOPIC}.h
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/docs/${HELP_TOPIC}
    )
endforeach()

add_executable(${TARGET}
        ${CMAKE_CURRENT_BINARY_DIR}/install_common_dynamic.sh.h
        ${CMAKE_CURRENT_BINARY_DIR}/install_common_static.sh.h
        ${HELP_TOPICS_H}
        src/ttrek_resolvo.cc
        src/ttrek_resolvo.h
        src/ttrek_git.c
        src/ttrek_git.h
        src/semver/semver.c
        src/PackageDatabase.h
        src/base64/cdecode.c
        src/base64/cencode.c
        src/semver/semver.c
        src/base64.c
        src/registry.c
        src/initSubCmd.c
        src/installSubCmd.c
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

# Resolvo::Resolvo brings in libgcc_s and thus we need to link statically as follows:
set(RESOLVO_LIB "${CMAKE_CURRENT_BINARY_DIR}/src/resolvo/cpp/libresolvo_cpp.a")
message(STATUS "RESOLVO_LIB: ${RESOLVO_LIB}")

message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
find_library(OPENSSL_SSL_LIBRARY NAMES libssl.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" "${CMAKE_INSTALL_PREFIX}/lib64" NO_DEFAULT_PATH)
find_library(OPENSSL_CRYPTO_LIBRARY NAMES libcrypto.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" "${CMAKE_INSTALL_PREFIX}/lib64" NO_DEFAULT_PATH)
set(OPENSSL_LIBRARIES "${OPENSSL_SSL_LIBRARY};${OPENSSL_CRYPTO_LIBRARY}")
find_library(CURL_LIBRARY NAMES libcurl.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(CJSON_LIBRARY NAMES libcjson.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(ZLIB_LIBRARY NAMES libz.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(TCL_LIBRARY NAMES libtcl9.0.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(GIT2_LIBRARY NAMES libgit2.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)
find_library(ARCHIVE_LIBRARY NAMES libarchive.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)

message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_INSTALL_PREFIX}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/resolvo/cpp/include")
target_link_libraries(${TARGET} PRIVATE ${RESOLVO_LIB} ${TCL_LIBRARY} ${ZLIB_LIBRARY} ${CJSON_LIBRARY} ${EXTRA_LIBS} ${CURL_LIBRARY} ${OPENSSL_LIBRARIES} ${GIT2_LIBRARY} ${ARCHIVE_LIBRARY})
if (NOT APPLE)
    if(${CMAKE_BUILD_TYPE} MATCHES "Release*")
        target_link_options(${TARGET} PRIVATE -static)
    endif()
endif()

install(TARGETS ${TARGET}
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
)

# temporary hack to include generated headers
include_directories(build/src/resolvo/cpp/generated_include)

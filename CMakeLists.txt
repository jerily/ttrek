cmake_minimum_required(VERSION 3.22.1)
project(ttrek VERSION 1.0.0 LANGUAGES C CXX)

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
    find_library(MATH_LIB NAMES libm.a)
    set(EXTRA_LIBS ${MATH_LIB})
else()
    find_library(CORE_FOUNDATION CoreFoundation)
    message(STATUS "CORE_FOUNDATION: ${CORE_FOUNDATION}")
    set(EXTRA_LIBS "-framework CoreFoundation -framework SystemConfiguration")
endif()

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS  "-g -DTCL_THREADS -DPROJECT_VERSION=${PROJECT_VERSION} ${CMAKE_CXX_FLAGS}")

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

add_executable(${TARGET}
        src/ttrek_resolvo.cc
        src/ttrek_resolvo.h
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
        src/pretendSubCmd.c
        src/ttrek.c
        src/common.c
        src/common.h
        src/base64/cdecode.h
        src/base64/cencode.h
        src/installer.h
        src/installer.c
        src/fsmonitor/fsmonitor.h
        src/fsmonitor/fsmonitor.c
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
find_library(TCL_LIBRARY NAMES libtcl8.6.a PATHS "${CMAKE_INSTALL_PREFIX}/lib" NO_DEFAULT_PATH)

message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
target_include_directories(${TARGET} PRIVATE "${CMAKE_INSTALL_PREFIX}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/resolvo/cpp/include")
target_link_libraries(${TARGET} PRIVATE ${RESOLVO_LIB} stdc++ ${TCL_LIBRARY} ${ZLIB_LIBRARY} ${CJSON_LIBRARY} ${EXTRA_LIBS} ${CURL_LIBRARY} ${OPENSSL_LIBRARIES})
if (NOT APPLE)
    target_link_options(${TARGET} PRIVATE -static)
endif()

install(TARGETS ${TARGET}
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
)

# temporary hack to include generated headers
include_directories(build/src/resolvo/cpp/generated_include)
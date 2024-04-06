cmake_minimum_required(VERSION 3.22.1)
project(ttrek VERSION 1.0.0 LANGUAGES C)

set(TARGET ${PROJECT_NAME})
set(CMAKE_C_STANDARD   11)
set(CMAKE_C_STANDARD_REQUIRED true)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(ZLIB_USE_STATIC_LIBS "ON")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
#find_package(TCL EXACT 8.6.13 REQUIRED)  # TCL_INCLUDE_PATH TCL_LIBRARY
#find_package(ZLIB REQUIRED) # ZLIB_INCLUDE_DIRS ZLIB_LIBRARIES
find_library(MATH_LIB NAMES libm.a)
#find_package(CURL 8.7.1 REQUIRED) # CURL_INCLUDE_DIRS CURL_LIBRARIES

message(STATUS "TCL_INCLUDE_PATH: ${TCL_INCLUDE_PATH}")
message(STATUS "TCL_LIBRARY: ${TCL_LIBRARY}")
message(STATUS "CJSON_LIBRARY: ${CJSON_LIBRARY}")
message(STATUS "CJSON_INCLUDE_PATH: ${CJSON_INCLUDE_PATH}")
message(STATUS "OPENSSL_LIBRARIES: ${OPENSSL_LIBRARIES}")

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_C_FLAGS  "-g -DTCL_THREADS -DPROJECT_VERSION=${PROJECT_VERSION} ${CMAKE_C_FLAGS}")

#add_library(foo STATIC IMPORTED)
#
## Set the properties of the imported library target
#set_target_properties(foo PROPERTIES
#    IMPORTED_LOCATION ${TCL_LIBRARY} # The full path to the library file
#    INTERFACE_INCLUDE_DIRECTORIES "${TCL_INCLUDE_PATH}" # The include directories for the library
#    #IMPORTED_LINK_INTERFACE_LIBRARIES "tcl" # The link libraries for the library
#    IMPORTED_NO_SONAME TRUE # Avoid linking to the library and include it in the executable
#)

add_executable(${TARGET}
        src/base64/cdecode.c
        src/base64/cencode.c
        src/semver/semver.c
        src/base64.c
        src/registry.c
        src/initSubCmd.c
        src/installSubCmd.c
        src/runSubCmd.c
        src/ttrek.c
        src/common.c)


include_directories(${TCL_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR} ${CJSON_INCLUDE_DIR} ${CURL_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})
target_link_libraries(${TARGET} ${TCL_LIBRARY} ${ZLIB_LIBRARY} ${CJSON_LIBRARY} ${MATH_LIB} ${CURL_LIBRARY} ${OPENSSL_LIBRARIES})
target_link_options(${TARGET} PRIVATE -static -static-libgcc -static-libstdc++)

install(TARGETS ${TARGET}
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${TARGET}${PROJECT_VERSION}
)

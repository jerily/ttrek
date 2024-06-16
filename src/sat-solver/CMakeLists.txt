cmake_minimum_required(VERSION 3.22.1)
project(test_resolvo_cpp VERSION 1.0.0 LANGUAGES C CXX)

set(TARGET ${PROJECT_NAME})
set(CMAKE_C_STANDARD   11)
set(CMAKE_C_STANDARD_REQUIRED true)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(BUILD_SHARED_LIBS OFF)

#include(FetchContent)
#FetchContent_Declare(
#        Resolvo
#        GIT_REPOSITORY https://github.com/mamba-org/resolvo.git
#        GIT_TAG resolvo_cpp-v0.1.0)
#FetchContent_MakeAvailable(Resolvo)

add_subdirectory(resolvo)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS  "-g -DTCL_THREADS -DPROJECT_VERSION=${PROJECT_VERSION} ${CMAKE_CXX_FLAGS}")

add_executable(test_resolvo_cpp
        tests/solve.cc
)

target_link_libraries(test_resolvo_cpp
        PRIVATE
        Resolvo::Resolvo
)

add_executable(ttrek_resolvo
        ttrek_resolvo.cc
)

target_link_libraries(ttrek_resolvo
        PRIVATE
        Resolvo::Resolvo
)

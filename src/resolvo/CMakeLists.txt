cmake_minimum_required(VERSION 3.22.1)
project(test_resolvo VERSION 1.0.0 LANGUAGES C CXX)

set(TARGET ${PROJECT_NAME})
set(CMAKE_C_STANDARD   11)
set(CMAKE_C_STANDARD_REQUIRED true)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(THREADS_PREFER_PTHREAD_FLAG ON)

message(STATUS "TCL_INCLUDE_PATH: ${TCL_INCLUDE_PATH}")
message(STATUS "TCL_LIBRARY: ${TCL_LIBRARY}")
message(STATUS "CJSON_LIBRARY: ${CJSON_LIBRARY}")
message(STATUS "CJSON_INCLUDE_PATH: ${CJSON_INCLUDE_PATH}")
message(STATUS "OPENSSL_LIBRARIES: ${OPENSSL_LIBRARIES}")

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS  "-g -DTCL_THREADS -DPROJECT_VERSION=${PROJECT_VERSION} ${CMAKE_CXX_FLAGS}")

add_executable(test_resolvo
        tests/test_resolvo.cc
        internal/Arena.h
        internal/ArenaId.h
        internal/NameId.h
        internal/SolvableId.h
        internal/StringId.h
        internal/VersionSetId.h
        Pool.h
        Solvable.h
        internal/ClauseId.h
        solver/Solver.h
        internal/LearntClauseId.h
        internal/CandidatesId.h
        internal/DependenciesId.h
        solver/Clause.h
        Problem.h
        solver/WatchMap.h
        solver/DecisionTracker.h
        solver/Decision.h
        solver/DecisionMap.h
        solver/PropagationError.h
        internal/FrozenCopyMap.h
        solver/SolverCache.h
        Common.h
        Range.h
        tests/solver.h
        DisplayClause.h
        DisplayName.h
        DisplayDecisionMap.h
        internal/Mapping.h
        DisplayUnsat.h
)

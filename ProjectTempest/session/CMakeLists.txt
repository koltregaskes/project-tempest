if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.25)
    project(project_tempest_session LANGUAGES CXX)
    enable_testing()
endif()

set(PROJECT_TEMPEST_CODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Code")

add_executable(project_tempest_session
    "${PROJECT_TEMPEST_CODE_DIR}/TempestSession.cpp"
    "${PROJECT_TEMPEST_CODE_DIR}/TempestAccessibility.cpp"
    "${PROJECT_TEMPEST_CODE_DIR}/TempestAccessibility.h"
    "${PROJECT_TEMPEST_CODE_DIR}/TempestInterface.cpp"
    "${PROJECT_TEMPEST_CODE_DIR}/TempestInterface.h"
    "${PROJECT_TEMPEST_CODE_DIR}/TempestSimulation.cpp"
    "${PROJECT_TEMPEST_CODE_DIR}/TempestSimulation.h"
)
target_compile_features(project_tempest_session PRIVATE cxx_std_20)
target_include_directories(project_tempest_session PRIVATE "${PROJECT_TEMPEST_CODE_DIR}")
set_target_properties(project_tempest_session PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(project_tempest_session PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT TARGET project_tempest_sim_tests)
    add_executable(project_tempest_sim_tests
        "${PROJECT_TEMPEST_CODE_DIR}/TempestAccessibility.cpp"
        "${PROJECT_TEMPEST_CODE_DIR}/TempestAccessibility.h"
        "${PROJECT_TEMPEST_CODE_DIR}/TempestAudio.cpp"
        "${PROJECT_TEMPEST_CODE_DIR}/TempestAudio.h"
        "${PROJECT_TEMPEST_CODE_DIR}/TempestInterface.cpp"
        "${PROJECT_TEMPEST_CODE_DIR}/TempestInterface.h"
        "${PROJECT_TEMPEST_CODE_DIR}/TempestSimulation.cpp"
        "${PROJECT_TEMPEST_CODE_DIR}/TempestSimulation.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Tests/TempestSimulationTests.cpp"
    )
    target_compile_features(project_tempest_sim_tests PRIVATE cxx_std_20)
    target_include_directories(project_tempest_sim_tests PRIVATE "${PROJECT_TEMPEST_CODE_DIR}")
    add_test(NAME project_tempest_sim_tests COMMAND project_tempest_sim_tests)
endif()

add_test(
    NAME project_tempest_session_victory
    COMMAND project_tempest_session
        --script "${CMAKE_CURRENT_SOURCE_DIR}/../Tests/session-victory.commands"
        --expect victory
)
add_test(
    NAME project_tempest_session_defeat
    COMMAND project_tempest_session
        --script "${CMAKE_CURRENT_SOURCE_DIR}/../Tests/session-defeat.commands"
        --expect defeat
)

# AURA / ASTRA — Backend build graph (BLD-0001)
#
# Backend-only build. No frontend/UI targets are defined here.
# Source files are discovered per module directory so the manifest can add
# one-file tasks without re-editing this file. Tests are discovered from
# tests/*.cpp and each becomes its own executable + CTest case.

cmake_minimum_required(VERSION 3.16)
project(AURA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Core backend library: every src/**/*.cpp except the host entry point.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE AURA_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER AURA_CORE_SOURCES EXCLUDE REGEX
  ".*/platform/windows/AuraBackendHost\\.cpp$")

add_library(aura_core STATIC ${AURA_CORE_SOURCES})
target_include_directories(aura_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(aura_core PUBLIC Threads::Threads)

if(WIN32)
  target_link_libraries(aura_core PUBLIC ws2_32)
endif()

# ---------------------------------------------------------------------------
# Backend host executable (double-click entry point; owns bridge lifecycle).
# ---------------------------------------------------------------------------
add_executable(aura_backend_host
  "${CMAKE_SOURCE_DIR}/src/platform/windows/AuraBackendHost.cpp")
target_link_libraries(aura_backend_host PRIVATE aura_core)

# ---------------------------------------------------------------------------
# Deterministic tests.
# ---------------------------------------------------------------------------
enable_testing()
file(GLOB AURA_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*.cpp")

foreach(test_src ${AURA_TEST_SOURCES})
  get_filename_component(test_name "${test_src}" NAME_WE)
  add_executable(${test_name} "${test_src}")
  target_link_libraries(${test_name} PRIVATE aura_core)
  target_include_directories(${test_name} PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

################################################################################
#                                                                              #
#                                BF JOB PROJECT                                #
#                                                                              #
################################################################################
###  CMakeList.txt : Multithreaded Job System Sub Project of the Engine      ###
################################################################################

cmake_minimum_required(VERSION 3.8)

project(BF_Job VERSION 0.0.1 DESCRIPTION "Multithreaded Job System Sub Project of the Engine.")

add_library(
  BF_Job
  STATIC
    # Public Headers
    "include/concurrent/initialization_token.hpp"
    "include/concurrent/job_api.hpp"
    "include/concurrent/job_assert.hpp"
    "include/concurrent/job_queue.hpp"

    # Source
    "src/job_system.cpp"
)

target_include_directories(
  BF_Job
  PUBLIC
    "include"
  PRIVATE
    "lib/pcg"
)

# $<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>
target_compile_options(BF_Job 
  PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4> # /WX
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic> # -Werror
)

set_target_properties(
  BF_Job
  PROPERTIES
    FOLDER                   "BluFedora/Foundation"
    CXX_STANDARD             17
    CXX_STANDARD_REQUIRED    on
    CXX_EXTENSIONS           off
    COMPILE_WARNING_AS_ERROR on
)

target_compile_features(
  BF_Job
  PUBLIC 
    cxx_std_17
)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# API Test Project

add_executable(
  BFJobTesting
  "${PROJECT_SOURCE_DIR}/tests/job_sys_main.cpp"
)

target_link_libraries(
  BFJobTesting
  PRIVATE
    BF_Job
    GTest::gtest_main
)

# gtest_discover_tests(BFJobTesting)

set_property(TARGET BFJobTesting PROPERTY FOLDER "BluFedora/Test")

if (EMSCRIPTEN)
  # target_compile_options(
  #   BF_Job
  #   PRIVATE
  #     "-pthread"
  # )

  target_link_options(
    BFJobTesting
    PRIVATE
      "-sTOTAL_MEMORY=33554432"
  )
endif()

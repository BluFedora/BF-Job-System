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
    "include/bf/job/bf_job_api.hpp"
    "include/bf/job/bf_job_config.hpp"
    "include/bf/job/bf_job_ext.hpp"

    # Private Headers
    "src/bf_job_queue.hpp"

    # Source
    "src/bf_job_system.cpp"
)

target_include_directories(
  BF_Job
  PUBLIC
    "include"
  PRIVATE
    "lib/pcg"
)

set_target_properties(
  BF_Job
  PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED on
    CXX_EXTENSIONS        off
    FOLDER                "BluFedora/Foundation"
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

gtest_discover_tests(BFJobTesting)

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

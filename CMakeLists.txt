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
  "${PROJECT_NAME}_static"
  STATIC
    "${PROJECT_SOURCE_DIR}/include/bf/job/bf_job_api.hpp"
    "${PROJECT_SOURCE_DIR}/include/bf/job/bf_job_config.hpp"
    "${PROJECT_SOURCE_DIR}/include/bf/job/bf_job_ext.hpp"
    "${PROJECT_SOURCE_DIR}/src/bf_job_system.cpp"
    "${PROJECT_SOURCE_DIR}/src/bf_job_queue.hpp"
)

target_include_directories(
  "${PROJECT_NAME}_static"
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

# API Test Project

add_executable(
  "${PROJECT_NAME}_test_01api"
  "${PROJECT_SOURCE_DIR}/tests/job_sys_main.cpp"
)

target_link_libraries(
  "${PROJECT_NAME}_test_01api"
  PRIVATE
    "${PROJECT_NAME}_static"
)
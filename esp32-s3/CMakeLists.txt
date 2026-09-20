cmake_minimum_required(VERSION 3.16)
# Task 5: vendored led_strip component (no registry access on the build host).
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gp2040-ce-s3)

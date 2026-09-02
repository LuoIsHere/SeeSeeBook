# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(MOONCAKE_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/dependencies/mooncake")
if(NOT EXISTS "${MOONCAKE_COMPONENT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Mooncake is missing; run scripts/fetch_dependencies.py first")
endif()
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/app"
    "${CMAKE_CURRENT_LIST_DIR}/core"
    "${CMAKE_CURRENT_LIST_DIR}/m5_hal"
    "${CMAKE_CURRENT_LIST_DIR}/services"
    "${CMAKE_CURRENT_LIST_DIR}/system"
    "${CMAKE_CURRENT_LIST_DIR}/ui"
    "${MOONCAKE_COMPONENT_DIR}"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(SeeSeeBook)

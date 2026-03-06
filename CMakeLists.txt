cmake_minimum_required(VERSION 3.16)

# Tell ESP-IDF to scan only the src component directory.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/src")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(modbus_bridge)

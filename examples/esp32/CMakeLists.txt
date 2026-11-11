# =============================================================================
# MAX22200 driver: ESP-IDF examples (ESP32-C6)
# =============================================================================
# Build with the ESP-IDF build scripts (git submodule in scripts/):
#   ./scripts/build_app.sh max22200_basic_example Debug
# The scripts read app_config.yml and pass APP_TYPE, BUILD_TYPE and APP_SOURCE_FILE.
# =============================================================================
cmake_minimum_required(VERSION 3.16)

set(DEFAULT_APP_TYPE "max22200_basic_example" CACHE STRING "Default app for the requirements phase")
set(DEFAULT_BUILD_TYPE "Debug" CACHE STRING "Default build type for the requirements phase")
set(DEFAULT_SOURCE_FILE "max22200_basic_example.cpp" CACHE STRING "Default source for the requirements phase")

if(NOT DEFINED APP_TYPE)
    message(FATAL_ERROR
        "APP_TYPE not defined. Use build_app.sh from examples/esp32/scripts (git submodule).\n"
        "Example: ./scripts/build_app.sh ${DEFAULT_APP_TYPE} ${DEFAULT_BUILD_TYPE}\n"
        "Or: idf.py -DAPP_TYPE=... -DBUILD_TYPE=... -DAPP_SOURCE_FILE=... build")
endif()
if(NOT DEFINED BUILD_TYPE)
    message(FATAL_ERROR "BUILD_TYPE not defined. Use build_app.sh (see README.md).")
endif()

message(STATUS "Building app type: ${APP_TYPE}")
message(STATUS "Build type: ${BUILD_TYPE}")
set(APP_TYPE "${APP_TYPE}" CACHE STRING "App type to build" FORCE)
set(BUILD_TYPE "${BUILD_TYPE}" CACHE STRING "Build type (Debug/Release)" FORCE)

# The driver repository root is an ESP-IDF component (its CMakeLists.txt registers itself under ESP_PLATFORM).
# Its component name is the name of the checkout folder; main/CMakeLists.txt works it out the same way.
get_filename_component(MAX22200_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND EXTRA_COMPONENT_DIRS "${MAX22200_REPO_ROOT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_max22200_${APP_TYPE}_app)

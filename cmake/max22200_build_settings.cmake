#===============================================================================
# MAX22200 driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(MAX22200_TARGET_NAME "max22200")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(MAX22200_VERSION_MAJOR 0)
set(MAX22200_VERSION_MINOR 1)
set(MAX22200_VERSION_PATCH 0)
set(MAX22200_VERSION "${MAX22200_VERSION_MAJOR}.${MAX22200_VERSION_MINOR}.${MAX22200_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(MAX22200_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/max22200_version.h.in")
set(MAX22200_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/max22200_generated")
set(MAX22200_VERSION_HEADER "${MAX22200_VERSION_HEADER_DIR}/max22200_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${MAX22200_VERSION_HEADER_DIR}")
configure_file("${MAX22200_VERSION_TEMPLATE}" "${MAX22200_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(MAX22200_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${MAX22200_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/max22200.ipp, included by inc/max22200.hpp.
set(MAX22200_SOURCE_FILES)

set(MAX22200_IDF_REQUIRES)

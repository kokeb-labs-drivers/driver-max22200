#===============================================================================
# MAX22200 driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   max22200, consumed as max22200::max22200 both through add_subdirectory() and
#   through find_package(max22200) after install.
#
#   cmake -S . -B build -DMAX22200_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/max22200_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${MAX22200_SOURCE_FILES}
        INCLUDE_DIRS ${MAX22200_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${MAX22200_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(MAX22200_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(max22200_driver
    VERSION ${MAX22200_VERSION}
    DESCRIPTION "C++20 driver for the Analog Devices MAX22200 and MAX22200A octal 36 V, 1 A serial-controlled solenoid and motor drivers."
    LANGUAGES CXX)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

# The installed layout keeps the source tree's inc/ + src/ pair, because max22200.hpp includes
# "../src/max22200.ipp" relative to itself:
#   <prefix>/include/max22200/inc/*.hpp, max22200_version.h
#   <prefix>/include/max22200/src/max22200.ipp
set(MAX22200_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/max22200/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${MAX22200_TARGET_NAME} INTERFACE)
target_include_directories(${MAX22200_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${MAX22200_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${MAX22200_INSTALL_INCLUDEDIR}>
)
target_compile_features(${MAX22200_TARGET_NAME} INTERFACE cxx_std_20)

# max22200::max22200 everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(max22200::max22200 ALIAS ${MAX22200_TARGET_NAME})
set_target_properties(${MAX22200_TARGET_NAME} PROPERTIES EXPORT_NAME max22200)

option(MAX22200_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links max22200::max22200" OFF)
if(MAX22200_ENABLE_WARNINGS)
    target_compile_options(${MAX22200_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(MAX22200_BUILD_TESTS "Build the host tests in tests/" OFF)
option(MAX22200_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(MAX22200_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${MAX22200_TARGET_NAME} EXPORT max22200Targets
    INCLUDES DESTINATION ${MAX22200_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${MAX22200_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${MAX22200_VERSION_HEADER}" DESTINATION ${MAX22200_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/max22200/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT max22200Targets
    FILE max22200Targets.cmake
    NAMESPACE max22200::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/max22200)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/max22200Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/max22200Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/max22200)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/max22200ConfigVersion.cmake"
    VERSION ${MAX22200_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/max22200Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/max22200ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/max22200)

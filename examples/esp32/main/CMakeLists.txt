# MAX22200 ESP32 examples: main component. The app source comes from app_config.yml through build_app.sh.
if(NOT DEFINED APP_TYPE)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(APP_TYPE "${DEFAULT_APP_TYPE}")
    else()
        message(FATAL_ERROR "APP_TYPE not defined: use build_app.sh")
    endif()
endif()
if(NOT DEFINED APP_SOURCE_FILE)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(APP_SOURCE_FILE "${DEFAULT_SOURCE_FILE}")
    else()
        message(FATAL_ERROR "APP_SOURCE_FILE not defined: use build_app.sh")
    endif()
endif()
message(STATUS "Building app: ${APP_TYPE}, source: ${APP_SOURCE_FILE}")

# The driver is the repository root (examples/esp32/CMakeLists.txt adds it to EXTRA_COMPONENT_DIRS). ESP-IDF
# names a component after its folder, so the driver's component name is the checkout folder's name. Work it out
# here, not in the project file: this file is also read in ESP-IDF's requirements pass.
get_filename_component(max22200_component "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
get_filename_component(max22200_component "${max22200_component}" NAME)

idf_component_register(
    SRCS "${APP_SOURCE_FILE}" "max22200_cxx20_check.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_rom freertos log ${max22200_component}
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
if(BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wpedantic -O0 -g3 -DDEBUG)
else()
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wpedantic -O2 -g -DNDEBUG)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE "EXAMPLE_TYPE_${APP_TYPE}=1")

# The header self-check: the driver's public headers as ISO C++20, whatever dialect ESP-IDF uses for the
# rest of the build (handbook KB-CPP20-001). Source options come after the component's, so this -std wins.
set_source_files_properties(max22200_cxx20_check.cpp PROPERTIES COMPILE_OPTIONS "-std=c++20;-pedantic-errors")

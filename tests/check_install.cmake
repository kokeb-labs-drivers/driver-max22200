# Installs the package from BUILD_DIR into a scratch prefix, then configures, builds and runs
# tests/install_consumer against that prefix only. Fails if a header the public headers include (the .ipp, the
# generated version header) is not installed, or if the exported target is not max22200::max22200.
#   cmake -DBUILD_DIR=... -DSOURCE_DIR=... -DWORK_DIR=... -DCXX_COMPILER=... -DGENERATOR=... -P check_install.cmake
file(REMOVE_RECURSE "${WORK_DIR}")
set(prefix "${WORK_DIR}/prefix")

function(run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "install check failed (${rc}): ${ARGN}")
  endif()
endfunction()

run("${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}")
run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/install_consumer" -B "${WORK_DIR}/consumer"
    -G "${GENERATOR}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_PREFIX_PATH=${prefix}"
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF)
run("${CMAKE_COMMAND}" --build "${WORK_DIR}/consumer")
find_program(consumer NAMES consumer consumer.exe PATHS "${WORK_DIR}/consumer"
             PATH_SUFFIXES Debug Release NO_DEFAULT_PATH NO_CACHE REQUIRED)
run("${consumer}")
message(STATUS "install check passed: ${prefix}")

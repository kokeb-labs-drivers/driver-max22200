# Runs Doxygen with the repository's _config/Doxyfile (WARN_AS_ERROR = FAIL_ON_WARNINGS) into OUT_DIR, from the
# repository root (the Doxyfile's paths are relative to it, as in the docs CI), so an
# undocumented public symbol in inc/ fails CTest the same way it fails the docs CI.
#   cmake -DDOXYGEN=<doxygen> -DSOURCE_DIR=<repo> -DOUT_DIR=<dir> -P tests/check_doxygen.cmake
file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")
file(READ "${SOURCE_DIR}/_config/Doxyfile" doxyfile)
string(APPEND doxyfile "\nOUTPUT_DIRECTORY = ${OUT_DIR}\n")
file(WRITE "${OUT_DIR}/Doxyfile" "${doxyfile}")
execute_process(COMMAND "${DOXYGEN}" "${OUT_DIR}/Doxyfile" WORKING_DIRECTORY "${SOURCE_DIR}"
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "doxygen failed (${rc}):\n${err}")
endif()
message(STATUS "doxygen: no warnings")

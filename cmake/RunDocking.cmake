if(NOT INFINITY_EXE OR NOT OUTPUT)
  message(FATAL_ERROR "INFINITY_EXE and OUTPUT required")
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
file(REMOVE_RECURSE "${OUTPUT}/data" "${OUTPUT}/captures")
file(COPY_FILE "${INFINITY_EXE}" "${OUTPUT}/infinity" ONLY_IF_DIFFERENT)
file(CHMOD "${OUTPUT}/infinity" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
find_program(capture_python NAMES python3 REQUIRED)
execute_process(COMMAND "${capture_python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/run_economy_native.py"
  "${OUTPUT}/infinity" "${OUTPUT}" --scenario docking-test
  OUTPUT_FILE "${OUTPUT}/native.log" ERROR_FILE "${OUTPUT}/native.log"
  RESULT_VARIABLE result TIMEOUT 340)
if(NOT result EQUAL 0)
  file(READ "${OUTPUT}/native.log" diagnostics)
  message(FATAL_ERROR "Native docking failed (${result}):\n${diagnostics}")
endif()

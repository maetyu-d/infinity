if(NOT INFINITE_EXE OR NOT OUTPUT)
  message(FATAL_ERROR "INFINITE_EXE and OUTPUT required")
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
file(REMOVE_RECURSE "${OUTPUT}/data" "${OUTPUT}/captures")
set(native_exe "${OUTPUT}/infinite")
file(COPY_FILE "${INFINITE_EXE}" "${native_exe}" ONLY_IF_DIFFERENT)
file(CHMOD "${native_exe}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
find_program(capture_python NAMES python3 REQUIRED)
execute_process(COMMAND "${capture_python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/run_economy_native.py"
  "${native_exe}" "${OUTPUT}" --scenario npc-combat-test
  OUTPUT_FILE "${OUTPUT}/native.log" ERROR_FILE "${OUTPUT}/native.log"
  RESULT_VARIABLE result TIMEOUT 210)
if(NOT result EQUAL 0)
  file(READ "${OUTPUT}/native.log" diagnostics)
  message(FATAL_ERROR "Native NPC combat failed (${result}):\n${diagnostics}")
endif()

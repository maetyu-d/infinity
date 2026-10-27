if(NOT INFINITE_EXE OR NOT OUTPUT)
  message(FATAL_ERROR "INFINITE_EXE and OUTPUT are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(COMMAND "${INFINITE_EXE}" --pilot-test --render-mode original --width 1440 --height 900
  --capture-dir "${OUTPUT}/captures" --profile "${OUTPUT}/frames.csv" --data "${OUTPUT}/data"
  OUTPUT_FILE "${OUTPUT}/native.log" ERROR_FILE "${OUTPUT}/native.log"
  RESULT_VARIABLE result TIMEOUT 120)
if(NOT result EQUAL 0)
  file(READ "${OUTPUT}/native.log" diagnostics)
  message(FATAL_ERROR "Native piloting failed (${result}):\n${diagnostics}")
endif()

if(NOT INFINITE_EXE OR NOT OUTPUT)
  message(FATAL_ERROR "INFINITE_EXE and OUTPUT are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(COMMAND "${INFINITE_EXE}" --landing-light-test --width 1440 --height 900
  --capture-dir "${OUTPUT}/captures" --profile "${OUTPUT}/frames.csv" --data "${OUTPUT}/data"
  OUTPUT_FILE "${OUTPUT}/native.log" ERROR_FILE "${OUTPUT}/native.log"
  RESULT_VARIABLE result TIMEOUT 130)
if(NOT result EQUAL 0)
  file(READ "${OUTPUT}/native.log" diagnostics)
  message(FATAL_ERROR "Native landing light failed (${result}):\n${diagnostics}")
endif()

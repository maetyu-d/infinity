if(NOT INFINITY_EXE OR NOT OUTPUT)
  message(FATAL_ERROR "INFINITY_EXE and OUTPUT are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(COMMAND "${INFINITY_EXE}" --ui-fit-test --capture-dir "${OUTPUT}/captures"
  --data "${OUTPUT}/data" --profile "${OUTPUT}/frames.csv"
  OUTPUT_FILE "${OUTPUT}/native.log" ERROR_FILE "${OUTPUT}/native.log"
  RESULT_VARIABLE result TIMEOUT 240)
if(NOT result EQUAL 0)
  file(READ "${OUTPUT}/native.log" diagnostics)
  message(FATAL_ERROR "Native UI fit failed (${result}):\n${diagnostics}")
endif()

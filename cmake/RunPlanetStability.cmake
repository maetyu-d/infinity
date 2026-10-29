if(NOT INFINITE_EXE OR NOT OUTPUT)
  message(FATAL_ERROR "INFINITE_EXE and OUTPUT are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(COMMAND "${INFINITE_EXE}" --planet-stability-test --render-mode original
  --width 960 --height 600 --capture-dir "${OUTPUT}/captures" --data "${OUTPUT}/data"
  --profile "${OUTPUT}/frames.csv"
  OUTPUT_FILE "${OUTPUT}/native.log" ERROR_FILE "${OUTPUT}/native.log"
  RESULT_VARIABLE result TIMEOUT 160)
if(NOT result EQUAL 0)
  file(READ "${OUTPUT}/native.log" diagnostics)
  message(FATAL_ERROR "Native planet stability failed (${result}):\n${diagnostics}")
endif()

if(NOT INFINITE_EXE OR NOT OUTPUT OR NOT VIEW_PATH)
  message(FATAL_ERROR "INFINITE_EXE, OUTPUT and VIEW_PATH are required")
endif()
if(NOT WIDTH)
  set(WIDTH 2840)
  set(HEIGHT 1800)
endif()
foreach(mode on off)
  file(MAKE_DIRECTORY "${OUTPUT}/${mode}")
  set(flags)
  if(mode STREQUAL "off")
    list(APPEND flags --no-sun-shadows)
  endif()
  execute_process(COMMAND "${INFINITE_EXE}" --terrain-saved-view-test "${VIEW_PATH}" ${flags}
    --width ${WIDTH} --height ${HEIGHT} --capture-dir "${OUTPUT}/${mode}/captures"
    --profile "${OUTPUT}/${mode}/frames.csv" --data "${OUTPUT}/${mode}/data"
    OUTPUT_FILE "${OUTPUT}/${mode}/native.log" ERROR_FILE "${OUTPUT}/${mode}/native.log"
    RESULT_VARIABLE result TIMEOUT 90)
  if(NOT result EQUAL 0)
    file(READ "${OUTPUT}/${mode}/native.log" diagnostics)
    message(FATAL_ERROR "Grazing surface ${mode} failed (${result}):\n${diagnostics}")
  endif()
endforeach()

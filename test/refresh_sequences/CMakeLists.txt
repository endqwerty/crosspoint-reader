find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(REFRESH_SANITIZER_ARGS)
if(CROSSPOINT_TEST_SANITIZERS)
  list(APPEND REFRESH_SANITIZER_ARGS --sanitize)
endif()
add_test(NAME RefreshSequences
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --report ${CMAKE_CURRENT_BINARY_DIR}/refresh-sequences.json
    ${REFRESH_SANITIZER_ARGS}
)
set_tests_properties(RefreshSequences PROPERTIES TIMEOUT 120)

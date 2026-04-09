if(NOT DEFINED ALIFE_SIM_EXECUTABLE)
    message(FATAL_ERROR "ALIFE_SIM_EXECUTABLE is required")
endif()

if(NOT DEFINED STATE_PATH)
    message(FATAL_ERROR "STATE_PATH is required")
endif()

execute_process(
    COMMAND "${ALIFE_SIM_EXECUTABLE}" --smoke-test --seed 1 --smoke-steps 1800 --report-format jsonl --save-state "${STATE_PATH}"
    RESULT_VARIABLE save_result
    OUTPUT_VARIABLE save_output
    ERROR_VARIABLE save_error
)
if(NOT save_result EQUAL 0)
    message(FATAL_ERROR "Initial save run failed: ${save_error}\n${save_output}")
endif()

execute_process(
    COMMAND "${ALIFE_SIM_EXECUTABLE}" --smoke-test --load-state "${STATE_PATH}" --smoke-steps 1800 --report-format jsonl
    RESULT_VARIABLE load_result
    OUTPUT_VARIABLE load_output
    ERROR_VARIABLE load_error
)
if(NOT load_result EQUAL 0)
    message(FATAL_ERROR "Load continuation run failed: ${load_error}\n${load_output}")
endif()

execute_process(
    COMMAND "${ALIFE_SIM_EXECUTABLE}" --smoke-test --seed 1 --smoke-steps 3600 --report-format jsonl
    RESULT_VARIABLE direct_result
    OUTPUT_VARIABLE direct_output
    ERROR_VARIABLE direct_error
)
if(NOT direct_result EQUAL 0)
    message(FATAL_ERROR "Direct comparison run failed: ${direct_error}\n${direct_output}")
endif()

set(load_normalized "${load_output}")
set(direct_normalized "${direct_output}")

string(REGEX REPLACE "\"wall_seconds\":[0-9.eE+-]+," "" load_normalized "${load_normalized}")
string(REGEX REPLACE "\"steps_per_second\":[0-9.eE+-]+," "" load_normalized "${load_normalized}")
string(REGEX REPLACE "\"steps\":[0-9]+," "" load_normalized "${load_normalized}")
string(REGEX REPLACE "\"simulated_seconds\":[0-9.eE+-]+," "" load_normalized "${load_normalized}")
string(REGEX REPLACE "\"wall_seconds\":[0-9.eE+-]+," "" direct_normalized "${direct_normalized}")
string(REGEX REPLACE "\"steps_per_second\":[0-9.eE+-]+," "" direct_normalized "${direct_normalized}")
string(REGEX REPLACE "\"steps\":[0-9]+," "" direct_normalized "${direct_normalized}")
string(REGEX REPLACE "\"simulated_seconds\":[0-9.eE+-]+," "" direct_normalized "${direct_normalized}")

if(NOT load_normalized STREQUAL direct_normalized)
    message(FATAL_ERROR "Round-trip persistence mismatch.\nLoaded:\n${load_output}\nDirect:\n${direct_output}")
endif()

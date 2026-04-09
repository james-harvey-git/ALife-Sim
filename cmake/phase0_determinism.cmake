if(NOT DEFINED ALIFE_SIM_EXECUTABLE)
    message(FATAL_ERROR "ALIFE_SIM_EXECUTABLE is required")
endif()

set(common_args
    --smoke-test
    --seed 7
    --smoke-steps 3600
    --report-format jsonl
    --snapshot
    --config-preset reef-dense
    --target-blooms 205
    --target-reefs 10
    --nutrient-recovery 0.47
    --add-connection-base 0.08
)

execute_process(
    COMMAND "${ALIFE_SIM_EXECUTABLE}" ${common_args}
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "First determinism probe failed: ${first_error}\n${first_output}")
endif()

execute_process(
    COMMAND "${ALIFE_SIM_EXECUTABLE}" ${common_args}
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "Second determinism probe failed: ${second_error}\n${second_output}")
endif()

set(first_normalized "${first_output}")
set(second_normalized "${second_output}")

string(REGEX REPLACE "\"wall_seconds\":[0-9.eE+-]+," "" first_normalized "${first_normalized}")
string(REGEX REPLACE "\"steps_per_second\":[0-9.eE+-]+," "" first_normalized "${first_normalized}")
string(REGEX REPLACE "\"config_label\":\"[^\"]+\"," "" first_normalized "${first_normalized}")
string(REGEX REPLACE "\"wall_seconds\":[0-9.eE+-]+," "" second_normalized "${second_normalized}")
string(REGEX REPLACE "\"steps_per_second\":[0-9.eE+-]+," "" second_normalized "${second_normalized}")
string(REGEX REPLACE "\"config_label\":\"[^\"]+\"," "" second_normalized "${second_normalized}")

if(NOT first_normalized STREQUAL second_normalized)
    message(FATAL_ERROR "Determinism mismatch.\nFirst:\n${first_output}\nSecond:\n${second_output}")
endif()

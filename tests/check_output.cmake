if(NOT DEFINED HELLO_EXE)
    message(FATAL_ERROR "HELLO_EXE must name the application executable")
endif()

execute_process(
    COMMAND "${HELLO_EXE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
    TIMEOUT 5
)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "hello failed (${result}): ${error_output}")
endif()

# Accept Windows and Unix line endings, while requiring a final newline.
string(REPLACE "\r\n" "\n" output "${output}")
if(NOT "${output}" STREQUAL "Hello, embedded!\n")
    message(FATAL_ERROR "Unexpected standard output: [${output}]")
endif()
if(NOT "${error_output}" STREQUAL "")
    message(FATAL_ERROR "Unexpected standard error: [${error_output}]")
endif()

if(NOT DEFINED HELLO_EXE)
    message(FATAL_ERROR "HELLO_EXE must name the application executable")
endif()

if(NOT DEFINED HELLO_CASE)
    set(HELLO_CASE default)
endif()

set(expected_error "")
set(expect_failure FALSE)
if(HELLO_CASE STREQUAL "default")
    set(expected_output "Hello, embedded!\n")
    execute_process(COMMAND "${HELLO_EXE}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error_output
        TIMEOUT 5)
elseif(HELLO_CASE STREQUAL "name" OR HELLO_CASE STREQUAL "special_name"
       OR HELLO_CASE STREQUAL "empty_name")
    if(HELLO_CASE STREQUAL "name")
        set(name "Alice")
    elseif(HELLO_CASE STREQUAL "special_name")
        set(name "Ada Lovelace 100% %s%n")
    else()
        set(name "")
    endif()
    set(expected_output "Hello, ${name}!\n")
    # Quoting preserves one argument, including an explicitly empty name.
    execute_process(COMMAND "${HELLO_EXE}" "${name}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error_output
        TIMEOUT 5)
elseif(HELLO_CASE STREQUAL "two_names" OR HELLO_CASE STREQUAL "three_names")
    set(expect_failure TRUE)
    set(expected_output "")
    set(expected_error "Usage: hello [name]\n")
    if(HELLO_CASE STREQUAL "two_names")
        execute_process(COMMAND "${HELLO_EXE}" "Alice" "Bob"
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error_output
            TIMEOUT 5)
    else()
        execute_process(COMMAND "${HELLO_EXE}" "Alice" "Bob" "Carol"
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error_output
            TIMEOUT 5)
    endif()
else()
    message(FATAL_ERROR "Unknown HELLO_CASE: ${HELLO_CASE}")
endif()

# Launch errors and timeouts are not application exit statuses.
if(NOT "${result}" MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR "${HELLO_CASE}: application did not exit normally: ${result}")
endif()
if(expect_failure)
    if("${result}" EQUAL 0)
        message(FATAL_ERROR "${HELLO_CASE}: expected nonzero exit status, got ${result}")
    endif()
elseif(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "${HELLO_CASE}: expected exit status 0, got ${result}")
endif()

# Accept Windows and Unix line endings, while requiring a final newline.
string(REPLACE "\r\n" "\n" output "${output}")
string(REPLACE "\r\n" "\n" error_output "${error_output}")
if(NOT "${output}" STREQUAL "${expected_output}")
    message(FATAL_ERROR "${HELLO_CASE}: expected stdout [${expected_output}], got [${output}]")
endif()
if(NOT "${error_output}" STREQUAL "${expected_error}")
    message(FATAL_ERROR "${HELLO_CASE}: expected stderr [${expected_error}], got [${error_output}]")
endif()

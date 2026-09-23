cmake_minimum_required(VERSION 3.20)
project(kda VERSION 0.1.0 LANGUAGES C)

add_executable(hello src/main.c)
set_target_properties(hello PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(MSVC)
    target_compile_options(hello PRIVATE /W4)
else()
    target_compile_options(hello PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME hello_default
        COMMAND "${CMAKE_COMMAND}"
            "-DHELLO_EXE=$<TARGET_FILE:hello>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_output.cmake"
    )
endif()

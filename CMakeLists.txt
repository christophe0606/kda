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
    add_library(kda_fir STATIC src/kda_fir_f32.c)
    target_include_directories(kda_fir PUBLIC src)
    set_target_properties(kda_fir PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_executable(test_fir tests/test_fir.c tests/fir_oracle.c)
    target_link_libraries(test_fir PRIVATE kda_fir)
    set_target_properties(test_fir PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(MSVC)
        target_compile_options(kda_fir PRIVATE /W4 /O2 /fp:fast)
        target_compile_options(test_fir PRIVATE /W4 /fp:strict)
    else()
        target_compile_options(kda_fir PRIVATE -Wall -Wextra -Wpedantic -O3 -ffast-math)
        target_compile_options(test_fir PRIVATE -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
        if(NOT WIN32)
            target_link_libraries(test_fir PRIVATE m)
        endif()
    endif()
    add_test(NAME fir_correctness COMMAND test_fir)

    foreach(case IN ITEMS default name special_name empty_name two_names three_names)
        add_test(NAME hello_${case}
            COMMAND "${CMAKE_COMMAND}"
                "-DHELLO_EXE=$<TARGET_FILE:hello>"
                "-DHELLO_CASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_output.cmake"
        )
    endforeach()
endif()

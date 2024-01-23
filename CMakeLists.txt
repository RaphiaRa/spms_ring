cmake_minimum_required(VERSION 3.9 FATAL_ERROR)
project(spms)

set(CMAKE_C_STANDARD 11)

# Extra warnings, treat warnings as error
if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" 
    OR "${CMAKE_C_COMPILER_ID}" STREQUAL "Clang" 
    OR "${CMAKE_C_COMPILER_ID}" STREQUAL "AppleClang")
  add_compile_options(-Wall -Wextra -pedantic -Werror)
endif()

add_library(spms 
    spms.c
)

target_include_directories(spms PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

add_library(spms::spms ALIAS spms)

if (NOT SPMS_DISABLE_TESTS)
    add_executable(test_spms
        test_spms.c
    )
    target_link_libraries(test_spms PUBLIC 
        spms::spms
    )
endif()

if (NOT SPMS_DISABLE_EXAMPLES)
    add_executable(example
        example.c
    )
    target_link_libraries(example PUBLIC 
        spms::spms
    )
    if(UNIX AND NOT APPLE)
        target_link_libraries(example PUBLIC 
            rt
        )
    endif()
endif()

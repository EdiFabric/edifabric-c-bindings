cmake_minimum_required(VERSION 3.16)
project(edifabric_native_c_examples C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(edifabric_x12 STATIC
    edifabric_x12.c
)
target_include_directories(edifabric_x12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(edifabric_x12 PUBLIC kernel32)
else()
    target_link_libraries(edifabric_x12 PUBLIC dl)
endif()

add_executable(example_all_functions
    example_all_functions.c
)
target_link_libraries(example_all_functions PRIVATE edifabric_x12)

# Copy sample assets next to the binary so the example finds edi/ and map/.
add_custom_command(TARGET example_all_functions POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/edi $<TARGET_FILE_DIR:example_all_functions>/edi
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/map $<TARGET_FILE_DIR:example_all_functions>/map
)

if(EXISTS "${CMAKE_SOURCE_DIR}/edifabric-x12-tools.dll")
    add_custom_command(TARGET example_all_functions POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/edifabric-x12-tools.dll
                $<TARGET_FILE_DIR:example_all_functions>/edifabric-x12-tools.dll
    )
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/edifabric-x12-tools.so")
    add_custom_command(TARGET example_all_functions POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/edifabric-x12-tools.so
                $<TARGET_FILE_DIR:example_all_functions>/edifabric-x12-tools.so
    )
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/edifabric-x12-tools.dylib")
    add_custom_command(TARGET example_all_functions POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/edifabric-x12-tools.dylib
                $<TARGET_FILE_DIR:example_all_functions>/edifabric-x12-tools.dylib
    )
endif()

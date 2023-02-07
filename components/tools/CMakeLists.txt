cmake_minimum_required(VERSION 3.20)

set(SRCES string_parser.hpp
    utilities.hpp
    vector_algorithms.hpp)


idf_component_register(SRCS ${SRCES} INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

if (${CXX_COMPILE_FLAGS} STREQUAL "")
    message(ERROR "No compile flags set for CXX files!")
endif ()
if (${C_COMPILE_FLAGS} STREQUAL "")
    message(ERROR "No compile flags set for C files!")
endif ()


set_source_files_properties(${SRCES} PROPERTIES COMPILE_FLAGS ${CXX_COMPILE_FLAGS})


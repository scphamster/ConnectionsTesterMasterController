cmake_minimum_required(VERSION 3.20)

set(CXX_SOURCES iic.cpp iic.hpp)
set(INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})

idf_component_register(SRCS ${CXX_SOURCES} INCLUDE_DIRS ${INCLUDES}
                       REQUIRES logger
                       mutex)

if (${CXX_COMPILE_FLAGS} STREQUAL "")
    message(ERROR "No compile flags set for CXX files!")
endif ()
if (${C_COMPILE_FLAGS} STREQUAL "")
    message(ERROR "No compile flags set for C files!")
endif ()


set_source_files_properties(${CXX_SOURCES} PROPERTIES COMPILE_FLAGS "${CXX_COMPILE_FLAGS} -std=c++2a")

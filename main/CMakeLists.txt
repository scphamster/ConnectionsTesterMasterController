cmake_minimum_required(VERSION 3.20)

set(MY_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

set(CXX_SOURCES main.cpp)

idf_component_register(SRCS ${CXX_SOURCES}
                       INCLUDE_DIRS ${MY_INCLUDE_DIRS}
                       REQUIRES application)

if (${CXX_COMPILE_FLAGS} STREQUAL "")
    message(ERROR "No compile flags set for CXX files!")
endif ()
if (${C_COMPILE_FLAGS} STREQUAL "")
    message(ERROR "No compile flags set for C files!")
endif ()

set_source_files_properties(${CXX_SOURCES} PROPERTIES COMPILE_FLAGS ${CXX_COMPILE_FLAGS})


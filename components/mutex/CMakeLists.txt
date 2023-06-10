cmake_minimum_required(VERSION 3.20)

set(CXX_SOURCES my_mutex.cpp my_mutex.hpp)
set(INCLUDES .)

idf_component_register(SRCS ${CXX_SOURCES} INCLUDE_DIRS ${INCLUDES})

#if (not DEFINED CXX_COMPILE_OPTIONS)
#    message(FATAL_ERROR "No compile options set for component: ${COMPONENT_LIB}")
#endif ()

target_compile_options(${COMPONENT_LIB} PUBLIC -Wall -fconcepts -std=c++2a -Ofast -fexceptions
)


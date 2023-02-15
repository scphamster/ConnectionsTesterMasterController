cmake_minimum_required(VERSION 3.20)

set(CXX_SOURCES task.hpp task.cpp)
set(INCLUDES .)

idf_component_register(SRCS ${CXX_SOURCES} INCLUDE_DIRS ${INCLUDES}
                       REQUIRES freertos)

target_compile_options(${COMPONENT_LIB} PUBLIC -Wall -fconcepts -std=c++2a -Ofast -fexceptions
)

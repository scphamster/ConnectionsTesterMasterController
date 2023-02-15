cmake_minimum_required(VERSION 3.20)

set(CXX_SOURCES
    bluetooth_gap.hpp
    bluetooth_gap.cpp
    bluetooth_spp.hpp
    bluetooth_spp.cpp
    bluetooth.hpp
    bluetooth.cpp
    )
set(C_SOURCES
    spp_task.c
    spp_task.h)

set(INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})

idf_component_register(SRCS ${CXX_SOURCES} ${C_SOURCES} INCLUDE_DIRS ${INCLUDES}
                       REQUIRES bt
                       mutex
                       logger
                       task
                       queue
                       proj_cfg
                       )


target_compile_options(${COMPONENT_LIB} PUBLIC -Wall -fconcepts -std=c++2a -Ofast -fexceptions
)

set_source_files_properties(${C_SOURCES} PROPERTIES COMPILE_FLAGS ${C_COMPILE_FLAGS})
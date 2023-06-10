cmake_minimum_required(VERSION 3.20)

set(CXX_SOURCES
    project_configs.hpp
    )

set(INCLUDES .)

idf_component_register(SRCS ${CXX_SOURCES} INCLUDE_DIRS ${INCLUDES}
                       )


target_compile_options(${COMPONENT_LIB} PUBLIC -Wall -fconcepts -std=c++2a -Ofast -fexceptions
)

set_source_files_properties(${C_SOURCES} PROPERTIES COMPILE_FLAGS ${C_COMPILE_FLAGS})
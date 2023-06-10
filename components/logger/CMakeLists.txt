cmake_minimum_required(VERSION 3.20)

set(CXX_SOURCES esp_logger.hpp esp_logger.cpp)
set(INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})

idf_component_register(SRCS ${CXX_SOURCES} INCLUDE_DIRS ${INCLUDES}
                       REQUIRES proj_cfg
                       mutex
                       task)

target_compile_options(${COMPONENT_LIB} PUBLIC -Wall -fconcepts -std=c++2a -Ofast -fexceptions
)


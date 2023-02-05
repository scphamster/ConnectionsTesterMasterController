cmake_minimum_required(VERSION 3.20)

set(SRCES include/communicator.hpp
    include/message.hpp)

idf_component_register(SRCS ${SRCES} INCLUDE_DIRS include)

set_source_files_properties(${SRCES} PROPERTIES COMPILE_FLAGS "-Wall -fconcepts -std=c++2a -Os -fexceptions")


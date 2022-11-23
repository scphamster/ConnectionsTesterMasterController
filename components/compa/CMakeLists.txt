cmake_minimum_required(VERSION 3.5)

idf_component_register(SRCS compa.cpp INCLUDE_DIRS include REQUIRES asio)

set_source_files_properties(compa.cpp include/compa.hpp PROPERTIES COMPILE_FLAGS " -fconcepts -std=c++2a")

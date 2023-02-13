cmake_minimum_required(VERSION 3.20)

set(SOURCES "reset_reason_notifier.hpp")

idf_component_register(SRCS ${SOURCES} INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

set_source_files_properties(${SORCES} PROPERTIES COMPILE_FLAGS "-Wall -fconcepts -std=c++2a -Os -fexceptions")

cmake_minimum_required(VERSION 3.20)

set(CXX_SOURCES queue.cpp queue.hpp)
set(INCLUDES .)

idf_component_register(SRCS ${CXX_SOURCES} INCLUDE_DIRS ${INCLUDES}
                       REQUIRES semaphore
                       mutex)

set_source_files_properties(${CXX_SOURCES} PROPERTIES COMPILE_FLAGS ${CXX_COMPILE_FLAGS})

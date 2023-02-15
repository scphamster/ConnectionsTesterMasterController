cmake_minimum_required(VERSION 3.20)

set(SOURCES
    include/main_apparatus.hpp
    include/main_apparatus.cpp
    include/board_controller.hpp
    include/data_link.hpp
    include/measurement_structures.hpp)

idf_component_register(SRCS ${SOURCES} INCLUDE_DIRS include
                        REQUIRES task queue tools mutex semaphore i2c bluetooth gpio cmd_interpreter communicator
                       )

target_compile_options(${COMPONENT_LIB} PUBLIC -std=c++2a)
set_source_files_properties(${SORCES} PROPERTIES COMPILE_FLAGS ${CXX_COMPILE_FLAGS})

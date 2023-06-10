cmake_minimum_required(VERSION 3.20)

set(SRCES include/communicator.hpp
    include/message.hpp
    include/communicator_concept.hpp
    )

idf_component_register(SRCS ${SRCES} INCLUDE_DIRS include
                       REQUIRES application
                       asio
                       logger
                       queue
                       io_board
                       tools)

set_source_files_properties(${SRCES} PROPERTIES COMPILE_FLAGS ${CXX_COMPILE_FLAGS})


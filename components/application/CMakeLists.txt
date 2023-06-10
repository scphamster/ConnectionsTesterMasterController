cmake_minimum_required(VERSION 3.20)

#set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)
#include($ENV{IDF_PATH}/tools/cmake/project.cmake)


set(SOURCES application.hpp ../proj_cfg/project_configs.hpp)
set(INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})

idf_component_register(SRCES ${SOURCES} INCLUDE_DIRS ${INCLUDES}
                       REQUIRES task
                       queue
                       logger
                       reset_reason_notifier
                       io_board
                       nvs_flash
                       protocol_examples_common
                       communicator
                       asio
                       bluetooth
                       proj_cfg)

set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS ${CXX_COMPILE_FLAGS})
set(COMPONENTS_DIR ../components)

set(MY_INCLUDE_DIRS ${COMPONENTS_DIR}
    ${COMPONENTS_DIR}/cpp_wrappers
    ${COMPONENTS_DIR}/cpp_wrappers/gpio
    ${COMPONENTS_DIR}/cpp_wrappers/bluetooth
    )

set(CXX_SOURCES main.cpp
                ${COMPONENTS_DIR}/cpp_wrappers/gpio/gpio.hpp
                ${COMPONENTS_DIR}/cpp_wrappers/gpio/gpio.cpp

                ${COMPONENTS_DIR}/cpp_wrappers/bluetooth/bluetooth.hpp
                )

idf_component_register(SRCS ${CXX_SOURCES}
                       INCLUDE_DIRS "."
#                       REQUIRES bt
                       )
set_source_files_properties(${CXX_SOURCES} PROPERTIES COMPILE_FLAGS "-Wall -fconcepts -std=c++2a")
target_include_directories(${COMPONENT_LIB} PRIVATE ${MY_INCLUDE_DIRS})

set(COMPONENTS_DIR ../components)

set(MY_INCLUDE_DIRS ${COMPONENTS_DIR}
    ${COMPONENTS_DIR}/cpp_wrappers
    ${COMPONENTS_DIR}/cpp_wrappers/gpio
    ${COMPONENTS_DIR}/cpp_wrappers/bluetooth
    ${COMPONENTS_DIR}/cpp_wrappers/logger

    ${COMPONENTS_DIR}/freertos_cpp_wrappers/task
    ${COMPONENTS_DIR}/freertos_cpp_wrappers/queue
    ${COMPONENTS_DIR}/freertos_cpp_wrappers/semaphore
    ${COMPONENTS_DIR}/freertos_cpp_wrappers/mutex

    )

set(CXX_SOURCES main.cpp
                ${COMPONENTS_DIR}/cpp_wrappers/gpio/gpio.hpp
                ${COMPONENTS_DIR}/cpp_wrappers/gpio/gpio.cpp

                ${COMPONENTS_DIR}/cpp_wrappers/bluetooth/bluetooth.hpp
                ${COMPONENTS_DIR}/cpp_wrappers/bluetooth/bluetooth.cpp

                ${COMPONENTS_DIR}/cpp_wrappers/logger/esp_logger.hpp
                ${COMPONENTS_DIR}/cpp_wrappers/logger/esp_logger.cpp

                ${COMPONENTS_DIR}/freertos_cpp_wrappers/task/task.hpp
                ${COMPONENTS_DIR}/freertos_cpp_wrappers/task/task.cpp

                ${COMPONENTS_DIR}/freertos_cpp_wrappers/queue/queue.cpp
                ${COMPONENTS_DIR}/freertos_cpp_wrappers/queue/queue.hpp

                ${COMPONENTS_DIR}/freertos_cpp_wrappers/semaphore/semaphore.hpp

                ${COMPONENTS_DIR}/freertos_cpp_wrappers/mutex/my_mutex.hpp
                ${COMPONENTS_DIR}/freertos_cpp_wrappers/mutex/my_mutex.cpp


                )

idf_component_register(SRCS ${CXX_SOURCES}
                       INCLUDE_DIRS "."
                       )
set_source_files_properties(${CXX_SOURCES} PROPERTIES COMPILE_FLAGS "-Wall -fconcepts -std=c++2a")
target_include_directories(${COMPONENT_LIB} PRIVATE ${MY_INCLUDE_DIRS})

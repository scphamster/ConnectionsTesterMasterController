idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS ".")

set_source_files_properties(main.cpp PROPERTIES COMPILE_FLAGS "-Wall -fconcepts -std=c++2a")

set(MY_INCLUDE_DIRS ../components
                    ../components/cpp_wrappers
    ../components/cpp_wrappers/gpio


                       )

target_include_directories(${COMPONENT_LIB} PRIVATE ${MY_INCLUDE_DIRS} )

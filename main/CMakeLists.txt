idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS ".")

set_source_files_properties(main.cpp PROPERTIES COMPILE_FLAGS "-Wall -fconcepts -std=c++2a")

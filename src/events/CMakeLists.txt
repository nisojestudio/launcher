add_library(nlp3_events
    host_event.cpp
)

target_compile_features(nlp3_events PUBLIC cxx_std_20)
target_include_directories(nlp3_events PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nlp3_events PUBLIC nlp3_core)

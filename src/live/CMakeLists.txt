add_library(nlp3_live
    module.cpp
    normalizer.cpp
    host_event_adapter.cpp
)

target_compile_features(nlp3_live PUBLIC cxx_std_20)
target_include_directories(nlp3_live PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nlp3_live PUBLIC nlp3_core nlp3_events)

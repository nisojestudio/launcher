add_library(nlp3_core
    engine.cpp
    live_input.cpp
    runtime_state.cpp
)

target_compile_features(nlp3_core PUBLIC cxx_std_20)
target_include_directories(nlp3_core PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(nlp3_render
    module.cpp
)

target_compile_features(nlp3_render PUBLIC cxx_std_20)
target_include_directories(nlp3_render PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nlp3_render PUBLIC nlp3_core)

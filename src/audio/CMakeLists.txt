add_library(nlp3_audio
    module.cpp
)

target_compile_features(nlp3_audio PUBLIC cxx_std_20)
target_include_directories(nlp3_audio PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nlp3_audio PUBLIC nlp3_core)

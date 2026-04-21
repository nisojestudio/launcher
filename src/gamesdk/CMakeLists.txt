add_library(nlp3_gamesdk
    game_catalog.cpp
    game_config.cpp
    game_factory.cpp
    game_input_mapper.cpp
    game_manifest.cpp
    game_module.cpp
    game_registry.cpp
    game_runtime_controller.cpp
)

target_compile_features(nlp3_gamesdk PUBLIC cxx_std_20)
target_include_directories(nlp3_gamesdk PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nlp3_gamesdk PUBLIC nlp3_events)

add_library(nlp3_host
    host_automation.cpp
    host_periodic_tts.cpp
    host_runtime.cpp
    local_game_registry.cpp
    session_state.cpp
)

target_compile_features(nlp3_host PUBLIC cxx_std_20)
target_include_directories(nlp3_host PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nlp3_host
    PUBLIC
        nlp3_events
        nlp3_gamesdk
        nlp3_tts
        nlp3_bridge
        nlp3_core
)

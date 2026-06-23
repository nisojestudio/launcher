add_library(nlp3_games
    event_counter_game.cpp
    live_timer_game.cpp
)

target_compile_features(nlp3_games PUBLIC cxx_std_20)
target_include_directories(nlp3_games PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nlp3_games PUBLIC nlp3_gamesdk)

if(WIN32)
    target_link_libraries(nlp3_games PRIVATE winmm)
endif()

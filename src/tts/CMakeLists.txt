add_library(nlp3_tts
    mock_tts_backend.cpp
    null_tts_backend.cpp
    real_tts_backend.cpp
    tts_policy.cpp
    tts_queue.cpp
    tts_scheduler.cpp
    tts_service.cpp
    tts_template_formatter.cpp
    voice_catalog.cpp
)

target_compile_features(nlp3_tts PUBLIC cxx_std_20)
target_include_directories(nlp3_tts PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nlp3_tts PUBLIC nlp3_events)
if(WIN32)
    target_link_libraries(nlp3_tts PUBLIC sapi ole32)
endif()

add_library(nlp3_bridge
    bridge_adapter.cpp
    tiktok_bridge_controller.cpp
    tiktok_external_event_codec.cpp
    tiktok_external_session_status.cpp
    tiktok_external_session_status_codec.cpp
    tiktok_external_inbox_adapter.cpp
    tiktok_external_ws_server.cpp
    tiktok_external_event_replay.cpp
    tiktok_external_event_recorder.cpp
    tiktok_bridge_external_session.cpp
    tiktok_bridge_stub.cpp
    tiktok_external_event_source.cpp
    tiktok_event_mapper.cpp
    tiktok_stub_event_source.cpp
    tiktok_bridge_stub_session.cpp
)

target_compile_features(nlp3_bridge PUBLIC cxx_std_20)
target_include_directories(nlp3_bridge PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nlp3_bridge PUBLIC nlp3_events)

if(WIN32)
    target_link_libraries(nlp3_bridge PRIVATE ws2_32)
endif()

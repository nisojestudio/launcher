if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

add_executable(nlp3_smoke_test
    smoke_test.cpp
)

target_link_libraries(nlp3_smoke_test PRIVATE nlp3_platform)

add_test(NAME nlp3_smoke_test COMMAND nlp3_smoke_test)

add_executable(nlp3_panel_app_smoke_test
    panel_app_smoke_test.cpp
)

target_link_libraries(nlp3_panel_app_smoke_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_app_smoke_test COMMAND nlp3_panel_app_smoke_test)

add_executable(nlp3_panel_external_demo_test
    panel_external_demo_test.cpp
)

target_link_libraries(nlp3_panel_external_demo_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_external_demo_test COMMAND nlp3_panel_external_demo_test)

add_executable(nlp3_event_counter_game_test
    event_counter_game_test.cpp
)

target_link_libraries(nlp3_event_counter_game_test PRIVATE nlp3_platform)

add_test(NAME nlp3_event_counter_game_test COMMAND nlp3_event_counter_game_test)

add_executable(nlp3_game_runtime_registry_test
    game_runtime_registry_test.cpp
)

target_link_libraries(nlp3_game_runtime_registry_test PRIVATE nlp3_platform)

add_test(NAME nlp3_game_runtime_registry_test COMMAND nlp3_game_runtime_registry_test)

add_executable(nlp3_event_counter_runtime_test
    event_counter_runtime_test.cpp
)

target_link_libraries(nlp3_event_counter_runtime_test PRIVATE nlp3_platform)

add_test(NAME nlp3_event_counter_runtime_test COMMAND nlp3_event_counter_runtime_test)

add_executable(nlp3_panel_controller_test
    panel_controller_test.cpp
)

target_link_libraries(nlp3_panel_controller_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_controller_test COMMAND nlp3_panel_controller_test)

add_executable(nlp3_panel_external_storage_test
    panel_external_storage_test.cpp
)

target_link_libraries(nlp3_panel_external_storage_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_external_storage_test COMMAND nlp3_panel_external_storage_test)

add_executable(nlp3_host_runtime_pipeline_test
    host_runtime_pipeline_test.cpp
)

target_link_libraries(nlp3_host_runtime_pipeline_test PRIVATE nlp3_platform)

add_test(NAME nlp3_host_runtime_pipeline_test COMMAND nlp3_host_runtime_pipeline_test)

add_executable(nlp3_panel_external_ws_console_test
    panel_external_ws_console_test.cpp
)

target_link_libraries(nlp3_panel_external_ws_console_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_external_ws_console_test COMMAND nlp3_panel_external_ws_console_test)

add_executable(nlp3_panel_external_runner_test
    panel_external_runner_test.cpp
)

target_link_libraries(nlp3_panel_external_runner_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_external_runner_test COMMAND nlp3_panel_external_runner_test)

add_executable(nlp3_panel_http_ui_test
    panel_http_ui_test.cpp
)

target_link_libraries(nlp3_panel_http_ui_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_http_ui_test COMMAND nlp3_panel_http_ui_test)

add_executable(nlp3_window_host_test
    window_host_test.cpp
)

target_link_libraries(nlp3_window_host_test PRIVATE nlp3_platform)

add_test(NAME nlp3_window_host_test COMMAND nlp3_window_host_test)

add_executable(nlp3_webview_host_test
    webview_host_test.cpp
)

target_link_libraries(nlp3_webview_host_test PRIVATE nlp3_platform)

add_test(NAME nlp3_webview_host_test COMMAND nlp3_webview_host_test)

add_executable(nlp3_panel_run_mode_test
    panel_run_mode_test.cpp
)

target_link_libraries(nlp3_panel_run_mode_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_run_mode_test COMMAND nlp3_panel_run_mode_test)

add_executable(nlp3_panel_services_test
    panel_services_test.cpp
)

target_link_libraries(nlp3_panel_services_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_services_test COMMAND nlp3_panel_services_test)

add_executable(nlp3_support_bundle_sanitizer_test
    support_bundle_sanitizer_test.cpp
)

target_link_libraries(nlp3_support_bundle_sanitizer_test PRIVATE nlp3_platform)
target_link_libraries(nlp3_support_bundle_sanitizer_test PRIVATE nlohmann_json::nlohmann_json)

add_test(NAME nlp3_support_bundle_sanitizer_test COMMAND nlp3_support_bundle_sanitizer_test)

add_executable(nlp3_panel_config_storage_test
    panel_config_storage_test.cpp
)

target_link_libraries(nlp3_panel_config_storage_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_config_storage_test COMMAND nlp3_panel_config_storage_test)

add_executable(nlp3_remote_game_distribution_test
    remote_game_distribution_test.cpp
)

target_link_libraries(nlp3_remote_game_distribution_test PRIVATE nlp3_platform)

add_test(NAME nlp3_remote_game_distribution_test COMMAND nlp3_remote_game_distribution_test)

add_executable(nlp3_panel_console_general_test
    panel_console_general_test.cpp
)

target_link_libraries(nlp3_panel_console_general_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_console_general_test COMMAND nlp3_panel_console_general_test)

add_executable(nlp3_tts_template_formatter_test
    tts_template_formatter_test.cpp
)

target_link_libraries(nlp3_tts_template_formatter_test PRIVATE nlp3_tts)

add_test(NAME nlp3_tts_template_formatter_test COMMAND nlp3_tts_template_formatter_test)

add_executable(nlp3_tts_real_backend_test
    tts_real_backend_test.cpp
)

target_link_libraries(nlp3_tts_real_backend_test PRIVATE nlp3_tts)

add_test(NAME nlp3_tts_real_backend_test COMMAND nlp3_tts_real_backend_test)

add_executable(nlp3_tts_scheduler_test
    tts_scheduler_test.cpp
)

target_link_libraries(nlp3_tts_scheduler_test PRIVATE nlp3_tts)

add_test(NAME nlp3_tts_scheduler_test COMMAND nlp3_tts_scheduler_test)

add_executable(nlp3_tts_service_test
    tts_service_test.cpp
)

target_link_libraries(nlp3_tts_service_test PRIVATE nlp3_tts)

add_test(NAME nlp3_tts_service_test COMMAND nlp3_tts_service_test)

add_executable(nlp3_panel_external_core_test
    panel_external_core_test.cpp
)

target_link_libraries(nlp3_panel_external_core_test PRIVATE nlp3_platform)

add_test(NAME nlp3_panel_external_core_test COMMAND nlp3_panel_external_core_test)

add_executable(nlp3_local_game_registry_test
    local_game_registry_test.cpp
)

target_link_libraries(nlp3_local_game_registry_test PRIVATE nlp3_platform)

add_test(NAME nlp3_local_game_registry_test COMMAND nlp3_local_game_registry_test)

add_executable(nlp3_live_timer_game_test
    live_timer_game_test.cpp
)

target_link_libraries(nlp3_live_timer_game_test PRIVATE nlp3_platform)

add_test(NAME nlp3_live_timer_game_test COMMAND nlp3_live_timer_game_test)

add_executable(nlp3_live_timer_api_smoke_test
    live_timer_api_smoke_test.cpp
)

target_link_libraries(nlp3_live_timer_api_smoke_test PRIVATE nlp3_platform)

add_test(NAME nlp3_live_timer_api_smoke_test COMMAND nlp3_live_timer_api_smoke_test)

add_executable(nlp3_tiktok_external_event_codec_test
    tiktok_external_event_codec_test.cpp
)

target_link_libraries(nlp3_tiktok_external_event_codec_test PRIVATE nlp3_bridge)

add_test(NAME nlp3_tiktok_external_event_codec_test COMMAND nlp3_tiktok_external_event_codec_test)

add_library(nlp3_test_asserts INTERFACE)
target_compile_options(nlp3_test_asserts INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/FI${CMAKE_CURRENT_SOURCE_DIR}/test_asserts_enabled.hpp>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-include>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:${CMAKE_CURRENT_SOURCE_DIR}/test_asserts_enabled.hpp>
)

get_property(NLP3_TEST_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(NLP3_TEST_TARGET IN LISTS NLP3_TEST_TARGETS)
    if(NLP3_TEST_TARGET MATCHES "^nlp3_.*_test$")
        target_link_libraries(${NLP3_TEST_TARGET} PRIVATE nlp3_test_asserts)
    endif()
endforeach()

if(EXISTS "${PROJECT_SOURCE_DIR}/tools/bridge_py/.venv/Scripts/python.exe")
    set(NLP3_TEST_PYTHON "${PROJECT_SOURCE_DIR}/tools/bridge_py/.venv/Scripts/python.exe")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/.venv/Scripts/python.exe")
    set(NLP3_TEST_PYTHON "${PROJECT_SOURCE_DIR}/.venv/Scripts/python.exe")
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(NLP3_TEST_PYTHON "${Python3_EXECUTABLE}")
endif()

add_test(
    NAME nlp3_bridge_python_tests
    COMMAND
        "${NLP3_TEST_PYTHON}" -m unittest discover
        -s "${PROJECT_SOURCE_DIR}/tools/bridge_py/tests"
        -t "${PROJECT_SOURCE_DIR}/tools/bridge_py"
        -v
)

add_test(
    NAME nlp3_local_game_bridge_python_tests
    COMMAND
        "${NLP3_TEST_PYTHON}" -m unittest discover
        -s "${PROJECT_SOURCE_DIR}/tools/game_bridge_py/tests"
        -t "${PROJECT_SOURCE_DIR}/tools/game_bridge_py"
        -v
)

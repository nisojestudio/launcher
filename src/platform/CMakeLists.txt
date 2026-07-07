set(NLP3_PANEL_UI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ui")
set(NLP3_PANEL_UI_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/panel_ui")
set(NLP3_WEBVIEW2_SDK_DIR "${PROJECT_SOURCE_DIR}/third_party/webview2/sdk-1.0.3800.47")
set(NLP3_WEBVIEW2_INCLUDE_DIR "${NLP3_WEBVIEW2_SDK_DIR}/build/native/include")
set(NLP3_WEBVIEW2_LOADER_DLL "${NLP3_WEBVIEW2_SDK_DIR}/build/native/x64/WebView2Loader.dll")

if(NOT DEFINED NLP3_PANEL_VERSION)
    set(NLP3_PANEL_VERSION "0.2.13")
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

function(nlp3_embed_text_asset INPUT_PATH OUTPUT_PATH)
    add_custom_command(
        OUTPUT "${OUTPUT_PATH}"
        COMMAND
            "${CMAKE_COMMAND}"
            -DINPUT=${INPUT_PATH}
            -DOUTPUT=${OUTPUT_PATH}
            -P "${PROJECT_SOURCE_DIR}/cmake/embed_text_asset.cmake"
        DEPENDS
            "${INPUT_PATH}"
            "${PROJECT_SOURCE_DIR}/cmake/embed_text_asset.cmake"
        VERBATIM
    )
endfunction()

set(NLP3_PANEL_UI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ui")
set(NLP3_PANEL_UI_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/panel_ui")
set(NLP3_PANEL_OVERLAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/overlay")
set(NLP3_PANEL_OVERLAY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/overlay")

set(NLP3_PANEL_UI_INDEX_INC "${NLP3_PANEL_UI_GENERATED_DIR}/panel_ui_index_html.inc")
set(NLP3_PANEL_UI_STYLES_INC "${NLP3_PANEL_UI_GENERATED_DIR}/panel_ui_styles_css.inc")
set(NLP3_PANEL_UI_APP_INC "${NLP3_PANEL_UI_GENERATED_DIR}/panel_ui_app_js.inc")
set(NLP3_PANEL_UI_GAME_PREVIEWS_INC "${NLP3_PANEL_UI_GENERATED_DIR}/panel_ui_game_previews_js.inc")
set(NLP3_PANEL_OVERLAY_LIVE_TIMER_INC "${NLP3_PANEL_OVERLAY_GENERATED_DIR}/overlay_live_timer_html.inc")

nlp3_embed_text_asset("${NLP3_PANEL_UI_SOURCE_DIR}/index.html" "${NLP3_PANEL_UI_INDEX_INC}")
nlp3_embed_text_asset("${NLP3_PANEL_UI_SOURCE_DIR}/styles.css" "${NLP3_PANEL_UI_STYLES_INC}")
nlp3_embed_text_asset("${NLP3_PANEL_UI_SOURCE_DIR}/app.js" "${NLP3_PANEL_UI_APP_INC}")
nlp3_embed_text_asset("${NLP3_PANEL_UI_SOURCE_DIR}/game-previews.js" "${NLP3_PANEL_UI_GAME_PREVIEWS_INC}")
nlp3_embed_text_asset("${NLP3_PANEL_OVERLAY_DIR}/live-timer.html" "${NLP3_PANEL_OVERLAY_LIVE_TIMER_INC}")

add_custom_target(
    nlp3_panel_ui_assets
    DEPENDS
        "${NLP3_PANEL_UI_INDEX_INC}"
        "${NLP3_PANEL_UI_STYLES_INC}"
        "${NLP3_PANEL_UI_APP_INC}"
        "${NLP3_PANEL_UI_GAME_PREVIEWS_INC}"
        "${NLP3_PANEL_OVERLAY_LIVE_TIMER_INC}"
)

add_library(nlp3_platform
    runtime.cpp
    live_pipeline.cpp
    local_game_catalog_source.cpp
    local_license_service.cpp
    cloudflare_tunnel_service.cpp
    external_bridge_runner.cpp
    external_game_bridge_runner.cpp
    external_game_manifest.cpp
    external_game_state.cpp
    overlay_assets.cpp
    panel_http_json.cpp
    panel_http_server.cpp
    panel_activity.cpp
    panel_app.cpp
    panel_console.cpp
    panel_controller.cpp
    panel_config_storage.cpp
    panel_diagnostics_builder.cpp
    panel_snapshot_builder.cpp
    panel_ui_assets.cpp
    panel_updater_service.cpp
    panel_view_model_builder.cpp
    remote_game_distribution_service.cpp
    server_license_service.cpp
    device_identifier.cpp
    support_bundle_exporter.cpp
    support_bundle_sanitizer.cpp
    win_http_client.cpp
    webview_host.cpp
    window_host.cpp
)

set_source_files_properties(
    panel_ui_assets.cpp
    PROPERTIES
        OBJECT_DEPENDS
            "${NLP3_PANEL_UI_INDEX_INC};${NLP3_PANEL_UI_STYLES_INC};${NLP3_PANEL_UI_APP_INC};${NLP3_PANEL_UI_GAME_PREVIEWS_INC}"
)
set_source_files_properties(
    overlay_assets.cpp
    PROPERTIES
        OBJECT_DEPENDS
            "${NLP3_PANEL_OVERLAY_LIVE_TIMER_INC}"
)

target_compile_definitions(nlp3_platform PRIVATE NLP3_PANEL_VERSION="${NLP3_PANEL_VERSION}")
target_compile_features(nlp3_platform PUBLIC cxx_std_20)
target_include_directories(
    nlp3_platform
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
    PRIVATE
        "${NLP3_PANEL_UI_GENERATED_DIR}"
        "${NLP3_PANEL_OVERLAY_GENERATED_DIR}"
)
if(WIN32)
    target_include_directories(
        nlp3_platform
        PRIVATE
            "${NLP3_WEBVIEW2_INCLUDE_DIR}"
    )
endif()
target_link_libraries(nlp3_platform
    PUBLIC
        nlp3_host
    PRIVATE
        nlp3_bridge
        nlp3_events
        nlp3_games
        nlp3_gamesdk
        nlp3_live
        nlohmann_json::nlohmann_json
        nlp3_tts
)
if(WIN32)
    target_link_libraries(nlp3_platform PRIVATE ws2_32 shell32 ole32 winhttp bcrypt)
else()
    message(STATUS "Nisoje Studio is currently Windows-first; Win32 UI/runtime link dependencies are disabled on non-Windows builds.")
endif()
add_dependencies(nlp3_platform nlp3_panel_ui_assets)

if(WIN32)
    add_executable(nlp3_app WIN32 main.cpp app_icon.rc)
else()
    add_executable(nlp3_app main.cpp)
endif()

target_link_libraries(nlp3_app PRIVATE nlp3_platform)
set_target_properties(nlp3_app PROPERTIES OUTPUT_NAME "NisojeStudio")

if(WIN32)
    add_custom_command(
        TARGET nlp3_app
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${NLP3_WEBVIEW2_LOADER_DLL}"
            "$<TARGET_FILE_DIR:nlp3_app>/WebView2Loader.dll"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:nlp3_app>"
            "$<TARGET_FILE_DIR:nlp3_app>/nlp3_app.exe"
        VERBATIM
    )
endif()

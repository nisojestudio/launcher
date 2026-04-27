#include "platform/panel_ui_assets.hpp"

namespace {

static constexpr const char* kPanelUiIndexHtml =
#include "panel_ui_index_html.inc"
;

static constexpr const char* kPanelUiStylesCss =
#include "panel_ui_styles_css.inc"
;

static constexpr const char* kPanelUiAppJs =
#include "panel_ui_app_js.inc"
;

static constexpr const char* kPanelUiGamePreviewsJs =
#include "panel_ui_game_previews_js.inc"
;

} // namespace

namespace nlp3::platform {

std::string_view panel_ui_index_html() noexcept {
    return kPanelUiIndexHtml;
}

std::string_view panel_ui_styles_css() noexcept {
    return kPanelUiStylesCss;
}

std::string_view panel_ui_app_js() noexcept {
    return kPanelUiAppJs;
}

std::string_view panel_ui_game_previews_js() noexcept {
    return kPanelUiGamePreviewsJs;
}

} // namespace nlp3::platform

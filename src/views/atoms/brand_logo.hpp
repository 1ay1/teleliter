#pragma once

#include <array>
#include <string>

#include <maya/maya.hpp>

#include "views/theme.hpp"

// ─── teleliter wordmark ──────────────────────────────────────────────────────
//
// 3-row box-drawing wordmark, center-aligned in the chat panel.
//
//     ╶┬╴┌─╴╷  ┌─╴╷  ╷╶┬╴┌─╴┌─┐
//      │ ├╴ │  ├╴ │  │ │ ├╴ ├┬┘
//      ╵ └─╴└─╴└─╴└─╴╵ ╵ └─╴╵└╴
//
// Bold cyan, no border, no padding frame. On narrow panels the wordmark
// is suppressed in favour of a 1-row `tl` chip so the search box still
// has room.

namespace tl::views {

[[nodiscard]] inline maya::Element render_brand_logo(
    int panel_w, bool /*caret_visible*/)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto brand_st = Style{}.with_fg(palette::brand()).with_bold();

    // Wordmark is 26 cells wide. Need a bit of breathing room on each
    // side, so require panel_w ≥ 28 for the full art; otherwise drop to
    // a compact one-row chip.
    if (panel_w < 28) {
        return hstack().grow(1)(
            spacer(),
            text(std::string{"\xe1\xb4\x9b\xca\x9f"}, brand_st),   // ᴛʟ
            spacer()
        );
    }

    static const std::array<std::string, 3> rows = {
        std::string{"\u2576\u252c\u2574\u250c\u2500\u2574\u2577  \u250c\u2500\u2574\u2577  \u2577\u2576\u252c\u2574\u250c\u2500\u2574\u250c\u2500\u2510"},
        std::string{" \u2502 \u251c\u2574 \u2502  \u251c\u2574 \u2502  \u2502 \u2502 \u251c\u2574 \u251c\u252c\u2518"},
        std::string{" \u2575 \u2514\u2500\u2574\u2514\u2500\u2574\u2514\u2500\u2574\u2514\u2500\u2574\u2575 \u2575 \u2514\u2500\u2574\u2575\u2514\u2574"},
    };

    return vstack().gap(0)(
        hstack().grow(1)(spacer(), text(rows[0], brand_st), spacer()),
        hstack().grow(1)(spacer(), text(rows[1], brand_st), spacer()),
        hstack().grow(1)(spacer(), text(rows[2], brand_st), spacer())
    );
}

}  // namespace tl::views

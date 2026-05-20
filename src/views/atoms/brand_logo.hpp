#pragma once

#include <string>

#include <maya/maya.hpp>

#include "views/theme.hpp"

// ─── teleliter wordmark ──────────────────────────────────────────────────────
//
// Borderless single-row brand strip. Text-only — no box, no padding
// frame — keeps the sidebar dense and lets the search input below sit
// flush near the top.
//
//   Compact panel_w ≥ 22   →  `❯_ teleliter`
//   Minimal panel_w < 22   →  `❯ tl_`
//
// Visual language:
//   ▸ accent-colored prompt `❯` + blinking `_` caret (same clock the
//     composer + search caret use, so every pulsing thing on screen is
//     in phase)
//   ▸ brand-color bold wordmark (cyan)

namespace tl::views {

[[nodiscard]] inline maya::Element render_brand_logo(
    int panel_w, bool caret_visible)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto accent_st = Style{}.with_fg(palette::accent()).with_bold();
    const auto brand_st  = Style{}.with_fg(palette::brand()).with_bold();

    // ─── Minimal — bare `❯ tl_` chip. ──────────────────────
    if (panel_w < 22) {
        return hstack().gap(0)(
            text(std::string{"\u276F "}, accent_st),
            text(std::string{"tl"},      brand_st),
            text(caret_visible ? std::string{"_"} : std::string{" "},
                 accent_st)
        );
    }

    return hstack().gap(0).align_items(Align::Center)(
        text(std::string{"\u276F"}, accent_st),                  // ❯
        text(caret_visible ? std::string{"_"} : std::string{" "},
             accent_st),
        text(std::string{" teleliter"}, brand_st)
    );
}

}  // namespace tl::views

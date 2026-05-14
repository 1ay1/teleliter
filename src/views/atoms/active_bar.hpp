#pragma once

#include <maya/maya.hpp>

#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// The 1-cell vertical accent stripe drawn at the left edge of an active /
// selected list row. Width stays 1 cell whether shown or hidden — for
// inactive rows we emit a single space so column alignment is preserved.

[[nodiscard]] inline maya::Element render_active_bar(bool active, bool flash = false)
{
    using namespace maya;
    using namespace maya::dsl;

    if (!active) return text(std::string{" "});

    // Full block (█) reads as a clean Telegram-Web-style "left rail"
    // highlight; ▌ was thin enough to disappear at small font sizes.
    const auto color = flash ? palette::amber() : palette::accent();
    return text(std::string{"█"},
        Style{}.with_fg(color).with_bold());
}

[[nodiscard]] inline maya::Element render_selector_caret(bool active)
{
    using namespace maya;
    using namespace maya::dsl;
    if (!active) return text(std::string{"  "});
    // ▶ is heavier than ›/❯ — reads as a clear pointer at any font size.
    return text(std::string{" ▶"},
        Style{}.with_fg(palette::accent()).with_bold());
}

}  // namespace tl::views

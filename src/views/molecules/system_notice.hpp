#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Centered, dim, italic "• X joined •" type line. Used for join / leave /
// system notice messages that don't deserve a bubble.

[[nodiscard]] inline maya::Element render_system_notice(std::string_view body)
{
    using namespace maya;
    using namespace maya::dsl;
    // width(100%) is load-bearing here — without an explicit width the
    // hstack shrinks to content and the leading/trailing spacers have
    // nothing to fill, leaving the notice left-aligned instead of
    // centered.
    return hstack().width(Dimension::percent(100)).justify(Justify::Center)(
        text(std::string{glyph::bullet} + " " + std::string{body}
             + " " + std::string{glyph::bullet},
            Style{}.with_fg(palette::dim()).with_italic())
    );
}

[[nodiscard]] inline maya::Element render_action_line(
    std::string_view author,
    std::string_view body,
    maya::Color author_tint)
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack().gap(1)(
        text(std::string{"* "}, Style{}.with_fg(palette::muted())),
        text(std::string{author} + " " + std::string{body},
            Style{}.with_fg(author_tint).with_italic())
    );
}

}  // namespace tl::views

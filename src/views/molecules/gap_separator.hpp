#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Centered "─── 5m later ───" divider drawn between messages that have a
// large time gap. Caller passes the pre-computed label.

[[nodiscard]] inline maya::Element render_gap_separator(std::string_view label)
{
    using namespace maya;
    using namespace maya::dsl;

    // One text node, single style — avoids any chance of an hstack with
    // gap() + justify(Center) collapsing its children to zero width, and
    // guarantees the label always renders next to its rules. Heavy box
    // glyph (━) reads at any font weight; muted-italic label is dim
    // enough to not compete with bubbles but bright enough to see.
    auto body = std::string{"━━━━━━━  "} + std::string{label}
              + std::string{"  ━━━━━━━"};
    return hstack()
        .width(Dimension::percent(100))
        .justify(Justify::Center)
        (text(body, Style{}.with_fg(palette::muted()).with_italic()));
}

}  // namespace tl::views

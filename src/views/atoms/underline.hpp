#pragma once

#include <cstddef>
#include <string>

#include <maya/maya.hpp>

#include "views/theme.hpp"

namespace tl::views {

// A heavy-dash strip used as the active-tab indicator. The dash width is
// expressed in cells; the caller knows how wide the labelled segment is.

[[nodiscard]] inline maya::Element render_underline(
    std::size_t cells,
    maya::Color color = palette::accent())
{
    using namespace maya;
    using namespace maya::dsl;
    // U+2501 ━ is one cell wide and reads as a heavy stroke.
    std::string dashes;
    dashes.reserve(cells * 3);
    for (std::size_t i = 0; i < cells; ++i) dashes += "━";
    return text(dashes, Style{}.with_fg(color).with_bold());
}

}  // namespace tl::views

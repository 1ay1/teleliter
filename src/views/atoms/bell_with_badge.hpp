#pragma once

#include <string>

#include <maya/maya.hpp>

#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// 🔔 with optional numeric badge. count == 0 returns the bell alone; count
// > 0 attaches a small amber badge. Used in the header right cluster as
// the "you have N pending notifications" indicator.

[[nodiscard]] inline maya::Element render_bell_with_badge(int count)
{
    using namespace maya;
    using namespace maya::dsl;
    if (count <= 0) {
        return text(std::string{"🔔"}, Style{}.with_fg(palette::muted()));
    }
    return hstack().gap(1)(
        text(std::string{"🔔"}, Style{}.with_fg(palette::amber())),
        text(std::to_string(count),
            Style{}.with_fg(palette::amber()).with_bold())
    );
}

}  // namespace tl::views

#pragma once

#include <string>

#include <maya/maya.hpp>

#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// One animated braille frame. Driver picks the frame from a tick counter
// (e.g. m.tick / 8) — keeps the widget pure.

[[nodiscard]] inline maya::Element render_spinner(int tick)
{
    using namespace maya;
    using namespace maya::dsl;
    const auto idx = static_cast<std::size_t>((tick % static_cast<int>(glyph::spinner_frames.size())
                       + static_cast<int>(glyph::spinner_frames.size()))
                      % static_cast<int>(glyph::spinner_frames.size()));
    return text(std::string{glyph::spinner_frames[idx]},
        Style{}.with_fg(palette::accent()).with_bold());
}

[[nodiscard]] inline maya::Element render_typing_dots(int tick)
{
    using namespace maya;
    using namespace maya::dsl;
    static constexpr const char* frames[4] = {"   ", ".  ", ".. ", "..."};
    const auto idx = static_cast<std::size_t>(((tick / 3) % 4 + 4) % 4);
    return text(std::string{frames[idx]},
        Style{}.with_fg(palette::muted()));
}

}  // namespace tl::views

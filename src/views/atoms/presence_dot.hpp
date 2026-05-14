#pragma once

#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// A single coloured glyph. Active/Away/Dnd are filled; Offline is a hollow
// ring. The four ANSI presence colors are defined in theme.hpp.

[[nodiscard]] inline maya::Element render_presence_dot(model::Presence p)
{
    using namespace maya;
    using namespace maya::dsl;
    const bool offline = (p == model::Presence::Offline);
    const auto sym = offline ? glyph::dot_off : glyph::dot_on;
    return text(std::string{sym},
        Style{}.with_fg(palette::presence_of(p)).with_bold());
}

[[nodiscard]] inline std::string presence_label(model::Presence p) noexcept
{
    switch (p) {
        case model::Presence::Active:  return "active";
        case model::Presence::Away:    return "away";
        case model::Presence::Dnd:     return "do not disturb";
        case model::Presence::Offline: return "offline";
    }
    return "offline";
}

[[nodiscard]] inline maya::Element render_presence(model::Presence p)
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack().gap(1)(
        render_presence_dot(p),
        text(presence_label(p), Style{}.with_fg(palette::presence_of(p)))
    );
}

}  // namespace tl::views

#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/clock.hpp"
#include "views/atoms/presence_dot.hpp"
#include "views/theme.hpp"

namespace tl::views {

// "● 12:34" — the user's own status indicator at the right of the header.
// Optional handle ("@ayush") between the dot and the clock.

[[nodiscard]] inline maya::Element render_self_presence_chip(
    model::Presence presence,
    std::string_view clock_text = "",
    std::string_view handle = "")
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack().gap(1)(
        render_presence_dot(presence),
        handle.empty() ? text(std::string{})
                       : text(std::string{handle}, Style{}.with_fg(palette::muted())),
        clock_text.empty() ? text(std::string{}) : render_clock(clock_text)
    );
}

}  // namespace tl::views

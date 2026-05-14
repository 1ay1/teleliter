#pragma once

#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Read-receipt indicator on outgoing messages. Sending → hollow circle.
// Sent → single tick (dim). Delivered → double tick (dim). Read → double
// tick (accent / cyan).

[[nodiscard]] inline maya::Element render_checkmark(model::ReadState s)
{
    using namespace maya;
    using namespace maya::dsl;

    switch (s) {
        case model::ReadState::Sending:
            return text(std::string{glyph::circle_o},
                Style{}.with_fg(palette::check_sent()).with_dim());
        case model::ReadState::Sent:
            return text(std::string{glyph::check_one},
                Style{}.with_fg(palette::check_done()));
        case model::ReadState::Delivered:
            return text(std::string{glyph::check_two},
                Style{}.with_fg(palette::check_done()));
        case model::ReadState::Read:
            return text(std::string{glyph::check_two},
                Style{}.with_fg(palette::check_read()).with_bold());
    }
    return text(std::string{});
}

}  // namespace tl::views

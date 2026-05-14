#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/bell_with_badge.hpp"
#include "views/atoms/dot_sep.hpp"
#include "views/atoms/presence_dot.hpp"
#include "views/glyphs.hpp"
#include "views/molecules/self_presence_chip.hpp"
#include "views/theme.hpp"

namespace tl::views {

// The compact cluster at the right edge of the header:
//   [mention_count][unread_count]  bell  ·  ● self_clock
// Mentions take priority over unread (mutually exclusive).

[[nodiscard]] inline maya::Element render_header_right_cluster(
    int mentions_pending,
    int unread_pending,
    model::Presence self_presence,
    std::string_view self_clock = "")
{
    using namespace maya;
    using namespace maya::dsl;

    std::vector<Element> items;

    if (mentions_pending > 0) {
        items.push_back(text(glyph::circled_digit(mentions_pending),
            Style{}.with_fg(palette::amber()).with_bold()));
        items.push_back(text(std::string{"  "}));
    } else if (unread_pending > 0) {
        items.push_back(text(std::to_string(unread_pending),
            Style{}.with_fg(palette::amber()).with_bold()));
        items.push_back(text(std::string{"  "}));
    }

    items.push_back(render_bell_with_badge(mentions_pending + unread_pending > 0
        ? mentions_pending + unread_pending : 0));
    items.push_back(render_dot_sep());
    items.push_back(render_self_presence_chip(self_presence, self_clock));

    return hstack().align_items(Align::Start)(items);
}

}  // namespace tl::views

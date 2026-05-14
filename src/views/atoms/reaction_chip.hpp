#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// A pill-like chip: emoji · count. self_reacted is shown by bolding + accent
// color (no bg, per the ANSI/no-forced-bg rule).

[[nodiscard]] inline maya::Element render_reaction_chip(
    std::string_view emoji,
    int count,
    bool self_reacted)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto sty = self_reacted
        ? Style{}.with_fg(palette::accent()).with_bold()
        : Style{}.with_fg(palette::amber()).with_dim();

    return text(std::string{emoji} + " " + std::to_string(count), sty);
}

[[nodiscard]] inline maya::Element render_reaction_chip(const model::ReactionVM& r)
{
    return render_reaction_chip(r.emoji, r.count, r.self_reacted);
}

}  // namespace tl::views

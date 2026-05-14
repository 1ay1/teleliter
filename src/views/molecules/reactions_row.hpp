#pragma once

#include <span>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/reaction_chip.hpp"

namespace tl::views {

[[nodiscard]] inline maya::Element render_reactions_row(
    std::span<const model::ReactionVM> reactions)
{
    using namespace maya;
    using namespace maya::dsl;

    if (reactions.empty()) return text(std::string{});

    std::vector<Element> chips;
    chips.reserve(reactions.size());
    for (const auto& r : reactions) chips.push_back(render_reaction_chip(r));

    return hstack().gap(1)(chips);
}

}  // namespace tl::views

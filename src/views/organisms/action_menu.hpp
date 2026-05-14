#pragma once

#include <span>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/molecules/action_button.hpp"
#include "views/molecules/section_header.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Section header + vertical list of action buttons. Used in the right
// info / member panels for "invite someone, channel settings, leave…"

[[nodiscard]] inline maya::Element render_action_menu(
    std::string_view section,
    std::span<const model::ActionItemVM> actions)
{
    using namespace maya;
    using namespace maya::dsl;

    std::vector<Element> rows;
    rows.reserve(actions.size() + 2);
    rows.push_back(render_section_header(section));
    rows.push_back(text(std::string{}));
    for (const auto& a : actions) rows.push_back(render_action_button(a));
    return vstack().gap(0)(rows);
}

}  // namespace tl::views

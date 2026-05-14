#pragma once

#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// One entry in an action menu — icon + label + optional kbd shortcut.
//   +   invite someone               i
// Icon is accent if ActionItemVM::accent is true, muted otherwise.

[[nodiscard]] inline maya::Element render_action_button(const model::ActionItemVM& a)
{
    using namespace maya;
    using namespace maya::dsl;
    const auto icon_c = a.accent ? palette::accent() : palette::muted();
    auto shortcut_el = a.shortcut.empty()
        ? text(std::string{})
        : text(a.shortcut, Style{}.with_fg(palette::dim()));

    return hstack().width(Dimension::percent(100)).padding(0, 1, 0, 1)(
        text(std::string{"  "} + a.glyph + std::string{"  "},
            Style{}.with_fg(icon_c).with_bold()),
        text(a.label, Style{}.with_fg(palette::text())),
        spacer(),
        shortcut_el
    );
}

}  // namespace tl::views

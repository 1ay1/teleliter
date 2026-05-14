#pragma once

#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Icon + label + on/off pill, full-width row. The toggle indicator is
// just colored text — no checkbox glyph, keeps it readable everywhere.

[[nodiscard]] inline maya::Element render_toggle_row(const model::ToggleVM& t)
{
    using namespace maya;
    using namespace maya::dsl;
    const auto state_color = t.on ? palette::green() : palette::muted();
    const auto state_label = t.on ? t.on_label : t.off_label;

    return hstack().width(Dimension::percent(100)).padding(0, 1, 0, 1)(
        text(std::string{" "} + t.glyph + "  ",
            Style{}.with_fg(palette::muted())),
        text(t.label, Style{}.with_fg(palette::text())),
        spacer(),
        text(state_label + std::string{" "},
            Style{}.with_fg(state_color).with_bold())
    );
}

}  // namespace tl::views

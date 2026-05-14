#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// One tab label. Active tab is bold + accent; inactive is muted. Optional
// numeric badge appears after the label.

[[nodiscard]] inline maya::Element render_tab(
    std::string_view label,
    bool active,
    int badge_count = 0)
{
    using namespace maya;
    using namespace maya::dsl;
    auto sty = active
        ? Style{}.with_fg(palette::accent()).with_bold()
        : Style{}.with_fg(palette::muted());

    if (badge_count <= 0) {
        return text(std::string{label}, sty);
    }
    return hstack().gap(1)(
        text(std::string{label}, sty),
        text(std::to_string(badge_count),
            Style{}.with_fg(palette::amber()).with_bold())
    );
}

[[nodiscard]] inline maya::Element render_tab(const model::TabVM& t, bool active)
{
    return render_tab(t.label, active, t.badge_count);
}

}  // namespace tl::views

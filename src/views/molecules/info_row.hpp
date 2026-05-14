#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "views/theme.hpp"

namespace tl::views {

// Icon + value-over-label pair as used inside info panels:
//   ☎   +1 555 0100
//       Phone

[[nodiscard]] inline maya::Element render_info_row(
    std::string_view icon,
    std::string_view label,
    std::string_view value)
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack()
        .width(Dimension::percent(100))
        .padding(0, 1, 0, 1)
        .align_items(Align::Start)
        (
            text(std::string{" "} + std::string{icon} + "  ",
                Style{}.with_fg(palette::muted())),
            vstack()(
                text(std::string{value}, Style{}.with_fg(palette::text())),
                text(std::string{label}, Style{}.with_fg(palette::dim()))
            )
        );
}

// Single-row "label : value" — used in dm_info_panel's notification toggle
// area and similar k/v lines that don't need a stacked layout.
[[nodiscard]] inline maya::Element render_kv_row(
    std::string_view label,
    std::string_view value,
    maya::Color value_color = palette::text())
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack()
        .width(Dimension::percent(100))
        .padding(0, 1, 0, 1)
        (
            text(std::string{label}, Style{}.with_fg(palette::muted())),
            spacer(),
            text(std::string{value},
                Style{}.with_fg(value_color).with_bold())
        );
}

}  // namespace tl::views

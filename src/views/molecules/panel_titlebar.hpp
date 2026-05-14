#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "views/atoms/close_button.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Title strip used at the top of side-panels. Bold muted label on the
// left, a close ✕ on the right.

[[nodiscard]] inline maya::Element render_panel_titlebar(
    std::string_view title,
    bool closeable = true)
{
    using namespace maya;
    using namespace maya::dsl;
    auto right = closeable
        ? render_close_button(false)
        : text(std::string{});

    return hstack().width(Dimension::percent(100))(
        text(std::string{" "} + std::string{title},
            Style{}.with_fg(palette::muted()).with_bold()),
        spacer(),
        right,
        text(std::string{" "})
    );
}

}  // namespace tl::views

#pragma once

#include <string>

#include <maya/maya.hpp>

#include "views/theme.hpp"

namespace tl::views {

// "✕" rendered in muted bold — sits in the top-right of panel title bars.

[[nodiscard]] inline maya::Element render_close_button(bool focused = false)
{
    using namespace maya;
    using namespace maya::dsl;
    const auto fg = focused ? palette::accent() : palette::muted();
    return text(std::string{"✕"}, Style{}.with_fg(fg).with_bold());
}

}  // namespace tl::views

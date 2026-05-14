#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "views/theme.hpp"

namespace tl::views {

// A glyph (typically emoji or symbol) plus optional inline label. State
// modifiers light up the foreground; no forced backgrounds.

enum class ButtonState : unsigned char {
    Idle,
    Hover,    // not yet wired — reserved
    Active,   // pressed / send-armed
    Disabled,
};

[[nodiscard]] inline maya::Element render_icon_button(
    std::string_view glyph,
    std::string_view label = "",
    ButtonState state = ButtonState::Idle)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto fg = [&] {
        switch (state) {
            case ButtonState::Active:   return palette::accent();
            case ButtonState::Hover:    return palette::brand();
            case ButtonState::Disabled: return palette::dim();
            case ButtonState::Idle:     return palette::muted();
        }
        return palette::muted();
    }();

    const auto sty = (state == ButtonState::Active)
        ? Style{}.with_fg(fg).with_bold()
        : Style{}.with_fg(fg);

    if (label.empty()) return text(std::string{glyph}, sty);
    return hstack().gap(1)(
        text(std::string{glyph}, sty),
        text(std::string{label}, sty)
    );
}

}  // namespace tl::views

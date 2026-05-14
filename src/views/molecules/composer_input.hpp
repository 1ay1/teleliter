#pragma once

#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// The textfield itself (no border, no buttons, no surrounding chrome).
// Renders the text, a blinking caret at cursor_bytes, and a placeholder
// when empty.

[[nodiscard]] inline maya::Element render_composer_input(
    const model::ComposerVM& c,
    bool focused)
{
    using namespace maya;
    using namespace maya::dsl;

    const bool empty       = c.text.empty();
    const bool is_command  = !empty && c.text.front() == '/';

    // Solid block cursor (█) — instantly readable as "your typing
    // target". Visibility blinks via c.caret_visible, which the program
    // toggles every Tick.
    const bool show_caret = focused && c.caret_visible;
    const auto caret_sty   = Style{}.with_fg(palette::accent()).with_bold();
    const auto caret_text  = show_caret
        ? text(std::string{"█"}, caret_sty)
        : text(std::string{" "});

    const auto prompt_sty  = is_command
        ? Style{}.with_fg(palette::amber()).with_bold()
        : Style{}.with_fg(palette::accent()).with_bold();

    if (empty) {
        return hstack().gap(1)(
            text(std::string{glyph::prompt}, prompt_sty),
            text(std::string{"Type a message…"},
                Style{}.with_fg(palette::muted()).with_italic()),
            caret_text
        );
    }

    const auto cur = std::min(c.cursor_bytes, c.text.size());
    auto before = c.text.substr(0, cur);
    auto after  = c.text.substr(cur);

    const auto body_sty = is_command
        ? Style{}.with_fg(palette::amber())
        : Style{}.with_fg(palette::text());

    return hstack().gap(1)(
        text(std::string{glyph::prompt}, prompt_sty),
        text(before, body_sty),
        caret_text,
        text(after, body_sty)
    );
}

}  // namespace tl::views

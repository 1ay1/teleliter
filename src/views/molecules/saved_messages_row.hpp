#pragma once

#include <string>

#include <maya/maya.hpp>

#include "views/atoms/avatar.hpp"
#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// The pinned "Saved Messages" entry at the very top of a chat list. Same
// shape as a regular chat_row but with a bookmark glyph in the trailing
// slot and a fixed "your notes & links" subtitle.

[[nodiscard]] inline maya::Element render_saved_messages_row()
{
    using namespace maya;
    using namespace maya::dsl;

    auto top = hstack().width(Dimension::percent(100)).align_items(Align::Start)(
        text(std::string{" "}),
        text(std::string{"  "}),
        text(std::string{" "}),
        render_avatar("SM", palette::accent()),
        text(std::string{"  "}),
        text(std::string{"Saved Messages"},
            Style{}.with_fg(palette::text()).with_bold()),
        spacer(),
        text(std::string{"🔖 "},
            Style{}.with_fg(palette::accent()))
    );

    auto bottom = hstack().width(Dimension::percent(100))(
        text(std::string{" "}),
        text(std::string{"         "}),
        text(std::string{"your notes & links"},
            Style{}.with_fg(palette::muted()).with_italic())
    );

    return vstack()(top, bottom);
}

}  // namespace tl::views

#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "views/theme.hpp"

namespace tl::views {

// A bordered single-row search input with:
//   - 🔍 icon prefix
//   - placeholder italic when empty, plain text when typed
//   - solid block █ caret blinking via `caret_visible` when focused
//   - clear (✕) button at the right edge when there's a query — wired
//     up by app/mouse.hpp's `is_search_clear` hit-test
//
// Border is round; tint flips to accent when focused (typing target).

[[nodiscard]] inline maya::Element render_search_input(
    std::string_view query,
    std::string_view placeholder = "Search",
    bool focused = false,
    bool caret_visible = true)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto border_c = focused ? palette::border_focus()
                                  : palette::border_idle();

    const bool show_caret = focused && caret_visible;
    const auto caret_sty  = Style{}.with_fg(palette::accent()).with_bold();
    auto caret = show_caret
        ? text(std::string{"█"}, caret_sty)
        : text(std::string{" "});

    // Clear (✕) button — appears whenever there's a query, even if the
    // search isn't currently focused, so users can wipe it with a click
    // without having to first focus the input.
    auto clear_btn = !query.empty()
        ? text(std::string{"✕"},
            Style{}.with_fg(focused ? palette::accent() : palette::muted())
                   .with_bold())
        : text(std::string{" "});

    auto icon = text(std::string{"🔍"},
        Style{}.with_fg(focused ? palette::accent() : palette::muted()));

    auto query_view = query.empty()
        ? text(std::string{placeholder},
            Style{}.with_fg(palette::dim()).with_italic())
        : text(std::string{query},
            Style{}.with_fg(palette::text()));

    return hstack()
        .width(Dimension::percent(100))
        .border(BorderStyle::Round, border_c)
        .padding(0, 1)
        .gap(1)
        .align_items(Align::Center)
        (
            icon,
            query_view,
            caret,
            spacer(),
            clear_btn
        );
}

}  // namespace tl::views

#pragma once

#include <utility>

#include <maya/maya.hpp>
#include <maya/widget/scrollable.hpp>
#include <maya/widget/scrollbar.hpp>

#include "views/theme.hpp"

namespace tl::views::util {

// teleliter's house scrollbar style — thin dim track always visible
// (so you know the column is scrollable) + solid full-block bright-
// white thumb (visible on any terminal theme, doesn't rely on the
// accent color being legible). Works in any terminal because both
// glyphs are core box-drawing.
[[nodiscard]] inline maya::ScrollbarStyle scrollbar_style() noexcept
{
    using namespace maya;
    return {
        .track_glyph   = "│",
        .thumb_glyph   = "█",
        .track_glyph_h = "─",
        .thumb_glyph_h = "█",
        .track_color   = Color::bright_black(),
        .thumb_color   = Color::bright_white(),
    };
}

// Wraps an Element in a vertically-scrollable viewport with a thin themed
// scrollbar on the right. `state` is a mutable ScrollState the caller owns
// (typically a member of the Model marked `mutable`). `viewport_height` of
// 0 means "use the entire allocated height".
//
// Usage:
//   util::scrolly_with_bar(make_list(), model.list_scroll, 0);

[[nodiscard]] inline maya::Element scrolly_with_bar(
    maya::Element inner,
    maya::ScrollState& state,
    int viewport_height = 0,
    maya::ScrollbarStyle style = maya::ScrollbarStyle::minimal())
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack().grow(1)(
        std::move(inner) | scrolly(state, viewport_height) | grow(1),
        scrollbar_y(state, viewport_height, style)
    );
}

// Horizontal counterpart: tabs strip + a narrow x-scrollbar below it.
[[nodiscard]] inline maya::Element scrollx_with_bar(
    maya::Element inner,
    maya::ScrollState& state,
    int viewport_width = 0,
    maya::ScrollbarStyle style = maya::ScrollbarStyle::minimal())
{
    using namespace maya;
    using namespace maya::dsl;
    return vstack()(
        std::move(inner) | scrollx(state, viewport_width),
        scrollbar_x(state, viewport_width, style)
    );
}

}  // namespace tl::views::util

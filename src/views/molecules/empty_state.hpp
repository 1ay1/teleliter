#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// A vertically-centered placeholder: glyph + headline + optional lines.

[[nodiscard]] inline maya::Element render_empty_state(
    std::string_view headline,
    std::span<const std::string_view> lines = {})
{
    using namespace maya;
    using namespace maya::dsl;

    std::vector<Element> rows;
    rows.reserve(2 + lines.size());
    rows.push_back(text(std::string{glyph::dim_circle},
        Style{}.with_fg(palette::muted())));
    rows.push_back(text(std::string{headline},
        Style{}.with_fg(palette::muted()).with_bold()));
    for (auto l : lines) {
        rows.push_back(text(std::string{l},
            Style{}.with_fg(palette::muted())));
    }

    return vstack()
        .grow(1)
        .justify(Justify::Center)
        .align_items(Align::Center)
        .gap(1)
        (rows);
}

}  // namespace tl::views

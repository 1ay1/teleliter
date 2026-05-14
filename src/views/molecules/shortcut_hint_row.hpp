#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Footer-style row: key glyph on the left, label pushed to the right,
// label muted. Use inside the bottom slot of a long sidebar list to
// surface keyboard affordances.

[[nodiscard]] inline maya::Element render_shortcut_hint_row(
    std::string_view key,
    std::string_view label)
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack().width(Dimension::percent(100)).padding(0, 1, 0, 1)(
        text(std::string{" "} + std::string{key},
            Style{}.with_fg(palette::text())),
        spacer(),
        text(std::string{label}, Style{}.with_fg(palette::muted()))
    );
}

[[nodiscard]] inline maya::Element render_shortcut_hint_row(const model::KeyHintVM& k)
{
    return render_shortcut_hint_row(k.key, k.label);
}

}  // namespace tl::views

#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// "<key>·<label>" pair used in footer hint rows. The key glyph is rendered
// in inverse video so it reads like a keycap; the label is muted.

[[nodiscard]] inline maya::Element render_key_hint(
    std::string_view key,
    std::string_view label)
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack().gap(1)(
        text(" " + std::string{key} + " ",
            Style{}.with_fg(palette::text()).with_inverse()),
        text(std::string{label}, Style{}.with_fg(palette::muted()))
    );
}

[[nodiscard]] inline maya::Element render_key_hint(const model::KeyHintVM& k)
{
    return render_key_hint(k.key, k.label);
}

}  // namespace tl::views

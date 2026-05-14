#pragma once

#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// One placeholder media tile — bordered, with a glyph + short label
// (duration / kind / etc). The caller decides cell width.

[[nodiscard]] inline maya::Element render_media_cell(
    const model::MediaItemVM& item,
    int width = 10)
{
    using namespace maya;
    using namespace maya::dsl;
    return vstack()
        .border(BorderStyle::Round, palette::muted())
        .padding(0, 1)
        .width(Dimension::fixed(width))
        (text(item.kind_glyph + std::string{" "} + item.label,
            Style{}.with_fg(palette::dim())));
}

}  // namespace tl::views

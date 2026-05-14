#pragma once

#include <maya/maya.hpp>

#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Single-glyph badge variants for use at the trailing edge of list rows.
//   render_unread_badge(12)  → ⑨ ... (N) for >9
//   render_mention_badge()   → @
//   render_flash_badge()     → ! amber-bold
//   render_pinned_badge()    → ◆ dim

[[nodiscard]] inline maya::Element render_unread_badge(int count)
{
    using namespace maya;
    using namespace maya::dsl;
    if (count <= 0) return text(std::string{});
    return text(glyph::circled_digit(count),
        Style{}.with_fg(palette::amber()).with_bold());
}

[[nodiscard]] inline maya::Element render_mention_badge()
{
    using namespace maya;
    using namespace maya::dsl;
    return text(std::string{"@"},
        Style{}.with_fg(palette::mention()).with_bold());
}

[[nodiscard]] inline maya::Element render_flash_badge()
{
    using namespace maya;
    using namespace maya::dsl;
    return text(std::string{"!"},
        Style{}.with_fg(palette::amber()).with_bold());
}

[[nodiscard]] inline maya::Element render_pinned_badge()
{
    using namespace maya;
    using namespace maya::dsl;
    return text(std::string{glyph::diamond},
        Style{}.with_fg(palette::dim()));
}

[[nodiscard]] inline maya::Element render_muted_badge()
{
    using namespace maya;
    using namespace maya::dsl;
    return text(std::string{"🔕"}, Style{}.with_fg(palette::dim()));
}

}  // namespace tl::views

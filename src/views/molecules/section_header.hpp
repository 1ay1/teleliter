#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "views/theme.hpp"

namespace tl::views {

// " — TITLE " divider used to label sub-groups inside long lists. Small
// em-dash prefix in dim color, label in muted bold.

[[nodiscard]] inline maya::Element render_section_header(std::string_view label)
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack()(
        text(std::string{" — "}, Style{}.with_fg(palette::dim())),
        text(std::string{label},
            Style{}.with_fg(palette::muted()).with_bold())
    );
}

}  // namespace tl::views

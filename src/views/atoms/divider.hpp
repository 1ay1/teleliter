#pragma once

#include <maya/maya.hpp>

#include "views/theme.hpp"

namespace tl::views {

// hdiv() — a 1-row horizontal rule. vdiv() — a 1-column vertical rule.
// Both use the dim/muted ANSI color so they vanish into the user's terminal
// theme but still convey separation.

[[nodiscard]] inline maya::Element hdiv()
{
    using namespace maya;
    return box()
        .border(BorderStyle::Single, palette::muted())
        .border_sides({true, false, false, false})();
}

[[nodiscard]] inline maya::Element vdiv()
{
    using namespace maya;
    return box()
        .border(BorderStyle::Single, palette::muted())
        .border_sides({false, false, false, true})();
}

}  // namespace tl::views

#pragma once

#include <string_view>

#include <maya/maya.hpp>

#include "views/atoms/avatar.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Avatar wrapped in a round border, padded so it reads as a "profile
// picture" frame rather than the inline avatar chip used in chat rows.

[[nodiscard]] inline maya::Element render_bordered_avatar(
    std::string_view initials,
    maya::Color tint)
{
    using namespace maya;
    using namespace maya::dsl;
    return vstack()
        .border(BorderStyle::Round, tint)
        .padding(0, 2)
        (render_avatar(initials, tint));
}

[[nodiscard]] inline maya::Element render_bordered_avatar_from_name(
    std::string_view name,
    maya::Color tint)
{
    using namespace maya;
    using namespace maya::dsl;
    return vstack()
        .border(BorderStyle::Round, tint)
        .padding(0, 2)
        (render_avatar_from_name(name, tint));
}

}  // namespace tl::views

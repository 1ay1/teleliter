#pragma once

#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/avatar.hpp"
#include "views/atoms/presence_dot.hpp"
#include "views/theme.hpp"

namespace tl::views {

// One member row in the right-side info panel:
//   avatar  name (bold if self)  ───  status text (muted italic)

[[nodiscard]] inline maya::Element render_member_row(const model::MemberVM& m)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto tint  = palette::tint_for(m.user.name);
    const auto av    = render_avatar_from_name_or_image(
        m.user.avatar_path, m.user.name, tint);

    auto name_sty = m.user.is_self
        ? Style{}.with_fg(palette::brand()).with_bold()
        : Style{}.with_fg(palette::text());

    auto status_sty = m.typing
        ? Style{}.with_fg(palette::accent()).with_italic()
        : Style{}.with_fg(palette::muted()).with_italic();

    // width(100%) is what lets the spacer push the status text to the
    // panel's right edge. Without it the hstack shrinks to content and
    // the status sits flush against the name.
    return hstack()
        .width(Dimension::percent(100))
        .gap(1)
        .padding(0, 1, 0, 1)
        .align_items(Align::Center)
        (
            av,
            render_presence_dot(m.user.presence),
            text(m.user.name, name_sty),
            spacer(),
            text(m.status_text, status_sty)
        );
}

}  // namespace tl::views

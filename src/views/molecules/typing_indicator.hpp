#pragma once

#include <span>
#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/spinner.hpp"
#include "views/theme.hpp"

namespace tl::views {

[[nodiscard]] inline maya::Element render_typing_indicator(
    std::span<const model::UserVM> typers,
    int tick)
{
    using namespace maya;
    using namespace maya::dsl;

    if (typers.empty()) return text(std::string{});

    std::string names;
    for (std::size_t i = 0; i < typers.size() && i < 3; ++i) {
        if (i > 0) names += (i + 1 == typers.size()) ? " and " : ", ";
        names += typers[i].name;
    }
    if (typers.size() > 3) names += " and others";
    names += (typers.size() == 1 ? " is typing" : " are typing");

    return hstack().gap(1)(
        render_typing_dots(tick),
        text(names, Style{}.with_fg(palette::muted()).with_italic())
    );
}

[[nodiscard]] inline maya::Element render_typing_indicator(const model::TypingVM& t)
{
    return render_typing_indicator(std::span<const model::UserVM>{t.typers}, t.tick);
}

}  // namespace tl::views

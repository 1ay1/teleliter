#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/key_hint.hpp"
#include "views/molecules/section_header.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Modal-ish help panel. Sections via section_header molecule; rows are
// inverse-video key caps + muted labels.

struct HelpSection {
    std::string_view                  title;
    std::span<const model::KeyHintVM> entries;
};

namespace detail::help {

[[nodiscard]] inline maya::Element row(const model::KeyHintVM& k)
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack().gap(2).padding(0, 1, 0, 1)(
        text(" " + k.key + " ",
            Style{}.with_fg(palette::text()).with_inverse()),
        text(k.label, Style{}.with_fg(palette::muted())),
        spacer()
    );
}

}  // namespace detail::help

[[nodiscard]] inline maya::Element render_help_overlay(
    std::span<const HelpSection> sections)
{
    using namespace maya;
    using namespace maya::dsl;

    std::vector<Element> rows;
    for (const auto& s : sections) {
        rows.push_back(render_section_header(s.title));
        rows.push_back(text(std::string{}));
        for (const auto& k : s.entries) {
            rows.push_back(detail::help::row(k));
        }
        rows.push_back(text(std::string{}));
    }
    rows.push_back(text(std::string{"  press esc to close"},
        Style{}.with_fg(palette::dim()).with_italic()));

    return vstack()
        .border(BorderStyle::Double, palette::accent())
        .border_text(std::string{" KEYBOARD HELP "},
                     BorderTextPos::Top, BorderTextAlign::Center)
        .padding(1, 2, 1, 2)
        .max_width(Dimension::fixed(64))
        (rows);
}

}  // namespace tl::views

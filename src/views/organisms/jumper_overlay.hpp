#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/glyphs.hpp"
#include "views/molecules/empty_state.hpp"
#include "views/theme.hpp"

namespace tl::views {

namespace detail::jumper {

[[nodiscard]] inline maya::Element row(
    const model::ChatListItemVM& chat,
    bool selected)
{
    using namespace maya;
    using namespace maya::dsl;
    auto marker = selected
        ? text(std::string{glyph::arrow},
            Style{}.with_fg(palette::accent()).with_bold())
        : text(std::string{" "});
    auto name_sty = selected
        ? Style{}.with_fg(palette::accent()).with_bold()
        : Style{}.with_fg(palette::text());
    return hstack()
        .width(Dimension::percent(100))
        .gap(2)
        .padding(0, 1, 0, 1)
        (
            marker,
            text(chat.title, name_sty),
            chat.topic.empty()
                ? text(std::string{})
                : text(chat.topic, Style{}.with_fg(palette::muted()).with_dim()),
            spacer(),
            chat.unread_count > 0
                ? text(" " + std::to_string(chat.unread_count),
                    Style{}.with_fg(palette::amber()).with_bold())
                : text(std::string{})
        );
}

}  // namespace detail::jumper

[[nodiscard]] inline maya::Element render_jumper_overlay(
    std::string_view filter,
    std::span<const model::ChatListItemVM> chats,
    std::span<const std::size_t> matches,
    std::size_t selected_match)
{
    using namespace maya;
    using namespace maya::dsl;

    auto prompt = hstack()
        .width(Dimension::percent(100))
        .gap(1)
        .padding(0, 1, 1, 1)
        (
            text(std::string{"/"},
                Style{}.with_fg(palette::accent()).with_bold()),
            text(std::string{filter},
                Style{}.with_fg(palette::text())),
            text(std::string{glyph::caret},
                Style{}.with_fg(palette::accent()).with_bold()),
            spacer(),
            text(std::to_string(matches.size()) + " match"
                 + (matches.size() == 1 ? "" : "es"),
                Style{}.with_fg(palette::muted()))
        );

    std::vector<Element> rows;
    rows.push_back(prompt);
    if (matches.empty()) {
        constexpr std::array<std::string_view, 1> hint = { "try a different name" };
        rows.push_back(render_empty_state("no matches", hint));
    } else {
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (matches[i] >= chats.size()) continue;
            rows.push_back(detail::jumper::row(chats[matches[i]], i == selected_match));
        }
    }

    return vstack()
        .border(BorderStyle::Round, palette::accent())
        .border_text(std::string{" JUMP TO CHAT "},
                     BorderTextPos::Top, BorderTextAlign::Center)
        .padding(1, 1, 1, 1)
        .max_width(Dimension::fixed(52))
        (rows);
}

}  // namespace tl::views

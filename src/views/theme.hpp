#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include <maya/maya.hpp>

#include "model/view_models.hpp"

// ANSI-only palette. Every color is one of maya's 16 named ANSI factories or
// Color::default_color() — so the user's terminal theme always wins. No hex,
// no RGB, no forced panel backgrounds. Small bg fills are reserved for badges
// and avatars that need strong contrast against ANY terminal scheme.

namespace tl::views::palette {

using maya::Color;

[[nodiscard]] constexpr Color text()         noexcept { return Color::default_color(); }
[[nodiscard]] constexpr Color dim()          noexcept { return Color::bright_black(); }
[[nodiscard]] constexpr Color muted()        noexcept { return Color::bright_black(); }
[[nodiscard]] constexpr Color brand()        noexcept { return Color::cyan(); }
[[nodiscard]] constexpr Color accent()       noexcept { return Color::magenta(); }
[[nodiscard]] constexpr Color amber()        noexcept { return Color::yellow(); }
[[nodiscard]] constexpr Color green()        noexcept { return Color::green(); }
[[nodiscard]] constexpr Color red()          noexcept { return Color::red(); }
[[nodiscard]] constexpr Color pink()         noexcept { return Color::bright_magenta(); }
[[nodiscard]] constexpr Color purple()       noexcept { return Color::magenta(); }
[[nodiscard]] constexpr Color border_idle()  noexcept { return Color::bright_black(); }
[[nodiscard]] constexpr Color border_focus() noexcept { return Color::cyan(); }
[[nodiscard]] constexpr Color self_msg()     noexcept { return Color::green(); }
[[nodiscard]] constexpr Color self_bubble()  noexcept { return Color::magenta(); }
[[nodiscard]] constexpr Color peer_bubble()  noexcept { return Color::bright_black(); }
[[nodiscard]] constexpr Color mention()      noexcept { return Color::yellow(); }
[[nodiscard]] constexpr Color check_read()   noexcept { return Color::magenta(); }
[[nodiscard]] constexpr Color check_done()   noexcept { return Color::bright_black(); }
[[nodiscard]] constexpr Color check_sent()   noexcept { return Color::bright_black(); }
[[nodiscard]] constexpr Color presence_active() noexcept { return Color::bright_green(); }
[[nodiscard]] constexpr Color presence_away()   noexcept { return Color::yellow(); }
[[nodiscard]] constexpr Color presence_dnd()    noexcept { return Color::red(); }
[[nodiscard]] constexpr Color presence_offline()noexcept { return Color::bright_black(); }

[[nodiscard]] constexpr Color presence_of(model::Presence p) noexcept
{
    switch (p) {
        case model::Presence::Active:  return presence_active();
        case model::Presence::Away:    return presence_away();
        case model::Presence::Dnd:     return presence_dnd();
        case model::Presence::Offline: return presence_offline();
    }
    return presence_offline();
}

// Stable, non-random author tints from the ANSI palette. Same name maps to
// the same color across renders by hashing the name into 6 ANSI colors.
[[nodiscard]] inline Color tint_for(std::string_view name) noexcept
{
    constexpr auto choices = std::array{
        &Color::cyan, &Color::magenta, &Color::yellow,
        &Color::green, &Color::bright_blue, &Color::bright_magenta,
    };
    std::size_t h = 0;
    for (auto c : name) h = h * 131 + static_cast<unsigned char>(c);
    return (choices[h % choices.size()])();
}

}  // namespace tl::views::palette

namespace tl::views::sty {

using maya::Color;
using maya::Style;

[[nodiscard]] inline constexpr Style plain()        noexcept { return Style{}; }
[[nodiscard]] inline constexpr Style bold()         noexcept { return Style{}.with_bold(); }
[[nodiscard]] inline constexpr Style dim()          noexcept { return Style{}.with_dim(); }
[[nodiscard]] inline constexpr Style italic()       noexcept { return Style{}.with_italic(); }
[[nodiscard]] inline constexpr Style inverse()      noexcept { return Style{}.with_inverse(); }

[[nodiscard]] inline constexpr Style muted()        noexcept { return Style{}.with_fg(palette::muted()); }
[[nodiscard]] inline constexpr Style brand()        noexcept { return Style{}.with_fg(palette::brand()).with_bold(); }
[[nodiscard]] inline constexpr Style accent()       noexcept { return Style{}.with_fg(palette::accent()).with_bold(); }
[[nodiscard]] inline constexpr Style selected()     noexcept { return Style{}.with_fg(palette::accent()).with_bold(); }
[[nodiscard]] inline constexpr Style unread_badge() noexcept { return Style{}.with_fg(palette::amber()).with_bold(); }
[[nodiscard]] inline constexpr Style self_author()  noexcept { return Style{}.with_fg(palette::self_msg()).with_bold(); }
[[nodiscard]] inline constexpr Style mention()      noexcept { return Style{}.with_fg(palette::mention()).with_bold(); }

}  // namespace tl::views::sty

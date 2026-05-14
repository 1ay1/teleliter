#pragma once

#include <array>
#include <string>
#include <string_view>

// Pre-encoded utf-8 strings used by the widgets. Kept as `inline constexpr`
// std::string_view so they're zero-overhead and concatenable.

namespace tl::views::glyph {

inline constexpr std::string_view dot_on    = "●";
inline constexpr std::string_view dot_off   = "○";
inline constexpr std::string_view bar_left  = "▌";    // active selection bar
inline constexpr std::string_view bar_thin  = "▏";
inline constexpr std::string_view arrow     = "›";
inline constexpr std::string_view chevron_r = "❯";
inline constexpr std::string_view chevron_d = "▾";
inline constexpr std::string_view ellipsis  = "…";
inline constexpr std::string_view prompt    = "›";
inline constexpr std::string_view enter     = "⏎";
inline constexpr std::string_view bell      = "🔔";
inline constexpr std::string_view spark     = "✦";
inline constexpr std::string_view caret     = "▎";
inline constexpr std::string_view check_one = "✓";
inline constexpr std::string_view check_two = "✓✓";
inline constexpr std::string_view circle_o  = "○";
inline constexpr std::string_view bullet    = "•";
inline constexpr std::string_view dash      = "─";
inline constexpr std::string_view tri_right = "▸";
inline constexpr std::string_view tri_left  = "◂";
inline constexpr std::string_view diamond   = "◆";
inline constexpr std::string_view pipe      = "│";
inline constexpr std::string_view dim_circle = "◌";

// Spinner frames for typing animation (4-frame braille cycle).
inline constexpr std::array<std::string_view, 4> spinner_frames = {
    "⠋", "⠙", "⠸", "⠴",
};

// Circled digits 0–9 (presets) + fallback "(N)" for >9.
[[nodiscard]] inline std::string circled_digit(int n) {
    if (n < 0) return "0";
    static constexpr std::array<std::string_view, 10> g = {
        "⓪", "①", "②", "③", "④", "⑤", "⑥", "⑦", "⑧", "⑨",
    };
    if (n < static_cast<int>(g.size())) return std::string{g[static_cast<std::size_t>(n)]};
    return "(" + std::to_string(n) + ")";
}

}  // namespace tl::views::glyph

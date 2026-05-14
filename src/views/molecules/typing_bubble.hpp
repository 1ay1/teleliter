#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/avatar.hpp"
#include "views/theme.hpp"

namespace tl::views {

// ─── Animated "is typing" bubble ────────────────────────────────────────────
//
// Drops in at the bottom of the message thread when one or more peers are
// typing. Visually mirrors a peer message bubble (rounded border, muted
// color, left-aligned, ~55% column width) — so it reads as "a message in
// progress" rather than a status badge floating on its own.
//
// Animation: three "dots" pulse left → right in a wave. At each frame
// exactly one dot is rendered with the accent color and a "filled" glyph;
// the other two carry a muted glyph. Frame index advances every other
// Tick (~500 ms), so a full cycle runs in ~2 s — fast enough to feel
// alive, slow enough that the eye doesn't read it as a strobe.
//
// Glyph palette is intentionally drawn from the dot family (●·) instead
// of using boxes/triangles so the bubble looks like text being written,
// not a generic loading indicator.

namespace detail::typing {

// 6-frame wave: a single dot ramps in from the left, builds to two lit
// dots, then collapses back. The brief "all dim" pause at the end gives
// the animation a breath — the eye latches onto motion + rest instead
// of pure motion.
inline constexpr int kFrames = 6;
inline constexpr std::array<std::array<int, 3>, kFrames> kIntensity = {{
    //  L  M  R   (2 = bright accent, 1 = mid, 0 = dim)
    {{ 2, 0, 0 }},
    {{ 2, 2, 0 }},
    {{ 1, 2, 2 }},
    {{ 0, 1, 2 }},
    {{ 0, 0, 1 }},
    {{ 0, 0, 0 }},
}};

[[nodiscard]] inline std::string typers_label(
    std::span<const model::UserVM> typers)
{
    if (typers.empty()) return {};
    if (typers.size() == 1) return typers[0].name;
    if (typers.size() == 2) return typers[0].name + " and " + typers[1].name;
    return typers[0].name + " and "
         + std::to_string(typers.size() - 1) + " others";
}

}  // namespace detail::typing

[[nodiscard]] inline maya::Element render_typing_bubble(
    std::span<const model::UserVM> typers,
    int tick,
    int column_width)
{
    using namespace maya;
    using namespace maya::dsl;

    if (typers.empty()) return text(std::string{});

    // Walk the cycle backwards-safe so a negative tick (paranoia) doesn't
    // index out-of-range.
    const int phase = ((tick / 2) % detail::typing::kFrames
                     + detail::typing::kFrames)
                    % detail::typing::kFrames;
    const auto& intensities = detail::typing::kIntensity[
        static_cast<std::size_t>(phase)];

    auto dot = [&](int intensity) -> Element {
        switch (intensity) {
            case 2: return text(std::string{"●"},
                Style{}.with_fg(palette::accent()).with_bold());
            case 1: return text(std::string{"●"},
                Style{}.with_fg(palette::muted()));
            default: return text(std::string{"·"},
                Style{}.with_fg(palette::dim()));
        }
    };

    // Single-row bubble: "[avatar] name   ● · ·". Mirroring the peer
    // bubble's geometry (max ~55% of column, rounded muted border, left
    // padding 1) keeps the thread visually coherent — the typing bubble
    // looks like a message that hasn't been committed yet.
    const std::string label = detail::typing::typers_label(typers);
    const auto tint         = palette::tint_for(label);

    auto avatar_el = render_avatar_from_name_or_image(
        typers.size() == 1 ? std::string_view{}     // group label has no image
                           : std::string_view{},
        label, tint);

    auto row = hstack().gap(1).align_items(Align::Center)(
        avatar_el,
        text(label, Style{}.with_fg(tint).with_bold()),
        text(std::string{"is typing"},
            Style{}.with_fg(palette::muted()).with_italic()),
        text(std::string{" "}),
        dot(intensities[0]),
        dot(intensities[1]),
        dot(intensities[2])
    );

    const int bw = std::max(20,
        std::min(column_width - 2, column_width * 55 / 100));

    auto bubble = vstack()
        .border(BorderStyle::Round, palette::peer_bubble())
        .padding(0, 1)
        .max_width(Dimension::fixed(bw))
        (row);

    return hstack().width(Dimension::percent(100))(
        bubble,
        spacer()
    );
}

}  // namespace tl::views

#pragma once

#include <cstdint>
#include <span>
#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/avatar.hpp"
#include "views/atoms/clock.hpp"
#include "views/atoms/presence_dot.hpp"
#include "views/glyphs.hpp"
#include "views/molecules/header_right_cluster.hpp"
#include "views/molecules/typing_indicator.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Header bar. Left: avatar + title + dynamic subtitle (presence label or
// "alice is typing …"). Right: header_right_cluster (mentions / bell /
// self presence + clock).

// Responsive breakpoints — used by the shell to ask for progressively
// simpler renderings as the middle column shrinks.
enum class HeaderDensity : unsigned char {
    Full,     // full right cluster (counters + bell + presence + clock)
    Compact,  // drops bell + counters; keeps a small self_presence_chip
    Minimal,  // hides right side entirely
};

[[nodiscard]] inline maya::Element render_header_bar(
    const model::HeaderInfoVM& h,
    std::span<const model::UserVM> typers_in_chat,
    int tick,
    model::Presence self_presence = model::Presence::Active,
    std::int64_t clock_seconds = 0,
    HeaderDensity density = HeaderDensity::Full)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto tint = palette::tint_for(h.chat_title);
    // Header content area is 2 rows tall (the top row is padding). The
    // avatar spans both content rows so it lines up next to the title +
    // subtitle stack on the right — full content-height portrait,
    // matching Telegram-web's header layout.
    auto av = render_avatar_image_sized(
        h.avatar_path,
        h.chat_title.empty() ? "??" : h.chat_title,
        tint,
        /*cells_w=*/4, /*cells_h=*/2);

    Element presence_el = h.is_dm
        ? hstack()(text(std::string{" "}), render_presence_dot(h.partner_presence))
        : Element{text(std::string{})};

    Element subtitle_el;
    if (!typers_in_chat.empty()) {
        subtitle_el = render_typing_indicator(typers_in_chat, tick);
    } else if (h.is_dm) {
        subtitle_el = text(presence_label(h.partner_presence),
            Style{}.with_fg(palette::presence_of(h.partner_presence)).with_dim());
    } else {
        auto sub = std::to_string(h.online_count) + " online";
        // In compact / minimal density, drop the topic — keeps subtitle short.
        if (density == HeaderDensity::Full && !h.subtitle.empty()) {
            sub += "  ·  " + h.subtitle;
        }
        subtitle_el = text(sub, Style{}.with_fg(palette::muted()).with_italic());
    }

    auto left = hstack().align_items(Align::Start).gap(1)(
        av,
        vstack()(
            hstack().gap(1)(
                text(h.chat_title.empty() ? std::string{"no chat"} : h.chat_title,
                    Style{}.with_fg(palette::accent()).with_bold()),
                presence_el),
            subtitle_el
        )
    );

    Element right;
    switch (density) {
        case HeaderDensity::Full:
            right = render_header_right_cluster(
                h.pending_mentions, h.pending_unread,
                self_presence, format_clock(clock_seconds));
            break;
        case HeaderDensity::Compact:
            right = hstack().gap(1)(
                render_presence_dot(self_presence),
                render_clock(clock_seconds));
            break;
        case HeaderDensity::Minimal:
            right = text(std::string{});
            break;
    }

    return hstack()
        .width(Dimension::percent(100))
        .align_items(Align::Start)
        .padding(0, 1)
        (left, spacer(), right);
}

[[nodiscard]] inline maya::Element render_header_bar(const model::HeaderInfoVM& h)
{
    return render_header_bar(h, std::span<const model::UserVM>{}, 0);
}

}  // namespace tl::views

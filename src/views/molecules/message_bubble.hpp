#pragma once

#include <string>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/avatar.hpp"
#include "views/atoms/checkmark.hpp"
#include "views/glyphs.hpp"
#include "views/molecules/reactions_row.hpp"
#include "views/molecules/system_notice.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Four render shapes for a message:
//   render_system_notice (in molecules/system_notice.hpp) — centered "• … •"
//   render_action_line   (in molecules/system_notice.hpp) — "* alice waves"
//   render_self_bubble   — rounded, accent border, right-aligned
//   render_peer_bubble   — rounded, muted border, left-aligned
// `render_message` picks one based on flags. Bubble width comes from the
// caller because it depends on the surrounding column geometry.

[[nodiscard]] inline maya::Element render_self_bubble(
    const model::MessageVM& m,
    int bubble_width)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto body_sty = m.mentions_you ? sty::bold() : sty::plain();
    const auto border_c = m.mentions_you ? palette::mention()
                                         : palette::self_bubble();

    std::vector<Element> rows;
    rows.push_back(text(m.body, body_sty));

    // Footer: reactions hugged left, time + checkmark hugged right on the
    // same row. spacer() between them eats whatever cells remain in the
    // bubble's inner width.
    auto footer = hstack().width(Dimension::percent(100))(
        m.reactions.empty() ? text(std::string{})
                            : render_reactions_row(m.reactions),
        spacer(),
        text(m.age_label, Style{}.with_fg(palette::dim())),
        text(std::string{"  "}),
        render_checkmark(m.read_state)
    );
    rows.push_back(footer);

    return vstack()
        .border(BorderStyle::Round, border_c)
        .padding(0, 1)
        .width(Dimension::fixed(bubble_width))
        (rows);
}

[[nodiscard]] inline maya::Element render_peer_bubble(
    const model::MessageVM& m,
    int bubble_width,
    bool show_author)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto body_sty = m.mentions_you ? sty::bold() : sty::plain();
    const auto border_c = m.mentions_you ? palette::mention()
                                         : palette::peer_bubble();

    std::vector<Element> rows;
    if (show_author) {
        rows.push_back(text(m.author_name,
            Style{}.with_fg(palette::tint_for(m.author_name)).with_bold()));
    }
    rows.push_back(text(m.body, body_sty));
    rows.push_back(hstack().width(Dimension::percent(100))(
        m.reactions.empty() ? text(std::string{})
                            : render_reactions_row(m.reactions),
        spacer(),
        text(m.age_label, Style{}.with_fg(palette::dim()))
    ));

    return vstack()
        .border(BorderStyle::Round, border_c)
        .padding(0, 1)
        .max_width(Dimension::fixed(bubble_width))
        (rows);
}

[[nodiscard]] inline maya::Element render_message(
    const model::MessageVM& m,
    int column_width)
{
    using namespace maya;
    using namespace maya::dsl;

    if (m.is_system) return render_system_notice(m.body);
    if (m.is_action) {
        return render_action_line(m.author_name, m.body,
            palette::tint_for(m.author_name));
    }

    // message_list no longer adds horizontal padding — the vdiv on the
    // left and the scrollbar column on the right serve as rails — so
    // the bubble fills the full column_width directly.
    if (m.from_me) {
        const int bw = std::max(16, std::min(column_width - 2, column_width * 55 / 100));
        const int pad = std::max(0, column_width - bw);
        return hstack().width(Dimension::percent(100))(
            text(std::string(static_cast<std::size_t>(pad), ' ')),
            render_self_bubble(m, bw)
        );
    }

    // Peer bubble with a 4-cell × 2-row author avatar to its left. The
    // avatar only shows when the bubble is the FIRST in an author run
    // (i.e. !compact) — follow-ups inside the same group leave the
    // avatar column blank, mirroring Telegram-web's group-aware layout.
    // Account for the avatar (4 cells) + gutter (1 cell) when sizing
    // the bubble so it doesn't overflow into the scrollbar area.
    constexpr int kAvatarW   = 4;
    constexpr int kAvatarGap = 1;
    const int bubble_avail   = std::max(8, column_width - kAvatarW - kAvatarGap);
    const int bw = std::max(16, std::min(bubble_avail, bubble_avail * 80 / 100));

    Element avatar_col;
    if (!m.compact) {
        avatar_col = render_avatar_image_sized(
            m.author_avatar_path,
            m.author_initials.empty() ? m.author_name : m.author_initials,
            palette::tint_for(m.author_name),
            kAvatarW, /*cells_h=*/2);
    } else {
        // Blank 4-wide column so the bubble stays aligned within the
        // author run.
        avatar_col = vstack().gap(0)(
            text(std::string{"    "}),
            text(std::string{"    "})
        );
    }

    return hstack()
        .width(Dimension::percent(100))
        .align_items(Align::Start)
        .gap(kAvatarGap)
        (
            avatar_col,
            render_peer_bubble(m, bw, !m.compact),
            spacer()
        );
}

}  // namespace tl::views

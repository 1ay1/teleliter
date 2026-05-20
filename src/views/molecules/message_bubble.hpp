#pragma once

#include <string>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/avatar.hpp"
#include "views/atoms/checkmark.hpp"
#include "views/glyphs.hpp"
#include "views/molecules/audio_note.hpp"
#include "views/molecules/media_cards.hpp"
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

// ─── Quoted-reply header inside a bubble ─────────────────────────────────
// Two-line block placed at the top of a bubble that's quoting another
// message. ▎ left rail in the original author's tint, then author name
// in bold, then the snippet in dim italic. Reads as "here's the
// context, scroll past it for the actual reply body."

namespace detail::bubble {

[[nodiscard]] inline maya::Element render_reply_header(
    const model::ReplyQuoteVM& q)
{
    using namespace maya;
    using namespace maya::dsl;
    const auto tint = palette::tint_for(q.author_name);
    return hstack().gap(0)(
        text(std::string{"▎"},
            Style{}.with_fg(tint).with_bold()),
        text(std::string{" "}),
        vstack().gap(0)(
            text(q.author_name,
                Style{}.with_fg(tint).with_bold()),
            text(q.snippet,
                Style{}.with_fg(palette::muted()).with_italic())
        )
    );
}

}  // namespace detail::bubble

namespace detail::bubble {

// Pick the right body renderer for a message. Order matters: at most
// one of these media kinds should be set, but if more than one is the
// first found wins (audio → video note → photo → sticker → animation
// → video → music → document → contact → location → poll). When none
// is set, falls back to the plain `body` text (or empty).
//
// inner_w is the bubble's inner content width (after border + padding
// have been subtracted) — every media card renderer obeys this so the
// bubble doesn't overflow into the message column's scrollbar.
[[nodiscard]] inline maya::Element render_body(
    const model::MessageVM& m, int inner_w, maya::Style body_sty)
{
    using namespace maya;
    using namespace maya::dsl;

    const int bar_count = std::max(8, inner_w - 16);

    if (m.audio_note.has_value()) return render_audio_note(*m.audio_note, bar_count);
    if (m.video_note.has_value()) return render_video_note(*m.video_note, bar_count);
    if (m.photo.has_value())      return render_photo_card(*m.photo, inner_w);
    if (m.sticker.has_value())    return render_sticker_card(*m.sticker, inner_w);
    if (m.animation.has_value())  return render_animation_card(*m.animation, inner_w);
    if (m.video.has_value())      return render_video_card(*m.video, inner_w);
    if (m.music.has_value())      return render_music_card(*m.music, inner_w);
    if (m.document.has_value())   return render_document_card(*m.document, inner_w);
    if (m.contact.has_value())    return render_contact_card(*m.contact, inner_w);
    if (m.location.has_value())   return render_location_card(*m.location, inner_w);
    if (m.poll.has_value())       return render_poll_card(*m.poll, inner_w);
    if (!m.body.empty())          return text(m.body, body_sty);
    return text(std::string{});
}

}  // namespace detail::bubble

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
    if (m.reply_quote.has_value()) {
        rows.push_back(detail::bubble::render_reply_header(*m.reply_quote));
    }
    // Body → dispatched on whichever media kind the message carries.
    // Bubble inner width = bubble_width minus border (2) and padding (2).
    const int inner_w = std::max(8, bubble_width - 4);
    rows.push_back(detail::bubble::render_body(m, inner_w, body_sty));
    // Link preview attaches below the body (matching Telegram-web).
    if (m.link_preview.has_value()) {
        rows.push_back(render_link_preview(*m.link_preview, inner_w));
    }

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

    // align_items(Start) keeps the body text left-aligned inside the
    // bubble. Without it the vstack defaults to Stretch — text elements
    // measure at their natural (longest-line) width and the cross-axis
    // stretch centers shorter wrapped lines in the leftover space,
    // which reads as "centered text" once the body wraps onto a second
    // line. The footer hstack is explicitly width(100%) so the right
    // edge of the time + checkmark still hugs the bubble's right wall.
    return vstack()
        .border(BorderStyle::Round, border_c)
        .padding(0, 1)
        .width(Dimension::fixed(bubble_width))
        .align_items(Align::Start)
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
    if (m.reply_quote.has_value()) {
        rows.push_back(detail::bubble::render_reply_header(*m.reply_quote));
    }
    const int inner_w = std::max(8, bubble_width - 4);
    rows.push_back(detail::bubble::render_body(m, inner_w, body_sty));
    if (m.link_preview.has_value()) {
        rows.push_back(render_link_preview(*m.link_preview, inner_w));
    }
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
        .align_items(Align::Start)
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

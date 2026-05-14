#pragma once

#include <algorithm>
#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/active_bar.hpp"
#include "views/atoms/avatar.hpp"
#include "views/atoms/badge.hpp"
#include "views/atoms/presence_dot.hpp"
#include "views/theme.hpp"

namespace tl::views {

// One chat row: 3 lines.
//   line 1: ▌ ▶ [avatar.top] title          ●  age
//   line 2: ▌   [avatar.bot] preview              ⓷
//   line 3: ─── faint separator stretching the panel width
//
// The avatar spans BOTH content lines (a 4-cell × 2-row half-block raster
// of the image, falling back to a tinted-initials chip of the same height
// when no image is available). The title and preview sit in a sibling
// vstack to the right of the avatar — this gives the row a real
// "card with portrait" shape rather than a strip of pixels above a
// strip of text.
//
// `panel_w` is the chat-list column width in cells; the row uses it to
// pre-truncate title + preview so the flex layout never tries to lay out
// content it can't fit.

[[nodiscard]] inline maya::Element render_chat_row(
    const model::ChatListItemVM& chat,
    bool selected,
    bool pane_focused,
    int panel_w = 32)
{
    using namespace maya;
    using namespace maya::dsl;

    // Selection is independent of input focus — the open chat keeps its
    // highlight whether keystrokes are landing in the chat list or the
    // composer. (Telegram Web behaves the same way.) `pane_focused`
    // remains in the signature in case future variants want to tint the
    // accent slightly differently.
    (void)pane_focused;
    const bool active = selected;

    const auto tint = palette::tint_for(chat.title);

    // ─── Left rail: active bar stretched across both content rows ────────
    auto bar_col = vstack().gap(0)(
        render_active_bar(active, chat.flash),
        active ? render_active_bar(true, chat.flash) : text(std::string{" "})
    );
    auto selector_col = vstack().gap(0)(
        render_selector_caret(active),
        text(std::string{"  "})
    );

    // ─── Avatar: 4 cells wide × 2 rows tall ──────────────────────────────
    // For selected rows without an image we keep the old "lit chip"
    // treatment — accent-colored block with bold initials — as a strong
    // selection cue. Image avatars don't get the override; the photo is
    // its own identity cue and the surrounding bar + accent title carry
    // the selection signal.
    Element avatar_col;
    if (active && chat.avatar_path.empty()) {
        auto initials = chat.initials.empty()
            ? std::string{chat.title.substr(0,
                std::min<std::size_t>(2, chat.title.size()))}
            : chat.initials;
        if (initials.size() > 2) initials.resize(2);
        if (initials.size() < 2)  initials.append(2 - initials.size(), ' ');
        auto chip = text(" " + initials + " ",
            Style{}.with_fg(maya::Color::black())
                   .with_bg(palette::accent())
                   .with_bold());
        auto filler = text(std::string{"    "},
            Style{}.with_bg(palette::accent()));
        avatar_col = vstack().gap(0)(chip, filler);
    } else {
        avatar_col = render_avatar_image_sized(
            chat.avatar_path, chat.title, tint, /*cells_w=*/4, /*cells_h=*/2);
    }

    // ─── Right column: title row + preview row ──────────────────────────
    auto name_sty = active
        ? Style{}.with_fg(palette::accent()).with_bold()
        : Style{}.with_fg(palette::text());

    Element presence_el = (chat.kind == model::ChatKind::Direct)
        ? hstack()(text(std::string{" "}), render_presence_dot(chat.partner_presence))
        : Element{text(std::string{})};

    // Title budget: panel_w minus prefix(10) − presence(2 if DM) − age(~5) −
    // trailing space(1). Floor at 6 cells so very narrow lists still show
    // something legible.
    const int title_budget = std::max(6,
        panel_w - 10 - (chat.kind == model::ChatKind::Direct ? 2 : 0)
                 - static_cast<int>(chat.last_message_time.size()) - 1);
    auto title_text = maya::truncate_end(chat.title,
        static_cast<int>(title_budget));

    auto title_row = hstack()
        .width(Dimension::percent(100))
        .align_items(Align::Center)
        (
            text(title_text, name_sty),
            presence_el,
            spacer(),
            text(chat.last_message_time, Style{}.with_fg(palette::dim())),
            text(std::string{" "})
        );

    // Preview style: selected rows brighten + italicize; mentions glow
    // amber; everything else stays muted so selection wins the eye.
    auto preview_sty = active
        ? Style{}.with_fg(palette::text()).with_italic()
        : (chat.mentions_pending
            ? Style{}.with_fg(palette::mention()).with_bold()
            : Style{}.with_fg(palette::muted()));
    if (chat.last_message_preview.empty()) preview_sty = preview_sty.with_italic();

    const int preview_budget = std::max(6, panel_w - 14);
    auto preview_text = chat.last_message_preview.empty()
        ? std::string{"no messages yet"}
        : maya::truncate_end(chat.last_message_preview,
            static_cast<int>(preview_budget));

    Element badge_el;
    if (chat.flash)                      badge_el = render_flash_badge();
    else if (chat.unread_count > 0)      badge_el = render_unread_badge(static_cast<int>(chat.unread_count));
    else                                 badge_el = text(std::string{});

    auto preview_row = hstack()
        .width(Dimension::percent(100))
        .align_items(Align::Center)
        (
            text(preview_text, preview_sty),
            spacer(),
            badge_el,
            text(std::string{" "})
        );

    auto text_col = vstack().gap(0).grow(1).shrink(1)(
        title_row, preview_row
    );

    // ─── Body row ─ everything wrapped in a single hstack ───────────────
    auto body = hstack()
        .width(Dimension::percent(100))
        .align_items(Align::Start)
        (
            bar_col,
            selector_col,
            text(std::string{" "}),
            avatar_col,
            text(std::string{"  "}),
            text_col
        );

    // ─── Hairline separator below the row ───────────────────────────────
    auto repeat = [](std::string_view g, int n) {
        std::string s;
        s.reserve(g.size() * static_cast<std::size_t>(std::max(0, n)));
        for (int i = 0; i < n; ++i) s.append(g);
        return s;
    };
    const int line_w = std::max(4, panel_w - 2);
    auto sep_text = active ? repeat("━", line_w)
                           : repeat("─", line_w);
    auto sep_sty  = active
        ? Style{}.with_fg(palette::accent()).with_bold()
        : Style{}.with_fg(palette::dim());
    auto separator = hstack().width(Dimension::percent(100))(
        text(std::string{" "}),
        text(std::move(sep_text), sep_sty)
    );

    return vstack().gap(0)(body, separator);
}

}  // namespace tl::views

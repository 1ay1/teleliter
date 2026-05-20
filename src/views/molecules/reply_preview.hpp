#pragma once

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// ─── Reply preview ──────────────────────────────────────────────────────────
//
// One-row banner that sits ABOVE the composer when a reply is staged.
// Shape mirrors Telegram-web's reply card:
//
//   ▎ ↩ Reply to <author>                              ✕
//   ▎ <snippet…>
//
// Squashed to a single row here (the composer band only grows by one row
// when active) — author + snippet share the row, separated by a colon.
// Esc cancels; clicking the ✕ also fires CancelReply.

[[nodiscard]] inline maya::Element render_reply_preview(
    const model::ReplyQuoteVM& quote)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto tint = palette::tint_for(quote.author_name);

    // ▎ is the same one-eighth left rail used by the composer focus
    // indicator — visually couples the preview to the composer below.
    auto rail = text(std::string{"▎"},
        Style{}.with_fg(tint).with_bold());

    auto label = text(std::string{"↩ "},
        Style{}.with_fg(palette::accent()).with_bold());

    auto author = text(quote.author_name,
        Style{}.with_fg(tint).with_bold());

    auto sep = text(std::string{": "},
        Style{}.with_fg(palette::dim()));

    auto snippet = text(quote.snippet,
        Style{}.with_fg(palette::muted()).with_italic());

    auto cancel = text(std::string{" ✕ "},
        Style{}.with_fg(palette::dim()));

    return hstack()
        .width(Dimension::percent(100))
        .padding(0, 1)
        .align_items(Align::Center)
        (
            rail,
            text(std::string{" "}),
            label,
            author,
            sep,
            snippet,
            spacer(),
            cancel
        );
}

}  // namespace tl::views

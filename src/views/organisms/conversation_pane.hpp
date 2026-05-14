#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/organisms/composer_bar.hpp"
#include "views/organisms/header_bar.hpp"
#include "views/organisms/message_list.hpp"
#include "views/theme.hpp"
#include "views/util/scroll.hpp"

namespace tl::views {

// ─── Conversation pane ───────────────────────────────────────────────────────
//
// The middle column of the app: chat header + scrollable message thread +
// composer bar, all stacked vertically with hdiv separators and a working
// truecolour scrollbar on the right.
//
//   ┌─────────────────────────────────────────────────┐
//   │ header_bar           (fixed 3 rows, top-padded) │
//   ├─────────────────────────────────────────────────┤
//   │                                                 │
//   │ message_list  (grows, scrolly + scrollbar_y)    │
//   │                                                 │
//   ├─────────────────────────────────────────────────┤
//   │ composer_bar         (fixed 1 row, no border)   │
//   └─────────────────────────────────────────────────┘
//
// The widget owns:
//   • responsive density (header / composer step down at narrow widths)
//   • the scrollbar dimensions (derived from viewport_h)
//   • the hdiv separators between regions
//   • all of the wrapping flex (grow/shrink/overflow) so the caller just
//     places the returned Element in their layout
//
// The caller owns the ScrollState (`msg_scroll`) so scroll position
// survives across re-renders — pass it as a mutable reference. Everything
// else is read-only.

// Header height — 1 blank row of top padding + 2 rows of content. Composer
// is a single inline row. These two constants are duplicated in mouse.hpp
// for hit-testing the middle column; if you change one, change both.
inline constexpr int kConversationHeaderH   = 3;
inline constexpr int kConversationComposerH = 1;

// All inputs the pane needs. References are stored briefly inside the
// function call; the caller's data must outlive the returned Element
// (true for every widget in this codebase — they all just compose value
// types around the model).
struct ConversationPaneInputs {
    // Header line: chat title, subtitle, counters.
    model::HeaderInfoVM                  header;

    // Live typers, surfaced under the chat title.
    std::span<const model::UserVM>       typers;

    // Messages to render, oldest → newest.
    std::span<const model::MessageVM>    messages;

    // Composer view-model (text + cursor).
    const model::ComposerVM*             composer = nullptr;

    // Persistent scroll position. Mutable because maya's scroll runtime
    // writes back viewport bounds + max_y each frame.
    maya::ScrollState*                   msg_scroll = nullptr;

    // Whether the composer currently owns input focus — drives caret +
    // border styling.
    bool                                 composer_focused = false;

    // Tick counter (for caret blink and other periodic visuals).
    int                                  tick = 0;

    // Wall-clock seconds since app start (drives the header clock).
    std::int64_t                         clock_seconds = 0;

    // Presence of the local user (header right cluster).
    model::Presence                      self_presence = model::Presence::Offline;

    // Inner width of the middle column in cells (the caller has already
    // accounted for scrollbar / divider columns).
    int                                  inner_w = 60;

    // Terminal height in cells — used to size the scrollbar so it
    // matches the message-area band exactly.
    int                                  term_h = 30;
};

[[nodiscard]] inline maya::Element render_conversation_pane(
    const ConversationPaneInputs& in)
{
    using namespace maya;
    using namespace maya::dsl;

    // ─── Responsive density breakpoints ──────────────────────────────────
    // Header right cluster and composer extras step down as the column
    // gets tighter. Thresholds match messenger.cpp's visible breakpoints.
    const HeaderDensity header_density =
        (in.inner_w >= 60) ? HeaderDensity::Full
      : (in.inner_w >= 30) ? HeaderDensity::Compact
                           : HeaderDensity::Minimal;
    const ComposerDensity composer_density =
        (in.inner_w >= 60) ? ComposerDensity::Full
      : (in.inner_w >= 36) ? ComposerDensity::Compact
                           : ComposerDensity::Minimal;

    // ─── Header band ─ fixed 3 rows, top-padded ──────────────────────────
    auto header_box = vstack()
        .height(Dimension::fixed(kConversationHeaderH))
        .padding(1, 0, 0, 0)
        .grow(0).shrink(0)
        .overflow(Overflow::Hidden)
        (render_header_bar(
            in.header,
            in.typers,
            in.tick,
            in.self_presence,
            in.clock_seconds,
            header_density));

    // ─── Composer band ─ fixed 1 row, no border ──────────────────────────
    auto composer_box = vstack()
        .height(Dimension::fixed(kConversationComposerH))
        .grow(0).shrink(0)
        .overflow(Overflow::Hidden)
        (render_composer_bar(
            in.composer ? *in.composer : model::ComposerVM{},
            in.composer_focused,
            composer_density));

    // ─── Messages band ─ grows + scrollbar on the right ──────────────────
    auto messages_inner = render_message_list(
        in.messages,
        model::TypingVM{
            .typers = std::vector<model::UserVM>{in.typers.begin(), in.typers.end()},
            .tick   = in.tick},
        in.inner_w);

    // Scrollbar height = total column rows − header(3) − 2× hdiv − composer(1).
    const int messages_sb_h = std::max(4,
        in.term_h - kConversationHeaderH - kConversationComposerH - 2);

    auto& mut_scroll = *in.msg_scroll;
    auto messages_box = hstack().grow(1).shrink(1)(
        std::move(messages_inner)
            | scrolly(mut_scroll, 0)
            | grow(1),
        scrollbar_y(mut_scroll, messages_sb_h, util::scrollbar_style()));

    return vstack().grow(1).shrink(1)(
        header_box, hdiv(),
        messages_box, hdiv(),
        composer_box
    );
}

}  // namespace tl::views

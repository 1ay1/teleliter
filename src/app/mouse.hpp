#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>

#include "model/view_models.hpp"

// Mouse geometry + hit-testing. The shell and the update layer both need
// to agree on where each panel lives so that wheel events route to the
// right scroll state and clicks land on the right target. This is the
// single source of truth they both consult.

namespace tl::app::mouse {

enum class Panel : unsigned char {
    None,
    Chats,
    Middle,    // covers header, messages, composer
    Right,
};

enum class MiddleRegion : unsigned char {
    None,
    Header,
    Messages,
    Composer,
};

struct Layout {
    int  term_w        = 0;
    int  term_h        = 0;
    int  chats_start   = 0;   // first column (inclusive)
    int  chats_w       = 0;   // panel width including scrollbar
    int  middle_start  = 0;
    int  middle_w      = 0;
    int  right_start   = 0;
    int  right_w       = 0;
    bool show_right    = false;

    int  header_h      = 0;   // 3
    int  composer_h    = 0;   // 1
};

// Layout constants — kept in sync with shell.hpp's render_shell so a
// single change ripples to mouse routing without coordination.
constexpr int kHeaderH        = 3;   // mirrors shell.hpp render_shell
constexpr int kComposerH      = 1;
constexpr int kChatsPct       = 28;
constexpr int kChatsMin       = 24;
constexpr int kChatsMax       = 34;
constexpr int kRightPct       = 30;
constexpr int kRightMin       = 26;
constexpr int kRightMax       = 38;
constexpr int kRightAutoHideAt = 100;

[[nodiscard]] inline Layout compute_layout(const model::AppModel& m) noexcept
{
    Layout l{};
    l.term_w   = std::max(40, m.term_w > 0 ? m.term_w : 120);
    l.term_h   = std::max(10, m.term_h > 0 ? m.term_h : 40);
    l.header_h   = kHeaderH;
    // Composer card grows with whatever the user has staged on it:
    //   reply preview (1 row), attachments strip (3 rows), multi-line
    //   text input (up to kComposerMaxRows), recording bar (replaces
    //   the text input). Mirrors views/organisms/composer_bar.hpp's
    //   composer_card_height — keep these in lockstep when changing.
    {
        constexpr int kComposerMaxRows = 6;
        int h = 0;
        if (m.composer.reply_quote.has_value())   ++h;
        if (!m.composer.attachments.empty())      h += 3;
        if (m.composer.recording) {
            ++h;
        } else {
            int lines = 1;
            for (char c : m.composer.text) if (c == '\n') ++lines;
            if (lines > kComposerMaxRows) lines = kComposerMaxRows;
            h += lines;
        }
        l.composer_h = std::max(kComposerH, h);
    }

    l.chats_w   = std::clamp(l.term_w * kChatsPct / 100, kChatsMin, kChatsMax);
    l.show_right = m.right_panel_open
        && m.selected_chat_index
        && *m.selected_chat_index < m.chats.size()
        && l.term_w >= kRightAutoHideAt;
    l.right_w   = l.show_right
        ? std::clamp(l.term_w * kRightPct / 100, kRightMin, kRightMax)
        : 0;
    const int dividers = l.show_right ? 2 : 1;
    l.middle_w  = std::max(20, l.term_w - l.chats_w - l.right_w - dividers);

    l.chats_start  = 0;
    l.middle_start = l.chats_w + 1;             // +1 vdiv
    l.right_start  = l.show_right
        ? l.middle_start + l.middle_w + 1       // +1 vdiv
        : l.term_w;
    return l;
}

[[nodiscard]] inline Panel panel_at_x(const Layout& l, int x) noexcept
{
    // Each panel's declared width includes its own 1-cell scrollbar
    // column on the right. We mark that column as Panel::None so that
    // clicks on the scrollbar are dedicated to maya's scroll runtime
    // (drag thumb / click track to jump) and don't ALSO fire the
    // content-area hit-tests like chat-row click.
    if (x < 0)                                     return Panel::None;

    // Chats panel content (excluding its scrollbar column).
    if (x < l.chats_w - 1)                         return Panel::Chats;
    if (x == l.chats_w - 1)                        return Panel::None;  // chats scrollbar
    if (x == l.chats_w)                            return Panel::None;  // vdiv

    // Middle column content (excluding its scrollbar column).
    const int middle_end = l.middle_start + l.middle_w;
    if (x < middle_end - 1)                        return Panel::Middle;
    if (x == middle_end - 1)                       return Panel::None;  // middle scrollbar

    if (!l.show_right)                             return Panel::None;
    if (x == middle_end)                           return Panel::None;  // vdiv

    // Right panel content (excluding its scrollbar column).
    const int right_end = l.right_start + l.right_w;
    if (x < right_end - 1)                         return Panel::Right;
    if (x == right_end - 1)                        return Panel::None;  // right scrollbar
    return Panel::None;
}

[[nodiscard]] inline MiddleRegion middle_region_at_y(const Layout& l, int y) noexcept
{
    if (y < 0 || y >= l.term_h)                          return MiddleRegion::None;
    if (y < l.header_h)                                  return MiddleRegion::Header;
    if (y == l.header_h)                                 return MiddleRegion::None;  // hdiv
    if (y >= l.term_h - l.composer_h)                    return MiddleRegion::Composer;
    if (y == l.term_h - l.composer_h - 1)                return MiddleRegion::None;  // hdiv
    return MiddleRegion::Messages;
}

// ─── Chats panel: map terminal y → chat index ────────────────────────────────
// Mirrors chat_list.hpp's row order exactly. Caller adds the scroll
// offset to terminal y before passing.

namespace detail {

[[nodiscard]] inline bool chat_visible(const model::ChatListItemVM& c,
                                       std::string_view query) noexcept
{
    if (query.empty()) return true;
    return c.title.find(query) != std::string::npos;
}

// Height (in rows) of the brand logo block above the search input.
// 3-row box-drawing wordmark at panel_w ≥ 28, 1-row `tl` chip below.
// One blank separator row always follows.
[[nodiscard]] inline int brand_logo_h(int panel_w) noexcept
{
    return panel_w < 28 ? 1 : 3;
}

[[nodiscard]] inline int brand_block_h(int panel_w) noexcept
{
    return brand_logo_h(panel_w) + 1;   // + blank separator below
}

}  // namespace detail

// Inverse of chat_at_content_y: given a chat index, returns its y
// position in chat-list content space. Mirrors the chat_list rendering
// order exactly. Returns -1 if the chat isn't visible under the
// current search filter / section gating.
[[nodiscard]] inline int chat_y_in_content(
    const model::AppModel& m, std::size_t target_idx) noexcept
{
    int row = 1;          // padding(1) top
    row += detail::brand_block_h(m.term_w > 0
        ? std::clamp(m.term_w * kChatsPct / 100, kChatsMin, kChatsMax)
        : kChatsMin);    // brand logo + blank
    row += 3;             // search input (bordered)
    row += 1;             // blank

    if (target_idx == 0)  return row;
    row += 2;             // saved_messages_row
    row += 1;             // blank

    const bool any_group = std::any_of(m.chats.begin(), m.chats.end(),
        [](const model::ChatListItemVM& c) {
            return c.kind != model::ChatKind::Direct
                && c.kind != model::ChatKind::System;
        });
    const bool any_dm = std::any_of(m.chats.begin(), m.chats.end(),
        [](const model::ChatListItemVM& c) {
            return c.kind == model::ChatKind::Direct;
        });

    if (any_group) {
        row += 1;         // section header
        row += 1;         // blank
        for (std::size_t i = 0; i < m.chats.size(); ++i) {
            const auto& c = m.chats[i];
            if (c.kind == model::ChatKind::Direct
             || c.kind == model::ChatKind::System) continue;
            if (!detail::chat_visible(c, m.search_query)) continue;
            if (i == target_idx) return row;
            row += 3;     // 2 content rows + 1 hairline separator
        }
    }
    if (any_dm) {
        if (any_group) row += 1;
        row += 1;
        row += 1;
        for (std::size_t i = 0; i < m.chats.size(); ++i) {
            const auto& c = m.chats[i];
            if (c.kind != model::ChatKind::Direct) continue;
            if (!detail::chat_visible(c, m.search_query)) continue;
            if (i == target_idx) return row;
            row += 3;
        }
    }
    return -1;
}

// After a selection change, nudge the chat-list scroll so the selected
// chat stays inside the viewport. No-op if it's already visible.
inline void ensure_chat_visible(model::AppModel& m) noexcept
{
    if (!m.selected_chat_index) return;
    const int chat_y = chat_y_in_content(m, *m.selected_chat_index);
    if (chat_y < 0) return;

    const int viewport_h = m.chats_scroll.viewport_bounds.h > 0
        ? m.chats_scroll.viewport_bounds.h
        : std::max(20, m.term_h);
    constexpr int kChatRowH    = 3;   // 2 content rows + 1 hairline separator
    constexpr int kEdgeMargin  = 1;   // keep a row of context above/below

    const int top    = m.chats_scroll.y;
    const int bottom = top + viewport_h;

    if (chat_y - kEdgeMargin < top) {
        m.chats_scroll.y = std::max(0, chat_y - kEdgeMargin);
    } else if (chat_y + kChatRowH + kEdgeMargin > bottom) {
        m.chats_scroll.y = chat_y + kChatRowH + kEdgeMargin - viewport_h;
    }
}

[[nodiscard]] inline std::optional<std::size_t>
chat_at_content_y(const model::AppModel& m, int y_in_content) noexcept
{
    if (y_in_content < 0) return std::nullopt;
    int row = 1;     // padding(1) top of chat_list
    row += detail::brand_block_h(m.term_w > 0
        ? std::clamp(m.term_w * kChatsPct / 100, kChatsMin, kChatsMax)
        : kChatsMin);    // brand logo + blank
    row += 3;        // search input (bordered → 3 rows)
    row += 1;        // blank

    // Saved Messages is chat index 0 (ChatKind::System).
    if (!m.chats.empty()) {
        if (y_in_content == row || y_in_content == row + 1) return std::size_t{0};
    }
    row += 2;
    row += 1;        // blank

    const bool any_group = std::any_of(m.chats.begin(), m.chats.end(),
        [](const model::ChatListItemVM& c) {
            return c.kind != model::ChatKind::Direct
                && c.kind != model::ChatKind::System;
        });
    const bool any_dm = std::any_of(m.chats.begin(), m.chats.end(),
        [](const model::ChatListItemVM& c) {
            return c.kind == model::ChatKind::Direct;
        });

    if (any_group) {
        row += 1;    // section header "CHATS"
        row += 1;    // blank
        for (std::size_t i = 0; i < m.chats.size(); ++i) {
            const auto& c = m.chats[i];
            if (c.kind == model::ChatKind::Direct
             || c.kind == model::ChatKind::System) continue;
            if (!detail::chat_visible(c, m.search_query)) continue;
            // Hit-test all 3 rows (top, bottom, separator) so a click
            // anywhere in the row's vertical band selects the chat —
            // including the hairline gutter below it.
            if (y_in_content >= row && y_in_content < row + 3) return i;
            row += 3;
        }
    }

    if (any_dm) {
        if (any_group) row += 1;     // blank between sections
        row += 1;                    // section header "DIRECT MESSAGES"
        row += 1;                    // blank
        for (std::size_t i = 0; i < m.chats.size(); ++i) {
            const auto& c = m.chats[i];
            if (c.kind != model::ChatKind::Direct) continue;
            if (!detail::chat_visible(c, m.search_query)) continue;
            if (y_in_content >= row && y_in_content < row + 3) return i;
            row += 3;
        }
    }
    return std::nullopt;
}

// ─── Right panel: close button hit-test ──────────────────────────────────────
// The ✕ sits at the panel's titlebar (y=1 inside the padded panel, so y=1
// of the right_panel hstack which itself starts at y=0). It occupies the
// last ~3 cells of the inner content.

[[nodiscard]] inline bool is_close_button(const Layout& l, int x, int y) noexcept
{
    if (!l.show_right) return false;
    if (y != 1)        return false;   // titlebar row inside padding(1)
    // close glyph is at right_inner.right - ~2 cells. right_inner ends one
    // cell before the scrollbar, so allow a 3-cell hot zone.
    const int right_inner_end = l.right_start + l.right_w - 1;  // before scrollbar
    return x >= right_inner_end - 3 && x < right_inner_end;
}

// Which vertical scrollbar column is at this x (if any). The scrollbar
// columns are intentionally Panel::None in panel_at_x, so we need a
// dedicated lookup to route click-on-scrollbar → jump-scroll.
enum class ScrollbarHit : unsigned char { None, Chats, Messages, Members };

[[nodiscard]] inline ScrollbarHit scrollbar_at_x(const Layout& l, int x) noexcept
{
    if (x == l.chats_w - 1) return ScrollbarHit::Chats;
    const int middle_end = l.middle_start + l.middle_w;
    if (x == middle_end - 1) return ScrollbarHit::Messages;
    if (l.show_right && x == l.right_start + l.right_w - 1) return ScrollbarHit::Members;
    return ScrollbarHit::None;
}

// Telegram-web pattern: clicking the chat header (avatar/title in the
// middle column's top kHeaderH rows) toggles the right info panel.
// Excludes the right cluster (counters/clock) area on the far right of
// the header so those don't accidentally toggle.
[[nodiscard]] inline bool is_chat_header(const Layout& l, int x, int y) noexcept
{
    if (y < 0 || y >= l.header_h)              return false;
    if (panel_at_x(l, x) != Panel::Middle)     return false;
    // Right ~20 cells of the header host the right_cluster (mentions /
    // bell / clock). Leave that zone alone.
    const int middle_end = l.middle_start + l.middle_w;
    return x < middle_end - 20;
}

// ─── Composer send/mic button hit-test ───────────────────────────────────────
// Send button is the last icon in the composer row. Composer lives at
// y = term_h - 1.

[[nodiscard]] inline bool is_composer_send(const Layout& l, int x, int y) noexcept
{
    if (y != l.term_h - 1) return false;
    if (panel_at_x(l, x) != Panel::Middle) return false;
    // Last ~3 cells of the middle column.
    const int middle_end = l.middle_start + l.middle_w;
    return x >= middle_end - 3 && x < middle_end;
}

// 📎 attach button — the cell run immediately to the left of the send
// button on the bottom composer row. Same 3-cell hot zone. In Full
// density only; the Compact / Minimal layouts drop the attach button
// so the hit-test naturally no-ops there (no attach button == clicks
// fall through to is_composer_input).
[[nodiscard]] inline bool is_composer_attach(const Layout& l, int x, int y) noexcept
{
    if (y != l.term_h - 1) return false;
    if (panel_at_x(l, x) != Panel::Middle) return false;
    const int middle_end = l.middle_start + l.middle_w;
    // Sits 3 cells left of the send button — leaves a 1-cell gap so
    // adjacent clicks don't race.
    return x >= middle_end - 7 && x < middle_end - 4;
}


// ─── Composer text area — any click inside the composer band
// (excluding the send button) focuses the composer. ───────────────

[[nodiscard]] inline bool is_composer_input(const Layout& l, int x, int y) noexcept
{
    if (panel_at_x(l, x) != Panel::Middle)        return false;
    if (middle_region_at_y(l, y) != MiddleRegion::Composer) return false;
    if (is_composer_send(l, x, y))                return false;
    return true;
}

// ─── Search input — top of the chat list panel ────────────────────────────
// The search input is 3 rows tall (rounded border + content + border)
// starting after the chat list's padding(1) top and the brand logo
// block, so it spans content rows (1 + brand_block_h) .. (3 + brand_block_h).
// Hit-test in terminal y space accounting for the chats_scroll offset.

[[nodiscard]] inline bool is_search_input(
    const Layout& l,
    int x, int y,
    int chats_scroll_y) noexcept
{
    if (panel_at_x(l, x) != Panel::Chats) return false;
    const int y_in_content = y + chats_scroll_y;
    const int top = 1 + detail::brand_block_h(l.chats_w);
    return y_in_content >= top && y_in_content <= top + 2;
}

// Search input's right-edge ✕ clear button. Lives on the search box's
// middle row. The hot zone is the last ~3 cells of the chats panel
// content area — wider than the glyph itself so it's easy to hit
// without precise aim.
[[nodiscard]] inline bool is_search_clear(
    const Layout& l,
    int x, int y,
    int chats_scroll_y) noexcept
{
    if (panel_at_x(l, x) != Panel::Chats) return false;
    const int y_in_content = y + chats_scroll_y;
    const int mid_row = 1 + detail::brand_block_h(l.chats_w) + 1;  // middle of the 3-row box
    if (y_in_content != mid_row) return false;
    const int chats_right_edge = l.chats_w - 1;   // last col before scrollbar
    return x >= chats_right_edge - 3 && x < chats_right_edge;
}

// ─── Info pane (DM right panel) ─────────────────────────────────────────────
// Row offsets inside the padded panel content. These mirror the row order
// in views/organisms/dm_info_panel.hpp's render_dm_info_panel exactly —
// keep both files in lockstep when changing the panel layout.
//
// Layout (y, 0-indexed, including padding(1) top):
//   0          padding(1) top
//   1          titlebar
//   2          blank
//   3..9       hero (7 rows: kCellsH)
//   10         blank
//   11         name row
//   12         presence row
//   13         blank
//   14..15     phone (value, label)
//   16         blank
//   17..18     username
//   19         blank
//   20..21     bio
//   22         blank
//   23         notifications toggle
//   24         blank
//   25         tabs labels
//   26         tabs underline
//   27         blank
//   28+        media list
constexpr int kInfoNotifY  = 23;
constexpr int kInfoTabsY   = 25;

// y of the title bar — used by the close-button hit test (overriding the
// existing y==1 heuristic to be panel-aware).
[[nodiscard]] inline bool is_right_panel(const Layout& l, int x, int y) noexcept
{
    return l.show_right && panel_at_x(l, x) == Panel::Right && y >= 0 && y < l.term_h;
}

// Tab hit-test. Returns the active-tab index a click resolves to, or -1.
// Mirrors render_tabs's label set + separators in dm_info_panel.
[[nodiscard]] inline int info_tab_at(
    const Layout& l, int x, int y, int members_scroll_y) noexcept
{
    if (!is_right_panel(l, x, y)) return -1;
    const int y_in_content = y + members_scroll_y;
    if (y_in_content != kInfoTabsY) return -1;

    // panel left edge = l.right_start; padding(1) adds 1 col of inset; the
    // tabs row itself starts with a leading " " before the first label.
    const int label_x0 = l.right_start + 1 + 1;   // padding + leading space
    int rel = x - label_x0;
    if (rel < 0) return -1;

    // Labels: "Media"/"Files"/"Links"/"Voice" (5 cells each) or the short
    // form (3 cells each) below 30 inner cols. Separators are " · " = 3 cells.
    const int panel_inner_w = std::max(8, l.right_w - 2);
    const int label_len = (panel_inner_w >= 30) ? 5 : 3;

    // Each tab spans [tab_start, tab_start + label_len). Between tabs sits
    // a 3-cell separator that doesn't pick anything.
    for (int i = 0; i < 4; ++i) {
        const int tab_start = i * (label_len + 3);
        const int tab_end   = tab_start + label_len;
        if (rel >= tab_start && rel < tab_end) return i;
    }
    return -1;
}

[[nodiscard]] inline bool is_info_notifications(
    const Layout& l, int x, int y, int members_scroll_y) noexcept
{
    if (!is_right_panel(l, x, y)) return false;
    const int y_in_content = y + members_scroll_y;
    return y_in_content == kInfoNotifY;
}

}  // namespace tl::app::mouse

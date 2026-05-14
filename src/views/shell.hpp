#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"

#include "views/atoms/divider.hpp"
#include "views/organisms/chat_list.hpp"
#include "views/organisms/conversation_pane.hpp"
#include "views/organisms/dm_info_panel.hpp"
#include "views/organisms/help_overlay.hpp"
#include "views/organisms/jumper_overlay.hpp"
#include "views/molecules/typing_indicator.hpp"
#include "views/organisms/member_list.hpp"
#include "views/theme.hpp"
#include "views/util/scroll.hpp"

namespace tl::views {

// Composes the full app layout. Mirrors messenger.cpp's view():
//   constexpr kHeaderH = 3 (top padded card)
//   constexpr kComposerH = 1 (single inline row)
//   messages, chats, members panes all scrolly + scrollbar_y'd
//   panels separated by hdiv / vdiv — no panel borders
//   overlays (jumper / help) rendered on top via a centered overlay row

namespace detail::shell {

[[nodiscard]] inline model::HeaderInfoVM derive_header(const model::AppModel& m)
{
    model::HeaderInfoVM h{};
    if (m.selected_chat_index && *m.selected_chat_index < m.chats.size()) {
        const auto& c = m.chats[*m.selected_chat_index];
        h.chat_title       = c.title;
        h.subtitle         = c.topic;
        h.is_dm            = (c.kind == model::ChatKind::Direct);
        h.partner_presence = c.partner_presence;
        h.avatar_path      = c.avatar_path;
    }
    int online = 0;
    for (const auto& mem : m.members) {
        if (mem.user.presence == model::Presence::Active) ++online;
    }
    h.online_count = online;
    h.total_count  = static_cast<int>(m.members.size());
    for (const auto& c : m.chats) {
        if (c.mentions_pending) ++h.pending_mentions;
        if (c.unread_count > 0) h.pending_unread += static_cast<int>(c.unread_count);
    }
    return h;
}

[[nodiscard]] inline std::vector<std::size_t>
jumper_matches(std::string_view filter, std::span<const model::ChatListItemVM> chats)
{
    std::vector<std::size_t> out;
    out.reserve(chats.size());
    for (std::size_t i = 0; i < chats.size(); ++i) {
        if (filter.empty() || chats[i].title.find(filter) != std::string::npos) {
            out.push_back(i);
        }
    }
    return out;
}

[[nodiscard]] inline std::vector<model::KeyHintVM> help_navigation()
{
    return {
        {"Tab",       "next chat"},
        {"Shift-Tab", "previous chat"},
        {"j / ↓",     "next chat"},
        {"k / ↑",     "prev chat"},
        {"Enter",     "open / send"},
        {"/",         "open chat jumper"},
        {"?",         "open this help"},
        {"i",         "toggle info panel"},
        {"q / Esc",   "quit"},
    };
}

[[nodiscard]] inline std::vector<model::KeyHintVM> help_composer()
{
    return {
        {"←/→",     "move cursor"},
        {"^A / ^E", "jump to line start / end"},
        {"^W",      "delete previous word"},
        {"^U / ^K", "clear to start / end of line"},
        {"Bksp",    "delete previous char"},
        {"Enter",   "send (or run /command)"},
    };
}

[[nodiscard]] inline std::vector<model::KeyHintVM> help_messages()
{
    return {
        {"PgUp",  "scroll up a page"},
        {"PgDn",  "scroll down a page"},
        {"g",     "jump to oldest"},
        {"G",     "jump to newest"},
        {"^L",    "clear current chat"},
    };
}

}  // namespace detail::shell

[[nodiscard]] inline maya::Element render_shell(const model::AppModel& m)
{
    using namespace maya;
    using namespace maya::dsl;

    const auto header     = detail::shell::derive_header(m);

    // ─── Geometry — mirrors messenger.cpp's middle_width() ─────────────
    // Bubble logic, message_list and the composer all want a "how many
    // cells does the middle column actually have" hint that matches the
    // flex layout's eventual decision. We derive it from the same percent
    // / clamp values the panels themselves use, then auto-hide the right
    // panel on very narrow terminals so the middle never collapses.

    const int term_w = std::max(40, m.term_w > 0 ? m.term_w : 120);
    const int term_h = std::max(10, m.term_h > 0 ? m.term_h : 30);

    const int sidebar_sb_h  = std::max(4, term_h);

    constexpr int kChatsPct   = 28;
    constexpr int kChatsMin   = 24;
    constexpr int kChatsMax   = 34;
    constexpr int kRightPct   = 30;
    constexpr int kRightMin   = 26;
    constexpr int kRightMax   = 38;
    constexpr int kRightAutoHideAt = 100;  // term_w below this drops the info panel

    const int chats_w = std::clamp(term_w * kChatsPct / 100, kChatsMin, kChatsMax);

    const bool show_right = m.right_panel_open
        && m.selected_chat_index
        && *m.selected_chat_index < m.chats.size()
        && term_w >= kRightAutoHideAt;

    const int right_w = show_right
        ? std::clamp(term_w * kRightPct / 100, kRightMin, kRightMax)
        : 0;

    // 1 vdiv between chats and middle, plus a second one when right is open.
    // chats_w and right_w already include their own scrollbar columns
    // (each panel is `[content | scrollbar]`), so the only scrollbar we
    // need to subtract here is the middle column's own.
    const int dividers     = show_right ? 2 : 1;
    const int middle_inner_w = std::max(24,
        term_w - chats_w - right_w - dividers - 1);

    // ─── Middle column — single conversation_pane widget owns header /
    //                    messages / composer + their scroll plumbing ──
    ConversationPaneInputs cp{};
    cp.header           = header;
    cp.typers           = std::span<const model::UserVM>{m.typers};
    cp.messages         = std::span<const model::MessageVM>{m.messages};
    cp.composer         = &m.composer;
    cp.msg_scroll       = &const_cast<maya::ScrollState&>(m.msg_scroll);
    cp.composer_focused = m.focus == model::FocusedPane::Composer;
    cp.tick             = m.tick;
    cp.clock_seconds    = m.clock_seconds;
    cp.self_presence    = m.self_presence;
    cp.inner_w          = middle_inner_w;
    cp.term_h           = term_h;
    auto middle = render_conversation_pane(cp);

    // ─── Chats panel ─ left, scrollable ──────────────────────────────────
    auto chats_inner = render_chat_list(
        std::span<const model::ChatListItemVM>{m.chats},
        m.selected_chat_index,
        std::string_view{m.search_query},
        m.focus == model::FocusedPane::ChatList,
        chats_w,
        m.composer.caret_visible);  // same blink phase as composer

    auto chats_panel = hstack()
        .width(Dimension::percent(kChatsPct))
        .min_width(Dimension::fixed(kChatsMin))
        .max_width(Dimension::fixed(kChatsMax))
        .shrink(0)
        (std::move(chats_inner) | scrolly(m.chats_scroll, 0) | grow(1),
         scrollbar_y(m.chats_scroll, sidebar_sb_h, util::scrollbar_style()));

    Element right_panel = text(std::string{});
    if (show_right) {
        const auto& c = m.chats[*m.selected_chat_index];
        Element right_inner;
        if (c.kind == model::ChatKind::Direct) {
            model::UserVM partner{};
            partner.name        = c.title;
            partner.initials    = c.initials.empty()
                ? std::string{c.title.begin(),
                              c.title.begin() + std::min<std::size_t>(2, c.title.size())}
                : c.initials;
            partner.presence    = c.partner_presence;
            partner.avatar_path = c.avatar_path;
            // tabs viewport = panel content width minus inner padding(1) on each side.
            const int tabs_viewport_w = std::max(8, right_w - 2);
            right_inner = render_dm_info_panel(
                partner, m.tabs_scroll, tabs_viewport_w, false);
        } else {
            right_inner = render_member_list(
                std::span<const model::MemberVM>{m.members},
                c.title, c.topic, false, c.avatar_path);
        }
        right_panel = hstack()
            .width(Dimension::percent(kRightPct))
            .min_width(Dimension::fixed(kRightMin))
            .max_width(Dimension::fixed(kRightMax))
            .shrink(0)
            (std::move(right_inner) | scrolly(m.members_scroll, 0) | grow(1),
             scrollbar_y(m.members_scroll, sidebar_sb_h, util::scrollbar_style()));
    }

    auto body = show_right
        ? hstack().grow(1)(chats_panel, vdiv(), middle, vdiv(), right_panel)
        : hstack().grow(1)(chats_panel, vdiv(), middle);

    // ─── Overlays ─ replace the body for now (z-stack pattern reserved
    //                for a future pass once maya's stack supports overlay
    //                hit-testing the way we want). ──────────────────────
    if (m.jumper_open) {
        const auto matches = detail::shell::jumper_matches(m.jumper_filter, m.chats);
        return vstack().grow(1).align_items(Align::Center).justify(Justify::Center)(
            render_jumper_overlay(m.jumper_filter,
                std::span<const model::ChatListItemVM>{m.chats},
                std::span<const std::size_t>{matches},
                m.jumper_index)
        );
    }
    if (m.help_open) {
        auto nav = detail::shell::help_navigation();
        auto com = detail::shell::help_composer();
        auto msg = detail::shell::help_messages();
        std::array<HelpSection, 3> sections = {
            HelpSection{"navigation", std::span<const model::KeyHintVM>{nav}},
            HelpSection{"composer",   std::span<const model::KeyHintVM>{com}},
            HelpSection{"messages",   std::span<const model::KeyHintVM>{msg}},
        };
        return vstack().grow(1).align_items(Align::Center).justify(Justify::Center)(
            render_help_overlay(std::span<const HelpSection>{sections})
        );
    }

    return body;
}

}  // namespace tl::views

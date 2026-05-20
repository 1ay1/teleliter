#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/brand_logo.hpp"
#include "views/atoms/search_input.hpp"
#include "views/molecules/chat_row.hpp"
#include "views/molecules/saved_messages_row.hpp"
#include "views/molecules/section_header.hpp"
#include "views/molecules/shortcut_hint_row.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Telegram-style left sidebar:
//   brand line ─ search box ─
//   Saved Messages (pinned)
//   — CHATS — (group + channel)
//   — DIRECT MESSAGES —
//   — SHORTCUTS — (key hints footer)

[[nodiscard]] inline maya::Element render_chat_list(
    std::span<const model::ChatListItemVM> chats,
    model::Option<std::size_t> selected_index,
    std::string_view search_query,
    bool focused,
    int panel_w = 32,
    bool caret_visible = true)
{
    using namespace maya;
    using namespace maya::dsl;

    std::vector<Element> rows;
    rows.reserve(chats.size() + 16);

    // Brand wordmark sits above the search input. caret_visible drives
    // its blink in lockstep with the composer + search caret so every
    // pulsing thing on the screen is in phase.
    rows.push_back(render_brand_logo(panel_w, caret_visible));
    rows.push_back(text(std::string{}));

    rows.push_back(render_search_input(search_query, "Search",
                                       focused, caret_visible));
    rows.push_back(text(std::string{}));

    rows.push_back(render_saved_messages_row());
    rows.push_back(text(std::string{}));

    bool any_group = false;
    for (std::size_t i = 0; i < chats.size(); ++i) {
        if (chats[i].kind == model::ChatKind::Direct
         || chats[i].kind == model::ChatKind::System) continue;
        if (!any_group) {
            rows.push_back(render_section_header("CHATS"));
            rows.push_back(text(std::string{}));
            any_group = true;
        }
        if (!search_query.empty()
         && chats[i].title.find(search_query) == std::string::npos) continue;
        const bool sel = selected_index && *selected_index == i;
        rows.push_back(render_chat_row(chats[i], sel, focused, panel_w));
    }

    bool any_dm = false;
    for (std::size_t i = 0; i < chats.size(); ++i) {
        if (chats[i].kind != model::ChatKind::Direct) continue;
        if (!any_dm) {
            if (any_group) rows.push_back(text(std::string{}));
            rows.push_back(render_section_header("DIRECT MESSAGES"));
            rows.push_back(text(std::string{}));
            any_dm = true;
        }
        if (!search_query.empty()
         && chats[i].title.find(search_query) == std::string::npos) continue;
        const bool sel = selected_index && *selected_index == i;
        rows.push_back(render_chat_row(chats[i], sel, focused, panel_w));
    }

    // Shortcuts footer hidden when the panel is at its minimum — saves
    // ~8 rows on tight terminals, and `?` still surfaces them via the
    // help overlay.
    if (panel_w >= 30) {
        rows.push_back(text(std::string{}));
        rows.push_back(render_section_header("SHORTCUTS"));
        rows.push_back(text(std::string{}));
        static const std::array<model::KeyHintVM, 5> hints = {
            model::KeyHintVM{"tab", "next pane"},
            model::KeyHintVM{"/",   "jump to chat"},
            model::KeyHintVM{"?",   "help"},
            model::KeyHintVM{"i",   "toggle info"},
            model::KeyHintVM{"q",   "quit"},
        };
        for (const auto& h : hints) rows.push_back(render_shortcut_hint_row(h));
    }

    // Borderless — the shell adds a vdiv to the right and wraps this in a
    // scrolly viewport + scrollbar_y so this list contributes only its
    // intrinsic content. Top padding 1 keeps the search box visually
    // aligned with the middle column's header (which has padding(1,0,0,0))
    // and the right panel (which uses padding(1)).
    return vstack()
        .padding(1, 1, 0, 1)
        (rows);
}

[[nodiscard]] inline maya::Element render_chat_list(
    std::span<const model::ChatListItemVM> chats,
    model::Option<std::size_t> selected_index,
    bool focused)
{
    return render_chat_list(chats, selected_index, std::string_view{}, focused);
}

}  // namespace tl::views

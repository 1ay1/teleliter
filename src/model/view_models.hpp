#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <maya/core/scroll_state.hpp>

#include "model/ids.hpp"
#include "model/result.hpp"

namespace tl::model {

// ─── Enums (closed sums) ──────────────────────────────────────────────────────

enum class Presence : unsigned char {
    Active,    // green dot, "online"
    Away,      // amber dot, "away"
    Dnd,       // red dot, "do not disturb"
    Offline,   // muted dot, "offline"
};

enum class ReadState : unsigned char {
    Sending,    // ○      — local-only
    Sent,       // ✓      — server received
    Delivered,  // ✓✓     — peer received
    Read,       // ✓✓ (accent) — peer read
};

enum class FocusedPane : unsigned char {
    ChatList,
    Messages,
    Composer,
};

enum class ChatKind : unsigned char {
    Direct,    // 1:1 DM
    Group,     // multi-user
    Channel,   // broadcast
    System,    // service / notification room
};

// ─── View-models ──────────────────────────────────────────────────────────────
// Plain aggregates. Strings are pre-formatted. Reaction emojis are stored as
// short utf-8 strings (one grapheme cluster). The view layer never reaches into
// TDLib types — these are the contract.

struct ReactionVM {
    std::string emoji;          // utf-8 single emoji
    int         count = 0;
    bool        self_reacted = false;
};

struct UserVM {
    UserId       id{};
    std::string  name;
    std::string  initials;      // pre-computed (1-2 chars)
    Presence     presence  = Presence::Offline;
    bool         is_self   = false;
    // Optional path to a JPG/PNG portrait. When set and loadable, the
    // big_avatar_block / member rows render it; otherwise initials.
    std::string  avatar_path;
};

struct MessageVM {
    MessageId   id{};
    UserId      author_id{};
    std::string author_name;
    std::string author_initials;
    std::string body;
    std::string timestamp;       // pre-formatted "HH:MM"
    std::string age_label;       // "5m" / "yesterday" / "Apr 5"
    bool        from_me        = false;
    bool        mentions_you   = false;
    bool        is_system      = false;
    bool        is_action      = false;   // "/me waves"
    bool        compact        = false;   // grouped with previous: hide author
    bool        show_gap_above = false;   // render gap-separator above this msg
    std::string gap_label;                // "5m later" — populated if show_gap_above
    ReadState   read_state = ReadState::Sent;
    std::vector<ReactionVM> reactions;
    std::string author_avatar_path;       // optional portrait for peer bubbles
};

struct ChatListItemVM {
    ChatId        id{};
    std::string   title;
    std::string   initials;               // for avatar
    std::string   topic;
    std::string   last_message_preview;
    std::string   last_message_time;
    std::size_t   unread_count = 0;
    bool          muted   = false;
    bool          pinned  = false;
    bool          flash   = false;        // recent activity highlight
    ChatKind      kind    = ChatKind::Direct;
    Presence      partner_presence = Presence::Offline;
    bool          mentions_pending = false;
    // Optional path to a JPG/PNG/BMP/GIF/TGA on disk. When set and
    // loadable, the avatar atom renders the image; otherwise it falls
    // back to the tinted-initials block. Empty by default so the seed
    // continues to render text avatars.
    std::string   avatar_path;
};

struct MemberVM {
    UserVM      user;
    std::string status_text;       // "active 5m ago", "typing…", etc.
    bool        typing = false;
};

struct ComposerVM {
    std::string text;
    std::size_t cursor_bytes = 0;
    bool        caret_visible = true;
    int         char_limit = 4096;
};

struct TypingVM {
    std::vector<UserVM> typers;
    int tick = 0;
};

struct KeyHintVM {
    std::string key;
    std::string label;
};

struct TabVM {
    std::string label;
    int         badge_count = 0;
};

struct MediaItemVM {
    std::string label;           // "1:05" / "photo" / "voice 0:42"
    std::string kind_glyph;      // "◆" / "▶" / "♪"
};

struct ActionItemVM {
    std::string glyph;
    std::string label;
    std::string shortcut;        // optional kbd hint
    bool        accent = false;  // tints the glyph with the accent color
};

struct ToggleVM {
    std::string glyph;           // ⚑ etc.
    std::string label;
    bool        on = true;
    std::string on_label  = "on";
    std::string off_label = "off";
};

struct HeaderInfoVM {
    std::string chat_title;
    std::string subtitle;
    int         online_count = 0;
    int         total_count  = 0;
    int         pending_mentions = 0;
    int         pending_unread   = 0;
    bool        is_dm = false;
    Presence    partner_presence = Presence::Offline;
    std::string avatar_path;
};

// ─── Aggregate Model ──────────────────────────────────────────────────────────

struct AppModel {
    std::vector<ChatListItemVM> chats;
    Option<std::size_t>         selected_chat_index{};

    std::vector<MessageVM>      messages;          // for the open chat
    std::vector<MemberVM>       members;           // for the open chat
    std::vector<UserVM>         typers;            // active typers in open chat

    ComposerVM                  composer;
    FocusedPane                 focus = FocusedPane::ChatList;

    std::string                 self_name     = "you";
    Presence                    self_presence = Presence::Active;
    std::string                 search_query;       // chat list filter

    int                         tick          = 0;  // animation counter
    std::int64_t                clock_seconds = 0;  // wall-clock seconds since app start
    int                         term_w        = 0;  // current terminal width
    int                         term_h        = 0;  // current terminal height

    bool                        right_panel_open = true;
    bool                        help_open    = false;
    bool                        jumper_open  = false;
    std::string                 jumper_filter;
    std::size_t                 jumper_index = 0;

    // Persistent scroll positions. `mutable` so a `const Model&` rendered
    // by the view layer can let maya's scrollable widgets writeback
    // viewport bounds / max_y each frame.
    mutable maya::ScrollState   msg_scroll{};
    mutable maya::ScrollState   chats_scroll{};
    mutable maya::ScrollState   members_scroll{};
    mutable maya::ScrollState   help_scroll{};
    mutable maya::ScrollState   tabs_scroll{};
};

}  // namespace tl::model

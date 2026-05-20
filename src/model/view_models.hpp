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

// Snapshot of the message being quoted in a reply. Kept inline (no
// pointer back into the messages list) so the reply preview keeps
// rendering even after the original message is deleted or scrolled
// out of view.
struct ReplyQuoteVM {
    MessageId   source_id{};
    std::string author_name;
    std::string snippet;        // first line, pre-truncated by the caller
};

// Voice / video notes. Both render as a play-button + waveform + clock
// inside the bubble. Video notes can't actually paint video frames in
// a terminal, so the widget shows a 🎬 badge and behaves like an
// audio-only player when triggered.
struct AudioNoteVM {
    std::string file_path;                 // for display / play action
    int         duration_secs = 0;
    int         progress_secs = 0;         // advances on Tick when playing
    bool        playing       = false;
    std::vector<std::uint8_t> waveform;    // 0..255 bar heights, evenly spaced
    // Optional polish:
    //   unread          → paints a small ● accent dot beside the clock
    //                    (Telegram's "you haven't played this yet" cue)
    //   playback_speed  → 1.0 / 1.5 / 2.0 — renders as a tiny right-side pill
    //                    that the user can rotate; affects nothing locally
    //                    (no real audio engine) but the UI affordance is
    //                    there for parity with Telegram-mobile.
    //   transcript      → collapsed by default; expanded line draws under
    //                    the waveform row when transcript_expanded is true.
    bool         unread             = false;
    double       playback_speed     = 1.0;
    std::string  transcript;
    bool         transcript_expanded = false;
};

struct VideoNoteVM {
    std::string file_path;
    int         duration_secs = 0;
    int         progress_secs = 0;
    bool        playing       = false;
    std::vector<std::uint8_t> waveform;
    // Telegram's video circles are muted by default and have a tap-to-
    // unmute affordance. Mirrored here for the UI to surface.
    bool        muted              = true;
    bool        unread             = false;
    double      playback_speed     = 1.0;
};

// ─── Rich media attachments (all text-rendered) ─────────────────────────────
// Each of these can be attached to a MessageVM via the optional fields at
// the bottom of that struct. Widgets in views/molecules/media_*.hpp turn
// them into bubble bodies built entirely from text + box-drawing glyphs.
//
// Common pattern: title line(s) → metadata line(s) → footer hint telling
// the user what command would open the underlying file/URL on a desktop.

// Photo / image. We can't show pixels in every bubble (would dominate
// the column at half-block resolution), so the default render is a
// labelled "card" — filename, dimensions, file size, optional caption.
// Set `show_thumb=true` to additionally render a tiny half-block
// thumbnail above the metadata when the file decodes.
struct PhotoVM {
    std::string file_path;       // local path; "" if remote-only
    std::string caption;         // optional text caption beneath the card
    int         width_px  = 0;
    int         height_px = 0;
    std::size_t size_bytes = 0;
    bool        show_thumb = false;
};

// Generic document / file attachment. Pure text card with an icon
// glyph, filename, size, optional mime label, and an open hint.
struct DocumentVM {
    std::string file_path;
    std::string filename;        // pre-formatted display name
    std::string mime;            // "application/pdf", "text/plain", …
    std::size_t size_bytes = 0;
    std::string caption;
};

// Sticker. A short text-art card: the sticker emoji big and centered,
// pack name underneath. Animated stickers (TGS) collapse to the same
// representation in a terminal.
struct StickerVM {
    std::string emoji;           // utf-8, 1-2 grapheme clusters
    std::string pack_name;       // "Animals" / "Memes" / …
    bool        animated = false;
};

// Animated GIF / short looping clip. Text card with a play hint; the
// terminal can't loop frames, so we show a single "animation" label
// and a duration.
struct AnimationVM {
    std::string file_path;
    int         duration_secs = 0;
    int         width_px  = 0;
    int         height_px = 0;
    std::size_t size_bytes = 0;
    std::string caption;
};

// Full-length video (NOT a video note). Wider card than VideoNoteVM,
// includes title + thumbnail metadata. Plays externally via mpv hint.
struct VideoVM {
    std::string file_path;
    std::string title;
    int         duration_secs = 0;
    int         width_px  = 0;
    int         height_px = 0;
    std::size_t size_bytes = 0;
    std::string caption;
};

// Music / audio track with metadata. Distinct from voice notes: tracks
// have a title + artist and render with a different glyph (♪ vs 🎙).
struct MusicTrackVM {
    std::string file_path;
    std::string title;
    std::string artist;
    int         duration_secs = 0;
    int         progress_secs = 0;
    bool        playing       = false;
    std::vector<std::uint8_t> waveform;
    double      playback_speed = 1.0;
};

// URL preview. Telegram-style card: site name (italic dim), title
// (bold), description (1-2 lines of muted text). The trailing rail
// glyph is the same ▎ accent the reply-quote uses.
struct LinkPreviewVM {
    std::string url;
    std::string site_name;       // "GitHub" / "maya.dev" / …
    std::string title;
    std::string description;
};

// Geographic point. Pure text — lat/long + optional venue name +
// address. No map in a terminal, so we show coordinates and a
// "open in maps" hint.
struct LocationVM {
    double      lat = 0.0;
    double      lng = 0.0;
    std::string venue_name;      // optional — "Blue Bottle Coffee"
    std::string address;         // optional — "66 Mint St, SF"
    bool        live = false;    // live-location flag
};

// Contact card. First/last name, phone, optional username.
struct ContactVM {
    std::string first_name;
    std::string last_name;
    std::string phone;
    std::string username;        // without leading '@'
};

// Poll option — text + tally + whether the local user voted for it.
struct PollOptionVM {
    std::string text;
    int         votes = 0;
    bool        self_voted = false;
};

// Poll. Renders as a question + list of options with horizontal bar
// charts (▏ ▎ ▍ ▌ ▋ ▊ ▉ █) sized by vote share.
struct PollVM {
    std::string question;
    std::vector<PollOptionVM> options;
    int         total_votes = 0;
    bool        closed      = false;
    bool        anonymous   = true;
    bool        multi_choice = false;
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
    Option<ReplyQuoteVM> reply_quote{};   // set when this msg replies to another
    Option<AudioNoteVM> audio_note{};     // voice message; replaces body in render
    Option<VideoNoteVM> video_note{};     // video note; audio-only playback in TUI
    // Rich media attachments. At most one of these should be set per
    // message — message_bubble dispatches on the first one it finds in
    // the order audio_note → video_note → photo → sticker → animation
    // → video → music → document → contact → location → poll, then falls
    // back to plain body text. link_preview attaches BELOW the body
    // (matching Telegram-web's behaviour) and is independent of the
    // other media kinds.
    Option<PhotoVM>       photo{};
    Option<StickerVM>     sticker{};
    Option<AnimationVM>   animation{};
    Option<VideoVM>       video{};
    Option<MusicTrackVM>  music{};
    Option<DocumentVM>    document{};
    Option<ContactVM>     contact{};
    Option<LocationVM>    location{};
    Option<PollVM>        poll{};
    Option<LinkPreviewVM> link_preview{};
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
    // Reply state. When set, the composer renders a preview row above
    // the input and the next SendComposer attaches the quote to the
    // outgoing MessageVM. Cleared by Send (after attach) or CancelReply.
    Option<ReplyQuoteVM> reply_quote{};

    // ─── Attachments ─────────────────────────────────────────────────
    // Pending uploads queued on the composer. They render as chips in a
    // row above the input; SendComposer drains them into the outgoing
    // message (one MessageVM per attachment kind, with the text body
    // attached to the first chip). Up to kMaxAttachments to keep the
    // composer card from eating the conversation pane.
    enum class AttachmentKind : unsigned char {
        File, Photo, Voice, Video,
    };
    struct Attachment {
        AttachmentKind kind = AttachmentKind::File;
        std::string    label;          // pre-formatted display name
        std::string    path;           // local fs path / URL
        std::size_t    size_bytes = 0; // 0 if unknown
        int            duration_secs = 0; // voice / video clips
    };
    std::vector<Attachment> attachments;
    static constexpr std::size_t kMaxAttachments = 6;

    // ─── Voice recording mode ───────────────────────────────────────────
    // When `recording` is true the composer flips into a dedicated
    // recording bar: a pulsing ● REC indicator, an mm:ss timer, a
    // small live waveform window, and stop / cancel actions. Sending
    // produces an AudioNoteVM with the captured duration + waveform.
    bool                       recording = false;
    int                        recording_secs = 0;
    std::vector<std::uint8_t>  recording_waveform;  // rolling, last N samples
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
    std::string label;           // primary text — filename / link title
    std::string kind_glyph;      // 📎 / 🎵 / 🎬 / 📷 / 🔗 / 📄
    std::string subtitle;        // size + author / domain — muted text
    std::string href;            // open target — file path or URL
    std::string open_hint;       // "mpv ↗" / "xdg-open ↗" / "in browser ↗"
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

    // Info-pane state. `info_active_tab` indexes the Media/Files/Links/Voice
    // strip; the shared-content list under the tabs filters its rows by
    // this. `notifications_on` is the toggle row's value (per-chat would be
    // nicer but the seed data has no place for it yet).
    int                         info_active_tab = 0;
    bool                        notifications_on = true;
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

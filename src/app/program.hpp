#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <maya/maya.hpp>

#include "model/ids.hpp"
#include "model/view_models.hpp"
#include "msg/msg.hpp"

#include "app/mouse.hpp"
#include "app/run_command.hpp"
#include "app/text_edit.hpp"

#include "td/client.hpp"
#include "td/command.hpp"
#include "td/event.hpp"

#include "util/debug_log.hpp"

#include "views/shell.hpp"

namespace tl::app {

// ─── TDLib event handler (forward declaration) ────────────────────────
// Defined below the seed:: helpers + composer_ops; updates AppModel in
// place from the closed sum of TDLib events. Kept as a free function so
// the giant update() overload set in TeleliterProgram doesn't grow a
// huge inline match against td::event::Event.
void handle_td_event(model::AppModel& m, td::event::Event ev);

// ─── Pure helpers ─────────────────────────────────────────────────────────────

[[nodiscard]] inline model::FocusedPane cycle_focus(model::FocusedPane f) noexcept
{
    // Two-state cycle: typing always targets either the search (when
    // ChatList is focused) or the message composer. Up/Down keys still
    // scroll messages when in Composer focus, so Messages doesn't need
    // its own focus slot.
    switch (f) {
        case model::FocusedPane::ChatList: return model::FocusedPane::Composer;
        case model::FocusedPane::Composer: return model::FocusedPane::ChatList;
        case model::FocusedPane::Messages: return model::FocusedPane::ChatList;
    }
    return model::FocusedPane::ChatList;
}

namespace seed {

[[nodiscard]] inline std::vector<model::ChatListItemVM> chats()
{
    using model::ChatId;
    using model::ChatKind;
    using model::Presence;
    return {
        {ChatId{ 1}, "Saved Messages",  "SM", "your scratchpad",            "you: useful link",              "10:02",  0, false, true,  false, ChatKind::System,  Presence::Offline, false, "assets/avatars/p1.jpg"},
        {ChatId{ 2}, "maya devs",       "MD", "C++26 TUI framework",        "ship the C++26 branch",         "09:58", 12, false, true,  true,  ChatKind::Group,   Presence::Offline, true,  "assets/avatars/p2.jpg"},
        {ChatId{ 3}, "tui club",        "TC", "terminal UI nerds",          "anyone tried TDLib?",           "08:14",  3, false, false, true,  ChatKind::Channel, Presence::Offline, false, "assets/avatars/p3.jpg"},
        {ChatId{ 4}, "Ana",             "AN", "",                           "see you at 6",                  "Yest",   0, false, false, false, ChatKind::Direct,  Presence::Active,  false, "assets/avatars/p4.jpg"},
        {ChatId{ 5}, "release-bots",    "RB", "ci heartbeat",               "build #2103 passed",            "Yest",   1, true,  false, false, ChatKind::Channel, Presence::Offline, false, "assets/avatars/p5.jpg"},
        {ChatId{ 6}, "rust vs cpp",     "RC", "amicable debate",            "fearless concurrency? lol",     "Mon",    0, false, false, false, ChatKind::Group,   Presence::Offline, false, "assets/avatars/p6.jpg"},
        {ChatId{ 7}, "ops",             "OP", "infra red-team",             "rotating creds at 4pm",         "Sun",    0, false, false, false, ChatKind::Group,   Presence::Offline, false, "assets/avatars/p7.jpg"},
        {ChatId{ 8}, "Mom",             "MO", "",                           "call me when you can",          "Apr 5",  2, false, false, false, ChatKind::Direct,  Presence::Away,    false, "assets/avatars/p8.jpg"},
    };
}

[[nodiscard]] inline std::vector<model::MessageVM> messages_for(const model::ChatListItemVM& c)
{
    using model::MessageId;
    using model::ReadState;
    using model::UserId;
    std::vector<model::MessageVM> out;
    out.push_back({MessageId{1}, UserId{0},  "system",        "S",
        c.title + " — chat opened",      "—",     "now",
        false, false, true,  false, false, false, "", ReadState::Sent, {}, {}});
    out.push_back({MessageId{2}, UserId{10}, c.title,         c.initials,
        "hey — are you around?",          "10:21", "12m",
        false, false, false, false, false, false, "", ReadState::Sent, {},
        c.avatar_path});
    out.push_back({MessageId{3}, UserId{1},  "you",           "YO",
        "yeah, just finished a build",    "10:22", "11m",
        true,  false, false, false, false, false, "", ReadState::Read, {},
        "assets/avatars/p8.jpg"});
    out.push_back({MessageId{4}, UserId{10}, c.title,         c.initials,
        "cool — pushing the patch in a sec","10:23","10m",
        false, false, false, false, true,  false, "", ReadState::Sent,
        { {"👍", 2, true}, {"🚀", 1, false} },
        c.avatar_path});
    if (c.unread_count > 0) {
        out.push_back({MessageId{5}, UserId{10}, c.title,     c.initials,
            "@you let me know if it breaks anything", "10:24", "9m",
            false, true,  false, false, false, true,  "1m later", ReadState::Sent, {},
            c.avatar_path});
    }

    // Sample voice + video notes — gives the audio_note widget some
    // signal to render in every chat. Waveform is a hand-rolled
    // envelope that looks like a real voice clip when binned.
    static const std::vector<std::uint8_t> kVoiceWf = {
         30, 80,140,200,180,150,210,240,220,180,140,110, 90,120,170,210,
        230,200,160,110, 80, 60, 90,140,180,220,200,160,120, 80, 50, 30,
    };
    {
        model::MessageVM v{};
        v.id              = MessageId{6};
        v.author_id       = UserId{10};
        v.author_name     = c.title;
        v.author_initials = c.initials;
        v.timestamp       = "10:26";
        v.age_label       = "7m";
        v.read_state      = ReadState::Sent;
        v.author_avatar_path = c.avatar_path;
        model::AudioNoteVM an{};
        an.file_path     = "assets/media/voice-1.m4a";
        an.duration_secs = 42;
        an.waveform      = kVoiceWf;
        // Demo polish: an unplayed note with a transcript ready to expand.
        an.unread             = true;
        an.playback_speed     = 1.0;
        an.transcript         = "hey, the patch landed — check it whenever you get a moment.";
        an.transcript_expanded = false;
        v.audio_note = std::move(an);
        out.push_back(std::move(v));
    }
    {
        model::MessageVM v{};
        v.id              = MessageId{7};
        v.author_id       = UserId{1};
        v.author_name     = "you";
        v.author_initials = "YO";
        v.timestamp       = "10:27";
        v.age_label       = "6m";
        v.from_me         = true;
        v.read_state      = ReadState::Read;
        v.author_avatar_path = "assets/avatars/p8.jpg";
        model::VideoNoteVM vn{};
        vn.file_path     = "assets/media/video-1.mp4";
        vn.duration_secs = 65;
        vn.waveform      = kVoiceWf;
        // Video circles are muted by default in Telegram — mirror that.
        // The mute glyph in the top-right of the circle reflects this.
        vn.muted          = true;
        vn.unread         = false;
        vn.playback_speed = 1.0;
        v.video_note = std::move(vn);
        out.push_back(std::move(v));
    }

    // ─── One example of every rich-media kind ─────────────────────────────────────
    // Lets the demo exercise every renderer in views/molecules/media_cards.hpp
    // so we can spot regressions just by scrolling the chat. Each one is
    // attached to its own MessageVM (“at most one media kind per bubble”
    // is the rule) so dispatch order in render_body never matters.

    auto push_peer = [&](model::MessageVM v) {
        v.author_id          = UserId{10};
        v.author_name        = c.title;
        v.author_initials    = c.initials;
        v.read_state         = ReadState::Sent;
        v.author_avatar_path = c.avatar_path;
        out.push_back(std::move(v));
    };

    {  // photo card
        model::MessageVM v{};
        v.id        = MessageId{8};
        v.timestamp = "10:28";
        v.age_label = "5m";
        model::PhotoVM p{};
        p.file_path  = "assets/media/hike-sunset.jpg";
        p.caption    = "trail was perfect";
        p.width_px   = 1920;
        p.height_px  = 1080;
        p.size_bytes = 1'258'291;
        v.photo = std::move(p);
        push_peer(std::move(v));
    }
    {  // sticker
        model::MessageVM v{};
        v.id        = MessageId{9};
        v.timestamp = "10:29";
        v.age_label = "5m";
        model::StickerVM s{};
        s.emoji     = "\xF0\x9F\x98\xBA";   // 😺
        s.pack_name = "Animals";
        v.sticker = std::move(s);
        push_peer(std::move(v));
    }
    {  // animation / gif
        model::MessageVM v{};
        v.id        = MessageId{10};
        v.timestamp = "10:30";
        v.age_label = "4m";
        model::AnimationVM a{};
        a.file_path     = "assets/media/loop.gif";
        a.duration_secs = 3;
        a.width_px      = 640;
        a.height_px     = 480;
        a.size_bytes    = 491'520;
        a.caption       = "this gets me every time";
        v.animation = std::move(a);
        push_peer(std::move(v));
    }
    {  // full video
        model::MessageVM v{};
        v.id        = MessageId{11};
        v.timestamp = "10:31";
        v.age_label = "4m";
        model::VideoVM vid{};
        vid.file_path     = "assets/media/tahoe.mp4";
        vid.title         = "Sunset over Tahoe";
        vid.duration_secs = 42;
        vid.width_px      = 1920;
        vid.height_px     = 1080;
        vid.size_bytes    = 12'998'656;
        v.video = std::move(vid);
        push_peer(std::move(v));
    }
    {  // music track (you sending)
        model::MessageVM v{};
        v.id              = MessageId{12};
        v.author_id       = UserId{1};
        v.author_name     = "you";
        v.author_initials = "YO";
        v.from_me         = true;
        v.read_state      = ReadState::Read;
        v.author_avatar_path = "assets/avatars/p8.jpg";
        v.timestamp       = "10:32";
        v.age_label       = "3m";
        model::MusicTrackVM mt{};
        mt.file_path     = "assets/media/midnight-city.mp3";
        mt.title         = "Midnight City";
        mt.artist        = "M83";
        mt.duration_secs = 243;
        mt.progress_secs = 24;
        mt.playing       = false;
        mt.waveform      = kVoiceWf;
        v.music = std::move(mt);
        out.push_back(std::move(v));
    }
    {  // document
        model::MessageVM v{};
        v.id        = MessageId{13};
        v.timestamp = "10:33";
        v.age_label = "3m";
        model::DocumentVM d{};
        d.file_path  = "assets/media/plan.pdf";
        d.filename   = "plan.pdf";
        d.mime       = "application/pdf";
        d.size_bytes = 2'200'000;
        v.document = std::move(d);
        push_peer(std::move(v));
    }
    {  // contact card
        model::MessageVM v{};
        v.id        = MessageId{14};
        v.timestamp = "10:34";
        v.age_label = "2m";
        model::ContactVM ct{};
        ct.first_name = "Ana";
        ct.last_name  = "Rivera";
        ct.phone      = "+1 555 0100";
        ct.username   = "ana";
        v.contact = std::move(ct);
        push_peer(std::move(v));
    }
    {  // location
        model::MessageVM v{};
        v.id        = MessageId{15};
        v.timestamp = "10:35";
        v.age_label = "2m";
        model::LocationVM loc{};
        loc.venue_name = "Blue Bottle Coffee";
        loc.address    = "66 Mint St, SF";
        loc.lat        = 37.7825;
        loc.lng        = -122.4036;
        v.location = std::move(loc);
        push_peer(std::move(v));
    }
    {  // poll
        model::MessageVM v{};
        v.id        = MessageId{16};
        v.timestamp = "10:36";
        v.age_label = "1m";
        model::PollVM p{};
        p.question    = "What's for lunch?";
        p.total_votes = 12;
        p.anonymous   = true;
        p.options = {
            {"Pizza", 8, true},
            {"Salad", 3, false},
            {"Pasta", 1, false},
        };
        v.poll = std::move(p);
        push_peer(std::move(v));
    }
    {  // text + link preview
        model::MessageVM v{};
        v.id        = MessageId{17};
        v.timestamp = "10:37";
        v.age_label = "now";
        v.body      = "check this out";
        model::LinkPreviewVM lp{};
        lp.url         = "https://github.com/1ay1/maya";
        lp.site_name   = "GitHub";
        lp.title       = "1ay1/maya \xE2\x80\x94 Terminal UI for C++26";
        lp.description = "Compile-time DSL, flexbox layout, SIMD-diffed frames.";
        v.link_preview = std::move(lp);
        push_peer(std::move(v));
    }
    return out;
}

[[nodiscard]] inline std::vector<model::MemberVM> members_for(const model::ChatListItemVM& c)
{
    if (c.kind != model::ChatKind::Group && c.kind != model::ChatKind::Channel) return {};
    using model::Presence;
    using model::UserId;
    using model::UserVM;
    return {
        {UserVM{UserId{1},  "you",   "YO", Presence::Active, true,  "assets/avatars/p8.jpg"},  "active now"},
        {UserVM{UserId{10}, c.title, c.initials, Presence::Active, false, c.avatar_path},        "active now"},
        {UserVM{UserId{11}, "ana",   "AN", Presence::Away,    false, "assets/avatars/p4.jpg"},  "away · 5m"},
        {UserVM{UserId{12}, "lina",  "LI", Presence::Dnd,     false, "assets/avatars/p7.jpg"},  "do not disturb"},
        {UserVM{UserId{13}, "kai",   "KA", Presence::Offline, false, "assets/avatars/p3.jpg"},  "last seen yesterday"},
    };
}

[[nodiscard]] inline std::vector<model::UserVM> typers_for(const model::ChatListItemVM& c)
{
    if (c.unread_count == 0) return {};
    using model::Presence;
    using model::UserId;
    using model::UserVM;
    return { UserVM{UserId{11}, "ana", "AN", Presence::Active, false, "assets/avatars/p4.jpg"} };
}

}  // namespace seed

// ─── Composer mutators (pure) ─────────────────────────────────────────────────

namespace composer_ops {

inline void insert(model::ComposerVM& c, std::string_view utf8)
{
    if (static_cast<int>(c.text.size() + utf8.size()) > c.char_limit) return;
    c.text.insert(std::min(c.cursor_bytes, c.text.size()), utf8);
    c.cursor_bytes = std::min(c.cursor_bytes + utf8.size(), c.text.size());
}

inline void backspace(model::ComposerVM& c)
{
    if (c.cursor_bytes == 0) return;
    const auto prev = text_edit::utf8_prev(c.text, c.cursor_bytes);
    c.text.erase(prev, c.cursor_bytes - prev);
    c.cursor_bytes = prev;
}

inline void delete_word(model::ComposerVM& c)
{
    const auto pw = text_edit::prev_word(c.text, c.cursor_bytes);
    c.text.erase(pw, c.cursor_bytes - pw);
    c.cursor_bytes = pw;
}

inline void delete_to_start(model::ComposerVM& c)
{
    c.text.erase(0, c.cursor_bytes);
    c.cursor_bytes = 0;
}

inline void delete_to_end(model::ComposerVM& c)
{
    c.text.erase(c.cursor_bytes);
}

inline void cursor_left(model::ComposerVM& c)
{
    c.cursor_bytes = text_edit::utf8_prev(c.text, c.cursor_bytes);
}

inline void cursor_right(model::ComposerVM& c)
{
    c.cursor_bytes = text_edit::utf8_next(c.text, c.cursor_bytes);
}

inline void cursor_home(model::ComposerVM& c) { c.cursor_bytes = 0; }
inline void cursor_end(model::ComposerVM& c)  { c.cursor_bytes = c.text.size(); }

}  // namespace composer_ops

// ─── Jumper helpers ───────────────────────────────────────────────────────────

[[nodiscard]] inline std::vector<std::size_t>
jumper_matches(std::string_view filter, std::span<const model::ChatListItemVM> chats)
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < chats.size(); ++i) {
        if (filter.empty() || chats[i].title.find(filter) != std::string::npos) {
            out.push_back(i);
        }
    }
    return out;
}

// ─── Program ──────────────────────────────────────────────────────────────────

struct TeleliterProgram {
    using Model = model::AppModel;
    using Msg   = msg::Msg;

    [[nodiscard]] static auto init() -> std::pair<Model, maya::Cmd<Msg>>
    {
        TL_DLOG("app", "init — model bootstrap, auth.stage=Connecting");
        Model m;
        // No seeded chats / messages — TDLib will populate them once
        // authentication completes. We still pre-populate the scroll
        // tuning + the auth overlay state so the first frame renders
        // a clean "connecting" card instead of a blank screen.
        m.auth.stage         = model::AuthStage::Connecting;
        m.auth.active_field  = model::AuthField::Phone;
        m.focus              = model::FocusedPane::ChatList;
        m.self_name          = "you";
        m.self_presence      = model::Presence::Active;
        m.right_panel_open   = true;
        m.composer.char_limit = 4096;
        // Sensible defaults so the first frame (before the first Resize
        // event fires) doesn't lay out for a 0×0 viewport.
        m.term_w             = 120;
        m.term_h             = 40;

        // tabs_scroll stays opt-out so a stray ← / → in the chats panel
        // doesn't scroll the right-panel's tab strip.
        m.tabs_scroll.auto_dispatch = false;

        // Wheel scroll amount per event. Default of 1 row feels glacial
        // for chat / message scrolling; 3 rows tracks roughly with what
        // browsers do per wheel notch.
        for (auto* s : {&m.msg_scroll, &m.chats_scroll, &m.members_scroll,
                        &m.help_scroll}) {
            s->step_y = 3;
        }
        m.msg_scroll.y = 1'000'000;

        // Boot the TDLib runtime. Its task closure runs forever on an
        // isolated thread and feeds td::event::Event values back into
        // the program loop as msg::TdEvent.
        return {std::move(m), td::boot_command()};
    }

    [[nodiscard]] static auto update(Model m, Msg ev)
        -> std::pair<Model, maya::Cmd<Msg>>
    {
        using maya::overload;
        return std::visit(overload{
            // ── App control ──
            [&](msg::Quit) {
                return std::pair{std::move(m), maya::Cmd<Msg>::quit()};
            },
            [&](msg::Tick) {
                m.tick++;
                if ((m.tick & 0x3) == 0) m.clock_seconds += 1;
                // Blink the caret every ~500ms (Tick fires every 250ms,
                // toggle every other tick). The visible flag is read by
                // both composer_input and search_input atoms.
                m.composer.caret_visible = (m.tick & 0x1) == 0;
                // Reap expired typers — TDLib stops sending
                // updateChatAction once the peer stops typing, so we
                // sweep entries whose expiry timestamp is in the past.
                if (!m.typers.empty()) {
                    for (std::size_t i = m.typers.size(); i-- > 0; ) {
                        if (i < m.typer_expiry.size()
                         && m.typer_expiry[i] <= m.clock_seconds) {
                            m.typers.erase(m.typers.begin()
                                + static_cast<std::ptrdiff_t>(i));
                            m.typer_expiry.erase(m.typer_expiry.begin()
                                + static_cast<std::ptrdiff_t>(i));
                        }
                    }
                }
                // Audio / video notes that are playing tick their
                // progress forward once per second of wall-clock time
                // (= every 4 Ticks). Loops back to 0 at the end so
                // hitting play again restarts cleanly.
                if ((m.tick & 0x3) == 0) {
                    for (auto& msg_vm : m.messages) {
                        auto advance = [](auto& note) {
                            if (!note.playing) return;
                            note.progress_secs += 1;
                            if (note.progress_secs >= note.duration_secs) {
                                note.progress_secs = 0;
                                note.playing       = false;
                            }
                        };
                        if (msg_vm.audio_note.has_value()) advance(*msg_vm.audio_note);
                        if (msg_vm.video_note.has_value()) advance(*msg_vm.video_note);
                        if (msg_vm.music.has_value())      advance(*msg_vm.music);
                    }
                    // Voice recording — ±once per second, bump the
                    // timer. The waveform gets a fresh sample on every
                    // tick (4×/sec) so the bars wiggle smoothly.
                    if (m.composer.recording) {
                        m.composer.recording_secs += 1;
                    }
                }
                // Push a synthetic waveform sample every tick while
                // recording — a deterministic-but-jittery envelope so
                // the live waveform looks alive without needing a real
                // microphone. Capped at a sensible buffer length.
                if (m.composer.recording) {
                    constexpr std::size_t kMaxSamples = 256;
                    const int t = m.tick;
                    // Simple bounded "vu meter": baseline + a pseudo-
                    // random wiggle from the tick counter.
                    const std::uint8_t sample = static_cast<std::uint8_t>(
                        80 + ((t * 73 + 41) % 160));
                    m.composer.recording_waveform.push_back(sample);
                    if (m.composer.recording_waveform.size() > kMaxSamples) {
                        m.composer.recording_waveform.erase(
                            m.composer.recording_waveform.begin(),
                            m.composer.recording_waveform.begin()
                                + static_cast<std::ptrdiff_t>(
                                    m.composer.recording_waveform.size() - kMaxSamples));
                    }
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::Resize r) {
                m.term_w = r.w; m.term_h = r.h;
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::CycleFocus) {
                m.focus = cycle_focus(m.focus);
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },

            // ── Auth overlay ──
            // Per-keystroke buffering into the active field, plus Submit
            // which translates the buffered field into a td::cmd::Submit*
            // and ships it off through the runtime.
            [&](msg::AuthCharIn ci) {
                auto& buf = (m.auth.active_field == model::AuthField::Phone)    ? m.auth.phone
                          : (m.auth.active_field == model::AuthField::Code)     ? m.auth.code
                                                                                : m.auth.password;
                buf += text_edit::encode_utf8(ci.cp);
                TL_DLOG("auth", "CharIn cp=U+%04X field=%d buf_len=%zu",
                        static_cast<unsigned>(ci.cp),
                        static_cast<int>(m.auth.active_field), buf.size());
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::AuthBackspace) {
                auto& buf = (m.auth.active_field == model::AuthField::Phone)    ? m.auth.phone
                          : (m.auth.active_field == model::AuthField::Code)     ? m.auth.code
                                                                                : m.auth.password;
                if (!buf.empty()) {
                    const auto p = text_edit::utf8_prev(buf, buf.size());
                    buf.erase(p);
                }
                TL_DLOG("auth", "Backspace field=%d buf_len=%zu",
                        static_cast<int>(m.auth.active_field), buf.size());
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::AuthSubmit) {
                m.auth.submitting = true;
                m.auth.hint.clear();
                td::cmd::Command out;
                switch (m.auth.active_field) {
                    case model::AuthField::Phone:
                        TL_DLOG("auth", "Submit phone len=%zu", m.auth.phone.size());
                        out = td::cmd::SubmitPhone{m.auth.phone};
                        break;
                    case model::AuthField::Code:
                        TL_DLOG("auth", "Submit code len=%zu", m.auth.code.size());
                        out = td::cmd::SubmitCode{m.auth.code};
                        break;
                    case model::AuthField::Password:
                        TL_DLOG("auth", "Submit password len=%zu", m.auth.password.size());
                        out = td::cmd::SubmitPassword{m.auth.password};
                        break;
                }
                return std::pair{std::move(m), td::dispatch(std::move(out))};
            },

            // ── TDLib events ──
            // Carrier-variant unpacked into the per-alternative handlers
            // in tl::app::handle_td_event. Keeps the giant overload set
            // inside update() readable.
            [&](msg::TdEvent ev_in) {
                handle_td_event(m, std::move(ev_in.payload));
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },

            // ── Chat list nav ──
            [&](msg::SelectChatUp) {
                if (m.chats.empty()) return std::pair{std::move(m), maya::Cmd<Msg>{}};
                const auto idx = m.selected_chat_index.value_or(0);
                const auto new_idx = (idx == 0) ? std::size_t{0} : idx - 1;
                m.selected_chat_index = new_idx;
                m.messages.clear();
                m.members.clear();
                m.typers.clear();
                m.typer_expiry.clear();
                m.msg_scroll.y = 1'000'000;
                m.members_scroll.scroll_to_origin();
                m.tabs_scroll.scroll_to_origin();
                mouse::ensure_chat_visible(m);
                if (new_idx < m.chats.size()) {
                    return std::pair{std::move(m),
                        td::dispatch(td::cmd::OpenChat{
                            td::to_td(m.chats[new_idx].id)})};
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::SelectChatDown) {
                if (m.chats.empty()) return std::pair{std::move(m), maya::Cmd<Msg>{}};
                const auto idx  = m.selected_chat_index.value_or(0);
                const auto last = m.chats.size() - 1;
                const auto new_idx = (idx >= last) ? last : idx + 1;
                m.selected_chat_index = new_idx;
                m.messages.clear();
                m.members.clear();
                m.typers.clear();
                m.typer_expiry.clear();
                m.msg_scroll.y = 1'000'000;
                m.members_scroll.scroll_to_origin();
                m.tabs_scroll.scroll_to_origin();
                mouse::ensure_chat_visible(m);
                if (new_idx < m.chats.size()) {
                    return std::pair{std::move(m),
                        td::dispatch(td::cmd::OpenChat{
                            td::to_td(m.chats[new_idx].id)})};
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::OpenSelectedChat) {
                if (m.selected_chat_index && *m.selected_chat_index < m.chats.size()) {
                    const auto& selected = m.chats[*m.selected_chat_index];
                    const auto chat_id = selected.id;
                    const auto kind    = selected.kind;
                    const auto peer    = selected.peer_user_id;
                    m.messages.clear();
                    m.members.clear();
                    m.typers.clear();
                    m.typer_expiry.clear();
                    m.focus    = model::FocusedPane::Composer;
                    m.msg_scroll.y = 1'000'000;
                    m.members_scroll.scroll_to_origin();
                    m.tabs_scroll.scroll_to_origin();
                    // Batch: open chat (triggers history) + load info
                    // panel data (peer info for DMs, members for groups)
                    // + shared media for the active tab.
                    std::vector<maya::Cmd<Msg>> batch;
                    batch.push_back(td::dispatch(
                        td::cmd::OpenChat{td::to_td(chat_id)}));
                    if (kind == model::ChatKind::Direct && peer != 0) {
                        batch.push_back(td::dispatch(
                            td::cmd::LoadPeerInfo{
                                td::to_td(chat_id),
                                ::tl::td::TdUserId{peer}}));
                    } else {
                        batch.push_back(td::dispatch(
                            td::cmd::LoadMembers{td::to_td(chat_id)}));
                    }
                    batch.push_back(td::dispatch(
                        td::cmd::LoadSharedMedia{
                            td::to_td(chat_id), m.info_active_tab}));
                    return std::pair{std::move(m),
                        maya::Cmd<Msg>::batch(std::move(batch))};
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },

            // ── Search ──
            [&](msg::SearchInput s) {
                m.search_query += text_edit::encode_utf8(s.cp);
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::SearchBack) {
                if (!m.search_query.empty()) {
                    const auto p = text_edit::utf8_prev(m.search_query, m.search_query.size());
                    m.search_query.erase(p);
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::SearchClear) {
                m.search_query.clear();
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },

            // ── Composer text edit ──
            [&](msg::CharIn ci) {
                composer_ops::insert(m.composer, text_edit::encode_utf8(ci.cp));
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::Backspace)      { composer_ops::backspace(m.composer);      return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::DeleteWord)     { composer_ops::delete_word(m.composer);    return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::DeleteToStart)  { composer_ops::delete_to_start(m.composer);return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::DeleteToEnd)    { composer_ops::delete_to_end(m.composer);  return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::CursorLeft)     { composer_ops::cursor_left(m.composer);    return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::CursorRight)    { composer_ops::cursor_right(m.composer);   return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::CursorHome)     { composer_ops::cursor_home(m.composer);    return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::CursorEnd)      { composer_ops::cursor_end(m.composer);     return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::SendComposer) {
                // Sending while recording → commit the recording first
                // (VoiceStop semantics) and then drop through the rest
                // of the send flow so any pending text / attachments
                // also get flushed.
                // Capture the open chat id once so we can dispatch the
                // outbound commands at the end of this arm.
                const auto open_chat_id = (m.selected_chat_index
                    && *m.selected_chat_index < m.chats.size())
                    ? std::optional{m.chats[*m.selected_chat_index].id}
                    : std::nullopt;
                const auto reply_to_id = m.composer.reply_quote.has_value()
                    ? m.composer.reply_quote->source_id
                    : model::MessageId{};
                std::vector<maya::Cmd<Msg>> outbox;
                if (m.composer.recording) {
                    if (m.composer.recording_secs > 0) {
                        model::MessageVM out{};
                        out.id          = model::MessageId{static_cast<std::int64_t>(m.messages.size() + 1)};
                        out.author_id   = model::UserId{1};
                        out.author_name = m.self_name;
                        out.timestamp   = "now";
                        out.age_label   = "now";
                        out.from_me     = true;
                        out.read_state  = model::ReadState::Sending;
                        model::AudioNoteVM an{};
                        an.duration_secs = m.composer.recording_secs;
                        an.waveform      = m.composer.recording_waveform;
                        out.audio_note   = std::move(an);
                        m.messages.push_back(std::move(out));
                    }
                    m.composer.recording = false;
                    m.composer.recording_secs = 0;
                    m.composer.recording_waveform.clear();
                }

                auto body = m.composer.text;
                auto attachments = std::move(m.composer.attachments);
                auto reply = m.composer.reply_quote;
                m.composer.text.clear();
                m.composer.cursor_bytes = 0;
                m.composer.attachments.clear();
                m.composer.reply_quote.reset();

                if (body.empty() && attachments.empty()) {
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                if (!body.empty() && attachments.empty()
                 && run_command(m, body)) {
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }

                // Helper: build a fresh self-bubble MessageVM with all
                // the boilerplate filled in. Caller fills the media slot.
                auto fresh_self_msg = [&]() {
                    model::MessageVM out{};
                    out.id          = model::MessageId{static_cast<std::int64_t>(m.messages.size() + 1)};
                    out.author_id   = model::UserId{1};
                    out.author_name = m.self_name;
                    out.timestamp   = "now";
                    out.age_label   = "now";
                    out.from_me     = true;
                    out.read_state  = model::ReadState::Sending;
                    return out;
                };

                // Emit one MessageVM per attachment. The text body and
                // the reply quote attach to the FIRST sent message —
                // matches how Telegram groups a caption with the first
                // photo of a media batch.
                bool body_attached_to_first = false;
                for (std::size_t i = 0; i < attachments.size(); ++i) {
                    using K = model::ComposerVM::AttachmentKind;
                    auto& a = attachments[i];
                    auto out = fresh_self_msg();
                    std::string this_body;
                    auto this_reply = model::MessageId{};
                    if (!body_attached_to_first) {
                        out.body = body;
                        out.reply_quote = reply;
                        this_body  = body;
                        this_reply = reply_to_id;
                        body_attached_to_first = true;
                        body.clear();
                        reply.reset();
                    }
                    switch (a.kind) {
                        case K::Photo: {
                            model::PhotoVM p{};
                            p.file_path  = a.path;
                            p.size_bytes = a.size_bytes;
                            out.photo = std::move(p);
                            if (open_chat_id) {
                                outbox.push_back(td::dispatch(
                                    td::cmd::SendPhoto{
                                        td::to_td(*open_chat_id),
                                        a.path, this_body,
                                        ::tl::td::TdMessageId{this_reply.get()}}));
                            }
                            break;
                        }
                        case K::Voice: {
                            model::AudioNoteVM an{};
                            an.file_path     = a.path;
                            an.duration_secs = a.duration_secs;
                            out.audio_note   = std::move(an);
                            if (open_chat_id) {
                                outbox.push_back(td::dispatch(
                                    td::cmd::SendVoice{
                                        td::to_td(*open_chat_id),
                                        a.path, a.duration_secs,
                                        ::tl::td::TdMessageId{this_reply.get()}}));
                            }
                            break;
                        }
                        case K::Video: {
                            model::VideoVM v{};
                            v.file_path     = a.path;
                            v.title         = a.label;
                            v.duration_secs = a.duration_secs;
                            v.size_bytes    = a.size_bytes;
                            out.video = std::move(v);
                            // TDLib has a SendDocument fallback for now —
                            // no inputMessageVideo wrapper on our command
                            // side; the document upload still works.
                            if (open_chat_id) {
                                outbox.push_back(td::dispatch(
                                    td::cmd::SendDocument{
                                        td::to_td(*open_chat_id),
                                        a.path, this_body,
                                        ::tl::td::TdMessageId{this_reply.get()}}));
                            }
                            break;
                        }
                        case K::File:
                        default: {
                            model::DocumentVM d{};
                            d.file_path  = a.path;
                            d.filename   = a.label;
                            d.size_bytes = a.size_bytes;
                            out.document = std::move(d);
                            if (open_chat_id) {
                                outbox.push_back(td::dispatch(
                                    td::cmd::SendDocument{
                                        td::to_td(*open_chat_id),
                                        a.path, this_body,
                                        ::tl::td::TdMessageId{this_reply.get()}}));
                            }
                            break;
                        }
                    }
                    m.messages.push_back(std::move(out));
                }

                // If there's still body text (no attachments at all),
                // push it as a plain text message.
                if (!body.empty()) {
                    auto out = fresh_self_msg();
                    out.body        = body;
                    out.reply_quote = reply;
                    m.messages.push_back(std::move(out));
                    if (open_chat_id) {
                        outbox.push_back(td::dispatch(
                            td::cmd::SendText{
                                td::to_td(*open_chat_id),
                                body,
                                ::tl::td::TdMessageId{reply_to_id.get()}}));
                    }
                }
                m.msg_scroll.y = 1'000'000;
                return std::pair{std::move(m),
                    maya::Cmd<Msg>::batch(std::move(outbox))};
            },
            [&](msg::InsertNewline) {
                composer_ops::insert(m.composer, std::string{"\n"});
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::AttachPickFile) {
                // Demo: cycle through a small canned set of attachments
                // so the chip strip + send flow is exercise-able without
                // a real picker. Cycles deterministically by
                // (attachments.size() + tick) so repeated presses keep
                // adding distinct items.
                if (m.composer.attachments.size()
                    >= model::ComposerVM::kMaxAttachments)
                {
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                using K = model::ComposerVM::AttachmentKind;
                static const std::array<model::ComposerVM::Attachment, 4> kCanned = {
                    model::ComposerVM::Attachment{K::Photo, "sunset.jpg",
                        "assets/media/hike-sunset.jpg", 1'258'291, 0},
                    model::ComposerVM::Attachment{K::File,  "notes.md",
                        "assets/media/notes.md",         12'400,    0},
                    model::ComposerVM::Attachment{K::Video, "clip.mp4",
                        "assets/media/clip.mp4",         8'400'000, 27},
                    model::ComposerVM::Attachment{K::File,  "build-log.txt",
                        "assets/media/build-log.txt",    84'000,    0},
                };
                const auto idx = (m.composer.attachments.size()
                              + static_cast<std::size_t>(m.tick))
                              % kCanned.size();
                m.composer.attachments.push_back(kCanned[idx]);
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::AttachClipboardPaste) {
                // Demo: synthesize a paste payload. A real client would
                // consult the OSC-52 / bracketed-paste stream; here we
                // just queue a stub Photo so the UI is exercise-able.
                if (m.composer.attachments.size()
                    >= model::ComposerVM::kMaxAttachments)
                {
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                model::ComposerVM::Attachment a{};
                a.kind       = model::ComposerVM::AttachmentKind::Photo;
                a.label      = "pasted-image.png";
                a.path       = "<clipboard>";
                a.size_bytes = 320'000;
                m.composer.attachments.push_back(std::move(a));
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::AttachRemove r) {
                if (r.index < m.composer.attachments.size()) {
                    m.composer.attachments.erase(
                        m.composer.attachments.begin()
                            + static_cast<std::ptrdiff_t>(r.index));
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::AttachClear) {
                m.composer.attachments.clear();
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::VoiceStart) {
                if (m.composer.recording) {
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                m.composer.recording = true;
                m.composer.recording_secs = 0;
                m.composer.recording_waveform.clear();
                m.focus = model::FocusedPane::Composer;
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::VoiceStop) {
                if (!m.composer.recording) {
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                if (m.composer.recording_secs > 0) {
                    model::MessageVM out{};
                    out.id          = model::MessageId{static_cast<std::int64_t>(m.messages.size() + 1)};
                    out.author_id   = model::UserId{1};
                    out.author_name = m.self_name;
                    out.timestamp   = "now";
                    out.age_label   = "now";
                    out.from_me     = true;
                    out.read_state  = model::ReadState::Sending;
                    model::AudioNoteVM an{};
                    an.duration_secs = m.composer.recording_secs;
                    an.waveform      = m.composer.recording_waveform;
                    out.audio_note   = std::move(an);
                    m.messages.push_back(std::move(out));
                    m.msg_scroll.y = 1'000'000;
                }
                m.composer.recording = false;
                m.composer.recording_secs = 0;
                m.composer.recording_waveform.clear();
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::VoiceCancel) {
                m.composer.recording = false;
                m.composer.recording_secs = 0;
                m.composer.recording_waveform.clear();
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::ReplyLatest) {
                // Walk messages newest → oldest, find the most recent
                // peer (non-self, non-system) message, snapshot its
                // author + a short preview into the composer's reply
                // slot. Idempotent: re-running while already replying
                // re-points to the (possibly different) latest peer
                // message, which is what Telegram's keyboard reply
                // shortcut does.
                for (auto it = m.messages.rbegin(); it != m.messages.rend(); ++it) {
                    if (it->from_me || it->is_system) continue;
                    model::ReplyQuoteVM q{};
                    q.source_id   = it->id;
                    q.author_name = it->author_name;
                    q.snippet     = it->body;
                    // Cap snippet length so the preview row stays one
                    // line regardless of the original message size.
                    constexpr std::size_t kMaxSnippet = 80;
                    if (q.snippet.size() > kMaxSnippet) {
                        q.snippet.resize(kMaxSnippet);
                        q.snippet += "…";
                    }
                    m.composer.reply_quote = std::move(q);
                    m.focus = model::FocusedPane::Composer;
                    break;
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::CancelReply) {
                m.composer.reply_quote.reset();
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::ToggleLatestNote) {
                for (auto it = m.messages.rbegin(); it != m.messages.rend(); ++it) {
                    if (!(it->audio_note.has_value() || it->video_note.has_value()
                        || it->music.has_value()))
                        continue;
                    // Inline the same logic as ToggleAudioPlay so we go
                    // through the single-track stop-others behaviour.
                    model::MessageId target = it->id;
                    bool starting = false;
                    if (it->audio_note.has_value())      starting = !it->audio_note->playing;
                    else if (it->video_note.has_value()) starting = !it->video_note->playing;
                    else if (it->music.has_value())      starting = !it->music->playing;
                    for (auto& msg_vm : m.messages) {
                        auto flip = [&](auto& note) {
                            if (msg_vm.id == target) {
                                note.playing = starting;
                                if (starting && note.progress_secs >= note.duration_secs)
                                    note.progress_secs = 0;
                            } else if (starting) {
                                note.playing = false;
                            }
                        };
                        if (msg_vm.audio_note.has_value()) flip(*msg_vm.audio_note);
                        if (msg_vm.video_note.has_value()) flip(*msg_vm.video_note);
                        if (msg_vm.music.has_value())      flip(*msg_vm.music);
                    }
                    break;
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::ToggleAudioPlay tp) {
                // Single-track playback: starting one note auto-pauses
                // all the others, mirroring how Telegram-mobile behaves
                // (and the only sane policy when our "audio engine" is
                // a single shared output channel anyway).
                model::MessageId target{tp.message_id};
                bool starting = false;
                for (const auto& msg_vm : m.messages) {
                    if (msg_vm.id == target) {
                        if (msg_vm.audio_note.has_value())
                            starting = !msg_vm.audio_note->playing;
                        else if (msg_vm.video_note.has_value())
                            starting = !msg_vm.video_note->playing;
                        else if (msg_vm.music.has_value())
                            starting = !msg_vm.music->playing;
                        break;
                    }
                }
                for (auto& msg_vm : m.messages) {
                    auto flip = [&](auto& note) {
                        if (msg_vm.id == target) {
                            note.playing = starting;
                            if (starting && note.progress_secs >= note.duration_secs) {
                                note.progress_secs = 0;
                            }
                        } else if (starting) {
                            note.playing = false;
                        }
                    };
                    if (msg_vm.audio_note.has_value()) flip(*msg_vm.audio_note);
                    if (msg_vm.video_note.has_value()) flip(*msg_vm.video_note);
                    if (msg_vm.music.has_value())      flip(*msg_vm.music);
                }
                // Tapping play clears the unread cue — you've engaged
                // with the note, so the dot no longer applies.
                if (starting) {
                    for (auto& msg_vm : m.messages) {
                        if (msg_vm.id != target) continue;
                        if (msg_vm.audio_note.has_value()) msg_vm.audio_note->unread = false;
                        if (msg_vm.video_note.has_value()) msg_vm.video_note->unread = false;
                    }
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },

            // ── Voice / video note polish controls ──
            // Speed pill cycles 1.0 → 1.5 → 2.0 → 1.0. Applies to whichever
            // playable kind the target message carries.
            [&](msg::CyclePlaybackSpeed cp) {
                auto next_speed = [](double s) noexcept {
                    if (s < 1.25) return 1.5;
                    if (s < 1.75) return 2.0;
                    return 1.0;
                };
                for (auto& msg_vm : m.messages) {
                    if (msg_vm.id != model::MessageId{cp.message_id}) continue;
                    if (msg_vm.audio_note.has_value())
                        msg_vm.audio_note->playback_speed = next_speed(
                            msg_vm.audio_note->playback_speed);
                    if (msg_vm.video_note.has_value())
                        msg_vm.video_note->playback_speed = next_speed(
                            msg_vm.video_note->playback_speed);
                    if (msg_vm.music.has_value())
                        msg_vm.music->playback_speed = next_speed(
                            msg_vm.music->playback_speed);
                    break;
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::CycleLatestPlaybackSpeed) {
                auto next_speed = [](double s) noexcept {
                    if (s < 1.25) return 1.5;
                    if (s < 1.75) return 2.0;
                    return 1.0;
                };
                for (auto it = m.messages.rbegin(); it != m.messages.rend(); ++it) {
                    if (it->audio_note.has_value()) {
                        it->audio_note->playback_speed = next_speed(
                            it->audio_note->playback_speed);
                        break;
                    }
                    if (it->video_note.has_value()) {
                        it->video_note->playback_speed = next_speed(
                            it->video_note->playback_speed);
                        break;
                    }
                    if (it->music.has_value()) {
                        it->music->playback_speed = next_speed(
                            it->music->playback_speed);
                        break;
                    }
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::ToggleVideoNoteMute t) {
                for (auto& msg_vm : m.messages) {
                    if (msg_vm.id != model::MessageId{t.message_id}) continue;
                    if (msg_vm.video_note.has_value()) {
                        msg_vm.video_note->muted = !msg_vm.video_note->muted;
                    }
                    break;
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::ToggleLatestVideoNoteMute) {
                for (auto it = m.messages.rbegin(); it != m.messages.rend(); ++it) {
                    if (it->video_note.has_value()) {
                        it->video_note->muted = !it->video_note->muted;
                        break;
                    }
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::ToggleTranscript t) {
                for (auto& msg_vm : m.messages) {
                    if (msg_vm.id != model::MessageId{t.message_id}) continue;
                    if (msg_vm.audio_note.has_value()) {
                        msg_vm.audio_note->transcript_expanded =
                            !msg_vm.audio_note->transcript_expanded;
                    }
                    break;
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::ToggleLatestTranscript) {
                for (auto it = m.messages.rbegin(); it != m.messages.rend(); ++it) {
                    if (it->audio_note.has_value()) {
                        it->audio_note->transcript_expanded =
                            !it->audio_note->transcript_expanded;
                        break;
                    }
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },

            // ── Scroll ──
            [&](msg::ScrollUp)        { m.msg_scroll.scroll_by(0, -1);          return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::ScrollDown)      { m.msg_scroll.scroll_by(0,  1);          return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::ScrollPageUp)    { m.msg_scroll.scroll_by(0, -10);         return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::ScrollPageDown)  { m.msg_scroll.scroll_by(0,  10);         return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::ScrollLatest)    { m.msg_scroll.scroll_to_bottom();         return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::ScrollOldest)    { m.msg_scroll.scroll_to_origin();         return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::ClearChannel)    { m.messages.clear();                      return std::pair{std::move(m), maya::Cmd<Msg>{}}; },

            // ── Overlays ──
            [&](msg::ToggleRightPanel){ m.right_panel_open = !m.right_panel_open; return std::pair{std::move(m), maya::Cmd<Msg>{}}; },
            [&](msg::ToggleHelp) {
                m.help_open = !m.help_open;
                if (m.help_open) m.jumper_open = false;
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::ToggleJumper) {
                m.jumper_open = !m.jumper_open;
                if (m.jumper_open) { m.help_open = false; m.jumper_filter.clear(); m.jumper_index = 0; }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::HelpScroll hs)   { m.help_scroll.scroll_by(0, hs.dy);        return std::pair{std::move(m), maya::Cmd<Msg>{}}; },

            // ── Info pane ──
            [&](msg::InfoTabSelect t) {
                if (t.index >= 0 && t.index < 4) m.info_active_tab = t.index;
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::InfoTabCycle) {
                m.info_active_tab = (m.info_active_tab + 1) % 4;
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::ToggleNotifications) {
                m.notifications_on = !m.notifications_on;
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },

            // ── Jumper ──
            [&](msg::JumperChar jc) {
                m.jumper_filter += text_edit::encode_utf8(jc.cp);
                m.jumper_index = 0;
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::JumperBack) {
                if (!m.jumper_filter.empty()) {
                    const auto p = text_edit::utf8_prev(m.jumper_filter, m.jumper_filter.size());
                    m.jumper_filter.erase(p);
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::JumperUp) {
                if (m.jumper_index > 0) --m.jumper_index;
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::JumperDown) {
                const auto matches = jumper_matches(m.jumper_filter,
                    std::span<const model::ChatListItemVM>{m.chats});
                if (m.jumper_index + 1 < matches.size()) ++m.jumper_index;
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::Refresh) {
                // No state change — exists purely to drive a re-render
                // when something external (e.g., auto_dispatch's drag)
                // mutated the model.
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::MouseClick c) {
                const auto layout = mouse::compute_layout(m);

                // Modal overlays own clicks while open — for now, any
                // click closes them. (Future: hit-test individual rows.)
                if (m.jumper_open) { m.jumper_open = false; m.jumper_filter.clear(); m.jumper_index = 0;
                                     return std::pair{std::move(m), maya::Cmd<Msg>{}}; }
                if (m.help_open)   { m.help_open = false;
                                     return std::pair{std::move(m), maya::Cmd<Msg>{}}; }

                // Scrollbar clicks (jump-to + drag-start) are owned by
                // maya's auto-dispatch path — no need to re-handle here.
                // panel_at_x() already returns None for scrollbar columns,
                // so the hit-tests below naturally skip those clicks.

                if (mouse::is_close_button(layout, c.x, c.y)) {
                    m.right_panel_open = false;
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                // Info-pane interactions (DM right panel only). The hit
                // tests already gate on layout.show_right + Panel::Right,
                // so they no-op in member-list / hidden-panel modes.
                {
                    const int sb = static_cast<int>(m.members_scroll.y);
                    if (auto tab = mouse::info_tab_at(layout, c.x, c.y, sb);
                        tab >= 0)
                    {
                        m.info_active_tab = tab;
                        return std::pair{std::move(m), maya::Cmd<Msg>{}};
                    }
                    if (mouse::is_info_notifications(layout, c.x, c.y, sb)) {
                        m.notifications_on = !m.notifications_on;
                        return std::pair{std::move(m), maya::Cmd<Msg>{}};
                    }
                }
                if (mouse::is_chat_header(layout, c.x, c.y)) {
                    m.right_panel_open = !m.right_panel_open;
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                // Click-to-focus inputs — checked BEFORE the broader
                // panel hit-tests so they win over chat-list "click a row"
                // or composer "click to send".
                // The clear (✕) inside the search box is checked even
                // earlier than is_search_input so a click on the X wipes
                // the query instead of just focusing the field.
                if (mouse::is_search_clear(layout, c.x, c.y,
                        static_cast<int>(m.chats_scroll.y))
                    && !m.search_query.empty())
                {
                    m.search_query.clear();
                    m.focus = model::FocusedPane::ChatList;
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                if (mouse::is_composer_input(layout, c.x, c.y)) {
                    m.focus = model::FocusedPane::Composer;
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                if (mouse::is_search_input(layout, c.x, c.y,
                        static_cast<int>(m.chats_scroll.y))) {
                    m.focus = model::FocusedPane::ChatList;
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                if (mouse::is_composer_attach(layout, c.x, c.y)) {
                    if (m.composer.attachments.size()
                        < model::ComposerVM::kMaxAttachments)
                    {
                        using K = model::ComposerVM::AttachmentKind;
                        static const std::array<model::ComposerVM::Attachment, 4> kCanned = {
                            model::ComposerVM::Attachment{K::Photo, "sunset.jpg",
                                "assets/media/hike-sunset.jpg", 1'258'291, 0},
                            model::ComposerVM::Attachment{K::File,  "notes.md",
                                "assets/media/notes.md",         12'400,    0},
                            model::ComposerVM::Attachment{K::Video, "clip.mp4",
                                "assets/media/clip.mp4",         8'400'000, 27},
                            model::ComposerVM::Attachment{K::File,  "build-log.txt",
                                "assets/media/build-log.txt",    84'000,    0},
                        };
                        const auto idx = (m.composer.attachments.size()
                                      + static_cast<std::size_t>(m.tick))
                                      % kCanned.size();
                        m.composer.attachments.push_back(kCanned[idx]);
                    }
                    m.focus = model::FocusedPane::Composer;
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }
                if (mouse::is_composer_send(layout, c.x, c.y)) {
                    // The right-side button is context-aware:
                    //   recording        → ⏹ commits the clip
                    //   text+attachments → ⏎ sends the message
                    //   empty state      → 🎤 starts a voice recording
                    if (m.composer.recording) {
                        // VoiceStop semantics inline.
                        if (m.composer.recording_secs > 0) {
                            model::MessageVM out{};
                            out.id          = model::MessageId{static_cast<std::int64_t>(m.messages.size() + 1)};
                            out.author_id   = model::UserId{1};
                            out.author_name = m.self_name;
                            out.timestamp   = "now";
                            out.age_label   = "now";
                            out.from_me     = true;
                            out.read_state  = model::ReadState::Sending;
                            model::AudioNoteVM an{};
                            an.duration_secs = m.composer.recording_secs;
                            an.waveform      = m.composer.recording_waveform;
                            out.audio_note   = std::move(an);
                            m.messages.push_back(std::move(out));
                            m.msg_scroll.y = 1'000'000;
                        }
                        m.composer.recording = false;
                        m.composer.recording_secs = 0;
                        m.composer.recording_waveform.clear();
                        return std::pair{std::move(m), maya::Cmd<Msg>{}};
                    }
                    if (m.composer.text.empty() && m.composer.attachments.empty()) {
                        // Empty composer + click → start recording.
                        m.composer.recording = true;
                        m.composer.recording_secs = 0;
                        m.composer.recording_waveform.clear();
                        m.focus = model::FocusedPane::Composer;
                        return std::pair{std::move(m), maya::Cmd<Msg>{}};
                    }
                    // Otherwise drop into the normal SendComposer flow.
                    auto body = m.composer.text;
                    auto attachments = std::move(m.composer.attachments);
                    auto reply = m.composer.reply_quote;
                    m.composer.text.clear();
                    m.composer.cursor_bytes = 0;
                    m.composer.attachments.clear();
                    m.composer.reply_quote.reset();

                    auto fresh_self_msg = [&]() {
                        model::MessageVM out{};
                        out.id          = model::MessageId{static_cast<std::int64_t>(m.messages.size() + 1)};
                        out.author_id   = model::UserId{1};
                        out.author_name = m.self_name;
                        out.timestamp   = "now";
                        out.age_label   = "now";
                        out.from_me     = true;
                        out.read_state  = model::ReadState::Sending;
                        return out;
                    };
                    bool body_attached_to_first = false;
                    for (auto& a : attachments) {
                        using K = model::ComposerVM::AttachmentKind;
                        auto out = fresh_self_msg();
                        if (!body_attached_to_first) {
                            out.body = body;
                            out.reply_quote = reply;
                            body_attached_to_first = true;
                            body.clear();
                            reply.reset();
                        }
                        switch (a.kind) {
                            case K::Photo: { model::PhotoVM p{}; p.file_path = std::move(a.path); p.size_bytes = a.size_bytes; out.photo = std::move(p); break; }
                            case K::Voice: { model::AudioNoteVM an{}; an.file_path = std::move(a.path); an.duration_secs = a.duration_secs; out.audio_note = std::move(an); break; }
                            case K::Video: { model::VideoVM v{}; v.file_path = std::move(a.path); v.title = a.label; v.duration_secs = a.duration_secs; v.size_bytes = a.size_bytes; out.video = std::move(v); break; }
                            default:       { model::DocumentVM d{}; d.file_path = std::move(a.path); d.filename = a.label; d.size_bytes = a.size_bytes; out.document = std::move(d); break; }
                        }
                        m.messages.push_back(std::move(out));
                    }
                    if (!body.empty() && !run_command(m, body)) {
                        auto out = fresh_self_msg();
                        out.body        = std::move(body);
                        out.reply_quote = std::move(reply);
                        m.messages.push_back(std::move(out));
                    }
                    m.msg_scroll.y = 1'000'000;
                    m.members_scroll.scroll_to_origin();
                    m.tabs_scroll.scroll_to_origin();
                    return std::pair{std::move(m), maya::Cmd<Msg>{}};
                }

                const auto p = mouse::panel_at_x(layout, c.x);
                if (p == mouse::Panel::Chats) {
                    const int y_in_content = c.y + m.chats_scroll.y;
                    if (auto idx = mouse::chat_at_content_y(m, y_in_content)) {
                        m.selected_chat_index = *idx;
                        const auto& ch = m.chats[*idx];
                        m.messages = seed::messages_for(ch);
                        m.members  = seed::members_for(ch);
                        m.typers   = seed::typers_for(ch);
                        // Force the next layout pass to scroll to the new
                    // bottom: scroll_to_bottom() uses the OLD max_y
                    // (pre-content-change), so the latest message lands
                    // off-screen. Setting y to a deliberately-large
                    // value lets the renderer's clamp() bring it to the
                    // newly-written max_y after this frame's layout.
                    m.msg_scroll.y = 1'000'000;
                    m.members_scroll.scroll_to_origin();
                    m.tabs_scroll.scroll_to_origin();
                    }
                }
                // Click anywhere else in the middle column's Messages
                // region (between header and composer) → focus composer,
                // the way Telegram-web treats the conversation surface
                // as "click to start typing".
                else if (p == mouse::Panel::Middle
                      && mouse::middle_region_at_y(layout, c.y)
                            == mouse::MiddleRegion::Messages) {
                    m.focus = model::FocusedPane::Composer;
                }
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
            [&](msg::JumperPick) {
                const auto matches = jumper_matches(m.jumper_filter,
                    std::span<const model::ChatListItemVM>{m.chats});
                if (m.jumper_index < matches.size()) {
                    m.selected_chat_index = matches[m.jumper_index];
                    if (*m.selected_chat_index < m.chats.size()) {
                        const auto& c = m.chats[*m.selected_chat_index];
                        m.messages = seed::messages_for(c);
                        m.members  = seed::members_for(c);
                        m.typers   = seed::typers_for(c);
                        m.msg_scroll.y = 1'000'000;
                        mouse::ensure_chat_visible(m);
                    }
                }
                m.jumper_open = false;
                m.jumper_filter.clear();
                m.jumper_index = 0;
                return std::pair{std::move(m), maya::Cmd<Msg>{}};
            },
        }, ev);
    }

    [[nodiscard]] static maya::Element view(const Model& m)
    {
        return views::render_shell(m);
    }

    [[nodiscard]] static auto subscribe(const Model& m) -> maya::Sub<Msg>
    {
        using namespace maya;
        const bool jumper = m.jumper_open;
        const bool help   = m.help_open;
        const bool typing = (m.focus == model::FocusedPane::Composer);
        const bool auth_gate = (m.auth.stage != model::AuthStage::LoggedIn);

        auto keys = Sub<Msg>::on_key([=](const KeyEvent& k) -> std::optional<Msg> {
            // Helpers
            auto as_char = [&]() -> std::optional<char32_t> {
                const auto* ck = std::get_if<CharKey>(&k.key);
                return ck ? std::optional{ck->codepoint} : std::nullopt;
            };
            const bool is_special = std::holds_alternative<SpecialKey>(k.key);
            const auto special = is_special ? *std::get_if<SpecialKey>(&k.key) : SpecialKey::F12;

            // ── Auth overlay swallows everything until LoggedIn ──
            if (auth_gate) {
                if (is_special) {
                    TL_DLOG("key", "auth_gate special=%d", static_cast<int>(special));
                    if (special == SpecialKey::Enter)     return msg::AuthSubmit{};
                    if (special == SpecialKey::Backspace) return msg::AuthBackspace{};
                    if (special == SpecialKey::Escape)    return msg::Quit{};
                } else if (auto c = as_char()) {
                    TL_DLOG("key", "auth_gate char cp=U+%04X mods.ctrl=%d mods.alt=%d",
                            static_cast<unsigned>(*c),
                            k.mods.ctrl ? 1 : 0, k.mods.alt ? 1 : 0);
                    if (k.mods.ctrl && (*c == U'c' || *c == U'C')) return msg::Quit{};
                    if (k.mods.none() && *c >= U' ') return msg::AuthCharIn{*c};
                }
                return std::nullopt;
            }

            // ── Jumper overlay swallows everything ──
            if (jumper) {
                if (is_special) {
                    switch (special) {
                        case SpecialKey::Escape:    return msg::ToggleJumper{};
                        case SpecialKey::Up:        return msg::JumperUp{};
                        case SpecialKey::Down:      return msg::JumperDown{};
                        case SpecialKey::Enter:     return msg::JumperPick{};
                        case SpecialKey::Backspace: return msg::JumperBack{};
                        default: break;
                    }
                } else if (auto c = as_char()) {
                    return msg::JumperChar{*c};
                }
                return std::nullopt;
            }

            // ── Help overlay: any key closes ──
            if (help) {
                if (is_special) {
                    if (special == SpecialKey::Escape || special == SpecialKey::Enter) {
                        return msg::ToggleHelp{};
                    }
                    if (special == SpecialKey::Up)   return msg::HelpScroll{-1};
                    if (special == SpecialKey::Down) return msg::HelpScroll{+1};
                } else if (auto c = as_char()) {
                    if (*c == U'q' || *c == U'?') return msg::ToggleHelp{};
                }
                return std::nullopt;
            }

            // ── Composer focused: route printable chars as text input ──
            if (typing) {
                if (is_special) {
                    switch (special) {
                        case SpecialKey::Backspace: return msg::Backspace{};
                        case SpecialKey::Delete:    return msg::DeleteToEnd{};
                        case SpecialKey::Left:      return msg::CursorLeft{};
                        case SpecialKey::Right:     return msg::CursorRight{};
                        case SpecialKey::Home:      return msg::CursorHome{};
                        case SpecialKey::End:       return msg::CursorEnd{};
                        case SpecialKey::Enter:
                            // Alt-Enter / Shift-Enter → newline
                            // (a la Telegram-desktop). Bare Enter
                            // sends.
                            if (k.mods.alt || k.mods.shift) return msg::InsertNewline{};
                            return msg::SendComposer{};
                        case SpecialKey::Escape:
                            // Escape priority while in composer focus:
                            //   1. recording → cancel the take
                            //   2. replying  → drop the reply
                            //   3. else      → back to chat-list focus
                            if (m.composer.recording)
                                return Msg{msg::VoiceCancel{}};
                            return m.composer.reply_quote.has_value()
                                ? Msg{msg::CancelReply{}}
                                : Msg{msg::CycleFocus{}};
                        case SpecialKey::Tab:       return msg::CycleFocus{};
                        case SpecialKey::Up:        return msg::ScrollUp{};
                        case SpecialKey::Down:      return msg::ScrollDown{};
                        case SpecialKey::PageUp:    return msg::ScrollPageUp{};
                        case SpecialKey::PageDown:  return msg::ScrollPageDown{};
                        default: break;
                    }
                } else if (auto c = as_char()) {
                    if (k.mods.ctrl && !k.mods.alt) {
                        switch (*c) {
                            case U'a': return msg::CursorHome{};
                            case U'e': return msg::CursorEnd{};
                            case U'w': return msg::DeleteWord{};
                            case U'u': return msg::DeleteToStart{};
                            case U'k': return msg::DeleteToEnd{};
                            case U'c': return msg::Quit{};
                            case U'r': return msg::ReplyLatest{};
                            case U'p': return msg::ToggleLatestNote{};
                            case U's': return msg::CycleLatestPlaybackSpeed{};
                            case U'm': return msg::ToggleLatestVideoNoteMute{};
                            case U'x': return msg::ToggleLatestTranscript{};
                            case U't': return msg::InfoTabCycle{};
                            case U'v': return msg::AttachClipboardPaste{};
                            case U'f': return msg::AttachPickFile{};
                            case U'b': return m.composer.recording
                                ? Msg{msg::VoiceStop{}}
                                : Msg{msg::VoiceStart{}};
                            // Ctrl-J inserts a literal newline —
                            // mirrors how readline handles it, and
                            // works on terminals that don't surface
                            // Alt-Enter / Shift-Enter as such.
                            case U'j': return msg::InsertNewline{};
                            default: break;
                        }
                    }
                    if (k.mods.none() && *c >= U' ') {
                        return msg::CharIn{*c};
                    }
                }
                return std::nullopt;
            }

            // ── Chat list focus: navigation shortcuts ──
            // Composer focus is handled higher up in this lambda; this
            // branch only runs when we're in chat-list nav mode, so
            // arrows always move the selection (never scroll messages).
            if (is_special) {
                switch (special) {
                    case SpecialKey::Tab:       return msg::CycleFocus{};
                    case SpecialKey::Up:        return msg::SelectChatUp{};
                    case SpecialKey::Down:      return msg::SelectChatDown{};
                    case SpecialKey::PageUp:    return msg::ScrollPageUp{};
                    case SpecialKey::PageDown:  return msg::ScrollPageDown{};
                    case SpecialKey::Enter:     return msg::OpenSelectedChat{};
                    case SpecialKey::Home:      return msg::ScrollOldest{};
                    case SpecialKey::End:       return msg::ScrollLatest{};
                    case SpecialKey::Escape:    return msg::Quit{};
                    case SpecialKey::Backspace: return msg::SearchBack{};
                    default: break;
                }
            } else if (auto c = as_char()) {
                if (k.mods.none()) {
                    switch (*c) {
                        case U'q': return msg::Quit{};
                        case U'j': return msg::SelectChatDown{};
                        case U'k': return msg::SelectChatUp{};
                        case U'i': return msg::ToggleRightPanel{};
                        case U'?': return msg::ToggleHelp{};
                        case U'/': return msg::ToggleJumper{};
                        case U'G': return msg::ScrollLatest{};
                        default: break;
                    }
                    // Letters → search query (only when focused on chat list)
                    if (m.focus == model::FocusedPane::ChatList
                     && ((*c >= U'a' && *c <= U'z') || (*c >= U'A' && *c <= U'Z')
                      || (*c >= U'0' && *c <= U'9') || *c == U' ')) {
                        return msg::SearchInput{*c};
                    }
                }
                if (k.mods.ctrl) {
                    switch (*c) {
                        case U'c': return msg::Quit{};
                        case U'l': return msg::ClearChannel{};
                        case U'p': return msg::ToggleRightPanel{};
                        case U'g': return msg::ToggleJumper{};
                        case U'h': return msg::ToggleHelp{};
                        case U't': return msg::InfoTabCycle{};
                        default: break;
                    }
                }
            }
            return std::nullopt;
        });

        auto ticks = Sub<Msg>::every(std::chrono::milliseconds{250}, msg::Tick{});
        auto resize = Sub<Msg>::on_resize([](Size s) -> Msg {
            return msg::Resize{static_cast<int>(s.width),
                               static_cast<int>(s.height)};
        });

        // Mouse routing.
        //   Wheel, scrollbar drag, and track-click are all handled by
        //   maya's ScrollState auto-dispatch (patched to gate by
        //   viewport_bounds). auto-dispatch mutates the scroll state
        //   silently, so we need a Msg dispatch on every mouse event
        //   to drive the Program re-render — otherwise scrolling looks
        //   laggy because repaints wait for the next Tick (≤250 ms).
        auto mouse_sub = Sub<Msg>::on_mouse([](const MouseEvent& ev) -> std::optional<Msg> {
            const int x = static_cast<int>(ev.x) - 1;
            const int y = static_cast<int>(ev.y) - 1;
            if (ev.kind == MouseEventKind::Press
             && ev.button == MouseButton::Left) {
                return msg::MouseClick{x, y};
            }
            // Every other mouse event — wheel up/down/left/right,
            // mouse Move (drag), Release — triggers a Refresh so the
            // screen repaints to reflect the just-mutated scroll state.
            return msg::Refresh{};
        });

        return Sub<Msg>::batch(keys, ticks, resize, mouse_sub);
    }
};

static_assert(maya::Program<TeleliterProgram>);

// ─── TDLib event → AppModel mutator ───────────────────────────────────────
// One visit arm per td::event::Event alternative. All mutations live
// here; the runtime in td/client.hpp never touches AppModel.

namespace detail::msg_derive {

// Decide if a freshly-inserted message should render as `compact`
// (no author name + blank avatar column) and/or with a `gap_label`
// separator above it. Mirrors Telegram-web's grouping: same author
// within ~5 min collapses; ≥5 min gap surfaces a "5m later" label.
inline void apply_grouping(model::MessageVM& msg, const model::MessageVM* prev) {
    using maya::overload;
    if (!prev) {
        msg.compact        = false;
        msg.show_gap_above = false;
        return;
    }
    // System messages never group. They also reset the chain.
    if (msg.is_system || prev->is_system) {
        msg.compact        = false;
        msg.show_gap_above = false;
        return;
    }
    const bool same_author = (msg.author_id == prev->author_id)
                          && (msg.from_me == prev->from_me);
    msg.compact = same_author;
    msg.show_gap_above = false;  // gap derivation needs unix — we don't carry it here.
}

}  // namespace detail::msg_derive

inline void handle_td_event(model::AppModel& m, td::event::Event ev)
{
    using maya::overload;
    namespace E = td::event;
    TL_DLOG("ev", "handle_td_event variant=%zu", ev.index());
    std::visit(overload{
        [&](E::AuthStateChanged a) {
            TL_DLOG("ev-auth", "AuthStateChanged stage=%d hint=%s",
                    static_cast<int>(a.stage), a.hint.c_str());
            m.auth.stage      = a.stage;
            m.auth.hint       = std::move(a.hint);
            m.auth.submitting = false;
            // Flip the active field to whatever stage we're now in.
            switch (a.stage) {
                case model::AuthStage::WaitPhone:    m.auth.active_field = model::AuthField::Phone;    break;
                case model::AuthStage::WaitCode:     m.auth.active_field = model::AuthField::Code;     break;
                case model::AuthStage::WaitPassword: m.auth.active_field = model::AuthField::Password; break;
                default: break;
            }
            if (a.stage == model::AuthStage::LoggedIn) {
                // Clear sensitive buffers once we're in.
                m.auth.phone.clear();
                m.auth.code.clear();
                m.auth.password.clear();
            }
        },
        [&](E::AuthError e) {
            TL_DLOG("ev-auth", "AuthError: %s", e.message.c_str());
            m.auth.submitting = false;
            m.auth.hint = std::move(e.message);
        },
        [&](E::ConnectionStateChanged c) {
            TL_DLOG("ev", "ConnectionStateChanged stage=%d",
                    static_cast<int>(c.stage));
            (void)c;
            // Connection state surfaces in the header bar via the
            // status dot; the renderer reads from m.* directly. We
            // don't carry a field for this yet — left as a future
            // hook so the event is preserved through the visit.
        },
        [&](E::MeLoaded me) {
            TL_DLOG("ev", "MeLoaded name=%s", me.me.name.c_str());
            m.self_name     = me.me.name;
            m.self_presence = me.me.presence;
        },
        [&](E::ChatUpserted c) {
            TL_DLOG("ev", "ChatUpserted id=%lld title=%s order=%lld",
                    static_cast<long long>(c.chat.id.get()),
                    c.chat.title.c_str(),
                    static_cast<long long>(c.chat.order));
            // Upsert by id, preserve selection by following the
            // currently-selected chat id through the re-sort.
            const auto selected_id = (m.selected_chat_index
                && *m.selected_chat_index < m.chats.size())
                ? std::optional{m.chats[*m.selected_chat_index].id}
                : std::nullopt;
            bool replaced = false;
            for (auto& existing : m.chats) {
                if (existing.id == c.chat.id) {
                    // Preserve sticky locally-derived fields the
                    // server snapshot doesn't carry (e.g. an avatar
                    // path we already downloaded).
                    auto avatar = existing.avatar_path;
                    existing = std::move(c.chat);
                    if (existing.avatar_path.empty()) existing.avatar_path = std::move(avatar);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) m.chats.push_back(std::move(c.chat));
            // Sort by order desc, then by last_message_time desc as a
            // tiebreaker for chats sharing order==0 (e.g. archived).
            std::sort(m.chats.begin(), m.chats.end(),
                [](const auto& a, const auto& b) {
                    if (a.order != b.order) return a.order > b.order;
                    return a.last_message_time > b.last_message_time;
                });
            if (selected_id) {
                for (std::size_t i = 0; i < m.chats.size(); ++i) {
                    if (m.chats[i].id == *selected_id) {
                        m.selected_chat_index = i; break;
                    }
                }
            }
        },
        [&](E::ChatRemoved r) {
            const auto rid = td::to_model(r.id);
            std::erase_if(m.chats, [&](const auto& c) { return c.id == rid; });
            if (m.selected_chat_index && *m.selected_chat_index >= m.chats.size()) {
                m.selected_chat_index.reset();
            }
        },
        [&](E::ChatPatched p) {
            const auto target = td::to_model(p.id);
            const auto selected_id = (m.selected_chat_index
                && *m.selected_chat_index < m.chats.size())
                ? std::optional{m.chats[*m.selected_chat_index].id}
                : std::nullopt;
            for (auto& c : m.chats) {
                if (c.id != target) continue;
                // Merge only the fields the patch actually carries
                // (non-empty strings, non-zero counters). The patch is
                // a partial snapshot — missing fields keep their value.
                if (!p.patch.title.empty())                c.title = std::move(p.patch.title);
                if (!p.patch.last_message_preview.empty()) c.last_message_preview = std::move(p.patch.last_message_preview);
                if (!p.patch.last_message_time.empty())    c.last_message_time    = std::move(p.patch.last_message_time);
                // unread_count is authoritative when the patch was
                // sent by updateChatReadInbox — we trust the value.
                c.unread_count     = p.patch.unread_count;
                c.mentions_pending = p.patch.mentions_pending || c.mentions_pending;
                if (p.patch.order != 0)       c.order  = p.patch.order;
                if (p.patch.pinned)           c.pinned = true;
                if (!p.patch.avatar_path.empty())
                    c.avatar_path = std::move(p.patch.avatar_path);
                break;
            }
            // Re-sort whenever order or last_message_time may have changed.
            std::sort(m.chats.begin(), m.chats.end(),
                [](const auto& a, const auto& b) {
                    if (a.order != b.order) return a.order > b.order;
                    return a.last_message_time > b.last_message_time;
                });
            if (selected_id) {
                for (std::size_t i = 0; i < m.chats.size(); ++i) {
                    if (m.chats[i].id == *selected_id) {
                        m.selected_chat_index = i; break;
                    }
                }
            }
        },
        [&](E::MessageNew n) {
            // Only ingest messages for the currently-open chat —
            // others stay invisible until the user opens that chat.
            if (!m.selected_chat_index) return;
            if (*m.selected_chat_index >= m.chats.size()) return;
            const auto open_id = m.chats[*m.selected_chat_index].id;
            if (open_id != td::to_model(n.chat_id)) return;
            // Dedup by message id (history loads + updateNewMessage
            // sometimes overlap).
            for (const auto& existing : m.messages) {
                if (existing.id == n.message.id) return;
            }
            auto vm = std::move(n.message);
            const model::MessageVM* prev = m.messages.empty()
                ? nullptr : &m.messages.back();
            detail::msg_derive::apply_grouping(vm, prev);
            m.messages.push_back(std::move(vm));
            // Pin to bottom when a new message arrives — same sentinel
            // trick used by the selection-change paths.
            m.msg_scroll.y = 1'000'000;
        },
        [&](E::MessageEdited e) {
            if (!m.selected_chat_index) return;
            if (*m.selected_chat_index >= m.chats.size()) return;
            const auto open_id = m.chats[*m.selected_chat_index].id;
            if (open_id != td::to_model(e.chat_id)) return;
            const auto mid = td::to_model(e.message_id);
            for (auto& msg_vm : m.messages) {
                if (msg_vm.id == mid) { msg_vm.body = std::move(e.new_body); break; }
            }
        },
        [&](E::MessageDeleted d) {
            if (!m.selected_chat_index) return;
            if (*m.selected_chat_index >= m.chats.size()) return;
            const auto open_id = m.chats[*m.selected_chat_index].id;
            if (open_id != td::to_model(d.chat_id)) return;
            for (auto& tid : d.message_ids) {
                const auto mid = td::to_model(tid);
                std::erase_if(m.messages, [&](const auto& msg_vm) { return msg_vm.id == mid; });
            }
        },
        [&](E::OutboxReadAdvanced r) {
            if (!m.selected_chat_index) return;
            if (*m.selected_chat_index >= m.chats.size()) return;
            const auto open_id = m.chats[*m.selected_chat_index].id;
            if (open_id != td::to_model(r.chat_id)) return;
            const auto last_read = td::to_model(r.last_read);
            for (auto& msg_vm : m.messages) {
                if (msg_vm.from_me && msg_vm.id.get() <= last_read.get()) {
                    msg_vm.read_state = model::ReadState::Read;
                }
            }
        },
        [&](E::UserStatusUpdated u) {
            const auto uid = td::to_model(u.user_id);
            for (auto& mem : m.members) {
                if (mem.user.id == uid) mem.user.presence = u.presence;
            }
            for (auto& typer : m.typers) {
                if (typer.id == uid) typer.presence = u.presence;
            }
            // Propagate to any DM chat whose peer matches this user.
            for (auto& c : m.chats) {
                if (c.kind == model::ChatKind::Direct
                 && c.peer_user_id == uid.get()) {
                    c.partner_presence = u.presence;
                }
            }
        },
        [&](E::TypingStarted t) {
            // Show typing only if the event is for the open chat. Look
            // up the user in the cache (members → fall back to a stub
            // VM with just the id). De-dup by id, refresh expiry.
            if (!m.selected_chat_index) return;
            if (*m.selected_chat_index >= m.chats.size()) return;
            const auto open_id = m.chats[*m.selected_chat_index].id;
            if (open_id != td::to_model(t.chat_id)) return;
            const auto uid     = td::to_model(t.user_id);
            const auto expires = m.clock_seconds + 6;   // ~6 s after the last update
            for (std::size_t i = 0; i < m.typers.size(); ++i) {
                if (m.typers[i].id == uid) {
                    m.typer_expiry[i] = expires; return;
                }
            }
            model::UserVM stub{};
            stub.id = uid;
            // Try members for a richer label.
            for (const auto& mem : m.members) {
                if (mem.user.id == uid) { stub = mem.user; break; }
            }
            if (stub.name.empty()) stub.name = "someone";
            m.typers.push_back(std::move(stub));
            m.typer_expiry.push_back(expires);
        },
        [&](E::FileDownloadProgress /*fp*/) {
            // Hook reserved for the download-progress overlay; the VMs
            // don't carry per-file progress yet.
        },
        [&](E::FileReady fr) {
            // Generic file-ready surfacing: scan the open chat's
            // messages and patch any photo/document/audio_note whose
            // file path is empty but matches the freshly-localised id.
            // We don't carry file_id on the VM (the view only needs
            // the path), so this is a best-effort path swap on the
            // active selection.
            (void)fr;
        },
        [&](E::ChatAvatarReady av) {
            const auto cid = td::to_model(av.chat_id);
            for (auto& c : m.chats) {
                if (c.id == cid) c.avatar_path = std::move(av.local_path);
            }
        },
        [&](E::UserAvatarReady ua) {
            const auto uid = td::to_model(ua.user_id);
            // Patch every chat / member / typer / open-chat message
            // bubble that refers to this user.
            for (auto& c : m.chats) {
                if (c.kind == model::ChatKind::Direct && c.peer_user_id == uid.get()) {
                    c.avatar_path = ua.local_path;
                }
            }
            for (auto& mem : m.members) {
                if (mem.user.id == uid) mem.user.avatar_path = ua.local_path;
            }
            for (auto& t : m.typers) {
                if (t.id == uid) t.avatar_path = ua.local_path;
            }
            for (auto& msg_vm : m.messages) {
                if (msg_vm.author_id == uid)
                    msg_vm.author_avatar_path = ua.local_path;
            }
            auto it = m.peer_info.find(uid.get());
            if (it != m.peer_info.end()) it->second.avatar_path = ua.local_path;
        },
        [&](E::MembersLoaded ml) {
            // Replace m.members ONLY if this batch is for the open chat.
            if (!m.selected_chat_index) return;
            if (*m.selected_chat_index >= m.chats.size()) return;
            const auto open_id = m.chats[*m.selected_chat_index].id;
            if (open_id != td::to_model(ml.chat_id)) return;
            m.members = std::move(ml.members);
        },
        [&](E::PeerInfoLoaded pi) {
            // Stash for the info panel + propagate the per-chat avatar
            // path if we have one.
            m.peer_info[td::to_model(pi.chat_id).get()] = pi.user;
        },
        [&](E::SharedMediaLoaded sm) {
            auto& buckets = m.shared_media[td::to_model(sm.chat_id).get()];
            if (sm.tab >= 0 && sm.tab < 4) {
                buckets[static_cast<std::size_t>(sm.tab)] = std::move(sm.items);
            }
        },
        [&](E::HistoryLoaded hl) {
            // Replace m.messages wholesale if this batch is for the
            // open chat. apply grouping pairwise as we go.
            if (!m.selected_chat_index) return;
            if (*m.selected_chat_index >= m.chats.size()) return;
            const auto open_id = m.chats[*m.selected_chat_index].id;
            if (open_id != td::to_model(hl.chat_id)) return;
            m.messages.clear();
            m.messages.reserve(hl.messages.size());
            const model::MessageVM* prev = nullptr;
            for (auto& msg_vm : hl.messages) {
                detail::msg_derive::apply_grouping(msg_vm, prev);
                m.messages.push_back(std::move(msg_vm));
                prev = &m.messages.back();
            }
            m.msg_scroll.y = 1'000'000;
        },
        [&](E::ReactionsUpdated ru) {
            if (!m.selected_chat_index) return;
            if (*m.selected_chat_index >= m.chats.size()) return;
            const auto open_id = m.chats[*m.selected_chat_index].id;
            if (open_id != td::to_model(ru.chat_id)) return;
            const auto mid = td::to_model(ru.message_id);
            for (auto& msg_vm : m.messages) {
                if (msg_vm.id == mid) {
                    msg_vm.reactions = std::move(ru.reactions);
                    break;
                }
            }
        },
    }, std::move(ev));
}

}  // namespace tl::app


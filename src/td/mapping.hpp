#pragma once

// Pure functions: td_api::* → model::*VM. No I/O, no allocation that
// outlives the return value, no globals. Each function takes a fully-
// populated TDLib object (by const ref) and returns the corresponding
// VM by value. Trivially unit-testable in isolation by hand-rolling
// the td_api struct (which is exactly what the runtime in client.hpp
// does to translate live updates).
//
// Why a header? TDLib emits a LOT of types. Each mapping is small and
// only ever called from src/td/client.cpp; making them inline avoids
// shipping a translation unit per type. The whole header is gated by
// TELELITER_HAS_TDLIB so the model::/view layers can still build in
// stub mode without TDLib installed.

#ifdef TELELITER_HAS_TDLIB

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include "model/view_models.hpp"
#include "td/ids.hpp"

namespace tl::td::map {

namespace td_api = ::td::td_api;

// ─── Small helpers ───────────────────────────────────────────────────────

// Two-char initials from a display name. Skips ascii whitespace and
// lower-cases nothing — display names are already mixed-case. We grab
// the first codepoint of the first two whitespace-separated tokens, or
// duplicate the first if there's only one token.
[[nodiscard]] inline std::string initials_of(std::string_view name) {
    auto first_cp = [](std::string_view sv) -> std::string {
        if (sv.empty()) return {};
        // Take a UTF-8 codepoint at the front. Trust TDLib output is valid.
        const auto b0 = static_cast<unsigned char>(sv[0]);
        std::size_t n = 1;
        if      ((b0 & 0xE0u) == 0xC0u) n = 2;
        else if ((b0 & 0xF0u) == 0xE0u) n = 3;
        else if ((b0 & 0xF8u) == 0xF0u) n = 4;
        if (n > sv.size()) n = sv.size();
        return std::string{sv.substr(0, n)};
    };
    // Token split on whitespace; take up to two tokens.
    std::string_view tokens[2];
    int seen = 0;
    std::size_t i = 0;
    while (i < name.size() && seen < 2) {
        while (i < name.size() && (name[i] == ' ' || name[i] == '\t')) ++i;
        const auto start = i;
        while (i < name.size() && name[i] != ' ' && name[i] != '\t') ++i;
        if (i > start) tokens[seen++] = name.substr(start, i - start);
    }
    if (seen == 0) return "?";
    auto out = first_cp(tokens[0]);
    if (seen >= 2) out += first_cp(tokens[1]);
    return out;
}

// HH:MM in the local timezone — Telegram's "last_message_time" shows a
// short clock for today and a date otherwise. We always emit HH:MM here
// and let the caller swap in a date string if the message is older
// than today; AppModel doesn't carry timezone info yet.
[[nodiscard]] inline std::string hhmm_local(std::int32_t unix_secs) {
    if (unix_secs <= 0) return {};
    const std::time_t t = unix_secs;
    std::tm bd{};
    localtime_r(&t, &bd);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", bd.tm_hour, bd.tm_min);
    return std::string{buf};
}

// Human-friendly relative age ("now", "5m", "3h", "yesterday", "Apr 5").
// Used as the bubble footer's `age_label`.
[[nodiscard]] inline std::string relative_age(std::int32_t when_unix,
                                              std::int32_t now_unix) {
    if (when_unix <= 0) return {};
    const auto diff = now_unix - when_unix;
    if (diff < 60)        return "now";
    if (diff < 3600)      return std::to_string(diff / 60) + "m";
    if (diff < 86400)     return std::to_string(diff / 3600) + "h";
    if (diff < 86400 * 2) return "yesterday";
    if (diff < 86400 * 7) return std::to_string(diff / 86400) + "d";
    const std::time_t t = when_unix;
    std::tm bd{};
    localtime_r(&t, &bd);
    static constexpr const char* mon[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"};
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%s %d", mon[bd.tm_mon], bd.tm_mday);
    return std::string{buf};
}

// ─── Presence ────────────────────────────────────────────────────────────

[[nodiscard]] inline model::Presence to_presence(const td_api::UserStatus& s) noexcept {
    switch (s.get_id()) {
        case td_api::userStatusOnline::ID:    return model::Presence::Active;
        case td_api::userStatusOffline::ID:   return model::Presence::Offline;
        case td_api::userStatusRecently::ID:  return model::Presence::Away;
        case td_api::userStatusLastWeek::ID:
        case td_api::userStatusLastMonth::ID:
        case td_api::userStatusEmpty::ID:
        default:                              return model::Presence::Offline;
    }
}

// ─── User ────────────────────────────────────────────────────────────────

[[nodiscard]] inline model::UserVM to_user(const td_api::user& u) {
    model::UserVM out{};
    out.id = model::UserId{u.id_};
    std::string name = u.first_name_;
    if (!u.last_name_.empty()) {
        if (!name.empty()) name += ' ';
        name += u.last_name_;
    }
    if (name.empty() && u.usernames_) name = u.usernames_->editable_username_;
    if (name.empty()) name = "User";
    out.name     = std::move(name);
    out.initials = initials_of(out.name);
    if (u.status_) out.presence = to_presence(*u.status_);
    out.phone    = u.phone_number_;
    if (u.usernames_ && !u.usernames_->active_usernames_.empty()) {
        out.username = u.usernames_->active_usernames_.front();
    } else if (u.usernames_) {
        out.username = u.usernames_->editable_username_;
    }
    // avatar_path filled later when the profile photo file lands.
    return out;
}

// ─── Chat list row ───────────────────────────────────────────────────────
// `last_message_text` is precomputed by the caller (the runtime knows
// how to flatten formatted text); we just plug it in plus the chat's
// header/avatar info.
[[nodiscard]] inline model::ChatListItemVM
to_chat_row(const td_api::chat& c,
            std::string_view last_message_text,
            std::int32_t     last_message_unix)
{
    model::ChatListItemVM out{};
    out.id    = model::ChatId{c.id_};
    out.title = c.title_.empty() ? std::string{"(no title)"} : c.title_;
    out.initials = initials_of(out.title);
    out.last_message_preview = std::string{last_message_text};
    out.last_message_time    = hhmm_local(last_message_unix);
    out.unread_count = static_cast<std::size_t>(std::max(0, c.unread_count_));
    out.mentions_pending = c.unread_mention_count_ > 0;
    // Notification settings → muted flag.
    if (c.notification_settings_ && c.notification_settings_->mute_for_ > 0) {
        out.muted = true;
    }
    // Kind from chat type + carry peer user_id for DMs (used by the
    // info panel to fetch userFullInfo).
    if (c.type_) {
        switch (c.type_->get_id()) {
            case td_api::chatTypePrivate::ID: {
                auto& p = static_cast<const td_api::chatTypePrivate&>(*c.type_);
                out.kind = model::ChatKind::Direct;
                out.peer_user_id = p.user_id_;
                break;
            }
            case td_api::chatTypeSecret::ID: {
                auto& s = static_cast<const td_api::chatTypeSecret&>(*c.type_);
                out.kind = model::ChatKind::Direct;
                out.peer_user_id = s.user_id_;
                break;
            }
            case td_api::chatTypeBasicGroup::ID:
                out.kind = model::ChatKind::Group;
                break;
            case td_api::chatTypeSupergroup::ID: {
                auto& s = static_cast<const td_api::chatTypeSupergroup&>(*c.type_);
                out.kind = s.is_channel_ ? model::ChatKind::Channel : model::ChatKind::Group;
                break;
            }
            default:
                out.kind = model::ChatKind::Direct;
        }
    }
    // Main-list ordering. positions_ is a vector; we take the entry
    // for chatListMain (the only list we surface). Larger order =
    // higher in the list — the receiver sorts descending.
    for (const auto& pos : c.positions_) {
        if (!pos || !pos->list_) continue;
        if (pos->list_->get_id() == td_api::chatListMain::ID) {
            out.order  = pos->order_;
            out.pinned = pos->is_pinned_;
            break;
        }
    }
    return out;
}

// ─── Message ─────────────────────────────────────────────────────────────
// Flatten formattedText to plain UTF-8. Entities (bold, mentions,
// links) are dropped for now; the message_bubble renderer doesn't carry
// per-run styling yet. The runtime caches the raw text alongside the
// VM for re-render once styling lands.
[[nodiscard]] inline std::string flatten(const td_api::formattedText& ft) {
    return ft.text_;
}

// Caller passes the author display name + initials + avatar — the
// runtime has the user cache, this function does not. Same for
// `from_me` (compared against the cached self id) and now_unix (for
// the age_label).
[[nodiscard]] inline model::MessageVM
to_message(const td_api::message& m,
           std::string_view author_name,
           std::string_view author_initials,
           std::string_view author_avatar_path,
           bool             from_me,
           std::int32_t     now_unix = 0)
{
    model::MessageVM out{};
    out.id              = model::MessageId{m.id_};
    if (m.sender_id_ && m.sender_id_->get_id() == td_api::messageSenderUser::ID) {
        const auto& s = static_cast<const td_api::messageSenderUser&>(*m.sender_id_);
        out.author_id = model::UserId{s.user_id_};
    }
    out.author_name        = std::string{author_name};
    out.author_initials    = std::string{author_initials};
    out.author_avatar_path = std::string{author_avatar_path};
    out.from_me            = from_me;
    out.timestamp          = hhmm_local(m.date_);
    if (now_unix > 0) {
        out.age_label = relative_age(m.date_, now_unix);
    }
    out.mentions_you       = m.contains_unread_mention_;

    // Read state — outgoing defaults to Sent; incoming carries no
    // ack state (always Sent). OutboxReadAdvanced upgrades to Read.
    out.read_state = model::ReadState::Sent;

    // Reactions — collapse the interaction_info reactions into the
    // simple {emoji, count, self_reacted} VM the view uses. We only
    // surface emoji reactions; custom (paid sticker) reactions render
    // with a placeholder glyph so they're at least visible.
    if (m.interaction_info_ && m.interaction_info_->reactions_) {
        for (const auto& r : m.interaction_info_->reactions_->reactions_) {
            if (!r || !r->type_) continue;
            model::ReactionVM rv{};
            rv.count        = r->total_count_;
            rv.self_reacted = r->is_chosen_;
            switch (r->type_->get_id()) {
                case td_api::reactionTypeEmoji::ID: {
                    auto& re = static_cast<const td_api::reactionTypeEmoji&>(*r->type_);
                    rv.emoji = re.emoji_;
                    break;
                }
                default:
                    rv.emoji = "\xF0\x9F\x91\x8D";   // 👍 placeholder
            }
            out.reactions.push_back(std::move(rv));
        }
    }

    // Content dispatch — only the common cases for now. Each branch
    // produces a fully-populated VM; the bubble renderer picks the
    // right widget from the optional fields.
    if (!m.content_) return out;
    switch (m.content_->get_id()) {
        case td_api::messageText::ID: {
            auto& mt = static_cast<const td_api::messageText&>(*m.content_);
            if (mt.text_) out.body = flatten(*mt.text_);
            // Link preview attached to a text message — TDLib carries
            // the unfurled site info on messageText.link_preview_.
            if (mt.link_preview_) {
                model::LinkPreviewVM lp{};
                lp.url         = mt.link_preview_->url_;
                lp.site_name   = mt.link_preview_->site_name_;
                lp.title       = mt.link_preview_->title_;
                lp.description = mt.link_preview_->description_
                    ? mt.link_preview_->description_->text_
                    : std::string{};
                out.link_preview = std::move(lp);
            }
            break;
        }
        case td_api::messagePhoto::ID: {
            auto& mp = static_cast<const td_api::messagePhoto&>(*m.content_);
            model::PhotoVM p{};
            if (mp.caption_) p.caption = flatten(*mp.caption_);
            if (mp.photo_ && !mp.photo_->sizes_.empty()) {
                auto& biggest = mp.photo_->sizes_.back();
                p.width_px  = biggest->width_;
                p.height_px = biggest->height_;
                if (biggest->photo_ && biggest->photo_->local_) {
                    p.file_path = biggest->photo_->local_->path_;
                    p.size_bytes = static_cast<std::size_t>(
                        std::max<std::int64_t>(0, biggest->photo_->size_));
                }
            }
            out.photo = std::move(p);
            break;
        }
        case td_api::messageDocument::ID: {
            auto& md = static_cast<const td_api::messageDocument&>(*m.content_);
            model::DocumentVM d{};
            if (md.caption_)  d.caption  = flatten(*md.caption_);
            if (md.document_) {
                d.filename = md.document_->file_name_;
                d.mime     = md.document_->mime_type_;
                if (md.document_->document_) {
                    if (md.document_->document_->local_)
                        d.file_path = md.document_->document_->local_->path_;
                    d.size_bytes = static_cast<std::size_t>(
                        std::max<std::int64_t>(0, md.document_->document_->size_));
                }
            }
            out.document = std::move(d);
            break;
        }
        case td_api::messageVoiceNote::ID: {
            auto& mv = static_cast<const td_api::messageVoiceNote&>(*m.content_);
            model::AudioNoteVM an{};
            if (mv.caption_) out.body = flatten(*mv.caption_);
            if (mv.voice_note_) {
                an.duration_secs = mv.voice_note_->duration_;
                an.waveform.assign(mv.voice_note_->waveform_.begin(),
                                   mv.voice_note_->waveform_.end());
                if (mv.voice_note_->voice_ && mv.voice_note_->voice_->local_)
                    an.file_path = mv.voice_note_->voice_->local_->path_;
            }
            an.unread = !mv.is_listened_;
            out.audio_note = std::move(an);
            break;
        }
        case td_api::messageVideoNote::ID: {
            auto& mv = static_cast<const td_api::messageVideoNote&>(*m.content_);
            model::VideoNoteVM vn{};
            if (mv.video_note_) {
                vn.duration_secs = mv.video_note_->duration_;
                if (mv.video_note_->video_ && mv.video_note_->video_->local_)
                    vn.file_path = mv.video_note_->video_->local_->path_;
            }
            vn.unread = !mv.is_viewed_;
            out.video_note = std::move(vn);
            break;
        }
        case td_api::messageVideo::ID: {
            auto& mv = static_cast<const td_api::messageVideo&>(*m.content_);
            model::VideoVM v{};
            if (mv.caption_) v.caption = flatten(*mv.caption_);
            if (mv.video_) {
                v.title         = mv.video_->file_name_;
                v.duration_secs = mv.video_->duration_;
                v.width_px      = mv.video_->width_;
                v.height_px     = mv.video_->height_;
                if (mv.video_->video_ && mv.video_->video_->local_)
                    v.file_path = mv.video_->video_->local_->path_;
                if (mv.video_->video_)
                    v.size_bytes = static_cast<std::size_t>(
                        std::max<std::int64_t>(0, mv.video_->video_->size_));
            }
            out.video = std::move(v);
            break;
        }
        case td_api::messageAudio::ID: {
            auto& ma = static_cast<const td_api::messageAudio&>(*m.content_);
            model::MusicTrackVM mt{};
            if (ma.caption_) out.body = flatten(*ma.caption_);
            if (ma.audio_) {
                mt.title         = ma.audio_->title_;
                mt.artist        = ma.audio_->performer_;
                mt.duration_secs = ma.audio_->duration_;
                if (ma.audio_->audio_ && ma.audio_->audio_->local_)
                    mt.file_path = ma.audio_->audio_->local_->path_;
            }
            out.music = std::move(mt);
            break;
        }
        case td_api::messageSticker::ID: {
            auto& ms = static_cast<const td_api::messageSticker&>(*m.content_);
            model::StickerVM s{};
            if (ms.sticker_) s.emoji = ms.sticker_->emoji_;
            out.sticker = std::move(s);
            break;
        }
        case td_api::messageAnimation::ID: {
            auto& ma = static_cast<const td_api::messageAnimation&>(*m.content_);
            model::AnimationVM a{};
            if (ma.caption_) a.caption = flatten(*ma.caption_);
            if (ma.animation_) {
                a.duration_secs = ma.animation_->duration_;
                a.width_px      = ma.animation_->width_;
                a.height_px     = ma.animation_->height_;
                if (ma.animation_->animation_ && ma.animation_->animation_->local_)
                    a.file_path = ma.animation_->animation_->local_->path_;
            }
            out.animation = std::move(a);
            break;
        }
        case td_api::messageContact::ID: {
            auto& mc = static_cast<const td_api::messageContact&>(*m.content_);
            model::ContactVM ct{};
            if (mc.contact_) {
                ct.first_name = mc.contact_->first_name_;
                ct.last_name  = mc.contact_->last_name_;
                ct.phone      = mc.contact_->phone_number_;
            }
            out.contact = std::move(ct);
            break;
        }
        case td_api::messageLocation::ID: {
            auto& ml = static_cast<const td_api::messageLocation&>(*m.content_);
            model::LocationVM loc{};
            if (ml.location_) {
                loc.lat = ml.location_->latitude_;
                loc.lng = ml.location_->longitude_;
            }
            loc.live = ml.live_period_ > 0;
            out.location = std::move(loc);
            break;
        }
        case td_api::messageVenue::ID: {
            auto& mv = static_cast<const td_api::messageVenue&>(*m.content_);
            model::LocationVM loc{};
            if (mv.venue_) {
                loc.venue_name = mv.venue_->title_;
                loc.address    = mv.venue_->address_;
                if (mv.venue_->location_) {
                    loc.lat = mv.venue_->location_->latitude_;
                    loc.lng = mv.venue_->location_->longitude_;
                }
            }
            out.location = std::move(loc);
            break;
        }
        case td_api::messagePoll::ID: {
            auto& mp = static_cast<const td_api::messagePoll&>(*m.content_);
            model::PollVM p{};
            if (mp.poll_) {
                p.question = mp.poll_->question_
                    ? mp.poll_->question_->text_ : std::string{};
                p.total_votes = mp.poll_->total_voter_count_;
                p.closed      = mp.poll_->is_closed_;
                p.anonymous   = mp.poll_->is_anonymous_;
                for (const auto& opt : mp.poll_->options_) {
                    model::PollOptionVM po{};
                    po.text       = opt->text_ ? opt->text_->text_ : std::string{};
                    po.votes      = opt->voter_count_;
                    po.self_voted = opt->is_chosen_;
                    p.options.push_back(std::move(po));
                }
            }
            out.poll = std::move(p);
            break;
        }
        default: {
            // Fall back to a system notice with the type id — keeps
            // the UI honest about unhandled content rather than
            // silently dropping the bubble.
            out.is_system = true;
            out.body = "[unsupported message type]";
            break;
        }
    }

    // Reply target — TDLib's reply_to_ is a messageReplyTo* sum; we
    // only handle the in-chat case (messageReplyToMessage). The author
    // name + snippet would need a lookup on the cached message store;
    // the runtime fills those after the fact via a follow-up patch.
    if (m.reply_to_ && m.reply_to_->get_id() == td_api::messageReplyToMessage::ID) {
        auto& rt = static_cast<const td_api::messageReplyToMessage&>(*m.reply_to_);
        model::ReplyQuoteVM q{};
        q.source_id = model::MessageId{rt.message_id_};
        // author_name + snippet stay empty here; the runtime upgrades
        // them when the original message is in the local cache.
        out.reply_quote = std::move(q);
    }
    return out;
}

}  // namespace tl::td::map

#endif  // TELELITER_HAS_TDLIB

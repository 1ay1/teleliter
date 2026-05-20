#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/image.hpp"
#include "views/molecules/audio_note.hpp"   // for kBars + mmss reuse
#include "views/util/image_load.hpp"
#include "views/theme.hpp"

// ─── Text-only renderers for every Telegram media kind ────────────────────────
//
// One header, one helper per kind. Each function takes its VM and an
// `inner_w` cell budget (the bubble's content width minus border + padding)
// and returns a maya::Element built entirely from text + box-drawing
// glyphs — no raster needed at the message-bubble layer.
//
// Style conventions across all kinds:
//   ▸ accent-bold glyph in the top-left corner identifies the kind
//   ▸ bold first line is the "name"   (filename / title / venue / question)
//   ▸ dim italic second line is meta  (size / artist / coords / "open in …")
//   ▸ optional caption renders below, in the surrounding text style
//   ▸ no terminal-bg fills — colors only on glyphs + text accents
//
// All widths are clamped so callers never have to defend against silly
// numbers; pass the bubble's inner width and forget about it.

namespace tl::views {

namespace detail::media {

// ─── Generic helpers ─────────────────────────────────────────────────────────

[[nodiscard]] inline std::string format_size(std::size_t bytes) noexcept
{
    constexpr double kKB = 1024.0;
    constexpr double kMB = 1024.0 * 1024.0;
    constexpr double kGB = 1024.0 * 1024.0 * 1024.0;
    char buf[32];
    if (bytes >= static_cast<std::size_t>(kGB)) {
        std::snprintf(buf, sizeof buf, "%.1f GB",
            static_cast<double>(bytes) / kGB);
    } else if (bytes >= static_cast<std::size_t>(kMB)) {
        std::snprintf(buf, sizeof buf, "%.1f MB",
            static_cast<double>(bytes) / kMB);
    } else if (bytes >= static_cast<std::size_t>(kKB)) {
        std::snprintf(buf, sizeof buf, "%.0f KB",
            static_cast<double>(bytes) / kKB);
    } else {
        std::snprintf(buf, sizeof buf, "%zu B", bytes);
    }
    return std::string{buf};
}

[[nodiscard]] inline std::string format_dims(int w, int h) noexcept
{
    if (w <= 0 || h <= 0) return {};
    return std::to_string(w) + "\xC3\x97" + std::to_string(h);   // ×
}

// Pick an "open with" hint based on file extension. Pure heuristic —
// fine for a demo UI; a real client would consult the local mime db.
[[nodiscard]] inline std::string_view open_hint_for(std::string_view path) noexcept
{
    auto ends_with = [&](std::string_view ext) noexcept {
        return path.size() >= ext.size()
            && path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
    };
    if (ends_with(".mp4") || ends_with(".mkv") || ends_with(".webm")
     || ends_with(".mov") || ends_with(".m4v") || ends_with(".gif"))
        return "mpv \xE2\x86\x97";                                // mpv ↗
    if (ends_with(".mp3") || ends_with(".m4a") || ends_with(".flac")
     || ends_with(".ogg") || ends_with(".wav") || ends_with(".opus"))
        return "mpv \xE2\x86\x97";
    if (ends_with(".pdf") || ends_with(".doc") || ends_with(".docx")
     || ends_with(".odt"))
        return "xdg-open \xE2\x86\x97";
    return "xdg-open \xE2\x86\x97";
}

// One-row "kind glyph + bold title + spacer + dim meta" header. Used by
// almost every card so they share a visual rhythm.
[[nodiscard]] inline maya::Element header_row(
    std::string_view glyph,
    std::string_view title,
    std::string_view meta)
{
    using namespace maya;
    using namespace maya::dsl;
    auto g = text(std::string{glyph},
        Style{}.with_fg(palette::accent()).with_bold());
    auto t = text(std::string{title},
        Style{}.with_fg(palette::text()).with_bold());
    if (meta.empty()) {
        return hstack().gap(1).align_items(Align::Center)(g, t);
    }
    return hstack().gap(1).align_items(Align::Center)(
        g, t, spacer(),
        text(std::string{meta}, Style{}.with_fg(palette::dim())));
}

[[nodiscard]] inline maya::Element meta_row(std::string_view txt)
{
    using namespace maya;
    using namespace maya::dsl;
    return text(std::string{txt}, Style{}.with_fg(palette::muted()).with_italic());
}

[[nodiscard]] inline maya::Element caption_row(std::string_view caption)
{
    using namespace maya;
    using namespace maya::dsl;
    if (caption.empty()) return text(std::string{});
    return text(std::string{caption}, Style{}.with_fg(palette::text()));
}

// Box-drawing rail used as a side-marker on the link-preview card and
// other "extracted-from-something-else" UI bits. One column wide, full
// height of the contained content, tinted in the accent color.
[[nodiscard]] inline maya::Element accent_rail()
{
    using namespace maya;
    using namespace maya::dsl;
    return text(std::string{"\xE2\x96\x8E"},                       // ▎
        Style{}.with_fg(palette::accent()).with_bold());
}

}  // namespace detail::media

// ─── Photo ───────────────────────────────────────────────────────────────────
//
//   📷  hike-sunset.jpg                          1.2 MB
//       1920×1080 · jpg
//       (optional tiny half-block thumbnail)
//       (optional caption text)
//
// `show_thumb` upgrades the card with a small image raster above the
// metadata. Off by default so most photos render as compact cards.
[[nodiscard]] inline maya::Element render_photo_card(
    const model::PhotoVM& p, int inner_w)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::media;

    // Filename = last path component, or "photo" if path is empty.
    std::string name = p.file_path;
    if (auto slash = name.find_last_of('/'); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    if (name.empty()) name = "photo";

    std::vector<Element> rows;
    rows.push_back(header_row("\xF0\x9F\x93\xB7", name,             // 📷
        p.size_bytes > 0 ? format_size(p.size_bytes) : std::string{}));

    if (p.show_thumb && !p.file_path.empty()) {
        const auto& img = tl::views::util::load_image_cached(p.file_path);
        if (img.ok()) {
            const int thumb_w = std::clamp(inner_w - 2, 16, 36);
            const int thumb_h = std::clamp(thumb_w / 3, 4, 10);
            rows.push_back(render_image(p.file_path, thumb_w, thumb_h,
                maya::Color::black(), /*circle=*/false));
        }
    }

    std::string meta_bits;
    if (auto d = format_dims(p.width_px, p.height_px); !d.empty()) {
        meta_bits = d;
    }
    if (!meta_bits.empty()) meta_bits += " \xC2\xB7 ";              // ·
    meta_bits += "photo";
    rows.push_back(meta_row(meta_bits));

    if (!p.caption.empty()) {
        rows.push_back(text(std::string{}));
        rows.push_back(caption_row(p.caption));
    }
    return vstack().gap(0).width(Dimension::fixed(std::max(16, inner_w)))(rows);
}

// ─── Document / file ─────────────────────────────────────────────────────────
//
//   📄  plan.pdf                                  2.1 MB
//       application/pdf · xdg-open ↗
[[nodiscard]] inline maya::Element render_document_card(
    const model::DocumentVM& d, int inner_w)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::media;

    std::string name = d.filename;
    if (name.empty()) {
        name = d.file_path;
        if (auto slash = name.find_last_of('/'); slash != std::string::npos) {
            name = name.substr(slash + 1);
        }
        if (name.empty()) name = "file";
    }

    std::string meta = d.mime.empty() ? std::string{"file"} : d.mime;
    meta += " \xC2\xB7 ";
    meta += open_hint_for(d.file_path.empty() ? d.filename : d.file_path);

    std::vector<Element> rows;
    rows.push_back(header_row("\xF0\x9F\x93\x84", name,             // 📄
        d.size_bytes > 0 ? format_size(d.size_bytes) : std::string{}));
    rows.push_back(meta_row(meta));
    if (!d.caption.empty()) {
        rows.push_back(text(std::string{}));
        rows.push_back(caption_row(d.caption));
    }
    return vstack().gap(0).width(Dimension::fixed(std::max(16, inner_w)))(rows);
}

// ─── Sticker ─────────────────────────────────────────────────────────────────
//
//          ╭───────╮
//          │   😺  │            (oversized emoji, centered)
//          ╰───────╯
//          Animals · sticker
//
// Stickers in Telegram have no caption, no metadata beyond the pack.
// We center the emoji on its own line for visual emphasis.
[[nodiscard]] inline maya::Element render_sticker_card(
    const model::StickerVM& s, int inner_w)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::media;

    std::string em = s.emoji.empty() ? std::string{"\xE2\x9C\xA8"} : s.emoji;  // ✨

    auto emoji_row = hstack().width(Dimension::percent(100)).justify(Justify::Center)(
        text(em, Style{}.with_bold()));
    auto pack_row  = hstack().width(Dimension::percent(100)).justify(Justify::Center)(
        text((s.animated ? std::string{"\xE2\x9A\xA1 "} : std::string{}) +
                 (s.pack_name.empty() ? std::string{"sticker"} : s.pack_name)
                 + " \xC2\xB7 sticker",
            Style{}.with_fg(palette::muted()).with_italic()));

    return vstack().gap(0).width(Dimension::fixed(std::max(12, inner_w)))(
        emoji_row, pack_row);
}

// ─── Animation (GIF) ─────────────────────────────────────────────────────────
//
//   🎞  loop.gif                                 480 KB
//       640×480 · 0:03 · animation · mpv ↗
[[nodiscard]] inline maya::Element render_animation_card(
    const model::AnimationVM& a, int inner_w)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::media;

    std::string name = a.file_path;
    if (auto slash = name.find_last_of('/'); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    if (name.empty()) name = "animation";

    std::string meta;
    if (auto d = format_dims(a.width_px, a.height_px); !d.empty()) {
        meta = d + " \xC2\xB7 ";
    }
    if (a.duration_secs > 0) {
        meta += detail::audio_note::mmss(a.duration_secs);
        meta += " \xC2\xB7 ";
    }
    meta += "animation \xC2\xB7 ";
    meta += open_hint_for(a.file_path);

    std::vector<Element> rows;
    rows.push_back(header_row("\xF0\x9F\x8E\x9E", name,             // 🎞
        a.size_bytes > 0 ? format_size(a.size_bytes) : std::string{}));
    rows.push_back(meta_row(meta));
    if (!a.caption.empty()) {
        rows.push_back(text(std::string{}));
        rows.push_back(caption_row(a.caption));
    }
    return vstack().gap(0).width(Dimension::fixed(std::max(16, inner_w)))(rows);
}

// ─── Video (full-length) ─────────────────────────────────────────────────────
//
//   🎬  Sunset over Tahoe                         12.4 MB
//       1920×1080 · 0:42 · video · mpv ↗
//       (caption)
[[nodiscard]] inline maya::Element render_video_card(
    const model::VideoVM& v, int inner_w)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::media;

    std::string name = v.title;
    if (name.empty()) {
        name = v.file_path;
        if (auto slash = name.find_last_of('/'); slash != std::string::npos) {
            name = name.substr(slash + 1);
        }
        if (name.empty()) name = "video";
    }

    std::string meta;
    if (auto d = format_dims(v.width_px, v.height_px); !d.empty()) {
        meta = d + " \xC2\xB7 ";
    }
    if (v.duration_secs > 0) {
        meta += detail::audio_note::mmss(v.duration_secs);
        meta += " \xC2\xB7 ";
    }
    meta += "video \xC2\xB7 ";
    meta += open_hint_for(v.file_path);

    std::vector<Element> rows;
    rows.push_back(header_row("\xF0\x9F\x8E\xAC", name,             // 🎬
        v.size_bytes > 0 ? format_size(v.size_bytes) : std::string{}));
    rows.push_back(meta_row(meta));
    if (!v.caption.empty()) {
        rows.push_back(text(std::string{}));
        rows.push_back(caption_row(v.caption));
    }
    return vstack().gap(0).width(Dimension::fixed(std::max(16, inner_w)))(rows);
}

// ─── Music track ─────────────────────────────────────────────────────────────
//
//   🎵  Midnight City                             4.0 MB
//       M83 · 4:03
//   [▶] ▁▃▅▇▆▅▇█▇▆▅▄▃▂▁  0:24 / 4:03
//
// Different glyph from voice notes (🎙). Renders waveform via the same
// audio_note helper so the playback affordance is consistent.
[[nodiscard]] inline maya::Element render_music_card(
    const model::MusicTrackVM& mt, int inner_w)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::media;

    std::string name = mt.title;
    if (name.empty()) {
        name = mt.file_path;
        if (auto slash = name.find_last_of('/'); slash != std::string::npos) {
            name = name.substr(slash + 1);
        }
        if (name.empty()) name = "track";
    }

    // Build a proxy AudioNoteVM so we can reuse the play/wave/clock row.
    model::AudioNoteVM proxy{};
    proxy.file_path     = mt.file_path;
    proxy.duration_secs = mt.duration_secs;
    proxy.progress_secs = mt.progress_secs;
    proxy.playing       = mt.playing;
    proxy.waveform      = mt.waveform;
    proxy.playback_speed = mt.playback_speed;

    const int bar_count = std::max(8, inner_w - 16);

    std::vector<Element> rows;
    rows.push_back(header_row("\xF0\x9F\x8E\xB5", name,             // 🎵
        mt.duration_secs > 0
            ? detail::audio_note::mmss(mt.duration_secs)
            : std::string{}));

    std::string sub = mt.artist.empty() ? std::string{"unknown artist"} : mt.artist;
    sub += " \xC2\xB7 track";
    rows.push_back(meta_row(sub));
    rows.push_back(render_audio_note(proxy, bar_count));
    return vstack().gap(0).width(Dimension::fixed(std::max(16, inner_w)))(rows);
}

// ─── Link preview ────────────────────────────────────────────────────────────
//
//   ▎ github.com
//   ▎ 1ay1/maya — Terminal UI for C++26
//   ▎ Compile-time DSL, flexbox layout, SIMD-diffed frames…
//
// Designed to render UNDER a body of text in the same bubble — i.e. when
// the user typed a URL, this is the auto-extracted preview below. Reads
// as one indented block via the ▎ rail.
[[nodiscard]] inline maya::Element render_link_preview(
    const model::LinkPreviewVM& lp, int inner_w)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::media;

    std::vector<Element> col_rows;
    if (!lp.site_name.empty()) {
        col_rows.push_back(text(lp.site_name,
            Style{}.with_fg(palette::muted()).with_italic()));
    }
    if (!lp.title.empty()) {
        col_rows.push_back(text(lp.title,
            Style{}.with_fg(palette::accent()).with_bold()));
    } else if (!lp.url.empty()) {
        col_rows.push_back(text(lp.url,
            Style{}.with_fg(palette::accent()).with_bold()));
    }
    if (!lp.description.empty()) {
        col_rows.push_back(text(lp.description,
            Style{}.with_fg(palette::muted())));
    }
    if (col_rows.empty()) {
        col_rows.push_back(text(lp.url,
            Style{}.with_fg(palette::dim())));
    }

    auto col = vstack().gap(0)(col_rows);

    return hstack().gap(1).align_items(Align::Start)
        .width(Dimension::fixed(std::max(16, inner_w)))
        (accent_rail(), std::move(col));
}

// ─── Location ────────────────────────────────────────────────────────────────
//
//   📍  Blue Bottle Coffee                        live
//       66 Mint St, SF
//       37.7825, -122.4036 · open in maps ↗
[[nodiscard]] inline maya::Element render_location_card(
    const model::LocationVM& loc, int inner_w)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::media;

    std::string title = loc.venue_name;
    if (title.empty()) title = "location";

    char coord_buf[64];
    std::snprintf(coord_buf, sizeof coord_buf,
        "%.4f, %.4f \xC2\xB7 open in maps \xE2\x86\x97",
        loc.lat, loc.lng);

    std::vector<Element> rows;
    rows.push_back(header_row("\xF0\x9F\x93\x8D", title,             // 📍
        loc.live ? std::string{"\xE2\x97\x8F live"} : std::string{}));
    if (!loc.address.empty()) {
        rows.push_back(text(loc.address,
            Style{}.with_fg(palette::text())));
    }
    rows.push_back(meta_row(coord_buf));
    return vstack().gap(0).width(Dimension::fixed(std::max(16, inner_w)))(rows);
}

// ─── Contact card ────────────────────────────────────────────────────────────
//
//   👤  Ana Rivera                                @ana
//       +1 555 0100
[[nodiscard]] inline maya::Element render_contact_card(
    const model::ContactVM& c, int inner_w)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::media;

    std::string name = c.first_name;
    if (!c.last_name.empty()) {
        if (!name.empty()) name += " ";
        name += c.last_name;
    }
    if (name.empty()) name = "contact";

    std::string meta = c.username.empty() ? std::string{} : std::string{"@"} + c.username;

    std::vector<Element> rows;
    rows.push_back(header_row("\xF0\x9F\x91\xA4", name, meta));      // 👤
    if (!c.phone.empty()) {
        rows.push_back(text(c.phone, Style{}.with_fg(palette::text())));
    }
    rows.push_back(meta_row("contact"));
    return vstack().gap(0).width(Dimension::fixed(std::max(16, inner_w)))(rows);
}

// ─── Poll ────────────────────────────────────────────────────────────────────
//
//   📊  What's for lunch?                         12 votes
//       ✓ Pizza        ████████░░░░░░░░  8 · 67%
//         Salad        ███░░░░░░░░░░░░░  3 · 25%
//         Pasta        █░░░░░░░░░░░░░░░  1 ·  8%
//       anonymous · single choice
//
// Bar charts use a single character per cell from the eighth-block
// scale (█ filled / ░ empty). Cell budget for the bar = inner_w minus
// option label + counts; clamped to a sensible range.
namespace detail::media {

[[nodiscard]] inline std::string poll_bar(int filled, int total)
{
    if (total < 1) total = 1;
    if (filled < 0) filled = 0;
    if (filled > total) filled = total;
    std::string out;
    out.reserve(static_cast<std::size_t>(total) * 3);
    for (int i = 0; i < filled; ++i) out += "\xE2\x96\x88";          // █
    for (int i = filled; i < total; ++i) out += "\xE2\x96\x91";       // ░
    return out;
}

}  // namespace detail::media

[[nodiscard]] inline maya::Element render_poll_card(
    const model::PollVM& p, int inner_w)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::media;

    const int total = std::max(p.total_votes, [&] {
        int sum = 0;
        for (const auto& o : p.options) sum += o.votes;
        return sum;
    }());

    // Find the longest option label so the bar charts line up.
    std::size_t label_w = 0;
    for (const auto& o : p.options) {
        label_w = std::max(label_w, o.text.size());
    }
    label_w = std::min(label_w, static_cast<std::size_t>(std::max(8, inner_w / 3)));

    // Bar cells = inner_w − ("✓ " + label + 2-space gap + " NN · NN%").
    const int counts_w = 10;            // " NN · NN%"
    const int bar_cells = std::clamp(
        inner_w - static_cast<int>(label_w) - 2 - counts_w - 2,
        4, 24);

    std::vector<Element> rows;
    rows.push_back(header_row("\xF0\x9F\x93\x8A",                   // 📊
        p.question.empty() ? std::string{"poll"} : p.question,
        std::to_string(total) + (total == 1 ? " vote" : " votes")));

    for (const auto& opt : p.options) {
        const int filled_cells = (total > 0)
            ? (opt.votes * bar_cells + total / 2) / total
            : 0;
        const int pct = (total > 0) ? (opt.votes * 100 + total / 2) / total : 0;

        std::string vote_mark = opt.self_voted ? "\xE2\x9C\x93 " : "  ";  // ✓
        std::string label_pad = opt.text;
        if (label_pad.size() < label_w)
            label_pad.append(label_w - label_pad.size(), ' ');

        // Build the three sub-rows in a single hstack so the bars align.
        char tail[32];
        std::snprintf(tail, sizeof tail, " %2d \xC2\xB7 %2d%%",
            opt.votes, pct);

        auto opt_row = hstack().gap(0).align_items(Align::Center)(
            text(vote_mark, Style{}
                .with_fg(opt.self_voted ? palette::accent() : palette::muted())
                .with_bold()),
            text(label_pad, Style{}.with_fg(palette::text())),
            text(std::string{"  "}),
            text(poll_bar(filled_cells, bar_cells),
                Style{}.with_fg(opt.self_voted
                    ? palette::accent()
                    : palette::dim())),
            text(std::string{tail}, Style{}.with_fg(palette::dim())));
        rows.push_back(opt_row);
    }

    std::string footer = p.anonymous ? std::string{"anonymous"} : std::string{"public"};
    footer += " \xC2\xB7 ";
    footer += p.multi_choice ? "multiple choice" : "single choice";
    if (p.closed) footer += " \xC2\xB7 closed";
    rows.push_back(meta_row(footer));

    return vstack().gap(0).width(Dimension::fixed(std::max(20, inner_w)))(rows);
}

}  // namespace tl::views

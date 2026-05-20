#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// ─── Audio note widget ──────────────────────────────────────────────────────
//
//   [▶]  ▁▂▄▇▆▅▃▂▁▁▂▃▄  0:24 / 1:05
//        └ already played ┘└ remaining ─┘
//
// One-row inline player that drops into a message bubble in place of
// the text body. State lives on the message (AudioNoteVM); the widget
// just maps progress + waveform into glyphs and tint.
//
// Glyphs:
//   ▁ ▂ ▃ ▄ ▅ ▆ ▇ █     (Lower 1/8 .. full block — 8-step bar amplitude)
//   ▶ ⏸                  (transport)
//
// Played portion uses the accent tint; the remaining portion is muted.
// The boundary cell where playback "is" gets an accent-bold treatment
// so the playhead reads as a moving wave front instead of a hard cut.

namespace detail::audio_note {

// 8-step amplitude ramp via the Unicode "lower block" series. Index 0 is
// "▁" (one-eighth bottom), index 7 is "█". A zero-amplitude column emits
// a half-dot so the waveform never disappears entirely.
inline constexpr const char* kBars[8] = {
    "\xe2\x96\x81",  // ▁
    "\xe2\x96\x82",  // ▂
    "\xe2\x96\x83",  // ▃
    "\xe2\x96\x84",  // ▄
    "\xe2\x96\x85",  // ▅
    "\xe2\x96\x86",  // ▆
    "\xe2\x96\x87",  // ▇
    "\xe2\x96\x88",  // █
};

[[nodiscard]] inline std::string mmss(int secs) noexcept
{
    if (secs < 0) secs = 0;
    const int m = secs / 60;
    const int s = secs % 60;
    std::string out = std::to_string(m);
    out += ':';
    if (s < 10) out += '0';
    out += std::to_string(s);
    return out;
}

}  // namespace detail::audio_note

[[nodiscard]] inline maya::Element render_audio_note(
    const model::AudioNoteVM& note,
    int bar_count)
{
    using namespace maya;
    using namespace maya::dsl;

    bar_count = std::max(8, bar_count);

    // Play / pause glyph. ▶ when stopped, ⏸ when playing — gives the
    // user a clear "click me to toggle" affordance.
    auto transport = text(
        note.playing ? std::string{"⏸"} : std::string{"▶"},
        Style{}.with_fg(palette::accent()).with_bold());

    // Waveform. We resample the source waveform into bar_count buckets
    // (nearest-neighbour — it's a UI element, not an analysis tool)
    // and tint each column by whether it's left or right of the
    // playhead position.
    const int  duration  = std::max(1, note.duration_secs);
    const int  progress  = std::clamp(note.progress_secs, 0, duration);
    const int  playhead  = (progress * bar_count + duration / 2) / duration;
    const auto wf_size   = note.waveform.empty() ? std::size_t{0} : note.waveform.size();

    std::vector<Element> bars;
    bars.reserve(static_cast<std::size_t>(bar_count));
    for (int i = 0; i < bar_count; ++i) {
        std::uint8_t amp = 64;   // sensible default when no waveform present
        if (wf_size > 0) {
            const std::size_t src_idx =
                (static_cast<std::size_t>(i) * wf_size)
                / static_cast<std::size_t>(bar_count);
            amp = note.waveform[std::min(src_idx, wf_size - 1)];
        }
        const int step = std::clamp(static_cast<int>(amp) / 32, 0, 7);
        Style sty;
        if (i < playhead) {
            sty = Style{}.with_fg(palette::accent()).with_bold();
        } else if (i == playhead && note.playing) {
            sty = Style{}.with_fg(palette::accent());
        } else {
            sty = Style{}.with_fg(palette::dim());
        }
        bars.push_back(text(std::string{detail::audio_note::kBars[step]}, sty));
    }

    auto waveform_row = hstack().gap(0)(bars);

    auto clock = text(
        detail::audio_note::mmss(progress) + " / "
            + detail::audio_note::mmss(note.duration_secs),
        Style{}.with_fg(palette::dim()));

    // Unread ● cue — small accent dot beside the clock when the local
    // user hasn't listened yet. Telegram drops it the first time you
    // tap play; we keep it in sync via the unread flag on the VM.
    auto unread_dot = note.unread
        ? text(std::string{"\xE2\x97\x8F"},                             // ●
               Style{}.with_fg(palette::accent()).with_bold())
        : text(std::string{});

    // Speed pill: "1×" / "1.5×" / "2×". Renders as muted text in a
    // square-bracket frame so it reads as a tap-target without needing
    // a real button. Hidden at default speed to keep the row quiet.
    auto speed_pill = [&]() -> Element {
        if (note.playback_speed <= 1.0 + 1e-6) return text(std::string{});
        char buf[8];
        if (std::abs(note.playback_speed - 1.5) < 1e-3) {
            std::snprintf(buf, sizeof buf, "1.5\xC3\x97");                // 1.5×
        } else if (std::abs(note.playback_speed - 2.0) < 1e-3) {
            std::snprintf(buf, sizeof buf, "2\xC3\x97");                  // 2×
        } else {
            std::snprintf(buf, sizeof buf, "%.1f\xC3\x97", note.playback_speed);
        }
        return text(std::string{buf},
            Style{}.with_fg(palette::accent()).with_bold());
    }();

    auto controls_row = hstack().gap(1).align_items(Align::Center)(
        transport,
        waveform_row,
        clock,
        unread_dot,
        speed_pill
    );

    if (note.transcript.empty() || !note.transcript_expanded) {
        return controls_row;
    }
    // Expanded transcript: muted italic block underneath. Telegram
    // shows this when you tap the "Aa" affordance — we don't draw a
    // separate button (no room), but the model bit drives the toggle
    // via the ToggleLatestTranscript / ToggleTranscript msgs.
    auto transcript_row = text(
        std::string{"  "} + note.transcript,
        Style{}.with_fg(palette::muted()).with_italic());
    return vstack().gap(0)(controls_row, transcript_row);
}

// Video note widget — the round "video circle" that Telegram users
// recognise. In a terminal we render it as a circular frame drawn with
// Unicode block / half-block glyphs, with a centered transport glyph,
// a clock overlay across the bottom, an optional mute badge in the
// top-right, and a thin waveform row beneath the circle (audio is the
// only thing that can actually play here, so the waveform doubles as
// the playback progress display).
//
// Shape (15×7 cells, scales with bubble width):
//      ▄▀▀▀▀▀▀▀▀▀▀▀▀▄
//    ▀▀             ▀▀
//   █                  █
//   █        ▶         █
//   █                  █
//    ▀▄   0:24 / 1:05  ▄▀
//      ▀▄▄▄▄▄▄▄▄▄▄▄▀
//   ▶ ◁◂◃◄◅  audio mode


// Video note widget — the round "video circle" Telegram users
// recognise. We can't actually paint video frames in a terminal, and
// faking a colored fill inside the ring reads as a glitchy pixelated
// thumbnail (worse than honest emptiness). So the circle is rendered
// as a pure OUTLINE: ▀ / ▄ half-blocks trace the ellipse edge, the
// inside is blank, and the centre carries a single ▶/⏸ transport
// glyph. Clock + mute live in a thin row beneath the ring so they're
// always legible without needing a colored backdrop.
//
//        ▄▀▀▀▀▀▀▀▀▀▀▀▀▄
//      ▀                  ▀
//     ▀                    ▀
//     █          ▶         █
//     ▀                    ▀
//      ▄                  ▄
//        ▀▄▄▄▄▄▄▄▄▄▄▄▀
//     ▶  0:24 / 1:05  🔇
//     ▶ ◁◂◃◄◅  audio mode

namespace detail::video_note {

// Returns the glyph + color for cell (cx, cy). Inside-the-ellipse and
// outside-the-ellipse both return a blank space — only edge cells get
// a half-block. Avoids the "random colored pixels" look that a filled
// ring produced.
struct CellGlyph {
    std::string_view utf8;
    maya::Color      fg = maya::Color::default_color();
};

[[nodiscard]] inline CellGlyph cell_for(
    int cx, int cy, int cols, int rows,
    maya::Color frame_color) noexcept
{
    const int px_w = cols;
    const int px_h = rows * 2;
    const double Cx = (px_w  - 1) * 0.5;
    const double Cy = (px_h  - 1) * 0.5;
    const double Rx =  px_w  * 0.5;
    const double Ry =  px_h  * 0.5;
    auto inside = [&](int px, int py) noexcept {
        const double dx = (px - Cx) / Rx;
        const double dy = (py - Cy) / Ry;
        return dx * dx + dy * dy <= 1.0;
    };
    const int top_py = cy * 2;
    const int bot_py = cy * 2 + 1;
    const bool t = inside(cx, top_py);
    const bool b = inside(cx, bot_py);
    // Edge — exactly one of the two pixels is inside the ellipse —
    // gets a half-block in the frame color. Pure-inside and pure-
    // outside both render as a blank cell so the ring is an outline,
    // not a filled disk.
    if (t && !b) return {"\xE2\x96\x80", frame_color};                    // ▀
    if (!t && b) return {"\xE2\x96\x84", frame_color};                    // ▄
    return {" ", maya::Color::default_color()};
}

}  // namespace detail::video_note

[[nodiscard]] inline maya::Element render_video_note(
    const model::VideoNoteVM& note,
    int bar_count)
{
    using namespace maya;
    using namespace maya::dsl;

    // Circle size — fixed cells. We could scale with bubble width, but
    // Telegram's circles are a fixed 200px-ish on mobile too, and that
    // visual consistency is half the point.
    constexpr int kCols = 15;
    constexpr int kRows = 7;

    const auto frame_c = palette::accent();

    constexpr int kCenterRow = kRows / 2;       // 3
    constexpr int kCenterCol = kCols / 2;       // 7

    // Build the ring — transport glyph at the centre, everything else
    // is either an edge half-block or a blank cell.
    std::vector<Element> rows;
    rows.reserve(static_cast<std::size_t>(kRows));

    for (int r = 0; r < kRows; ++r) {
        std::vector<Element> cells;
        cells.reserve(static_cast<std::size_t>(kCols));
        for (int c = 0; c < kCols; ++c) {
            // Centre transport glyph (▶ / ⏸). Sits on a blank cell
            // so it reads without competing with frame fill.
            if (r == kCenterRow && c == kCenterCol) {
                cells.push_back(text(
                    note.playing ? std::string{"\xE2\x8F\xB8"}            // ⏸
                                 : std::string{"\xE2\x96\xB6"},            // ▶
                    Style{}.with_fg(palette::accent()).with_bold()));
                continue;
            }
            const auto cell = detail::video_note::cell_for(
                c, r, kCols, kRows, frame_c);
            cells.push_back(text(std::string{cell.utf8},
                Style{}.with_fg(cell.fg)));
        }
        rows.push_back(hstack().gap(0)
            .width(Dimension::fixed(kCols))
            .height(Dimension::fixed(1))
            .grow(0).shrink(0)
            (cells));
    }

    auto circle = vstack().gap(0)
        .width(Dimension::fixed(kCols))
        .height(Dimension::fixed(kRows))
        .grow(0).shrink(0)
        (rows);

    // Clock + mute live in their own row directly under the ring —
    // legible without a colored backdrop because they sit on the
    // bubble's normal background.
    const std::string clock =
        detail::audio_note::mmss(
            std::clamp(note.progress_secs, 0,
                std::max(1, note.duration_secs)))
        + " / "
        + detail::audio_note::mmss(note.duration_secs);

    auto mute_badge = note.muted
        ? text(std::string{"\xF0\x9F\x94\x87"},                            // 🔇
            Style{}.with_fg(palette::muted()).with_bold())
        : text(std::string{"\xF0\x9F\x94\x88"},                            // 🔈
            Style{}.with_fg(palette::accent()).with_bold());

    auto meta_row = hstack().gap(1).align_items(Align::Center)(
        text(clock, Style{}.with_fg(palette::text())),
        mute_badge);

    // Below the circle, a thin transport row — same audio-only
    // waveform we use for voice notes, plus an italic "audio mode"
    // hint. This gives the user the same scrubbable affordance Telegram
    // has on the expanded video-note view.
    model::AudioNoteVM proxy{};
    proxy.file_path     = note.file_path;
    proxy.duration_secs = note.duration_secs;
    proxy.progress_secs = note.progress_secs;
    proxy.playing       = note.playing;
    proxy.waveform      = note.waveform;
    proxy.unread        = note.unread;
    proxy.playback_speed = note.playback_speed;

    auto wave_row = render_audio_note(proxy, std::max(8, bar_count));
    auto hint_row = text(std::string{"video note \xC2\xB7 audio only in terminal"},
        Style{}.with_fg(palette::muted()).with_italic());

    return vstack().gap(0).align_items(Align::Start)(
        std::move(circle),
        std::move(meta_row),
        std::move(wave_row),
        std::move(hint_row));
}

}  // namespace tl::views

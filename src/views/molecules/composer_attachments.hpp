#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Row of attachment chips that sits above the composer input when the
// user has queued files / images / voice notes / etc. Each chip is a
// rounded card with a kind glyph + name + size + a tiny ✕ remove button.
// The shell hit-tests the ✕ via mouse helpers in app/mouse.hpp.

namespace detail::composer_attach {

[[nodiscard]] inline std::string_view glyph_for(
    model::ComposerVM::AttachmentKind k) noexcept
{
    using K = model::ComposerVM::AttachmentKind;
    switch (k) {
        case K::Photo:  return "\xF0\x9F\x93\xB7";   // 📷
        case K::Voice:  return "\xF0\x9F\x8E\x99";   // 🎙
        case K::Video:  return "\xF0\x9F\x8E\xAC";   // 🎬
        case K::File:
        default:        return "\xF0\x9F\x93\x8E";   // 📎
    }
}

[[nodiscard]] inline std::string format_size(std::size_t bytes) noexcept
{
    constexpr double kKB = 1024.0;
    constexpr double kMB = 1024.0 * 1024.0;
    char buf[24];
    if (bytes == 0)                                 return {};
    if (bytes >= static_cast<std::size_t>(kMB)) {
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

[[nodiscard]] inline std::string format_dur(int secs) noexcept
{
    if (secs <= 0) return {};
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
    return std::string{buf};
}

}  // namespace detail::composer_attach

[[nodiscard]] inline maya::Element render_attachment_chip(
    const model::ComposerVM::Attachment& a, bool focused = false)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::composer_attach;

    auto glyph = text(std::string{glyph_for(a.kind)},
        Style{}.with_fg(palette::accent()).with_bold());
    auto name = text(a.label.empty() ? std::string{"attachment"} : a.label,
        Style{}.with_fg(palette::text()).with_bold());
    std::string meta;
    if (a.kind == model::ComposerVM::AttachmentKind::Voice
     || a.kind == model::ComposerVM::AttachmentKind::Video) {
        meta = format_dur(a.duration_secs);
    } else {
        meta = format_size(a.size_bytes);
    }
    auto meta_el = meta.empty()
        ? text(std::string{})
        : text(meta, Style{}.with_fg(palette::dim()));
    auto remove = text(std::string{"\xE2\x9C\x95"},                       // ✕
        Style{}.with_fg(focused ? palette::accent() : palette::muted())
               .with_bold());

    return hstack().gap(1).padding(0, 1).align_items(Align::Center)
        .border(BorderStyle::Round, palette::border_idle())
        (glyph, name, meta_el, text(std::string{" "}), remove);
}

[[nodiscard]] inline maya::Element render_attachment_strip(
    std::span<const model::ComposerVM::Attachment> attachments)
{
    using namespace maya;
    using namespace maya::dsl;
    if (attachments.empty()) return text(std::string{});

    std::vector<Element> chips;
    chips.reserve(attachments.size() * 2);
    for (std::size_t i = 0; i < attachments.size(); ++i) {
        chips.push_back(render_attachment_chip(attachments[i]));
        if (i + 1 < attachments.size()) chips.push_back(text(std::string{" "}));
    }
    // Tight padding(0,1) so the chips don't kiss the composer's left rail.
    return hstack().gap(0).padding(0, 1).align_items(Align::Center)(chips);
}

// ─── Voice recording bar ─────────────────────────────────────────────────────
//
//   ●  REC   0:04   ▁▂▄▇▆▅▃▂▁▁▂▃▄  ⏹ stop   ✕ cancel
//   └ pulse ┘└────── live waveform ──────────┘└── actions ──┘
//
// Replaces the input row while recording. The pulse dot blinks via
// `caret_visible` (same clock everything else uses) so the entire UI's
// heartbeat is in phase. Waveform is a rolling sample buffer maintained
// on the model via Tick.
[[nodiscard]] inline maya::Element render_recording_bar(
    const model::ComposerVM& c)
{
    using namespace maya;
    using namespace maya::dsl;
    using namespace detail::composer_attach;

    // Pulse dot. accent ● while caret_visible, muted ○ otherwise.
    auto pulse = c.caret_visible
        ? text(std::string{"\xE2\x97\x8F"},                                // ●
            Style{}.with_fg(palette::red()).with_bold())
        : text(std::string{"\xE2\x97\x8B"},                                // ○
            Style{}.with_fg(palette::red()).with_bold());

    auto rec_label = text(std::string{"REC"},
        Style{}.with_fg(palette::red()).with_bold());

    auto timer = text(format_dur(c.recording_secs),
        Style{}.with_fg(palette::text()).with_bold());

    // Live waveform — last ~24 samples, padded with ▁ when the buffer
    // hasn't filled yet.
    static constexpr const char* kBars[8] = {
        "\xE2\x96\x81", "\xE2\x96\x82", "\xE2\x96\x83", "\xE2\x96\x84",
        "\xE2\x96\x85", "\xE2\x96\x86", "\xE2\x96\x87", "\xE2\x96\x88",
    };
    std::string wave;
    constexpr int kWaveCells = 24;
    const int n = static_cast<int>(c.recording_waveform.size());
    for (int i = 0; i < kWaveCells; ++i) {
        const int src = n - kWaveCells + i;
        std::uint8_t amp = (src >= 0)
            ? c.recording_waveform[static_cast<std::size_t>(src)]
            : std::uint8_t{16};
        wave += kBars[std::clamp(amp / 32, 0, 7)];
    }
    auto wave_el = text(std::move(wave),
        Style{}.with_fg(palette::accent()).with_bold());

    auto stop_btn = text(std::string{"\xE2\x8F\xB9 send"},                  // ⏹ send
        Style{}.with_fg(palette::accent()).with_bold());
    auto cancel_btn = text(std::string{"\xE2\x9C\x95 cancel"},              // ✕ cancel
        Style{}.with_fg(palette::muted()).with_bold());

    return hstack().gap(1).padding(0, 1).align_items(Align::Center)
        .width(Dimension::percent(100))
        (
            pulse,
            rec_label,
            timer,
            wave_el,
            spacer(),
            stop_btn,
            text(std::string{"  "}),
            cancel_btn
        );
}

}  // namespace tl::views

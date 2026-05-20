#pragma once

#include <algorithm>
#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/icon_button.hpp"
#include "views/glyphs.hpp"
#include "views/molecules/composer_attachments.hpp"
#include "views/molecules/composer_input.hpp"
#include "views/molecules/reply_preview.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Composer card. The card flexes between several shapes:
//
//   ┌────────────────────────────────────────────────────────────────┐
//   │ ▎ reply preview                                              ✕ │   (when replying)
//   ├────────────────────────────────────────────────────────────────┤
//   │ ╭ chip ╮ ╭ chip ╮ ╭ chip ╮                                     │   (when attachments)
//   ├────────────────────────────────────────────────────────────────┤
//   │ ▎ 😊  ❯ first line of message                       42/4096 📎 │
//   │     ❯ second line if user hit Alt-Enter                      ⏎ │
//   └────────────────────────────────────────────────────────────────┘
//
// During voice recording the input row is replaced by a recording bar:
//   │ ●  REC   0:04   ▁▂▄▇▆▅▃▂▁▁▂▃▄                ⏹ send   ✕ cancel │
//
// composer_card_height(c, w) returns the total row count the
// conversation pane needs to reserve. Components:
//   1 row  reply preview            (when c.reply_quote)
//   1 row  attachments strip        (when !c.attachments.empty)
//   N rows multi-line input         (1..kMaxInputRows)
//   1 row  recording bar            (replaces input when c.recording)

enum class ComposerDensity : unsigned char { Full, Compact, Minimal };

// Maximum visible body rows before scrolling kicks in. Keep this in
// double digits and the conversation pane will start eating messages;
// 6 lets you write a small paragraph without becoming a vim buffer.
inline constexpr int kComposerMaxInputRows = 6;

[[nodiscard]] inline int composer_card_height(
    const model::ComposerVM& c, int viewport_w = 60) noexcept
{
    int h = 0;
    if (c.reply_quote.has_value())    ++h;
    if (!c.attachments.empty())       h += 3;   // bordered chip strip = 3 rows
    if (c.recording) {
        ++h;                                    // recording replaces input
    } else {
        h += composer_visible_lines(c, viewport_w, kComposerMaxInputRows);
    }
    return std::max(1, h);
}

// Back-compat single-arg shim — earlier callers pass the VM alone and
// expect a sensible default. Used by header_bar's neighbours that
// don't know the panel's inner width.
[[nodiscard]] inline int composer_card_height(
    const model::ComposerVM& c) noexcept
{
    return composer_card_height(c, 60);
}

namespace detail::composer_bar {

[[nodiscard]] inline maya::Element focus_rail(bool focused, int rows)
{
    using namespace maya;
    using namespace maya::dsl;
    // A column of rail glyphs the same height as the input so the rail
    // grows with the input. Looks like a single tall accent stripe.
    std::vector<Element> cells;
    cells.reserve(static_cast<std::size_t>(std::max(1, rows)));
    const auto sty = Style{}
        .with_fg(focused ? palette::accent() : palette::muted())
        .with_bold();
    for (int i = 0; i < std::max(1, rows); ++i) {
        cells.push_back(text(focused ? std::string{"\xE2\x96\x8E"}      // ▎
                                     : std::string{" "}, sty));
    }
    return vstack().gap(0)(cells);
}

}  // namespace detail::composer_bar

[[nodiscard]] inline maya::Element render_composer_bar(
    const model::ComposerVM& c,
    bool focused,
    ComposerDensity density = ComposerDensity::Full,
    int viewport_w = 60)
{
    using namespace maya;
    using namespace maya::dsl;

    // ─── Recording mode short-circuits everything else ───────────────────
    if (c.recording) {
        return render_recording_bar(c);
    }

    // Char counter only appears once the user is within ~80% full —
    // empty state shouldn't display 0/4096 as visual noise.
    const int len   = static_cast<int>(c.text.size());
    const bool show_count = len > c.char_limit * 4 / 5;
    const auto count_str  = show_count
        ? std::to_string(len) + "/" + std::to_string(c.char_limit)
        : std::string{};
    const auto count_sty  = (len >= c.char_limit - 16)
        ? Style{}.with_fg(palette::red()).with_bold()
        : Style{}.with_fg(palette::amber());

    // Right button: 🎤 when there's nothing to send (no text + no
    // attachments), otherwise ⏎. The 🎤 click starts a recording.
    const bool can_send = !c.text.empty() || !c.attachments.empty();
    auto right_btn = can_send
        ? render_icon_button("\xE2\x8F\x8E", "",                           // ⏎
            focused ? ButtonState::Active : ButtonState::Idle)
        : render_icon_button("\xF0\x9F\x8E\xA4", "",                       // 🎤
            ButtonState::Idle);

    const int body_rows = composer_visible_lines(c, viewport_w, kComposerMaxInputRows);
    auto rail = detail::composer_bar::focus_rail(focused, body_rows);

    Element input_row;
    if (density == ComposerDensity::Minimal) {
        input_row = hstack()
            .width(Dimension::percent(100))
            .padding(0, 1)
            .align_items(Align::Start)
            (rail,
             text(std::string{" "}),
             render_composer_input(c, focused, viewport_w),
             spacer(),
             right_btn);
    } else if (density == ComposerDensity::Compact) {
        input_row = hstack()
            .width(Dimension::percent(100))
            .padding(0, 1)
            .align_items(Align::Start)
            (rail,
             text(std::string{" "}),
             render_composer_input(c, focused, viewport_w),
             spacer(),
             show_count ? text(count_str, count_sty) : text(std::string{}),
             show_count ? text(std::string{"  "}) : text(std::string{}),
             right_btn);
    } else {
        auto emoji_btn  = render_icon_button("\xF0\x9F\x98\x8A", "",        // 😊
            focused ? ButtonState::Hover : ButtonState::Idle);
        auto attach_btn = render_icon_button("\xF0\x9F\x93\x8E", "",        // 📎
            ButtonState::Idle);

        input_row = hstack()
            .width(Dimension::percent(100))
            .padding(0, 1)
            .align_items(Align::Start)
            (
                rail,
                text(std::string{" "}),
                emoji_btn,
                text(std::string{" "}),
                render_composer_input(c, focused, viewport_w),
                spacer(),
                show_count ? text(count_str, count_sty) : text(std::string{}),
                show_count ? text(std::string{"  "}) : text(std::string{}),
                attach_btn,
                text(std::string{" "}),
                right_btn
            );
    }

    // Compose the stacked rows. Order: reply preview → attachments
    // strip → input. Anything that's not currently active just isn't
    // emitted so composer_card_height matches what we render.
    std::vector<Element> rows;
    rows.reserve(4);
    if (c.reply_quote.has_value()) {
        rows.push_back(render_reply_preview(*c.reply_quote));
    }
    if (!c.attachments.empty()) {
        rows.push_back(render_attachment_strip(
            std::span<const model::ComposerVM::Attachment>{c.attachments}));
    }
    rows.push_back(std::move(input_row));

    if (rows.size() == 1) return std::move(rows.front());
    return vstack().gap(0).width(Dimension::percent(100))(rows);
}

}  // namespace tl::views

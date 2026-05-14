#pragma once

#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/icon_button.hpp"
#include "views/glyphs.hpp"
#include "views/molecules/composer_input.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Single-row composer. Telegram-mobile shape: emoji on the left, input
// fills the middle, then char-count, attach, send/mic on the right. The
// shell pins this to height 1 with overflow Hidden.
//
// Focus is a first-class concern: when `focused` is true the composer
// shows an accent left-rail (▎), an accent prompt and caret, so the user
// can see at a glance "typing lands here". When not focused everything
// drops to muted/dim — the chat list's search input is the typing
// target instead.
//
// Responsive density:
//   Full     — emoji + count + attach + send + focus rail
//   Compact  — drops emoji + attach + count, keeps focus rail + input + send
//   Minimal  — focus rail + input + send

enum class ComposerDensity : unsigned char { Full, Compact, Minimal };

namespace detail::composer_bar {

[[nodiscard]] inline maya::Element focus_rail(bool focused)
{
    using namespace maya;
    using namespace maya::dsl;
    return text(focused ? std::string{"▎"} : std::string{" "},
        Style{}.with_fg(focused ? palette::accent() : palette::muted())
               .with_bold());
}

}  // namespace detail::composer_bar

[[nodiscard]] inline maya::Element render_composer_bar(
    const model::ComposerVM& c,
    bool focused,
    ComposerDensity density = ComposerDensity::Full)
{
    using namespace maya;
    using namespace maya::dsl;

    // Char counter only appears once the user is within the "approaching
    // limit" zone (~80% full) — empty state shouldn't display 0/4096 as
    // visual noise. Color steps up from dim → amber → red as you near
    // the cliff.
    const int len   = static_cast<int>(c.text.size());
    const bool show_count = len > c.char_limit * 4 / 5;
    const auto count_str  = show_count
        ? std::to_string(len) + "/" + std::to_string(c.char_limit)
        : std::string{};
    const auto count_sty  = (len >= c.char_limit - 16)
        ? Style{}.with_fg(palette::red()).with_bold()
        : Style{}.with_fg(palette::amber());

    auto right_btn = c.text.empty()
        ? render_icon_button("🎤", "", ButtonState::Idle)
        : render_icon_button("⏎", "", focused ? ButtonState::Active : ButtonState::Idle);

    auto rail = detail::composer_bar::focus_rail(focused);

    if (density == ComposerDensity::Minimal) {
        return hstack()
            .width(Dimension::percent(100))
            .padding(0, 1)
            .align_items(Align::Center)
            (rail,
             text(std::string{" "}),
             render_composer_input(c, focused),
             spacer(),
             right_btn);
    }

    if (density == ComposerDensity::Compact) {
        return hstack()
            .width(Dimension::percent(100))
            .padding(0, 1)
            .align_items(Align::Center)
            (rail,
             text(std::string{" "}),
             render_composer_input(c, focused),
             spacer(),
             show_count ? text(count_str, count_sty) : text(std::string{}),
             show_count ? text(std::string{"  "}) : text(std::string{}),
             right_btn);
    }

    auto emoji_btn  = render_icon_button("😊", "",
        focused ? ButtonState::Hover : ButtonState::Idle);
    auto attach_btn = render_icon_button("📎", "", ButtonState::Idle);

    return hstack()
        .width(Dimension::percent(100))
        .padding(0, 1)
        .align_items(Align::Center)
        (
            rail,
            text(std::string{" "}),
            emoji_btn,
            text(std::string{" "}),
            render_composer_input(c, focused),
            spacer(),
            show_count ? text(count_str, count_sty) : text(std::string{}),
            show_count ? text(std::string{"  "}) : text(std::string{}),
            attach_btn,
            text(std::string{" "}),
            right_btn
        );
}

}  // namespace tl::views

#pragma once

// Login overlay rendered when AppModel.auth.stage != LoggedIn. Three-
// state form (phone → code → password) reusing brand_logo + a bordered
// input row + a key-hint footer. Mirrors how render_help_overlay /
// render_jumper_overlay compose: a vstack inside a centered card.

#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "util/debug_log.hpp"
#include "views/atoms/brand_logo.hpp"
#include "views/atoms/spinner.hpp"
#include "views/theme.hpp"

namespace tl::views {

namespace detail::auth {

[[nodiscard]] inline std::string_view stage_title(model::AuthStage s) noexcept
{
    using S = model::AuthStage;
    switch (s) {
        case S::Connecting:    return "connecting to telegram…";
        case S::WaitPhone:     return "sign in to telegram";
        case S::WaitCode:      return "enter the code you received";
        case S::WaitPassword:  return "enter your two-step password";
        case S::LoggedIn:      return "signed in";
        case S::LoggedOut:     return "signed out";
    }
    return {};
}

[[nodiscard]] inline std::string_view field_placeholder(model::AuthField f) noexcept
{
    switch (f) {
        case model::AuthField::Phone:    return "+1 555 555 0100";
        case model::AuthField::Code:     return "12345";
        case model::AuthField::Password: return "••••••••";
    }
    return {};
}

[[nodiscard]] inline std::string_view field_label(model::AuthField f) noexcept
{
    switch (f) {
        case model::AuthField::Phone:    return "phone";
        case model::AuthField::Code:     return "code";
        case model::AuthField::Password: return "password";
    }
    return {};
}

[[nodiscard]] inline const std::string&
active_buffer(const model::AuthVM& a) noexcept
{
    switch (a.active_field) {
        case model::AuthField::Phone:    return a.phone;
        case model::AuthField::Code:     return a.code;
        case model::AuthField::Password: return a.password;
    }
    return a.phone;
}

// Render the active buffer as visible text. Passwords mask to bullets;
// codes show as-is so the user can verify against the sms.
[[nodiscard]] inline std::string render_buffer(const model::AuthVM& a)
{
    const auto& buf = active_buffer(a);
    if (a.active_field != model::AuthField::Password) return buf;
    // Bullet per UTF-8 codepoint, not per byte, so a password with
    // accented chars masks 1:1 instead of 1:N.
    std::string out;
    out.reserve(buf.size() * 3);
    for (std::size_t i = 0; i < buf.size(); ) {
        const auto b0 = static_cast<unsigned char>(buf[i]);
        std::size_t n = 1;
        if      ((b0 & 0xE0u) == 0xC0u) n = 2;
        else if ((b0 & 0xF0u) == 0xE0u) n = 3;
        else if ((b0 & 0xF8u) == 0xF0u) n = 4;
        if (i + n > buf.size()) n = buf.size() - i;
        out += "\xE2\x80\xA2";   // U+2022 •
        i += n;
    }
    return out;
}

}  // namespace detail::auth

[[nodiscard]] inline maya::Element render_auth_overlay(
    const model::AuthVM& a, int tick, bool caret_visible)
{
    using namespace maya;
    using namespace maya::dsl;
    namespace D = detail::auth;

    const auto stage = a.stage;
    const bool show_input = stage == model::AuthStage::WaitPhone
                         || stage == model::AuthStage::WaitCode
                         || stage == model::AuthStage::WaitPassword;
    TL_DLOG("ui-auth", "render stage=%d field=%d show_input=%d submitting=%d",
            static_cast<int>(stage),
            static_cast<int>(a.active_field),
            show_input ? 1 : 0,
            a.submitting ? 1 : 0);

    // ─── Header: brand logo + stage title ─────────────────────────────
    auto logo = render_brand_logo(48, caret_visible);
    auto title = text(std::string{D::stage_title(stage)},
        Style{}.with_fg(palette::text()).with_bold());

    // ─── Status line: hint (or spinner during Connecting / submitting) ─
    Element status;
    if (stage == model::AuthStage::Connecting || a.submitting) {
        status = hstack().gap(1)
            (render_spinner(tick),
             text(a.hint.empty() ? std::string{"please wait…"} : a.hint,
                  Style{}.with_fg(palette::muted())));
    } else if (!a.hint.empty()) {
        status = text(a.hint,
            Style{}.with_fg(palette::muted()).with_italic());
    } else {
        status = text(std::string{});
    }

    // ─── Input row (bordered, focused tint) ───────────────────────────
    Element input_row = text(std::string{});
    if (show_input) {
        const auto buf = D::render_buffer(a);
        const auto placeholder = D::field_placeholder(a.active_field);
        const bool empty = buf.empty();
        const auto label_st = Style{}.with_fg(palette::muted());
        const auto caret_st = Style{}.with_fg(palette::accent()).with_bold();
        const auto value_st = Style{}.with_fg(palette::text());
        const auto ph_st    = Style{}.with_fg(palette::dim()).with_italic();

        auto label = text(std::string{D::field_label(a.active_field)} + ": ",
                          label_st);
        auto value = empty
            ? text(std::string{placeholder}, ph_st)
            : text(buf, value_st);
        auto caret = (caret_visible && !a.submitting)
            ? text(std::string{"█"}, caret_st)
            : text(std::string{" "});

        input_row = hstack()
            .border(BorderStyle::Round, palette::border_focus())
            .padding(0, 1)
            .gap(1)
            .align_items(Align::Center)
            .width(Dimension::fixed(46))
            (label, value, caret, spacer());
    }

    // ─── Footer: key hints ────────────────────────────────────────────
    auto hint_row = hstack().gap(2).justify(Justify::Center)
        (text(std::string{"⏎ submit"}, Style{}.with_fg(palette::muted())),
         text(std::string{"⌫ delete"}, Style{}.with_fg(palette::muted())),
         text(std::string{"^C quit"},   Style{}.with_fg(palette::muted())));

    // ─── Compose the card ─────────────────────────────────────────────
    auto card = vstack()
        .align_items(Align::Center)
        .gap(1)
        .padding(2, 4)
        .border(BorderStyle::Round, palette::border_idle())
        .width(Dimension::fixed(56))
        (
            logo,
            title,
            input_row,
            status,
            hint_row
        );

    // Caller centers (see shell.hpp), matching jumper/help overlays.
    return card;
}

}  // namespace tl::views

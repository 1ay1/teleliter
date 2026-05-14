#pragma once

#include <cctype>
#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "views/atoms/image.hpp"
#include "views/util/image_load.hpp"
#include "views/theme.hpp"

namespace tl::views {

// 4-cell square holding two uppercase letters on a tinted background. One
// of the few places where a forced bg fill is allowed (memory rule: avatars
// need strong contrast against any terminal theme).
//
// When an image path is supplied AND the file loads successfully, the
// initials block is replaced by a half-block raster of the image at the
// same 4-cell width (rendering 2 rows tall — the only avatar shape that
// expands vertically). Callers fall back transparently when no path is
// set, so existing call sites keep their single-row layout.

namespace detail::avatar {

[[nodiscard]] inline std::string two_letters(std::string_view name) noexcept
{
    std::string out;
    out.reserve(2);
    for (auto c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            if (out.size() == 2) break;
        }
    }
    if (out.empty()) out = "??";
    if (out.size() == 1) out.push_back(out.front());
    return out;
}

}  // namespace detail::avatar

[[nodiscard]] inline maya::Element render_avatar(
    std::string_view initials_in,
    maya::Color tint)
{
    using namespace maya;
    using namespace maya::dsl;

    std::string initials = initials_in.empty()
        ? std::string{"??"}
        : std::string{initials_in};
    if (initials.size() > 2) initials.resize(2);
    if (initials.size() == 1) initials.push_back(' ');

    return Element{text(" " + initials + " ",
        Style{}.with_fg(Color::black()).with_bg(tint).with_bold())};
}

[[nodiscard]] inline maya::Element render_avatar_from_name(
    std::string_view name,
    maya::Color tint)
{
    return render_avatar(detail::avatar::two_letters(name), tint);
}

// Image-backed avatar. Renders the image at 4 cells wide × 1 row tall so
// it slots into existing chat-row layouts that expect a single-line
// avatar block. The image is sampled at 4 px × 2 px (cells × half-blocks);
// detail is lossy at this size but identity / colour still read clearly,
// matching what Telegram-web does at its compact-list density.
//
// Falls back to render_avatar(initials, tint) when the image fails to
// decode (file missing, unsupported format, …) so callers never have to
// branch on "did the file load?" at call-sites.
[[nodiscard]] inline maya::Element render_avatar_image(
    std::string_view image_path,
    std::string_view initials_fallback,
    maya::Color tint_fallback)
{
    if (image_path.empty())
        return render_avatar(initials_fallback, tint_fallback);

    const auto& img = tl::views::util::load_image_cached(image_path);
    if (!img.ok())
        return render_avatar(initials_fallback, tint_fallback);

    // 4-cell width with a 1-row footprint = 4 wide × 2 tall pixel sample
    // via half-blocks. Same horizontal real-estate as the text avatar so
    // surrounding layouts don't reflow.
    return render_image(image_path, 4, 1);
}

[[nodiscard]] inline maya::Element render_avatar_from_name_or_image(
    std::string_view image_path,
    std::string_view name,
    maya::Color tint)
{
    return render_avatar_image(image_path, detail::avatar::two_letters(name), tint);
}

// ─── Sized image avatar ──────────────────────────────────────────────────────
// Renders the image at arbitrary cell dimensions. Used by chat rows that
// want a 2-row tall avatar (= 4×4 pixel sample, much sharper than 4×2),
// and by message bubbles wanting a small in-bubble author avatar.
//
// Falls back to a tinted-initials chip of the same height when no path
// is set or decode fails. The fallback is repeated cell_h times so the
// surrounding layout doesn't reflow between image-loaded and
// image-missing states.

namespace detail::avatar {

[[nodiscard]] inline maya::Element padded_initials_block(
    std::string_view initials_in,
    maya::Color tint,
    int cells_h)
{
    using namespace maya;
    using namespace maya::dsl;
    std::string initials = initials_in.empty()
        ? std::string{"??"} : std::string{initials_in};
    if (initials.size() > 2) initials.resize(2);
    if (initials.size() == 1) initials.push_back(' ');

    auto chip_style = Style{}
        .with_fg(maya::Color::black())
        .with_bg(tint)
        .with_bold();
    std::vector<Element> rows;
    rows.reserve(static_cast<std::size_t>(cells_h));
    for (int i = 0; i < cells_h; ++i) {
        if (i == cells_h / 2) {
            rows.push_back(text(" " + initials + " ", chip_style));
        } else {
            // Empty cells matching the avatar width so the column is square-ish.
            rows.push_back(text(std::string{"    "}, chip_style));
        }
    }
    return vstack().gap(0)(rows);
}

}  // namespace detail::avatar

[[nodiscard]] inline maya::Element render_avatar_image_sized(
    std::string_view image_path,
    std::string_view initials_fallback,
    maya::Color tint_fallback,
    int cells_w,
    int cells_h,
    bool circle = false)
{
    if (image_path.empty()) {
        if (cells_h == 1)
            return render_avatar(initials_fallback, tint_fallback);
        return detail::avatar::padded_initials_block(
            initials_fallback, tint_fallback, cells_h);
    }
    const auto& img = tl::views::util::load_image_cached(image_path);
    if (!img.ok()) {
        if (cells_h == 1)
            return render_avatar(initials_fallback, tint_fallback);
        return detail::avatar::padded_initials_block(
            initials_fallback, tint_fallback, cells_h);
    }
    return render_image(image_path, cells_w, cells_h,
        maya::Color::black(), circle);
}

}  // namespace tl::views

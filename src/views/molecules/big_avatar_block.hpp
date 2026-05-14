#pragma once

#include <string>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/avatar.hpp"
#include "views/atoms/bordered_avatar.hpp"
#include "views/atoms/image.hpp"
#include "views/atoms/presence_dot.hpp"
#include "views/util/image_load.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Top-of-panel hero block: bordered avatar (or big circular photo when an
// image is provided), name in bold, then a colored presence line. Mirrors
// Telegram-web's profile-header card.
//
// Avatar shape:
//   • image_path set + decodes → 12-cell × 6-row circular half-block
//     photo, ellipse-masked so the corners cut cleanly into the panel.
//   • otherwise → the existing rounded-border initials chip.

[[nodiscard]] inline maya::Element render_big_avatar_block(
    std::string_view name,
    std::string_view initials,
    maya::Color tint,
    model::Presence presence,
    std::string_view subtitle = "",
    std::string_view image_path = "")
{
    using namespace maya;
    using namespace maya::dsl;

    // Always run the initials through two_letters: uppercases, trims to
    // 2 chars, pads to 2. Means callers can pass "ma" or "" or "Mama" and
    // the chip is always a clean 2-letter cap.
    auto resolved_initials = detail::avatar::two_letters(
        initials.empty() ? name : initials);

    // Centering pattern: each row is its own width(100%) hstack. The
    // avatar row also needs an explicit height(3) — without it, the
    // hstack collapses to 1 row in the parent vstack's column flow and
    // clips the 3-row-tall bordered chip to nothing. Using spacers
    // either side instead of justify(Center) avoids any flex behaviour
    // that might also resize the chip.
    auto center_row = [](Element inner) {
        return hstack()
            .width(Dimension::percent(100))
            (spacer(), std::move(inner), spacer());
    };

    // Try the image path first. We compute hero_avatar lazily so the
    // initials chip only builds when no decoded photo is available —
    // keeps the path-empty fast path cheap.
    // 18×9 cells = 18×18 pixel sample via half-blocks. Sharper than the
    // earlier 12×6 take — heads and faces resolve clearly at this size
    // while still fitting inside the 26-cell minimum right-panel width.
    constexpr int kHeroCellsW = 18;
    constexpr int kHeroCellsH = 9;
    Element hero_avatar;
    bool image_ok = false;
    if (!image_path.empty()) {
        const auto& img = tl::views::util::load_image_cached(image_path);
        if (img.ok()) {
            hero_avatar = render_image(image_path, kHeroCellsW, kHeroCellsH,
                maya::Color::black(), /*circle=*/true);
            image_ok = true;
        }
    }
    if (!image_ok) {
        hero_avatar = render_bordered_avatar(resolved_initials, tint);
    }
    const int avatar_h = image_ok ? kHeroCellsH : 3;

    // Center the hero with justify(Center) rather than flanking spacers.
    // Flex's default cross-axis behaviour (Stretch) was forcing the hero
    // to take the full row height, and combined with .height(fixed) on
    // the hstack the descendant text()s ended up with a viewport that
    // didn't match their natural sizes — visible as an empty rectangle.
    // Natural sizing + justify(Center) keeps things simple: the row is
    // exactly as tall as the hero, and the hero anchors mid-width.
    auto avatar_row = hstack()
        .width(Dimension::percent(100))
        .justify(Justify::Center)
        .align_items(Align::Start)
        .grow(0).shrink(0)
        (std::move(hero_avatar));
    (void)avatar_h;
    auto name_row   = center_row(text(std::string{name},
        Style{}.with_fg(palette::text()).with_bold()));
    auto presence_row = center_row(hstack().gap(1)(
        render_presence_dot(presence),
        text(presence_label(presence),
            Style{}.with_fg(palette::presence_of(presence)))));

    // 1-row blank between the bordered avatar and the name; everything
    // below the name stacks tightly so name + subtitle + presence reads
    // as one identity block.
    auto blank = text(std::string{});
    if (subtitle.empty()) {
        return vstack()
            (avatar_row, blank, name_row, presence_row);
    }
    auto subtitle_row = center_row(text(std::string{subtitle},
        Style{}.with_fg(palette::muted()).with_italic()));
    return vstack()
        (avatar_row, blank, name_row, subtitle_row, presence_row);
}

[[nodiscard]] inline maya::Element render_big_avatar_block(const model::UserVM& u)
{
    return render_big_avatar_block(
        u.name,
        u.initials.empty() ? detail::avatar::two_letters(u.name) : u.initials,
        palette::tint_for(u.name),
        u.presence,
        /*subtitle=*/"",
        u.avatar_path);
}

}  // namespace tl::views

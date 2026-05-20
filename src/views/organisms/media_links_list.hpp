#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/theme.hpp"

namespace tl::views {

// ─── Media + files as openable link rows ────────────────────────────────────
//
//   📎  report.pdf
//       12.4 MB · from Ana                            xdg-open ↗
//
//   🎵  voice_2.m4a
//       0:42 · 32 kbps                                mpv ↗
//
// Two lines per item — first is the title + glyph, second is muted
// subtitle + a right-aligned "how to open" hint. Items are visual
// only (no real click action wired); the hint advertises the command
// that would open the underlying href on a desktop system.
//
// Replaces the older media_grid for the right info panel — a list reads
// better than thumbnail cells when the items are a mix of photos,
// files, voice notes, links, etc., and each item carries a real path
// or URL the user can copy.

namespace detail::media_links {

[[nodiscard]] inline maya::Element render_row(const model::MediaItemVM& item)
{
    using namespace maya;
    using namespace maya::dsl;

    auto glyph = text(
        item.kind_glyph.empty() ? std::string{"◆"} : item.kind_glyph,
        Style{}.with_fg(palette::accent()).with_bold());

    auto title = text(item.label,
        Style{}.with_fg(palette::text()).with_bold());

    auto subtitle = text(item.subtitle,
        Style{}.with_fg(palette::muted()).with_italic());

    auto open_hint = text(
        item.open_hint.empty() ? std::string{} : item.open_hint,
        Style{}.with_fg(palette::dim()));

    auto top = hstack()
        .width(Dimension::percent(100))
        .gap(1)
        .align_items(Align::Center)
        (
            glyph,
            title,
            spacer(),
            open_hint
        );

    // The second row indents under the glyph so the subtitle visually
    // belongs to the row above. Empty subtitle = collapse to a 0-width
    // text() (still consumes the row slot for predictable spacing).
    auto bottom = hstack()
        .width(Dimension::percent(100))
        .padding(0, 0, 0, 4)
        (subtitle);

    return vstack().gap(0)(top, bottom);
}

}  // namespace detail::media_links

[[nodiscard]] inline maya::Element render_media_links_list(
    std::span<const model::MediaItemVM> items)
{
    using namespace maya;
    using namespace maya::dsl;

    if (items.empty()) {
        return hstack().width(Dimension::percent(100)).justify(Justify::Center)(
            text(std::string{"— nothing shared yet —"},
                Style{}.with_fg(palette::dim()).with_italic())
        );
    }

    std::vector<Element> rows;
    rows.reserve(items.size() * 2);
    for (std::size_t i = 0; i < items.size(); ++i) {
        rows.push_back(detail::media_links::render_row(items[i]));
        if (i + 1 < items.size()) rows.push_back(text(std::string{}));
    }
    return vstack().gap(0)(rows);
}

}  // namespace tl::views

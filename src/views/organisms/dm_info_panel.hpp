#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/atoms/avatar.hpp"
#include "views/atoms/divider.hpp"
#include "views/atoms/image.hpp"
#include "views/atoms/presence_dot.hpp"
#include "views/atoms/tab.hpp"
#include "views/atoms/underline.hpp"
#include "views/molecules/info_row.hpp"
#include "views/molecules/panel_titlebar.hpp"
#include "views/molecules/toggle_row.hpp"
#include "views/organisms/media_links_list.hpp"
#include "views/util/image_load.hpp"
#include "views/theme.hpp"

namespace tl::views {

// ─── Layout constants (mirrored in app/mouse.hpp for hit-testing) ─────────────
// Heights in cell rows of each section inside the padded panel.
inline constexpr int kInfoHeroAvatarH = 8;   // hero photo / initials chip
inline constexpr int kInfoTabsRowY    = 0;   // populated dynamically; see helper

// presence_label is declared in molecules/big_avatar_block.hpp but we use
// it here too. Re-declared inline so we don't need to pull that header.
[[nodiscard]] inline std::string_view dm_presence_label(model::Presence p) noexcept
{
    switch (p) {
        case model::Presence::Active:  return "active now";
        case model::Presence::Away:    return "away";
        case model::Presence::Dnd:     return "do not disturb";
        case model::Presence::Offline: return "offline";
    }
    return "offline";
}

namespace detail::dm_info {

// ─── Hero block ─ photo (or initials chip) + name + presence ─────────────────
// Self-contained, no flex-layout pitfalls: a vstack of plain rows, each
// pre-sized. Width is whatever the panel gives us; the centered photo
// pads itself with spacers to land mid-column.
[[nodiscard]] inline maya::Element render_hero(const model::UserVM& u)
{
    using namespace maya;
    using namespace maya::dsl;

    // Resolved initials — at most 2 uppercased chars.
    auto initials = detail::avatar::two_letters(
        u.initials.empty() ? u.name : u.initials);
    auto tint     = palette::tint_for(u.name);

    // ─── Avatar ─ image if it loads, otherwise tinted initials chip ─────
    // 14 × 7 cells. The image atom returns a fixed-size element which we
    // center horizontally with spacer() on either side. Keeping the
    // dimensions modest keeps the panel readable at the 26-col minimum.
    constexpr int kCellsW = 14;
    constexpr int kCellsH = 7;

    Element avatar_el;
    bool used_image = false;
    if (!u.avatar_path.empty()) {
        const auto& img = tl::views::util::load_image_cached(u.avatar_path);
        if (img.ok()) {
            avatar_el = render_image(u.avatar_path, kCellsW, kCellsH,
                                     maya::Color::black(), /*circle=*/true);
            used_image = true;
        }
    }
    if (!used_image) {
        // Pad an initials chip to a kCellsW × kCellsH block so the panel
        // doesn't reflow when the image is missing.
        avatar_el = detail::avatar::padded_initials_block(
            initials, tint, kCellsH);
    }

    auto avatar_row = hstack()
        .width(Dimension::percent(100))
        .height(Dimension::fixed(kCellsH))
        .justify(Justify::Center)
        .align_items(Align::Center)
        .grow(0).shrink(0)
        (spacer(), std::move(avatar_el), spacer());

    auto name_row = hstack()
        .width(Dimension::percent(100))
        .justify(Justify::Center)
        (text(u.name, Style{}.with_fg(palette::text()).with_bold()));

    auto presence_row = hstack()
        .width(Dimension::percent(100))
        .justify(Justify::Center)
        .gap(1)
        (render_presence_dot(u.presence),
         text(std::string{dm_presence_label(u.presence)},
              Style{}.with_fg(palette::presence_of(u.presence))));

    return vstack().gap(0).width(Dimension::percent(100))(
        avatar_row,
        text(std::string{}),     // blank
        name_row,
        presence_row);
}

// ─── Tabs strip ──────────────────────────────────────────────────────────────
// Renders four tab labels with " · " separators plus an underline beneath
// the active one. No scroll wrapper — we always fit Media/Files/Links/Voice
// in the 24+ col panel even at the minimum width because we drop to short
// labels there.
[[nodiscard]] inline maya::Element render_tabs(int active, int viewport_w)
{
    using namespace maya;
    using namespace maya::dsl;

    // Two label sets — full when there's room, abbreviated when tight.
    // The 4×label widths + 3×separator widths need to fit in viewport_w.
    static constexpr std::array<std::string_view, 4> kFull   =
        {"Media", "Files", "Links", "Voice"};
    static constexpr std::array<std::string_view, 4> kShort  =
        {"Med", "Fil", "Lnk", "Voi"};

    const auto& labels = (viewport_w >= 30) ? kFull : kShort;

    std::vector<Element> row_children;
    row_children.reserve(labels.size() * 2 + 1);
    row_children.push_back(text(std::string{" "}));

    std::size_t underline_offset = 1;
    std::size_t underline_width  = labels[static_cast<std::size_t>(active)].size();

    for (std::size_t i = 0; i < labels.size(); ++i) {
        const bool is_active = (static_cast<int>(i) == active);
        row_children.push_back(render_tab(
            labels[i], is_active, /*badge=*/0));
        if (i + 1 < labels.size()) {
            row_children.push_back(text(std::string{" \u00b7 "},
                Style{}.with_fg(palette::dim())));
        }
        if (static_cast<int>(i) < active) {
            underline_offset += labels[i].size() + 3;   // " · " is 3 cells
        }
    }

    auto labels_row = hstack().gap(0)(row_children);
    auto underline_row = hstack().gap(0)(
        text(std::string(underline_offset, ' ')),
        render_underline(static_cast<int>(underline_width), palette::accent()));

    return vstack().gap(0)(labels_row, underline_row);
}

// Filter the seeded media items to those that belong on the active tab.
// Media   → 📷
// Files   → 📄 / 📎
// Links   → 🔗
// Voice   → 🎵 / 🎬
[[nodiscard]] inline bool item_on_tab(const model::MediaItemVM& it, int tab) noexcept
{
    const auto& g = it.kind_glyph;
    switch (tab) {
        case 0: return g == "\xF0\x9F\x93\xB7";                                       // 📷
        case 1: return g == "\xF0\x9F\x93\x84" || g == "\xF0\x9F\x93\x8E";           // 📄 / 📎
        case 2: return g == "\xF0\x9F\x94\x97";                                       // 🔗
        case 3: return g == "\xF0\x9F\x8E\xB5" || g == "\xF0\x9F\x8E\xAC";           // 🎵 / 🎬
        default: return false;
    }
}

}  // namespace detail::dm_info

// Telegram-style user info card. Order: titlebar → hero block → contact
// rows → notifications toggle → tabs (Media / Files / Links / Voice) →
// shared-content list filtered by the active tab.

[[nodiscard]] inline maya::Element render_dm_info_panel(
    const model::UserVM&     partner,
    int                      active_tab,
    bool                     notifications_on,
    int                      panel_inner_w,
    bool                     focused = false)
{
    using namespace maya;
    using namespace maya::dsl;

    (void)focused;

    std::vector<Element> rows;

    rows.push_back(render_panel_titlebar(" USER INFO"));
    rows.push_back(text(std::string{}));
    rows.push_back(detail::dm_info::render_hero(partner));
    rows.push_back(text(std::string{}));

    rows.push_back(render_info_row("\xE2\x98\x8E", "Phone", "+1 555 0100"));   // ☎
    rows.push_back(text(std::string{}));
    rows.push_back(render_info_row("@", "Username", "@" + partner.name));
    rows.push_back(text(std::string{}));
    rows.push_back(render_info_row("\xE2\x93\x98", "Bio",                       // ⓘ
        "engineer \xC2\xB7 gardener \xC2\xB7 runner"));
    rows.push_back(text(std::string{}));
    rows.push_back(render_toggle_row(
        {"\xE2\x9A\x91", "Notifications", notifications_on, "on", "off"}));    // ⚑
    rows.push_back(text(std::string{}));

    rows.push_back(detail::dm_info::render_tabs(active_tab, panel_inner_w));
    rows.push_back(text(std::string{}));

    // Full seeded media set — same items as before, just tagged by glyph
    // so we can filter per tab. Tab-active filter lives in detail.
    static const std::array<model::MediaItemVM, 8> all_media = {
        // Media (📷)
        model::MediaItemVM{"hike-sunset.jpg", "\xF0\x9F\x93\xB7",
            "1.2 MB \xC2\xB7 from Ana",     "assets/media/hike-sunset.jpg",
            "xdg-open \xE2\x86\x97"},
        model::MediaItemVM{"selfie-cafe.png", "\xF0\x9F\x93\xB7",
            "640 KB \xC2\xB7 from Ana",     "assets/media/selfie-cafe.png",
            "xdg-open \xE2\x86\x97"},
        // Files (📄 / 📎)
        model::MediaItemVM{"plan.pdf",        "\xF0\x9F\x93\x84",
            "2.1 MB \xC2\xB7 8 pages",       "assets/media/plan.pdf",
            "xdg-open \xE2\x86\x97"},
        model::MediaItemVM{"build-log.txt",   "\xF0\x9F\x93\x8E",
            "84 KB \xC2\xB7 ci snapshot",    "assets/media/build-log.txt",
            "xdg-open \xE2\x86\x97"},
        // Links (🔗)
        model::MediaItemVM{"maya.dev",        "\xF0\x9F\x94\x97",
            "https://maya.dev/docs",         "https://maya.dev/docs",
            "in browser \xE2\x86\x97"},
        model::MediaItemVM{"github.com",      "\xF0\x9F\x94\x97",
            "github.com/1ay1/maya",          "https://github.com/1ay1/maya",
            "in browser \xE2\x86\x97"},
        // Voice (🎵 / 🎬)
        model::MediaItemVM{"voice 0:42",      "\xF0\x9F\x8E\xB5",
            "voice note \xC2\xB7 32 kbps",   "assets/media/voice-1.m4a",
            "mpv \xE2\x86\x97"},
        model::MediaItemVM{"video 1:05",      "\xF0\x9F\x8E\xAC",
            "video note \xC2\xB7 720p",      "assets/media/video-1.mp4",
            "mpv \xE2\x86\x97"},
    };

    std::vector<model::MediaItemVM> filtered;
    filtered.reserve(all_media.size());
    for (const auto& it : all_media) {
        if (detail::dm_info::item_on_tab(it, active_tab)) filtered.push_back(it);
    }
    rows.push_back(render_media_links_list(
        std::span<const model::MediaItemVM>{filtered}));

    return vstack().padding(1)(rows);
}

}  // namespace tl::views

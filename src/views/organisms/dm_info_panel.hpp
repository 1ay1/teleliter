#pragma once

#include <array>
#include <span>
#include <string>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/molecules/big_avatar_block.hpp"
#include "views/molecules/info_row.hpp"
#include "views/molecules/panel_titlebar.hpp"
#include "views/molecules/tabs_row.hpp"
#include "views/molecules/toggle_row.hpp"
#include "views/organisms/media_grid.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Telegram-style user info card. Order: titlebar → big avatar block →
// contact rows → notifications toggle → tabs (Stories / Media / Files /
// …) → placeholder media grid under the active tab.

[[nodiscard]] inline maya::Element render_dm_info_panel(
    const model::UserVM& partner,
    maya::ScrollState& tabs_scroll,
    int tabs_viewport_w,
    bool focused)
{
    using namespace maya;
    using namespace maya::dsl;

    std::vector<Element> rows;

    rows.push_back(render_panel_titlebar(" USER INFO"));
    rows.push_back(text(std::string{}));
    rows.push_back(render_big_avatar_block(partner));
    rows.push_back(text(std::string{}));
    rows.push_back(render_info_row("☎", "Phone",    "+1 555 0100"));
    rows.push_back(text(std::string{}));
    rows.push_back(render_info_row("@", "Username", "@" + partner.name));
    rows.push_back(text(std::string{}));
    rows.push_back(render_info_row("ⓘ", "Bio",
        "engineer · gardener · runner"));
    rows.push_back(text(std::string{}));
    rows.push_back(render_toggle_row({"⚑", "Notifications", true, "on", "off"}));
    rows.push_back(text(std::string{}));

    static const std::array<model::TabVM, 5> tab_specs = {
        model::TabVM{"Stories"}, model::TabVM{"Media"},
        model::TabVM{"Files"},   model::TabVM{"Links"},
        model::TabVM{"Voice"},
    };
    rows.push_back(render_tabs_row(
        std::span<const model::TabVM>{tab_specs}, /*active*/ 0,
        &tabs_scroll, tabs_viewport_w));
    rows.push_back(text(std::string{}));

    static const std::array<model::MediaItemVM, 4> media = {
        model::MediaItemVM{"1:05", "◆"},
        model::MediaItemVM{"0:42", "◆"},
        model::MediaItemVM{"2:18", "◆"},
        model::MediaItemVM{"0:55", "◆"},
    };
    rows.push_back(render_media_grid(std::span<const model::MediaItemVM>{media}));

    (void)focused;
    return vstack().padding(1)(rows);
}

}  // namespace tl::views

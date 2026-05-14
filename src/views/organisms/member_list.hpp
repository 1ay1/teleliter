#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/molecules/big_avatar_block.hpp"
#include "views/molecules/member_row.hpp"
#include "views/molecules/panel_titlebar.hpp"
#include "views/molecules/section_header.hpp"
#include "views/organisms/action_menu.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Right sidebar for groups / channels. Mirrors dm_info_panel's structure
// but lists members instead of contact info, and ends with an action menu
// (invite, settings, leave).

[[nodiscard]] inline maya::Element render_member_list(
    std::span<const model::MemberVM> members,
    std::string_view channel_name,
    std::string_view channel_topic,
    bool focused,
    std::string_view channel_avatar_path = "")
{
    using namespace maya;
    using namespace maya::dsl;

    int online = 0;
    for (const auto& m : members) {
        if (m.user.presence == model::Presence::Active) ++online;
    }

    std::vector<Element> rows;
    rows.push_back(render_panel_titlebar(" — CHANNEL INFO"));
    rows.push_back(text(std::string{}));

    rows.push_back(render_big_avatar_block(
        channel_name,
        channel_name.size() >= 2 ? channel_name.substr(0, 2) : std::string_view{"##"},
        palette::tint_for(channel_name),
        model::Presence::Offline,
        channel_topic,
        channel_avatar_path));
    rows.push_back(text(std::string{}));
    rows.push_back(hstack().width(Dimension::percent(100)).justify(Justify::Center)(
        text(std::to_string(online) + " online  ·  "
             + std::to_string(members.size()) + " total",
            Style{}.with_fg(palette::dim()).with_italic())
    ));
    rows.push_back(text(std::string{}));

    rows.push_back(render_section_header("MEMBERS"));
    rows.push_back(text(std::string{}));
    for (const auto& m : members) rows.push_back(render_member_row(m));
    rows.push_back(text(std::string{}));

    static const std::array<model::ActionItemVM, 3> actions = {
        model::ActionItemVM{"+",  "invite someone",     "i", true},
        model::ActionItemVM{"⚙",  "channel settings",   "s", false},
        model::ActionItemVM{"🔍", "search members",     "/", false},
    };
    rows.push_back(render_action_menu("ACTIONS",
        std::span<const model::ActionItemVM>{actions}));
    rows.push_back(text(std::string{}));
    rows.push_back(text(std::string{" /status to change yours"},
        Style{}.with_fg(palette::dim()).with_italic()));

    (void)focused;
    // Borderless — wrapped in the shell's right-panel hstack.
    return vstack().padding(1)(rows);
}

[[nodiscard]] inline maya::Element render_member_list(
    std::span<const model::MemberVM> members,
    bool focused)
{
    return render_member_list(members, "channel", "", focused);
}

}  // namespace tl::views

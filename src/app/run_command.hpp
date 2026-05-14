#pragma once

#include <string>
#include <string_view>

#include "model/ids.hpp"
#include "model/view_models.hpp"

namespace tl::app {

// Parses and applies a slash command on the model. Returns true if the
// command was recognized (and the caller should NOT send the body as a
// normal message). Mirrors messenger.cpp's run_command at a smaller scope.

namespace detail::cmd {

[[nodiscard]] inline std::pair<std::string_view, std::string_view>
split_first_word(std::string_view body) noexcept
{
    const auto sp = body.find(' ');
    if (sp == std::string_view::npos) return {body, std::string_view{}};
    return {body.substr(0, sp), body.substr(sp + 1)};
}

inline void push_system(model::AppModel& m, std::string body)
{
    using model::MessageId;
    using model::ReadState;
    model::MessageVM sys{};
    sys.id          = MessageId{static_cast<std::int64_t>(m.messages.size() + 1)};
    sys.body        = std::move(body);
    sys.is_system   = true;
    sys.timestamp   = "now";
    sys.age_label   = "now";
    sys.read_state  = ReadState::Sent;
    m.messages.push_back(std::move(sys));
}

[[nodiscard]] inline model::Presence parse_presence(std::string_view s) noexcept
{
    using model::Presence;
    if (s == "active")  return Presence::Active;
    if (s == "away")    return Presence::Away;
    if (s == "dnd")     return Presence::Dnd;
    if (s == "offline") return Presence::Offline;
    return Presence::Active;
}

}  // namespace detail::cmd

[[nodiscard]] inline bool run_command(model::AppModel& m, std::string_view body)
{
    if (body.empty() || body.front() != '/') return false;

    const auto [cmd_with_slash, arg] = detail::cmd::split_first_word(body);
    const auto cmd = cmd_with_slash.substr(1);

    if (cmd == "clear") {
        m.messages.clear();
        return true;
    }
    if (cmd == "me") {
        model::MessageVM me{};
        me.id          = model::MessageId{static_cast<std::int64_t>(m.messages.size() + 1)};
        me.author_id   = model::UserId{1};
        me.author_name = m.self_name;
        me.body        = std::string{arg};
        me.is_action   = true;
        me.from_me     = true;
        me.timestamp   = "now";
        me.age_label   = "now";
        m.messages.push_back(std::move(me));
        return true;
    }
    if (cmd == "status") {
        m.self_presence = detail::cmd::parse_presence(arg);
        detail::cmd::push_system(m, "you are now " + std::string{arg});
        return true;
    }
    if (cmd == "topic") {
        if (m.selected_chat_index && *m.selected_chat_index < m.chats.size()) {
            m.chats[*m.selected_chat_index].topic = std::string{arg};
            detail::cmd::push_system(m, "topic set to: " + std::string{arg});
        }
        return true;
    }
    if (cmd == "who") {
        std::string list = "members: ";
        for (std::size_t i = 0; i < m.members.size(); ++i) {
            if (i > 0) list += ", ";
            list += m.members[i].user.name;
        }
        detail::cmd::push_system(m, std::move(list));
        return true;
    }
    if (cmd == "help") {
        m.help_open = true;
        return true;
    }
    if (cmd == "quit") {
        // Sentinel handled in update — push a marker the controller catches.
        detail::cmd::push_system(m, "bye — press q to exit");
        return true;
    }

    detail::cmd::push_system(m, "unknown command: " + std::string{cmd});
    return true;
}

}  // namespace tl::app

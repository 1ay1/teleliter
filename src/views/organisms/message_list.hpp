#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/molecules/empty_state.hpp"
#include "views/molecules/gap_separator.hpp"
#include "views/molecules/message_bubble.hpp"
#include "views/molecules/typing_bubble.hpp"
#include "views/molecules/typing_indicator.hpp"
#include "views/theme.hpp"

namespace tl::views {

// Self-contained widget that renders an entire chat thread from a list of
// messages. Responsibilities:
//   • picks the empty-state hint when there are no real messages
//   • auto-groups consecutive same-author bubbles (suppress repeated
//     author headers on follow-ups, telegram-style)
//   • inserts a centered "• today •" header at the top so the thread
//     always sits below a date anchor
//   • inserts a centered date separator wherever age_label suggests a
//     day boundary (best-effort — anything containing "Yest", "Apr",
//     "May", … gets its own divider)
//   • renders system / action messages with the centered notice shape
//
// The caller only supplies the messages and the available column width;
// the widget owns layout decisions inside that column.

namespace detail {

// True if `age_label` reads as "older than today" (best-effort heuristic
// over the pre-formatted labels the seed layer produces). Anything that
// is purely a duration like "12m" / "2h" / "now" is considered today.
[[nodiscard]] inline bool age_is_older(std::string_view age) noexcept
{
    if (age.empty())                            return false;
    if (age == "now")                           return false;
    // Pure duration suffix? "12m", "5h", "30s" → today.
    bool all_digits_then_unit = true;
    for (std::size_t i = 0; i < age.size(); ++i) {
        const char c = age[i];
        const bool is_digit = c >= '0' && c <= '9';
        const bool is_unit  = (i == age.size() - 1) && (c == 'm' || c == 'h'
                            || c == 's' || c == 'd');
        if (!is_digit && !is_unit) { all_digits_then_unit = false; break; }
    }
    if (all_digits_then_unit)                   return false;
    return true;
}

[[nodiscard]] inline std::string normalize_date_label(std::string_view age)
{
    if (age == "Yest")                              return "yesterday";
    if (!age.empty() && age.front() >= 'A' && age.front() <= 'Z') {
        // "Apr 5", "Mon" etc. — pass through lowercased.
        std::string out{age};
        for (char& c : out) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return out;
    }
    return std::string{age};
}

[[nodiscard]] inline maya::Element render_date_header(std::string_view label)
{
    using namespace maya;
    using namespace maya::dsl;
    return hstack().width(Dimension::percent(100)).justify(Justify::Center)(
        text(std::string{"• "} + std::string{label} + " •",
            Style{}.with_fg(palette::dim()).with_italic())
    );
}

}  // namespace detail

[[nodiscard]] inline maya::Element render_message_list(
    std::span<const model::MessageVM> messages,
    const model::TypingVM& typing,
    int column_width)
{
    using namespace maya;
    using namespace maya::dsl;

    const bool only_system = !messages.empty()
        && std::all_of(messages.begin(), messages.end(),
            [](const model::MessageVM& m) { return m.is_system; });

    if (messages.empty() || only_system) {
        constexpr std::array<std::string_view, 2> hints = {
            "no messages here yet",
            "type below to start the conversation",
        };
        return render_empty_state("◌ silence", hints);
    }

    // 1 cell of padding on each side: left = breathing room from the
    // vdiv separating chats panel; right = breathing room from the
    // scrollbar. Bubble math accounts for both.
    const int inner_w = std::max(16, column_width - 2);

    std::vector<Element> rows;
    rows.reserve(messages.size() * 2 + 4);

    // Top date anchor — always show the oldest message's day so the
    // thread starts under a label even if every message is "today".
    {
        std::string head_label = "today";
        for (const auto& m : messages) {
            if (m.is_system) continue;
            if (detail::age_is_older(m.age_label)) {
                head_label = detail::normalize_date_label(m.age_label);
            }
            break;
        }
        rows.push_back(detail::render_date_header(head_label));
    }

    std::string last_date_label;
    model::UserId prev_author{};
    bool prev_was_real_message = false;

    for (std::size_t i = 0; i < messages.size(); ++i) {
        const auto& m = messages[i];

        // Day boundary → centered date row, then reset grouping so the
        // first bubble after the divider re-emits its author header.
        if (!m.is_system && !m.is_action) {
            std::string label = detail::age_is_older(m.age_label)
                ? detail::normalize_date_label(m.age_label)
                : std::string{"today"};
            if (label != last_date_label) {
                if (!last_date_label.empty()) {
                    rows.push_back(detail::render_date_header(label));
                }
                last_date_label = std::move(label);
                prev_was_real_message = false;
            }
        }

        if (m.is_system || m.is_action) {
            rows.push_back(render_message(m, inner_w));
            prev_was_real_message = false;
            continue;
        }

        // Auto-compact: hide the author label when the previous bubble is
        // by the same person. We do this without mutating the VM by
        // building a local copy and flipping `compact`.
        model::MessageVM view = m;
        view.compact = prev_was_real_message && (m.author_id == prev_author);

        rows.push_back(render_message(view, inner_w));
        prev_author = m.author_id;
        prev_was_real_message = true;
    }

    // Append the live typing bubble (if any) at the very bottom of the
    // thread, just below the most recent message. The bubble animates
    // off the same tick as the rest of the app, so blink/dot cycles
    // stay in sync.
    if (!typing.typers.empty()) {
        rows.push_back(render_typing_bubble(
            std::span<const model::UserVM>{typing.typers},
            typing.tick,
            inner_w));
    }

    return vstack()
        .gap(0)
        .padding(0, 1, 0, 1)
        (rows);
}

}  // namespace tl::views

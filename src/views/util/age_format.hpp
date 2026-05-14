#pragma once

#include <cstdint>
#include <string>

namespace tl::views::util {

// "How long ago" formatter. Pure: maps a non-negative second count to a
// short label ("just now" / "5m" / "1h" / "2d" / "Mar 12"). Lives in
// views/util because the format choices are visual, not semantic — the
// model layer carries raw seconds.

[[nodiscard]] inline std::string fmt_age(std::int64_t seconds_ago)
{
    if (seconds_ago < 0) seconds_ago = 0;
    if (seconds_ago < 5)           return "just now";
    if (seconds_ago < 60)          return std::to_string(seconds_ago) + "s";
    if (seconds_ago < 3600)        return std::to_string(seconds_ago / 60) + "m";
    if (seconds_ago < 86400)       return std::to_string(seconds_ago / 3600) + "h";
    if (seconds_ago < 7 * 86400)   return std::to_string(seconds_ago / 86400) + "d";
    if (seconds_ago < 30 * 86400)  return std::to_string(seconds_ago / (7 * 86400)) + "w";
    return std::to_string(seconds_ago / (30 * 86400)) + "mo";
}

// "5m later" style label for gap separators between messages.
[[nodiscard]] inline std::string fmt_gap(std::int64_t gap_seconds)
{
    if (gap_seconds < 60)    return std::to_string(gap_seconds) + "s later";
    if (gap_seconds < 3600)  return std::to_string(gap_seconds / 60) + "m later";
    if (gap_seconds < 86400) return std::to_string(gap_seconds / 3600) + "h later";
    return std::to_string(gap_seconds / 86400) + "d later";
}

}  // namespace tl::views::util

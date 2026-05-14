#pragma once

#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>

#include <maya/maya.hpp>

#include "views/theme.hpp"

namespace tl::views {

// One-cell-wide formatted clock atom. Caller passes either a pre-formatted
// string ("12:34" / "01:23:45") or a raw second count and lets the helper
// format it. Rendered in dim color so it never competes with the brand bar.

[[nodiscard]] inline std::string format_clock(std::int64_t seconds)
{
    if (seconds < 0) seconds = 0;
    char buf[32];
    if (seconds >= 3600) {
        std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld",
            static_cast<long long>(seconds / 3600),
            static_cast<long long>((seconds / 60) % 60),
            static_cast<long long>(seconds % 60));
    } else {
        std::snprintf(buf, sizeof buf, "%lld:%02lld",
            static_cast<long long>(seconds / 60),
            static_cast<long long>(seconds % 60));
    }
    return std::string{buf};
}

[[nodiscard]] inline maya::Element render_clock(std::string_view text_in)
{
    using namespace maya;
    using namespace maya::dsl;
    return text(std::string{text_in}, Style{}.with_fg(palette::dim()));
}

[[nodiscard]] inline maya::Element render_clock(std::int64_t seconds)
{
    return render_clock(format_clock(seconds));
}

}  // namespace tl::views

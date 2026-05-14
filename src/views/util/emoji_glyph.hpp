#pragma once

#include <string>
#include <string_view>

namespace tl::views::util {

// Tiny shortcode → emoji glyph table for messages that came in with
// :name: tokens (mirroring messenger.cpp's emoji_glyph helper). Falls
// back to the literal shortcode if the name isn't known — caller can
// trivially extend this map.

[[nodiscard]] inline std::string emoji_for(std::string_view shortcode)
{
    if (shortcode == ":smile:")    return "😊";
    if (shortcode == ":laugh:")    return "😂";
    if (shortcode == ":wink:")     return "😉";
    if (shortcode == ":cry:")      return "😢";
    if (shortcode == ":fire:")     return "🔥";
    if (shortcode == ":heart:")    return "❤️";
    if (shortcode == ":rocket:")   return "🚀";
    if (shortcode == ":thumbsup:" || shortcode == ":+1:") return "👍";
    if (shortcode == ":thumbsdown:" || shortcode == ":-1:") return "👎";
    if (shortcode == ":check:")    return "✅";
    if (shortcode == ":cross:")    return "❌";
    if (shortcode == ":eyes:")     return "👀";
    if (shortcode == ":wave:")     return "👋";
    if (shortcode == ":thinking:") return "🤔";
    if (shortcode == ":party:")    return "🎉";
    return std::string{shortcode};
}

// Replaces every `:token:` substring in `body` with its emoji glyph. Pure,
// allocates one string. O(n·m) but n and m are small for chat messages.
[[nodiscard]] inline std::string render_inline(std::string_view body)
{
    std::string out;
    out.reserve(body.size());
    std::size_t i = 0;
    while (i < body.size()) {
        if (body[i] == ':') {
            const auto end = body.find(':', i + 1);
            if (end != std::string_view::npos && end - i <= 24) {
                auto code = body.substr(i, end - i + 1);
                auto glyph = emoji_for(code);
                if (glyph != code) {
                    out.append(glyph);
                    i = end + 1;
                    continue;
                }
            }
        }
        out.push_back(body[i++]);
    }
    return out;
}

}  // namespace tl::views::util

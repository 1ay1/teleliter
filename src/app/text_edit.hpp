#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tl::app::text_edit {

// Cursor-aware utf-8 helpers. All operate on byte offsets into a string
// (the composer holds utf-8 in std::string). Pure functions — no globals.

[[nodiscard]] inline std::size_t utf8_prev(std::string_view s, std::size_t pos) noexcept
{
    if (pos == 0) return 0;
    std::size_t p = pos - 1;
    while (p > 0 && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) --p;
    return p;
}

[[nodiscard]] inline std::size_t utf8_next(std::string_view s, std::size_t pos) noexcept
{
    if (pos >= s.size()) return s.size();
    std::size_t p = pos + 1;
    while (p < s.size() && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) ++p;
    return p;
}

[[nodiscard]] inline std::size_t prev_word(std::string_view s, std::size_t pos) noexcept
{
    std::size_t p = pos;
    while (p > 0 && std::isspace(static_cast<unsigned char>(s[p - 1]))) --p;
    while (p > 0 && !std::isspace(static_cast<unsigned char>(s[p - 1]))) --p;
    return p;
}

[[nodiscard]] inline std::string encode_utf8(char32_t cp) noexcept
{
    std::string s;
    if (cp < 0x80) {
        s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x110000) {
        s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}

}  // namespace tl::app::text_edit

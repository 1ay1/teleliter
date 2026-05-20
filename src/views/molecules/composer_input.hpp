#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <maya/maya.hpp>

#include "model/view_models.hpp"
#include "views/glyphs.hpp"
#include "views/theme.hpp"

namespace tl::views {

// ─── Multi-line composer text field ──────────────────────────────────────────
//
// Renders the composer body as one row per logical or wrapped line, with
// a blinking caret painted at the user's cursor position. No surrounding
// chrome (the composer_bar adds the border + buttons).
//
//   Logical lines  → split on '\n' (Alt-Enter / Shift-Enter inserts)
//   Soft-wrapped   → lines longer than `viewport_w` get broken at
//                    `viewport_w` byte boundaries. ASCII-clean; emoji
//                    runs will be approximated.
//
// Caller passes `viewport_w` (how many cells the text area can paint
// into); the wrap routine respects it. The number of visible rows the
// caller should reserve is exposed via `composer_visible_lines(c, w, max)`.

namespace detail::composer {

// Split `text` on '\n' and (optionally) soft-wrap each segment to
// `viewport_w` byte-cells. Returns a vector of pieces; each piece keeps
// its byte offset back into the source so the caret can be positioned
// even when the cursor is exactly on a wrap boundary.
struct Slice {
    std::size_t begin;   // byte offset in source
    std::size_t end;     // exclusive
};

[[nodiscard]] inline std::vector<Slice> split_lines(
    std::string_view text, int viewport_w)
{
    std::vector<Slice> out;
    if (text.empty()) {
        out.push_back({0, 0});
        return out;
    }
    if (viewport_w < 1) viewport_w = 1;

    std::size_t lo = 0;
    while (lo <= text.size()) {
        // Find the next hard newline.
        const auto nl = text.find('\n', lo);
        const std::size_t hard_end =
            (nl == std::string_view::npos) ? text.size() : nl;

        // Soft-wrap the [lo, hard_end) span into viewport_w chunks.
        std::size_t seg_lo = lo;
        while (true) {
            const std::size_t remaining = hard_end - seg_lo;
            if (remaining <= static_cast<std::size_t>(viewport_w)) {
                out.push_back({seg_lo, hard_end});
                break;
            }
            out.push_back({seg_lo,
                seg_lo + static_cast<std::size_t>(viewport_w)});
            seg_lo += static_cast<std::size_t>(viewport_w);
        }

        if (nl == std::string_view::npos) break;
        lo = hard_end + 1;     // skip the '\n'
        // A trailing newline produces an empty final line so the caret
        // can sit on it.
        if (lo == text.size()) {
            out.push_back({lo, lo});
            break;
        }
    }
    return out;
}

// Find the slice that contains the caret. If the caret sits exactly at
// a slice boundary we prefer the END of the earlier slice unless that
// slice ends with a hard newline (in which case the caret jumps to the
// start of the next slice).
[[nodiscard]] inline std::size_t locate_caret_line(
    std::string_view text,
    const std::vector<Slice>& lines,
    std::size_t cursor_bytes) noexcept
{
    if (lines.empty()) return 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& s = lines[i];
        if (cursor_bytes < s.end) return i;
        if (cursor_bytes == s.end) {
            // Edge case: at the end of a line that ends with '\n' the
            // caret really lives at the start of the next line.
            if (s.end < text.size() && text[s.end] == '\n'
             && i + 1 < lines.size()) {
                return i + 1;
            }
            return i;
        }
    }
    return lines.size() - 1;
}

}  // namespace detail::composer

// Number of rows the composer body will occupy at the given viewport
// width, clamped to [1, max_rows]. Used by composer_card_height so the
// surrounding flex can reserve the right amount of space.
[[nodiscard]] inline int composer_visible_lines(
    const model::ComposerVM& c, int viewport_w, int max_rows = 6) noexcept
{
    if (c.text.empty()) return 1;
    const auto lines = detail::composer::split_lines(c.text, viewport_w);
    const int n = static_cast<int>(lines.size());
    return std::clamp(n, 1, std::max(1, max_rows));
}

[[nodiscard]] inline maya::Element render_composer_input(
    const model::ComposerVM& c,
    bool focused,
    int viewport_w = 60)
{
    using namespace maya;
    using namespace maya::dsl;

    const bool empty       = c.text.empty();
    const bool is_command  = !empty && c.text.front() == '/';

    const bool show_caret = focused && c.caret_visible;
    const auto caret_sty   = Style{}.with_fg(palette::accent()).with_bold();
    const auto caret_el    = show_caret
        ? text(std::string{"█"}, caret_sty)
        : text(std::string{" "});

    const auto prompt_sty  = is_command
        ? Style{}.with_fg(palette::amber()).with_bold()
        : Style{}.with_fg(palette::accent()).with_bold();

    if (empty) {
        return hstack().gap(1)(
            text(std::string{glyph::prompt}, prompt_sty),
            text(std::string{"Type a message…"},
                Style{}.with_fg(palette::muted()).with_italic()),
            caret_el
        );
    }

    const auto body_sty = is_command
        ? Style{}.with_fg(palette::amber())
        : Style{}.with_fg(palette::text());

    const auto lines = detail::composer::split_lines(c.text, viewport_w);
    const std::size_t cur =
        std::min(c.cursor_bytes, c.text.size());
    const std::size_t caret_line =
        detail::composer::locate_caret_line(c.text, lines, cur);

    std::vector<Element> rows;
    rows.reserve(lines.size());

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& s = lines[i];
        std::string slice = c.text.substr(s.begin, s.end - s.begin);

        // First row carries the prompt glyph; continuation rows align
        // under the body with a 2-cell indent so they read as the same
        // logical message.
        Element prefix = (i == 0)
            ? Element{text(std::string{glyph::prompt}, prompt_sty)}
            : Element{text(std::string{"  "})};

        if (i != caret_line) {
            rows.push_back(hstack().gap(1)(
                std::move(prefix),
                text(std::move(slice), body_sty)));
            continue;
        }
        // Caret-bearing row: split at the caret byte and stitch the
        // caret in between. cursor_bytes is in source coordinates so we
        // shift it relative to the slice's start.
        const std::size_t local_cur = (cur >= s.begin && cur <= s.end)
            ? cur - s.begin
            : (cur < s.begin ? std::size_t{0} : s.end - s.begin);
        auto before = slice.substr(0, local_cur);
        auto after  = slice.substr(local_cur);
        rows.push_back(hstack().gap(1)(
            std::move(prefix),
            text(std::move(before), body_sty),
            caret_el,
            text(std::move(after), body_sty)));
    }

    return vstack().gap(0)(rows);
}

}  // namespace tl::views

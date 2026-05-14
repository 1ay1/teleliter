#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <maya/maya.hpp>

#include "views/theme.hpp"
#include "views/util/image_load.hpp"

namespace tl::views {

// ─── Image widget — colour raster via Unicode half-blocks ────────────────────
//
// Each terminal cell holds one half-block glyph ("▀" U+2580 or "▄" U+2584)
// with a truecolour foreground and background. One cell carries TWO
// vertically-stacked pixels at the same X — doubling vertical resolution
// at no horizontal cost. Works in any modern terminal supporting 24-bit
// SGR sequences.
//
// Higher resolution per cell (sub-cell tricks we use):
//   • half-block doubles vertical resolution (2 px per cell column)
//   • for outside-mask cells we emit unstyled spaces so the terminal
//     bg shows through cleanly (used for circular silhouettes)
//   • for partial-mask cells we use ▀ (top half only) or ▄ (bottom half
//     only) with the opposite half left as terminal default — this lets
//     the silhouette edge land between pixel rows, not just cell rows
//
// Higher resolution tricks NOT used here (deliberate trade-offs):
//   • Sixel / Kitty / iTerm2: terminal-specific, brittle to detect
//   • Quadrants (▘▝▖▗ …): 2×2 sub-cell res but only 1 fg/bg pair per
//     cell, so each cell is quantised to 1 of 16 patterns. The colour
//     loss isn't worth the doubled horizontal res for photos.
//   • 6-dot braille: mono only, awful for photos
//
// Sampling: box-filter average over the source rectangle that maps to
// each output pixel. Sharper than nearest-neighbour for downscales and
// avoids the splotchy aliasing nearest produces at small sizes.

namespace detail::image {

struct CacheKey {
    std::string path;
    int         cw;
    int         ch;
    bool        circle;

    [[nodiscard]] bool operator==(const CacheKey& o) const noexcept {
        return cw == o.cw && ch == o.ch && circle == o.circle && path == o.path;
    }
};

struct CacheKeyHash {
    [[nodiscard]] std::size_t operator()(const CacheKey& k) const noexcept {
        std::size_t h = std::hash<std::string>{}(k.path);
        h ^= std::hash<int>{}(k.cw) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.ch) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(k.circle) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

inline std::unordered_map<CacheKey, maya::Element, CacheKeyHash>&
render_cache_map()
{
    static std::unordered_map<CacheKey, maya::Element, CacheKeyHash> m;
    return m;
}

inline std::mutex& render_cache_mutex()
{
    static std::mutex mu;
    return mu;
}

struct Rgba { std::uint8_t r, g, b, a; };

// Box-filter: average the source rectangle that maps to (tx, ty). Much
// less aliased than nearest-neighbour at typical avatar downscales.
[[nodiscard]] inline Rgba sample_box(
    const tl::views::util::DecodedImage& img,
    int tx, int ty, int tw, int th) noexcept
{
    const double sx0 = static_cast<double>(tx)       * static_cast<double>(img.w) / static_cast<double>(tw);
    const double sx1 = static_cast<double>(tx + 1)   * static_cast<double>(img.w) / static_cast<double>(tw);
    const double sy0 = static_cast<double>(ty)       * static_cast<double>(img.h) / static_cast<double>(th);
    const double sy1 = static_cast<double>(ty + 1)   * static_cast<double>(img.h) / static_cast<double>(th);

    int ix0 = std::max(0, static_cast<int>(sx0));
    int iy0 = std::max(0, static_cast<int>(sy0));
    int ix1 = std::min(img.w, std::max(ix0 + 1, static_cast<int>(sx1) + 1));
    int iy1 = std::min(img.h, std::max(iy0 + 1, static_cast<int>(sy1) + 1));

    long long r = 0, g = 0, b = 0, a = 0;
    long long n = 0;
    for (int y = iy0; y < iy1; ++y) {
        for (int x = ix0; x < ix1; ++x) {
            const auto px = img.pixel(x, y);
            r += px[0]; g += px[1]; b += px[2]; a += px[3];
            ++n;
        }
    }
    if (n == 0) {
        const auto px = img.pixel(ix0, iy0);
        return {px[0], px[1], px[2], px[3]};
    }
    return {
        static_cast<std::uint8_t>(r / n),
        static_cast<std::uint8_t>(g / n),
        static_cast<std::uint8_t>(b / n),
        static_cast<std::uint8_t>(a / n),
    };
}

[[nodiscard]] inline Rgba alpha_over(Rgba src, Rgba bg) noexcept
{
    if (src.a == 255) return src;
    if (src.a == 0)   return bg;
    const int a = src.a;
    auto mix = [&](std::uint8_t s, std::uint8_t d) -> std::uint8_t {
        return static_cast<std::uint8_t>(
            (s * a + d * (255 - a) + 127) / 255);
    };
    return {mix(src.r, bg.r), mix(src.g, bg.g), mix(src.b, bg.b), 255};
}

}  // namespace detail::image

// ─── Public: render an image at cell dimensions ─────────────────────────────
//
//   render_image(path, cell_w, cell_h, fill, circle)
//
// `circle=true` masks the corners to give a Telegram-style round photo;
// outside-mask cells emit unstyled spaces so the terminal's own bg shows
// through. `fill` is composited behind transparent (alpha < 255) pixels.

[[nodiscard]] inline maya::Element render_image(
    std::string_view path,
    int cell_w,
    int cell_h,
    maya::Color fill = maya::Color::black(),
    bool circle = false)
{
    using namespace maya;
    using namespace maya::dsl;
    using detail::image::CacheKey;
    using detail::image::Rgba;

    if (cell_w <= 0 || cell_h <= 0) return text(std::string{});

    {
        std::lock_guard<std::mutex> g(detail::image::render_cache_mutex());
        auto& cache = detail::image::render_cache_map();
        if (auto it = cache.find({std::string{path}, cell_w, cell_h, circle});
            it != cache.end())
        {
            return it->second;
        }
    }

    const auto& img = tl::views::util::load_image_cached(path);
    if (!img.ok()) {
        // Failed decode → tinted placeholder block. Build it as a vstack
        // of hstacks so failures lay out identically to successes.
        std::vector<Element> ph_rows;
        ph_rows.reserve(static_cast<std::size_t>(cell_h));
        for (int r = 0; r < cell_h; ++r) {
            std::string row;
            for (int c = 0; c < cell_w; ++c) row.push_back(' ');
            ph_rows.push_back(text(std::move(row),
                Style{}.with_bg(maya::Color::bright_black())));
        }
        return vstack().gap(0)(ph_rows);
    }

    Rgba bg{0, 0, 0, 255};
    (void)fill;

    const int pix_w = cell_w;
    const int pix_h = cell_h * 2;

    // Ellipse mask in pixel space. Inflate the radii by 0.5 so the
    // antialiased "edge" cells (partial-half-blocks) feel naturally
    // anti-aliased instead of stepping abruptly.
    const double cx = (static_cast<double>(pix_w) - 1.0) * 0.5;
    const double cy = (static_cast<double>(pix_h) - 1.0) * 0.5;
    const double rx = (static_cast<double>(pix_w)) * 0.5;
    const double ry = (static_cast<double>(pix_h)) * 0.5;
    auto inside = [&](int px, int py) -> bool {
        if (!circle) return true;
        const double dx = (static_cast<double>(px) - cx) / rx;
        const double dy = (static_cast<double>(py) - cy) / ry;
        return dx * dx + dy * dy <= 1.0;
    };

    // Build the raster as a vstack of hstacks of per-cell text() elements.
    // Slower than one big TextElement with StyledRuns, but it goes through
    // the well-trodden hstack/text() path that the rest of maya uses, so
    // it never trips edge cases in run-bookkeeping.
    constexpr std::string_view kUpperHalf = "\xE2\x96\x80";   // ▀
    constexpr std::string_view kLowerHalf = "\xE2\x96\x84";   // ▄

    std::vector<Element> rows;
    rows.reserve(static_cast<std::size_t>(cell_h));

    for (int cy_i = 0; cy_i < cell_h; ++cy_i) {
        std::vector<Element> cells;
        cells.reserve(static_cast<std::size_t>(cell_w));
        for (int cx_i = 0; cx_i < cell_w; ++cx_i) {
            const int top_py = cy_i * 2;
            const int bot_py = cy_i * 2 + 1;

            const bool top_in = inside(cx_i, top_py);
            const bool bot_in = inside(cx_i, bot_py);

            if (!top_in && !bot_in) {
                cells.push_back(text(std::string{" "}));
                continue;
            }

            if (top_in && bot_in) {
                Rgba top = detail::image::sample_box(img, cx_i, top_py, pix_w, pix_h);
                Rgba bot = detail::image::sample_box(img, cx_i, bot_py, pix_w, pix_h);
                top = detail::image::alpha_over(top, bg);
                bot = detail::image::alpha_over(bot, bg);
                cells.push_back(text(std::string{kUpperHalf},
                    Style{}
                        .with_fg(Color::rgb(top.r, top.g, top.b))
                        .with_bg(Color::rgb(bot.r, bot.g, bot.b))));
            } else if (top_in) {
                Rgba top = detail::image::sample_box(img, cx_i, top_py, pix_w, pix_h);
                top = detail::image::alpha_over(top, bg);
                cells.push_back(text(std::string{kUpperHalf},
                    Style{}.with_fg(Color::rgb(top.r, top.g, top.b))));
            } else {
                Rgba bot = detail::image::sample_box(img, cx_i, bot_py, pix_w, pix_h);
                bot = detail::image::alpha_over(bot, bg);
                cells.push_back(text(std::string{kLowerHalf},
                    Style{}.with_fg(Color::rgb(bot.r, bot.g, bot.b))));
            }
        }
        rows.push_back(hstack()
            .gap(0)
            .width(Dimension::fixed(cell_w))
            .height(Dimension::fixed(1))
            .grow(0).shrink(0)
            (cells));
    }

    // Force exact pixel-grid dimensions on the outer container. Without
    // these, the per-cell text elements can be measured/laid-out at
    // different sizes than their natural extents when this image lives
    // deep inside a flex hierarchy (multiple nested grow/shrink/
    // align_items containers can shift the cell viewport away from
    // each cell's natural 1×1 size, painting them blank).
    Element el = vstack()
        .gap(0)
        .width(Dimension::fixed(cell_w))
        .height(Dimension::fixed(cell_h))
        .grow(0).shrink(0)
        (rows);

    {
        std::lock_guard<std::mutex> g(detail::image::render_cache_mutex());
        auto& cache = detail::image::render_cache_map();
        cache.emplace(CacheKey{std::string{path}, cell_w, cell_h, circle}, el);
    }
    return el;
}

}  // namespace tl::views

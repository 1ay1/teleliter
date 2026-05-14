#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Forward declarations from stb_image — we don't pull the full header into
// every view that wants an image, only into the one TU that actually calls
// the loader. Same for the resizer.
extern "C" {
    unsigned char* stbi_load(const char* filename, int* x, int* y,
                             int* channels_in_file, int desired_channels);
    void           stbi_image_free(void* retval_from_stbi_load);
    const char*    stbi_failure_reason(void);
}

namespace tl::views::util {

// ─── DecodedImage: a flat RGBA pixel buffer with a stable size ──────────────

struct DecodedImage {
    int                  w = 0;
    int                  h = 0;
    std::vector<std::uint8_t> rgba;   // size == 4 * w * h (R,G,B,A interleaved)

    [[nodiscard]] bool ok() const noexcept { return w > 0 && h > 0; }
    [[nodiscard]] std::array<std::uint8_t, 4> pixel(int x, int y) const noexcept {
        if (x < 0 || y < 0 || x >= w || y >= h) return {0, 0, 0, 0};
        const std::size_t base = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w)
                                + static_cast<std::size_t>(x)) * 4;
        return {rgba[base + 0], rgba[base + 1], rgba[base + 2], rgba[base + 3]};
    }
};

// ─── Process-wide decode cache ──────────────────────────────────────────────
// Each path is decoded at most once. Caller keeps a reference (path), the
// cache owns the pixels. Lookups are thread-safe under a single mutex —
// we're nowhere near a hot inner loop here so this is the right trade-off.

namespace detail {

inline std::unordered_map<std::string, DecodedImage>& image_cache_map()
{
    static std::unordered_map<std::string, DecodedImage> m;
    return m;
}

inline std::mutex& image_cache_mutex()
{
    static std::mutex mu;
    return mu;
}

}  // namespace detail

namespace detail {

// Try a few candidate locations so relative paths work whether the user
// runs the binary from the project root, from build/, or from build/Debug.
// The candidates are checked in order; the first decode that returns
// pixels wins. Absolute paths skip the search and try the original.
[[nodiscard]] inline std::vector<std::string>
candidate_paths(std::string_view raw)
{
    std::vector<std::string> out;
    out.emplace_back(raw);
    if (!raw.empty() && raw.front() == '/') return out;   // absolute → no search
    out.push_back(std::string{"../"}    + std::string{raw});
    out.push_back(std::string{"../../"} + std::string{raw});
    return out;
}

}  // namespace detail

// Returns a stable const reference to a DecodedImage. On decode failure
// (file missing, unsupported format, …) the returned image has w == h == 0
// — callers should check ok() before reading pixels. A negative-cache
// entry is stored so we don't re-attempt decoding every frame.
[[nodiscard]] inline const DecodedImage& load_image_cached(std::string_view path)
{
    std::string key{path};
    {
        std::lock_guard<std::mutex> g(detail::image_cache_mutex());
        auto& cache = detail::image_cache_map();
        if (auto it = cache.find(key); it != cache.end()) return it->second;
    }

    DecodedImage out;
    for (const auto& candidate : detail::candidate_paths(path)) {
        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load(candidate.c_str(), &w, &h, &ch, 4);
        if (px != nullptr && w > 0 && h > 0) {
            out.w = w;
            out.h = h;
            const std::size_t bytes = static_cast<std::size_t>(w)
                                    * static_cast<std::size_t>(h) * 4u;
            out.rgba.assign(px, px + bytes);
            stbi_image_free(px);
            break;
        }
    }
    // Failure paths leave out.{w,h} == 0; the negative entry caches the
    // miss so we don't hit the disk again every frame.

    std::lock_guard<std::mutex> g(detail::image_cache_mutex());
    auto& cache = detail::image_cache_map();
    auto [it, inserted] = cache.emplace(std::move(key), std::move(out));
    return it->second;
}

}  // namespace tl::views::util

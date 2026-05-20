#pragma once

// Central debug log. Append-only, line-buffered, mutex-guarded.
//
// Activation: TELELITER_DEBUG_LOG=1                → ~/.teleliter/debug.log
//             TELELITER_DEBUG_LOG=/path/to/file    → that path
//             unset / "0" / ""                      → all calls are no-ops
//
// The TUI owns stdout/stderr and any printf would corrupt the frame, so
// the only safe sink is a file. Subsystems tag their lines with a short
// prefix ("td", "ui", "key", "ev"...) so `tail -F` is greppable.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace tl::dlog {

namespace detail {

inline std::mutex& sink_mutex() {
    static std::mutex m;
    return m;
}

inline std::FILE*& sink_file() {
    static std::FILE* fp = nullptr;
    return fp;
}

inline bool& sink_initialised() {
    static bool b = false;
    return b;
}

inline void open_sink_once() {
    if (sink_initialised()) return;
    sink_initialised() = true;
    const char* env = std::getenv("TELELITER_DEBUG_LOG");
    if (!env || !*env || std::strcmp(env, "0") == 0) return;

    std::string path;
    if (env[0] == '/' || env[0] == '.' || env[0] == '~') {
        path = env;
    } else {
        const char* home = std::getenv("HOME");
        path = home ? std::string{home} + "/.teleliter/debug.log"
                    : std::string{"/tmp/teleliter-debug.log"};
        // Best-effort mkdir of the parent. mkdir -p without errno noise.
        if (home) {
            std::string dir = std::string{home} + "/.teleliter";
            (void)std::system(("mkdir -p " + dir + " 2>/dev/null").c_str());
        }
    }
    sink_file() = std::fopen(path.c_str(), "a");
    if (sink_file()) {
        std::setvbuf(sink_file(), nullptr, _IOLBF, 0);
    }
}

inline std::string timestamp() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto t   = clock::to_time_t(now);
    const auto us  = std::chrono::duration_cast<std::chrono::microseconds>(
                         now.time_since_epoch()).count() % 1'000'000;
    std::tm tm{};
    ::localtime_r(&t, &tm);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%06ld",
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<long>(us));
    return buf;
}

}  // namespace detail

[[nodiscard]] inline bool enabled() {
    detail::open_sink_once();
    return detail::sink_file() != nullptr;
}

inline void write_line(std::string_view tag, std::string_view body) {
    if (!enabled()) return;
    std::lock_guard lk{detail::sink_mutex()};
    auto* fp = detail::sink_file();
    if (!fp) return;
    std::fprintf(fp, "[%s] %.*s: %.*s\n",
                 detail::timestamp().c_str(),
                 static_cast<int>(tag.size()), tag.data(),
                 static_cast<int>(body.size()), body.data());
}

// printf-style. Cheap-bails when disabled so call sites are zero-cost.
inline void logf(std::string_view tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

inline void logf(std::string_view tag, const char* fmt, ...) {
    if (!enabled()) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write_line(tag, buf);
}

}  // namespace tl::dlog

#define TL_DLOG(tag, ...) ::tl::dlog::logf((tag), __VA_ARGS__)

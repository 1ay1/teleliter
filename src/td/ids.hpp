#pragma once

// Strong-typed wrappers around TDLib's int64 IDs. The intent is that
// the rest of the codebase ONLY sees these types, never raw `int64_t`,
// so the compiler refuses to confuse a chat id with a user id with a
// message id — same Tagged<U,Tag> trick model/ids.hpp uses, kept in a
// separate namespace so the two ID spaces don't accidentally unify.
//
// Conversion to/from the public model:: ids is explicit on purpose:
// the boundary is exactly the four free functions at the bottom of
// this header. Anywhere `to_model(...)` / `to_td(...)` is called is
// the place where wire data crosses into the app's domain types.

#include <compare>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

#include "model/ids.hpp"

namespace tl::td {

template <typename Underlying, typename Tag>
class Tagged {
public:
    using underlying_type = Underlying;
    using tag_type        = Tag;

    constexpr Tagged() noexcept = default;
    constexpr explicit Tagged(Underlying v)
        noexcept(std::is_nothrow_move_constructible_v<Underlying>)
        : value_(std::move(v)) {}

    [[nodiscard]] constexpr const Underlying& get() const noexcept { return value_; }
    [[nodiscard]] constexpr Underlying&       get()       noexcept { return value_; }

    friend constexpr auto operator<=>(const Tagged&, const Tagged&) noexcept = default;

private:
    Underlying value_{};
};

struct TdChatTag;
struct TdUserTag;
struct TdMessageTag;
struct TdFileTag;

using TdChatId    = Tagged<std::int64_t, TdChatTag>;
using TdUserId    = Tagged<std::int64_t, TdUserTag>;
// TDLib message ids are int53 but transit as int64; we keep the full width.
using TdMessageId = Tagged<std::int64_t, TdMessageTag>;
// TDLib file ids are int32 (download identifiers). Kept as int64 here so
// every TD id has identical storage and we never have to think about
// integer-narrowing at the boundary.
using TdFileId    = Tagged<std::int64_t, TdFileTag>;

// ─── Boundary conversions ────────────────────────────────────────────────
// Intentionally explicit, not implicit. Calling these is the documented
// way to cross between the wire layer (td::) and the app's model (model::).

[[nodiscard]] constexpr model::ChatId to_model(TdChatId id) noexcept {
    return model::ChatId{id.get()};
}
[[nodiscard]] constexpr model::UserId to_model(TdUserId id) noexcept {
    return model::UserId{id.get()};
}
[[nodiscard]] constexpr model::MessageId to_model(TdMessageId id) noexcept {
    return model::MessageId{id.get()};
}

[[nodiscard]] constexpr TdChatId    to_td(model::ChatId id) noexcept    { return TdChatId{id.get()}; }
[[nodiscard]] constexpr TdUserId    to_td(model::UserId id) noexcept    { return TdUserId{id.get()}; }
[[nodiscard]] constexpr TdMessageId to_td(model::MessageId id) noexcept { return TdMessageId{id.get()}; }

}  // namespace tl::td

template <typename U, typename Tag>
struct std::hash<tl::td::Tagged<U, Tag>> {
    [[nodiscard]] std::size_t operator()(const tl::td::Tagged<U, Tag>& t) const noexcept {
        return std::hash<U>{}(t.get());
    }
};

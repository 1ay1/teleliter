#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace tl::model {

template <typename Underlying, typename Tag>
class Tagged {
public:
    using underlying_type = Underlying;
    using tag_type        = Tag;

    constexpr Tagged() noexcept = default;
    constexpr explicit Tagged(Underlying v) noexcept(std::is_nothrow_move_constructible_v<Underlying>)
        : value_(std::move(v)) {}

    [[nodiscard]] constexpr const Underlying& get() const noexcept { return value_; }
    [[nodiscard]] constexpr Underlying&       get()       noexcept { return value_; }

    friend constexpr auto operator<=>(const Tagged&, const Tagged&) noexcept = default;

private:
    Underlying value_{};
};

struct ChatTag;
struct UserTag;
struct MessageTag;

using ChatId    = Tagged<std::int64_t, ChatTag>;
using UserId    = Tagged<std::int64_t, UserTag>;
using MessageId = Tagged<std::int64_t, MessageTag>;

}  // namespace tl::model

template <typename U, typename Tag>
struct std::hash<tl::model::Tagged<U, Tag>> {
    [[nodiscard]] std::size_t operator()(const tl::model::Tagged<U, Tag>& t) const noexcept {
        return std::hash<U>{}(t.get());
    }
};

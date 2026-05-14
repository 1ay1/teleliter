#pragma once

#include <expected>
#include <optional>
#include <string>

namespace tl::model {

template <typename T, typename E>
using Result = std::expected<T, E>;

template <typename T>
using Option = std::optional<T>;

struct Error {
    std::string message;
};

template <typename T>
using AppResult = Result<T, Error>;

}  // namespace tl::model

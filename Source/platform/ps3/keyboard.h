#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace devilution {

// Opens the native PS3 system keyboard and returns entered UTF-8 text.
// maxUtf8Bytes is the destination field's byte limit. numericOnly requests
// the numeric keypad. Canceling the keyboard returns nullopt.
std::optional<std::string> PS3GetTextInput(
    std::string_view guideText,
    std::string_view initialText,
    std::size_t maxUtf8Bytes,
    bool numericOnly = false);

} // namespace devilution

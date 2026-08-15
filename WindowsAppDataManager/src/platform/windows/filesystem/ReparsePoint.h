#pragma once

#include <filesystem>

namespace wam::platform::windows {

[[nodiscard]] bool isReparsePoint(const std::filesystem::path &path) noexcept;

} // namespace wam::platform::windows

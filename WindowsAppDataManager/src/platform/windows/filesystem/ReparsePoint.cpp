#include "ReparsePoint.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace wam::platform::windows {

bool isReparsePoint(const std::filesystem::path &path) noexcept
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
            && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code error;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, error));
#endif
}

} // namespace wam::platform::windows

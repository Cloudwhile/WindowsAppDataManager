#include "WindowAppearance.h"

#include <QWindow>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <dwmapi.h>
#include <windows.h>
#endif

namespace wam::platform::windows {

void setDarkTitleBar(QWindow *window, bool dark)
{
#ifdef Q_OS_WIN
    if (!window)
        return;

    const HWND handle = reinterpret_cast<HWND>(window->winId());
    const BOOL enabled = dark ? TRUE : FALSE;
    constexpr DWORD immersiveDarkMode = 20;
    constexpr DWORD immersiveDarkModeBefore20H1 = 19;
    HRESULT result = DwmSetWindowAttribute(
            handle, immersiveDarkMode, &enabled, sizeof(enabled));
    if (FAILED(result)) {
        DwmSetWindowAttribute(
                handle, immersiveDarkModeBefore20H1,
                &enabled, sizeof(enabled));
    }

    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
                         | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
    Q_UNUSED(window)
    Q_UNUSED(dark)
#endif
}

} // namespace wam::platform::windows

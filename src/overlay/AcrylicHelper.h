#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <cstdint>

#pragma comment(lib, "dwmapi.lib")

namespace VirtualOverlay {

// Backdrop type for Windows 11
enum class SystemBackdropType {
    None = 0,
    Mica = 2,
    Acrylic = 3,
    MicaAlt = 4
};

// Helper for applying acrylic/blur effects to windows
class AcrylicHelper {
public:
    // Apply acrylic blur to a window
    // Returns true if effect was applied successfully
    static bool ApplyAcrylic(HWND hwnd, uint32_t tintColor, float opacity);

    // Apply mica effect (Windows 11 only)
    static bool ApplyMica(HWND hwnd);

    // Remove backdrop effect
    static bool RemoveBackdrop(HWND hwnd);

    // Check if running Windows 11 22H2+
    static bool IsWindows11_22H2OrLater();

    // Check if running Windows 11
    static bool IsWindows11();

    // Check if running Windows 10
    static bool IsWindows10();

    // Extend frame into client area (required for blur)
    static bool ExtendFrameIntoClientArea(HWND hwnd);

    // Enable dark mode for window
    static bool SetDarkMode(HWND hwnd, bool enable);

private:
    // Windows 11 22H2+ approach (documented)
    static bool ApplySystemBackdrop(HWND hwnd, SystemBackdropType type);

    // Windows 10/11 21H2 approach (undocumented SetWindowCompositionAttribute)
    static bool ApplyAccentPolicy(HWND hwnd, uint32_t tintColor, float opacity);

    // Get Windows build number
    static DWORD GetWindowsBuild();
};

}  // namespace VirtualOverlay

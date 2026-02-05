#include "AcrylicHelper.h"
#include "../utils/Logger.h"

namespace VirtualOverlay {

// Undocumented structures for SetWindowCompositionAttribute
struct ACCENT_POLICY {
    int AccentState;
    int AccentFlags;
    DWORD GradientColor;
    int AnimationId;
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    int Attribute;
    PVOID Data;
    SIZE_T SizeOfData;
};

// Accent states
constexpr int ACCENT_DISABLED = 0;
constexpr int ACCENT_ENABLE_BLURBEHIND = 3;
constexpr int ACCENT_ENABLE_ACRYLICBLURBEHIND = 4;

// Window composition attributes
constexpr int WCA_ACCENT_POLICY = 19;
constexpr int WCA_USEDARKMODECOLORS = 26;

// DWM attributes (some not in old headers)
constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 = 19;
constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
constexpr DWORD DWMWA_SYSTEMBACKDROP_TYPE = 38;

// Function pointer for undocumented API
typedef BOOL(WINAPI* SetWindowCompositionAttributeFn)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

static SetWindowCompositionAttributeFn GetSetWindowCompositionAttribute() {
    static SetWindowCompositionAttributeFn fn = nullptr;
    static bool loaded = false;

    if (!loaded) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            fn = reinterpret_cast<SetWindowCompositionAttributeFn>(
                GetProcAddress(user32, "SetWindowCompositionAttribute")
            );
        }
        loaded = true;
    }

    return fn;
}

DWORD AcrylicHelper::GetWindowsBuild() {
    static DWORD build = 0;
    if (build == 0) {
        OSVERSIONINFOEXW osvi = { sizeof(osvi) };
        typedef LONG(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
        
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
            if (fn) {
                fn(reinterpret_cast<PRTL_OSVERSIONINFOW>(&osvi));
                build = osvi.dwBuildNumber;
            }
        }
    }
    return build;
}

bool AcrylicHelper::IsWindows11_22H2OrLater() {
    return GetWindowsBuild() >= 22621;
}

bool AcrylicHelper::IsWindows11() {
    return GetWindowsBuild() >= 22000;
}

bool AcrylicHelper::IsWindows10() {
    DWORD build = GetWindowsBuild();
    return build >= 17134 && build < 22000;  // 1803 to before Win11
}

bool AcrylicHelper::ExtendFrameIntoClientArea(HWND hwnd) {
    MARGINS margins = { -1, -1, -1, -1 };  // Extend to entire window
    HRESULT hr = DwmExtendFrameIntoClientArea(hwnd, &margins);
    if (FAILED(hr)) {
        LOG_WARN("DwmExtendFrameIntoClientArea failed: 0x%08X", hr);
        return false;
    }
    return true;
}

bool AcrylicHelper::SetDarkMode(HWND hwnd, bool enable) {
    BOOL value = enable ? TRUE : FALSE;
    
    // Try new attribute first (20H1+)
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
    if (SUCCEEDED(hr)) {
        return true;
    }

    // Fall back to older attribute
    hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &value, sizeof(value));
    return SUCCEEDED(hr);
}

bool AcrylicHelper::ApplyAcrylic(HWND hwnd, uint32_t tintColor, float opacity) {
    if (!hwnd) return false;

    // Try Windows 11 22H2+ approach first
    if (IsWindows11_22H2OrLater()) {
        if (ApplySystemBackdrop(hwnd, SystemBackdropType::Acrylic)) {
            ExtendFrameIntoClientArea(hwnd);
            SetDarkMode(hwnd, true);
            return true;
        }
    }

    // Fall back to SetWindowCompositionAttribute
    return ApplyAccentPolicy(hwnd, tintColor, opacity);
}

bool AcrylicHelper::ApplyMica(HWND hwnd) {
    if (!IsWindows11()) {
        LOG_WARN("Mica effect requires Windows 11");
        return false;
    }

    if (IsWindows11_22H2OrLater()) {
        if (ApplySystemBackdrop(hwnd, SystemBackdropType::Mica)) {
            ExtendFrameIntoClientArea(hwnd);
            SetDarkMode(hwnd, true);
            return true;
        }
    }

    // Mica not available on older Windows 11 builds via this method
    LOG_WARN("Mica requires Windows 11 22H2+");
    return false;
}

bool AcrylicHelper::RemoveBackdrop(HWND hwnd) {
    if (!hwnd) return false;

    if (IsWindows11_22H2OrLater()) {
        ApplySystemBackdrop(hwnd, SystemBackdropType::None);
    }

    // Also reset accent policy
    auto fn = GetSetWindowCompositionAttribute();
    if (fn) {
        ACCENT_POLICY policy = { ACCENT_DISABLED, 0, 0, 0 };
        WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &policy, sizeof(policy) };
        fn(hwnd, &data);
    }

    return true;
}

bool AcrylicHelper::ApplySystemBackdrop(HWND hwnd, SystemBackdropType type) {
    DWORD backdropType = static_cast<DWORD>(type);
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
    
    if (FAILED(hr)) {
        LOG_DEBUG("DwmSetWindowAttribute(SYSTEMBACKDROP_TYPE) failed: 0x%08X", hr);
        return false;
    }

    return true;
}

bool AcrylicHelper::ApplyAccentPolicy(HWND hwnd, uint32_t tintColor, float opacity) {
    auto fn = GetSetWindowCompositionAttribute();
    if (!fn) {
        LOG_WARN("SetWindowCompositionAttribute not available");
        return false;
    }

    // Convert color and opacity to ARGB format
    // The color format is 0xAABBGGRR (note: BGR, not RGB)
    uint8_t alpha = static_cast<uint8_t>(opacity * 255.0f);
    uint8_t r = (tintColor >> 16) & 0xFF;
    uint8_t g = (tintColor >> 8) & 0xFF;
    uint8_t b = tintColor & 0xFF;

    DWORD gradientColor = (alpha << 24) | (b << 16) | (g << 8) | r;

    ACCENT_POLICY policy = {};
    policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    policy.AccentFlags = 2;  // Draw all borders
    policy.GradientColor = gradientColor;
    policy.AnimationId = 0;

    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attribute = WCA_ACCENT_POLICY;
    data.Data = &policy;
    data.SizeOfData = sizeof(policy);

    if (!fn(hwnd, &data)) {
        // Try blur behind as fallback (works on more systems)
        policy.AccentState = ACCENT_ENABLE_BLURBEHIND;
        if (!fn(hwnd, &data)) {
            LOG_WARN("SetWindowCompositionAttribute failed");
            return false;
        }
    }

    ExtendFrameIntoClientArea(hwnd);
    SetDarkMode(hwnd, true);
    return true;
}

}  // namespace VirtualOverlay

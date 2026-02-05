# Research: Virtual Overlay

**Date**: February 4, 2026  
**Feature**: [spec.md](spec.md) | [plan.md](plan.md)

---

## 1. Windows Magnification API

### Decision
Use `MagSetFullscreenTransform()` from the Windows Magnification API for system-wide zoom.

### Rationale
- **Official Windows API** — Designed for accessibility zoom, operates at DWM level
- **GPU-accelerated** — Smooth performance without custom rendering
- **Simple interface** — Single function call to set zoom level and offset
- **Automatic cleanup** — `MagUninitialize()` resets zoom on exit (addresses crash recovery requirement)

### Alternatives Considered
| Alternative | Rejected Because |
|-------------|------------------|
| Custom screen capture + render | High CPU/GPU, latency, complexity |
| DirectX overlay | Would obscure content rather than magnify |
| Windows Magnifier integration | Conflicts with existing magnifier, no programmatic control |

### Key Implementation Details
```cpp
#include <Magnification.h>
#pragma comment(lib, "Magnification.lib")

// Initialize once at startup
MagInitialize();

// Apply zoom (call on scroll and cursor move)
MagSetFullscreenTransform(zoomFactor, offsetX, offsetY);

// Cleanup resets zoom to 1.0x (handles crash requirement)
MagUninitialize();
```

### Constraints
- Requires Windows Vista+ (we target Windows 10 1803+, so no issue)
- Does not work on secure desktop (lock screen, UAC dialogs)
- May conflict if Windows Magnifier is already active

---

## 2. Virtual Desktop COM Interfaces

### Decision
Use undocumented `IVirtualDesktopManagerInternal` COM interface with version-specific GUID tables.

### Rationale
- **Only way to get desktop names** — Documented `IVirtualDesktopManager` only checks if window is on current desktop
- **Can register for notifications** — Essential for overlay trigger
- **Well-documented by community** — Multiple open-source projects maintain GUID tables

### Alternatives Considered
| Alternative | Rejected Because |
|-------------|------------------|
| `IVirtualDesktopManager` (documented) | Cannot get desktop names or register for switch events |
| Shell hooks / window enumeration | Doesn't detect desktop switches |
| Registry monitoring | Unreliable, not real-time |

### Key Implementation Details
```cpp
// GUIDs change between Windows builds - maintain lookup table
struct VirtualDesktopGUIDs {
    CLSID CLSID_VirtualDesktopManagerInternal;
    IID IID_IVirtualDesktopManagerInternal;
    IID IID_IVirtualDesktopNotification;
};

// Version detection
bool IsWindows11_22H2OrLater();
const VirtualDesktopGUIDs& GetGUIDsForCurrentBuild();

// Fallback: If COM fails, count desktops by index (no names)
```

### Version Support Matrix
| Windows Version | Build Range | Status |
|-----------------|-------------|--------|
| Windows 10 21H2 | 19043-19044 | Supported (tested GUIDs) |
| Windows 11 21H2 | 22000 | Supported |
| Windows 11 22H2 | 22621 | Supported |
| Windows 11 23H2 | 22631 | Supported |
| Windows 11 24H2 | 26100+ | Requires testing |

### Risks
- GUIDs may change in future Windows updates
- Mitigation: Graceful fallback to desktop index, log warning

---

## 3. Direct2D Overlay Rendering

### Decision
Use Direct2D with `ID2D1HwndRenderTarget` for overlay rendering.

### Rationale
- **Hardware accelerated** — GPU rendering for smooth animations
- **Native Windows** — No external dependencies
- **Text rendering quality** — DirectWrite integration for crisp fonts
- **Required for blur effects** — Combines with DWM backdrop

### Alternatives Considered
| Alternative | Rejected Because |
|-------------|------------------|
| GDI+ | Slower, software rendering, no blur composition |
| Raw GDI | No antialiasing, no alpha compositing |
| OpenGL/Vulkan | Overkill, portability not needed |

### Key Implementation Details
```cpp
#include <d2d1.h>
#include <dwrite.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// Factory initialization (once)
ID2D1Factory* pFactory;
D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pFactory);

// Create render target for overlay window
ID2D1HwndRenderTarget* pRenderTarget;
pFactory->CreateHwndRenderTarget(
    D2D1::RenderTargetProperties(),
    D2D1::HwndRenderTargetProperties(hwnd, size),
    &pRenderTarget
);
```

---

## 4. Windows 11 Acrylic Blur Effect

### Decision
Use `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)` for Windows 11, fall back to `SetWindowCompositionAttribute` for Windows 10.

### Rationale
- **Native system appearance** — Matches Windows 11 flyout style
- **Official API on Win11** — `DWMWA_SYSTEMBACKDROP_TYPE` is documented
- **Fallback exists** — Undocumented but stable `SetWindowCompositionAttribute` for Win10

### Implementation Details

**Windows 11 22H2+ (Documented):**
```cpp
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// DWMWA_SYSTEMBACKDROP_TYPE = 38
enum DWM_SYSTEMBACKDROP_TYPE {
    DWMSBT_TRANSIENTWINDOW = 3  // Acrylic
};

DWORD backdropType = DWMSBT_TRANSIENTWINDOW;
DwmSetWindowAttribute(hwnd, 38, &backdropType, sizeof(backdropType));

// Extend frame
MARGINS margins = {-1, -1, -1, -1};
DwmExtendFrameIntoClientArea(hwnd, &margins);
```

**Windows 10 Fallback (Undocumented):**
```cpp
typedef BOOL(WINAPI* SetWindowCompositionAttributeFn)(HWND, void*);

struct ACCENT_POLICY { int AccentState, Flags, Color, AnimationId; };
struct WINCOMPATTR { int Attr; void* Data; ULONG Size; };

ACCENT_POLICY policy = {3, 2, 0xCC000000, 0}; // ACCENT_ENABLE_BLURBEHIND
WINCOMPATTR data = {19, &policy, sizeof(policy)};

auto fn = (SetWindowCompositionAttributeFn)
    GetProcAddress(GetModuleHandle(L"user32"), "SetWindowCompositionAttribute");
fn(hwnd, &data);
```

### Risks
- `SetWindowCompositionAttribute` is undocumented (but widely used, stable since Windows 10 1803)
- Blur may be disabled by system settings → detect and use solid fallback

---

## 5. Low-Level Input Hooks

### Decision
Use `SetWindowsHookEx` with `WH_KEYBOARD_LL` and `WH_MOUSE_LL` for global input capture.

### Rationale
- **System-wide capture** — Works in all applications
- **Low latency** — Hooks execute synchronously
- **Standard approach** — Same as other system utilities

### Implementation Details
```cpp
HHOOK hKeyboardHook = SetWindowsHookEx(
    WH_KEYBOARD_LL, KeyboardProc, hInstance, 0);
    
HHOOK hMouseHook = SetWindowsHookEx(
    WH_MOUSE_LL, MouseProc, hInstance, 0);

// In hook callback - minimal processing
LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == WM_MOUSEWHEEL) {
        auto* info = (MSLLHOOKSTRUCT*)lParam;
        if (IsModifierHeld()) {
            PostMessage(hwndMain, WM_USER_ZOOM, 
                GET_WHEEL_DELTA_WPARAM(info->mouseData), 0);
            return 1; // Consume event
        }
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}
```

### Constraints
- Cannot intercept input to elevated (admin) processes from non-elevated app
- Hook must return quickly (<200ms) or Windows disconnects it
- Must not block in hook callback — use PostMessage

---

## 6. Touchpad Pinch Gesture

### Decision
Use `WM_GESTURE` with `GID_ZOOM` for touchpad pinch-to-zoom support.

### Rationale
- **Official Windows API** — Documented gesture handling
- **Precision touchpad support** — Works with modern Windows laptops

### Implementation Details
```cpp
// Register window for gestures
GESTURECONFIG gc = { GID_ZOOM, GC_ZOOM, 0 };
SetGestureConfig(hwnd, 0, 1, &gc, sizeof(GESTURECONFIG));

// Handle in WndProc
case WM_GESTURE: {
    GESTUREINFO gi = { sizeof(GESTUREINFO) };
    if (GetGestureInfo((HGESTUREINFO)lParam, &gi)) {
        if (gi.dwID == GID_ZOOM) {
            // gi.ullArguments = current distance between fingers
            // Compare to previous to determine zoom delta
        }
    }
    CloseGestureInfoHandle((HGESTUREINFO)lParam);
    break;
}
```

### Alternatives Considered
| Alternative | Rejected Because |
|-------------|------------------|
| Raw touch input (`WM_POINTER`) | More complex, would need gesture recognition |
| Third-party gesture library | Unnecessary dependency |

---

## 7. Configuration Storage

### Decision
Use JSON file with nlohmann/json header-only library.

### Rationale
- **Human readable** — Users can manually edit if needed
- **Single header** — No build complexity, no DLL
- **Industry standard** — Well-documented, easy to debug

### File Location
```
%APPDATA%\virtual-overlay\config.json
```
Portable mode: Check for `config.json` next to executable first.

### Schema Validation
Validate required fields on load, merge with defaults for missing fields.

---

## 8. Logging

### Decision
Simple file logger with daily rotation, no external dependencies.

### Implementation
```cpp
class Logger {
    void Log(Level level, const char* format, ...);
    // Levels: DEBUG, INFO, WARN, ERROR
};

// Log to: %APPDATA%\virtual-overlay\logs\virtual-overlay.log
// Rotate: Keep last 7 days, max 10MB per file
// Privacy: Never log desktop names, window titles, or user content
```

---

## Summary of Technology Decisions

| Area | Technology | Confidence |
|------|------------|------------|
| Screen zoom | Windows Magnification API | High |
| Desktop detection | Undocumented COM (with fallback) | Medium |
| Overlay rendering | Direct2D | High |
| Blur effect | DWM (Win11) / SetWindowCompositionAttribute (Win10) | High |
| Input capture | Low-level hooks | High |
| Gestures | WM_GESTURE | High |
| Configuration | JSON (nlohmann/json) | High |
| Logging | Custom file logger | High |

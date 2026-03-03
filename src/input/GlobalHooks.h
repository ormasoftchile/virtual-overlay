#pragma once

#include <windows.h>
#include <functional>

namespace VirtualOverlay {

// Mouse hook callback
using MouseCallback = std::function<bool(WPARAM wParam, MSLLHOOKSTRUCT* hookData)>;

// Dynamic mouse hook for scroll-wheel zoom capture.
// No permanent hooks are installed — the mouse hook is only active
// while the modifier key is held (detected via GetAsyncKeyState polling).
class GlobalHooks {
public:
    static GlobalHooks& Instance();

    // Dynamic mouse hook management
    bool InstallMouseHook();
    void UninstallMouseHook();
    bool IsMouseHookInstalled() const;

    // Set callback for mouse hook events
    // Return true from callback to consume the event
    void SetMouseCallback(MouseCallback callback);

private:
    GlobalHooks();
    ~GlobalHooks();
    GlobalHooks(const GlobalHooks&) = delete;
    GlobalHooks& operator=(const GlobalHooks&) = delete;

    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    HHOOK m_mouseHook = nullptr;
    MouseCallback m_mouseCallback;
};

}  // namespace VirtualOverlay

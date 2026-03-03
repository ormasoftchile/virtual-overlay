#pragma once

#include <windows.h>
#include <functional>

namespace VirtualOverlay {

// Hook event callbacks
using KeyboardCallback = std::function<bool(WPARAM wParam, KBDLLHOOKSTRUCT* hookData)>;
using MouseCallback = std::function<bool(WPARAM wParam, MSLLHOOKSTRUCT* hookData)>;

// Global low-level hooks for keyboard and mouse input
class GlobalHooks {
public:
    static GlobalHooks& Instance();

    // Install keyboard hook (mouse hook is managed dynamically)
    // mainHwnd: Window to receive hook messages
    bool Install(HWND mainHwnd);

    // Uninstall all hooks
    void Uninstall();

    // Check if keyboard hook is installed
    bool IsInstalled() const;

    // Dynamic mouse hook management - install only when needed
    // (e.g., when modifier key is held for scroll-wheel zoom)
    bool InstallMouseHook();
    void UninstallMouseHook();
    bool IsMouseHookInstalled() const;

    // Set callbacks for hook events
    // Return true from callback to consume the event (prevent further processing)
    void SetKeyboardCallback(KeyboardCallback callback);
    void SetMouseCallback(MouseCallback callback);

    // Get the main window handle (for posting messages)
    HWND GetMainWindow() const { return m_mainHwnd; }

private:
    GlobalHooks();
    ~GlobalHooks();
    GlobalHooks(const GlobalHooks&) = delete;
    GlobalHooks& operator=(const GlobalHooks&) = delete;

    // Hook procedures (static for Windows API)
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    HHOOK m_keyboardHook = nullptr;
    HHOOK m_mouseHook = nullptr;
    HWND m_mainHwnd = nullptr;

    KeyboardCallback m_keyboardCallback;
    MouseCallback m_mouseCallback;
};

}  // namespace VirtualOverlay

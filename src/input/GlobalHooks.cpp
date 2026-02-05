#include "GlobalHooks.h"
#include "../utils/Logger.h"

namespace VirtualOverlay {

// Static instance for hook callbacks (needed because hooks are static functions)
static GlobalHooks* g_hooksInstance = nullptr;

GlobalHooks& GlobalHooks::Instance() {
    static GlobalHooks instance;
    return instance;
}

GlobalHooks::GlobalHooks() {
    g_hooksInstance = this;
}

GlobalHooks::~GlobalHooks() {
    if (m_keyboardHook || m_mouseHook) {
        Uninstall();
    }
    g_hooksInstance = nullptr;
}

bool GlobalHooks::Install(HWND mainHwnd) {
    if (m_keyboardHook && m_mouseHook) {
        return true;  // Already installed
    }

    m_mainHwnd = mainHwnd;

    // Install low-level keyboard hook
    m_keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        GetModuleHandleW(nullptr),
        0
    );

    if (!m_keyboardHook) {
        DWORD error = GetLastError();
        LOG_ERROR("Failed to install keyboard hook, error: %lu", error);
        return false;
    }

    // Install low-level mouse hook
    m_mouseHook = SetWindowsHookExW(
        WH_MOUSE_LL,
        LowLevelMouseProc,
        GetModuleHandleW(nullptr),
        0
    );

    if (!m_mouseHook) {
        DWORD error = GetLastError();
        LOG_ERROR("Failed to install mouse hook, error: %lu", error);
        
        // Cleanup keyboard hook
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
        return false;
    }

    LOG_INFO("Global hooks installed");
    return true;
}

void GlobalHooks::Uninstall() {
    if (m_keyboardHook) {
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
    }

    if (m_mouseHook) {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }

    m_mainHwnd = nullptr;
    LOG_INFO("Global hooks uninstalled");
}

bool GlobalHooks::IsInstalled() const {
    return m_keyboardHook != nullptr && m_mouseHook != nullptr;
}

void GlobalHooks::SetKeyboardCallback(KeyboardCallback callback) {
    m_keyboardCallback = callback;
}

void GlobalHooks::SetMouseCallback(MouseCallback callback) {
    m_mouseCallback = callback;
}

LRESULT CALLBACK GlobalHooks::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_hooksInstance && g_hooksInstance->m_keyboardCallback) {
        KBDLLHOOKSTRUCT* hookData = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        
        // Call the callback, if it returns true, consume the event
        if (g_hooksInstance->m_keyboardCallback(wParam, hookData)) {
            return 1;  // Consume the event
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK GlobalHooks::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_hooksInstance && g_hooksInstance->m_mouseCallback) {
        MSLLHOOKSTRUCT* hookData = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        
        // Call the callback, if it returns true, consume the event
        if (g_hooksInstance->m_mouseCallback(wParam, hookData)) {
            return 1;  // Consume the event
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace VirtualOverlay

#include "GlobalHooks.h"
#include "../utils/Logger.h"

namespace VirtualOverlay {

static GlobalHooks* g_hooksInstance = nullptr;

GlobalHooks& GlobalHooks::Instance() {
    static GlobalHooks instance;
    return instance;
}

GlobalHooks::GlobalHooks() {
    g_hooksInstance = this;
}

GlobalHooks::~GlobalHooks() {
    if (m_mouseHook) {
        UninstallMouseHook();
    }
    g_hooksInstance = nullptr;
}

bool GlobalHooks::InstallMouseHook() {
    if (m_mouseHook) {
        return true;
    }

    m_mouseHook = SetWindowsHookExW(
        WH_MOUSE_LL,
        LowLevelMouseProc,
        GetModuleHandleW(nullptr),
        0
    );

    if (!m_mouseHook) {
        DWORD error = GetLastError();
        LOG_ERROR("Failed to install mouse hook, error: %lu", error);
        return false;
    }

    return true;
}

void GlobalHooks::UninstallMouseHook() {
    if (m_mouseHook) {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }
}

bool GlobalHooks::IsMouseHookInstalled() const {
    return m_mouseHook != nullptr;
}

void GlobalHooks::SetMouseCallback(MouseCallback callback) {
    m_mouseCallback = callback;
}

LRESULT CALLBACK GlobalHooks::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_hooksInstance && g_hooksInstance->m_mouseCallback) {
        MSLLHOOKSTRUCT* hookData = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        if (g_hooksInstance->m_mouseCallback(wParam, hookData)) {
            return 1;
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace VirtualOverlay

#include "InputHandler.h"
#include "GlobalHooks.h"
#include "../utils/Logger.h"

namespace VirtualOverlay {

InputHandler& InputHandler::Instance() {
    static InputHandler instance;
    return instance;
}

InputHandler::InputHandler() = default;

InputHandler::~InputHandler() {
    if (m_initialized) {
        Shutdown();
    }
}

bool InputHandler::Init(HWND mainHwnd, UINT modifierVK) {
    if (m_initialized) {
        return true;
    }

    m_mainHwnd = mainHwnd;
    m_modifierVK = modifierVK;
    m_modifierHeld = false;
    m_enabled = true;

    // No hooks installed at init. Mouse hook is installed dynamically
    // when modifier key is detected via GetAsyncKeyState polling.
    // This avoids permanent low-level hooks that cause system-wide
    // input latency on the thread's message pump.

    GlobalHooks::Instance().SetMouseCallback(
        [this](WPARAM wParam, MSLLHOOKSTRUCT* hookData) {
            return this->OnMouseEvent(wParam, hookData);
        }
    );

    m_initialized = true;
    LOG_INFO("InputHandler initialized with modifier key: %u (poll-based)", modifierVK);
    return true;
}

void InputHandler::Shutdown() {
    if (!m_initialized) {
        return;
    }

    GlobalHooks::Instance().SetMouseCallback(nullptr);
    GlobalHooks::Instance().UninstallMouseHook();

    m_modifierHeld = false;
    m_initialized = false;
    LOG_INFO("InputHandler shutdown");
}

void InputHandler::PollModifierState() {
    if (!m_enabled || !m_mainHwnd) {
        return;
    }

    bool pressed = IsModifierPressed();

    if (pressed && !m_modifierHeld) {
        m_modifierHeld = true;
        // Install mouse hook to capture scroll wheel for zoom
        GlobalHooks::Instance().InstallMouseHook();
        PostMessageW(m_mainHwnd, WM_USER_MODIFIER_DOWN, 0, 0);
    } else if (!pressed && m_modifierHeld) {
        m_modifierHeld = false;
        // Remove mouse hook - no longer needed
        GlobalHooks::Instance().UninstallMouseHook();
        PostMessageW(m_mainHwnd, WM_USER_MODIFIER_UP, 0, 0);
    }
}

bool InputHandler::IsModifierHeld() const {
    return m_modifierHeld;
}

void InputHandler::SetEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled && m_modifierHeld) {
        m_modifierHeld = false;
        GlobalHooks::Instance().UninstallMouseHook();
    }
}

bool InputHandler::IsEnabled() const {
    return m_enabled;
}

void InputHandler::SetModifierKey(UINT modifierVK) {
    m_modifierVK = modifierVK;
    m_modifierHeld = false;
    GlobalHooks::Instance().UninstallMouseHook();
}

bool InputHandler::OnMouseEvent(WPARAM wParam, MSLLHOOKSTRUCT* hookData) {
    if (!m_enabled || !hookData || !m_mainHwnd) {
        return false;
    }

    // Handle mouse wheel when modifier is held
    if (wParam == WM_MOUSEWHEEL && m_modifierHeld) {
        short wheelDelta = static_cast<short>(HIWORD(hookData->mouseData));

        if (wheelDelta > 0) {
            PostMessageW(m_mainHwnd, WM_USER_ZOOM_OUT, 0, 0);
        } else if (wheelDelta < 0) {
            PostMessageW(m_mainHwnd, WM_USER_ZOOM_IN, 0, 0);
        }

        // Consume the wheel event when modifier is held
        return true;
    }

    return false;
}

bool InputHandler::IsModifierPressed() const {
    // GetAsyncKeyState returns the current physical key state.
    // High bit set = key is currently down.
    switch (m_modifierVK) {
        case VK_CONTROL:
            return (GetAsyncKeyState(VK_LCONTROL) & 0x8000) ||
                   (GetAsyncKeyState(VK_RCONTROL) & 0x8000);
        case VK_MENU:  // Alt
            return (GetAsyncKeyState(VK_LMENU) & 0x8000) ||
                   (GetAsyncKeyState(VK_RMENU) & 0x8000);
        case VK_SHIFT:
            return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ||
                   (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
        case VK_LWIN:
            return (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
                   (GetAsyncKeyState(VK_RWIN) & 0x8000);
        default:
            return (GetAsyncKeyState(m_modifierVK) & 0x8000) != 0;
    }
}

}  // namespace VirtualOverlay

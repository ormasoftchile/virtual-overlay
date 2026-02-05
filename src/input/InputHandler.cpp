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

    // Install global hooks
    if (!GlobalHooks::Instance().Install(mainHwnd)) {
        LOG_ERROR("Failed to install global hooks for InputHandler");
        return false;
    }

    // Set up callbacks
    GlobalHooks::Instance().SetKeyboardCallback(
        [this](WPARAM wParam, KBDLLHOOKSTRUCT* hookData) {
            return this->OnKeyboardEvent(wParam, hookData);
        }
    );

    GlobalHooks::Instance().SetMouseCallback(
        [this](WPARAM wParam, MSLLHOOKSTRUCT* hookData) {
            return this->OnMouseEvent(wParam, hookData);
        }
    );

    m_initialized = true;
    LOG_INFO("InputHandler initialized with modifier key: %u", modifierVK);
    return true;
}

void InputHandler::Shutdown() {
    if (!m_initialized) {
        return;
    }

    GlobalHooks::Instance().SetKeyboardCallback(nullptr);
    GlobalHooks::Instance().SetMouseCallback(nullptr);
    GlobalHooks::Instance().Uninstall();

    m_initialized = false;
    LOG_INFO("InputHandler shutdown");
}

bool InputHandler::IsModifierHeld() const {
    return m_modifierHeld;
}

void InputHandler::SetEnabled(bool enabled) {
    m_enabled = enabled;
}

bool InputHandler::IsEnabled() const {
    return m_enabled;
}

void InputHandler::SetModifierKey(UINT modifierVK) {
    m_modifierVK = modifierVK;
    m_modifierHeld = false;  // Reset state on change
}

void InputHandler::SetCursorTracking(bool enabled) {
    m_trackCursor = enabled;
}

bool InputHandler::IsCursorTracking() const {
    return m_trackCursor;
}

bool InputHandler::OnKeyboardEvent(WPARAM wParam, KBDLLHOOKSTRUCT* hookData) {
    if (!m_enabled || !hookData || !m_mainHwnd) {
        return false;
    }

    // Check if this is our modifier key
    if (!IsModifierKey(hookData->vkCode)) {
        return false;
    }

    bool wasHeld = m_modifierHeld;

    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        if (!m_modifierHeld) {
            m_modifierHeld = true;
            PostMessageW(m_mainHwnd, WM_USER_MODIFIER_DOWN, 0, 0);
        }
    } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        if (m_modifierHeld) {
            m_modifierHeld = false;
            PostMessageW(m_mainHwnd, WM_USER_MODIFIER_UP, 0, 0);
        }
    }

    // Don't consume modifier key events - let them pass through
    return false;
}

bool InputHandler::OnMouseEvent(WPARAM wParam, MSLLHOOKSTRUCT* hookData) {
    if (!m_enabled || !hookData || !m_mainHwnd) {
        return false;
    }

    // Handle mouse wheel when modifier is held
    if (wParam == WM_MOUSEWHEEL && m_modifierHeld) {
        // Extract wheel delta - need to sign-extend from HIWORD
        short wheelDelta = static_cast<short>(HIWORD(hookData->mouseData));

        if (wheelDelta > 0) {
            // Scroll up (away from user) = zoom out (swapped based on user feedback)
            PostMessageW(m_mainHwnd, WM_USER_ZOOM_OUT, 0, 0);
        } else if (wheelDelta < 0) {
            // Scroll down (toward user) = zoom in (swapped based on user feedback)
            PostMessageW(m_mainHwnd, WM_USER_ZOOM_IN, 0, 0);
        }

        // Consume the wheel event when modifier is held
        return true;
    }

    // Track cursor movement for pan - only when cursor tracking is enabled (we're zoomed)
    // or when modifier is held (user is about to zoom)
    if (wParam == WM_MOUSEMOVE && (m_trackCursor || m_modifierHeld)) {
        // Pack coordinates into lParam (x in low word, y in high word)
        LPARAM coords = MAKELPARAM(hookData->pt.x, hookData->pt.y);
        PostMessageW(m_mainHwnd, WM_USER_CURSOR_MOVE, 0, coords);
        // Don't consume mouse move events
        return false;
    }

    return false;
}

bool InputHandler::IsModifierKey(UINT vkCode) const {
    switch (m_modifierVK) {
        case VK_CONTROL:
            return vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL;
        case VK_MENU:  // Alt
            return vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU;
        case VK_SHIFT:
            return vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT;
        case VK_LWIN:
            return vkCode == VK_LWIN || vkCode == VK_RWIN;
        default:
            return vkCode == m_modifierVK;
    }
}

}  // namespace VirtualOverlay

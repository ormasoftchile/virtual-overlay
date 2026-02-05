#pragma once

#include <windows.h>

namespace VirtualOverlay {

// Custom messages for zoom events
constexpr UINT WM_USER_ZOOM_IN = WM_USER + 100;
constexpr UINT WM_USER_ZOOM_OUT = WM_USER + 101;
constexpr UINT WM_USER_ZOOM_RESET = WM_USER + 102;
constexpr UINT WM_USER_MODIFIER_DOWN = WM_USER + 103;
constexpr UINT WM_USER_MODIFIER_UP = WM_USER + 104;
constexpr UINT WM_USER_CURSOR_MOVE = WM_USER + 105;

// Coordinates zoom input from global hooks
class InputHandler {
public:
    static InputHandler& Instance();

    // Initialize with the main window and modifier key
    // modifierVK: Virtual key code for modifier (VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN)
    bool Init(HWND mainHwnd, UINT modifierVK);

    // Shutdown input handling
    void Shutdown();

    // Check if modifier key is currently held
    bool IsModifierHeld() const;

    // Enable/disable zoom input
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    // Change the modifier key
    void SetModifierKey(UINT modifierVK);

    // Enable/disable cursor tracking (for panning when zoomed)
    void SetCursorTracking(bool enabled);
    bool IsCursorTracking() const;

private:
    InputHandler();
    ~InputHandler();
    InputHandler(const InputHandler&) = delete;
    InputHandler& operator=(const InputHandler&) = delete;

    // Hook callbacks
    bool OnKeyboardEvent(WPARAM wParam, KBDLLHOOKSTRUCT* hookData);
    bool OnMouseEvent(WPARAM wParam, MSLLHOOKSTRUCT* hookData);

    bool IsModifierKey(UINT vkCode) const;

    HWND m_mainHwnd = nullptr;
    UINT m_modifierVK = VK_CONTROL;
    bool m_modifierHeld = false;
    bool m_enabled = true;
    bool m_trackCursor = false;
    bool m_initialized = false;
};

}  // namespace VirtualOverlay

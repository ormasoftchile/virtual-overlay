#pragma once

#include <windows.h>

namespace VirtualOverlay {

// Custom messages for zoom events
constexpr UINT WM_USER_ZOOM_IN = WM_USER + 100;
constexpr UINT WM_USER_ZOOM_OUT = WM_USER + 101;
constexpr UINT WM_USER_ZOOM_RESET = WM_USER + 102;
constexpr UINT WM_USER_MODIFIER_DOWN = WM_USER + 103;
constexpr UINT WM_USER_MODIFIER_UP = WM_USER + 104;

// Coordinates zoom input: polls modifier key, manages mouse hook dynamically
class InputHandler {
public:
    static InputHandler& Instance();

    // Initialize with the main window and modifier key
    // modifierVK: Virtual key code for modifier (VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN)
    bool Init(HWND mainHwnd, UINT modifierVK);

    // Shutdown input handling
    void Shutdown();

    // Poll modifier key state. Call from timer.
    // Posts WM_USER_MODIFIER_DOWN/UP on state transitions.
    // Installs/uninstalls mouse hook as needed.
    void PollModifierState();

    // Check if modifier key is currently held
    bool IsModifierHeld() const;

    // Enable/disable zoom input
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    // Change the modifier key
    void SetModifierKey(UINT modifierVK);

private:
    InputHandler();
    ~InputHandler();
    InputHandler(const InputHandler&) = delete;
    InputHandler& operator=(const InputHandler&) = delete;

    // Mouse hook callback (only for wheel interception)
    bool OnMouseEvent(WPARAM wParam, MSLLHOOKSTRUCT* hookData);

    // Check if a VK code matches our modifier key family
    bool IsModifierPressed() const;

    HWND m_mainHwnd = nullptr;
    UINT m_modifierVK = VK_CONTROL;
    bool m_modifierHeld = false;
    bool m_enabled = true;
    bool m_initialized = false;
};

}  // namespace VirtualOverlay

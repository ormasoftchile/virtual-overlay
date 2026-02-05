#pragma once

#include "OverlayConfig.h"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

namespace VirtualOverlay {

using Microsoft::WRL::ComPtr;

// Custom messages for overlay
constexpr UINT WM_OVERLAY_SHOW = WM_USER + 200;
constexpr UINT WM_OVERLAY_HIDE = WM_USER + 201;
constexpr UINT WM_OVERLAY_UPDATE = WM_USER + 202;

// Timer IDs
constexpr UINT_PTR TIMER_OVERLAY_ANIMATION = 10;
constexpr UINT_PTR TIMER_OVERLAY_AUTOHIDE = 11;
constexpr UINT_PTR TIMER_OVERLAY_DODGE = 12;
constexpr UINT TIMER_ANIMATION_INTERVAL_MS = 16;  // ~60 FPS
constexpr UINT TIMER_DODGE_INTERVAL_MS = 50;      // Check mouse position 20 times/sec

// Overlay window displaying virtual desktop info
class OverlayWindow {
public:
    static OverlayWindow& Instance();

    // Initialize the overlay window
    bool Init(HINSTANCE hInstance);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Show overlay with desktop info
    void Show(int desktopIndex, const std::wstring& desktopName);
    
    // Hide overlay immediately
    void Hide();

    // Update settings
    void ApplySettings(const OverlaySettings& settings);

    // Get current state
    OverlayState GetState() const { return m_state.state; }
    bool IsVisible() const { return m_state.state != OverlayState::Hidden; }

    // Get window handle
    HWND GetHwnd() const { return m_hwnd; }

    // Called when display configuration changes
    void OnDisplayChanged();

private:
    OverlayWindow();
    ~OverlayWindow();
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    // Window procedure
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Rendering
    bool CreateRenderResources();
    void DiscardRenderResources();
    void Render();
    void RenderWatermark();  // Per-pixel alpha rendering for watermark mode

    // Animation
    void StartFadeIn();
    void StartFadeOut();
    void UpdateAnimation();
    void OnAnimationTimer();
    void OnAutoHideTimer();
    void OnDodgeTimer();
    OverlayPosition GetOppositeHorizontalPosition(OverlayPosition pos);

    // Positioning
    void CalculateWindowPosition(int& x, int& y, int width, int height);
    void UpdateWindowPosition();

    // Text formatting
    std::wstring FormatDisplayText();

    // Window
    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    bool m_initialized = false;

    // Settings and state
    OverlaySettings m_settings;
    OverlayRuntimeState m_state;

    // Render resources
    ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    ComPtr<ID2D1SolidColorBrush> m_textBrush;
    ComPtr<ID2D1SolidColorBrush> m_backgroundBrush;
    ComPtr<ID2D1SolidColorBrush> m_borderBrush;
    ComPtr<IDWriteTextFormat> m_textFormat;

    // Calculated dimensions
    int m_windowWidth = 200;
    int m_windowHeight = 60;
    
    // Dodge state
    bool m_isDodging = false;
    OverlayPosition m_originalPosition = OverlayPosition::TopCenter;
    RECT m_dodgeMonitorRect = {};  // Monitor to stay on during dodge
};

}  // namespace VirtualOverlay

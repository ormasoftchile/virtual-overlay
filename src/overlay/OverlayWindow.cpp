#include "OverlayWindow.h"
#include "AcrylicHelper.h"
#include "../utils/D2DRenderer.h"
#include "../utils/Logger.h"
#include "../utils/Monitor.h"
#include <algorithm>
#include <regex>

namespace VirtualOverlay {

constexpr wchar_t OVERLAY_WINDOW_CLASS[] = L"VirtualOverlayWindow";

OverlayWindow& OverlayWindow::Instance() {
    static OverlayWindow instance;
    return instance;
}

OverlayWindow::OverlayWindow() = default;

OverlayWindow::~OverlayWindow() {
    if (m_initialized) {
        Shutdown();
    }
}

bool OverlayWindow::Init(HINSTANCE hInstance) {
    if (m_initialized) {
        return true;
    }

    m_hInstance = hInstance;

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = OVERLAY_WINDOW_CLASS;

    if (!RegisterClassExW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("Failed to register overlay window class: %lu", error);
            return false;
        }
    }

    // Create layered, topmost, non-activating window
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    DWORD style = WS_POPUP;

    m_hwnd = CreateWindowExW(
        exStyle,
        OVERLAY_WINDOW_CLASS,
        L"",
        style,
        0, 0, m_windowWidth, m_windowHeight,
        nullptr,
        nullptr,
        hInstance,
        this  // Pass this pointer for WM_CREATE
    );

    if (!m_hwnd) {
        LOG_ERROR("Failed to create overlay window: %lu", GetLastError());
        return false;
    }

    // Initialize Direct2D renderer
    if (!D2DRenderer::Instance().Init()) {
        LOG_ERROR("Failed to initialize D2D renderer for overlay");
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    // Create render resources
    if (!CreateRenderResources()) {
        LOG_WARN("Failed to create initial render resources");
    }

    // Apply blur effect (only for notification mode)
    if (m_settings.mode != OverlayMode::Watermark) {
        AcrylicHelper::ApplyAcrylic(m_hwnd, m_settings.style.tintColor, m_settings.style.tintOpacity);
    }

    // Don't call SetLayeredWindowAttributes here - it prevents UpdateLayeredWindow from working
    // It will be set in StartFadeIn for notification mode, or not at all for watermark mode

    m_initialized = true;
    LOG_INFO("OverlayWindow initialized");
    return true;
}

void OverlayWindow::Shutdown() {
    if (!m_initialized) {
        return;
    }

    KillTimer(m_hwnd, TIMER_OVERLAY_ANIMATION);
    KillTimer(m_hwnd, TIMER_OVERLAY_AUTOHIDE);
    KillTimer(m_hwnd, TIMER_OVERLAY_DODGE);

    DiscardRenderResources();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    m_initialized = false;
    LOG_INFO("OverlayWindow shutdown");
}

void OverlayWindow::Show(int desktopIndex, const std::wstring& desktopName) {
    LOG_DEBUG("OverlayWindow::Show called: index=%d, name=%ws, initialized=%d, enabled=%d, mode=%d",
              desktopIndex, desktopName.c_str(), m_initialized ? 1 : 0, m_settings.enabled ? 1 : 0,
              static_cast<int>(m_settings.mode));
    
    if (!m_initialized || !m_settings.enabled) {
        LOG_WARN("OverlayWindow::Show returning early: initialized=%d, enabled=%d",
                 m_initialized ? 1 : 0, m_settings.enabled ? 1 : 0);
        return;
    }

    m_state.currentDesktopIndex = desktopIndex;
    m_state.currentDesktopName = desktopName;

    // Always update position (settings may have changed)
    UpdateWindowPosition();

    // Watermark mode: always visible, no animation, use per-pixel alpha
    if (m_settings.mode == OverlayMode::Watermark) {
        m_state.state = OverlayState::Visible;
        m_state.opacity = 1.0f;
        m_state.slideOffset = 0.0f;
        // Show window first
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, 
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // Use UpdateLayeredWindow for true per-pixel alpha
        RenderWatermark();
        
        // Start dodge timer if enabled
        if (m_settings.dodgeOnHover) {
            SetTimer(m_hwnd, TIMER_OVERLAY_DODGE, TIMER_DODGE_INTERVAL_MS, nullptr);
        }
        
        LOG_DEBUG("Watermark Show: using per-pixel alpha");
        return;
    }

    // Notification mode: fade in/out
    // If already visible or fading in, just refresh
    if (m_state.state == OverlayState::Visible || m_state.state == OverlayState::FadeIn) {
        // Force redraw with new text/position
        InvalidateRect(m_hwnd, nullptr, TRUE);
        if (m_settings.autoHide) {
            m_state.visibleStartTime = GetTickCount();
        }
        return;
    }

    // If fading out, stop and restart fade-in
    if (m_state.state == OverlayState::FadeOut) {
        KillTimer(m_hwnd, TIMER_OVERLAY_ANIMATION);
    }

    // Start fade-in
    StartFadeIn();
}

void OverlayWindow::Hide() {
    if (!m_initialized) {
        return;
    }

    if (m_state.state == OverlayState::Hidden || m_state.state == OverlayState::FadeOut) {
        return;
    }

    // Watermark mode: immediate hide, no animation
    if (m_settings.mode == OverlayMode::Watermark) {
        KillTimer(m_hwnd, TIMER_OVERLAY_DODGE);
        ShowWindow(m_hwnd, SW_HIDE);
        m_state.state = OverlayState::Hidden;
        m_isDodging = false;
        m_dodgeMonitorRect = {};
        LOG_DEBUG("Watermark hidden immediately");
        return;
    }

    // Notification mode: fade out
    StartFadeOut();
}

void OverlayWindow::OnDisplayChanged() {
    if (!m_initialized || !IsVisible()) {
        return;
    }

    LOG_DEBUG("OnDisplayChanged: repositioning overlay");
    
    // Update position for new display configuration
    UpdateWindowPosition();
    
    // Force redraw
    InvalidateRect(m_hwnd, nullptr, TRUE);
    UpdateWindow(m_hwnd);
}

void OverlayWindow::ApplySettings(const OverlaySettings& settings) {
    LOG_INFO("OverlayWindow::ApplySettings - mode=%d, position=%d, monitor=%d, enabled=%d",
             static_cast<int>(settings.mode), static_cast<int>(settings.position), 
             static_cast<int>(settings.monitor), settings.enabled ? 1 : 0);
    
    m_settings = settings;

    // Reset resources that depend on settings (font size, opacity)
    m_textFormat.Reset();
    m_textBrush.Reset();

    // Reapply blur effect (not for watermark mode - needs transparent background)
    if (m_hwnd) {
        if (settings.mode == OverlayMode::Watermark) {
            AcrylicHelper::RemoveBackdrop(m_hwnd);
        } else {
            switch (settings.style.backdrop) {
                case BlurType::Acrylic:
                    AcrylicHelper::ApplyAcrylic(m_hwnd, settings.style.tintColor, settings.style.tintOpacity);
                    break;
                case BlurType::Mica:
                    AcrylicHelper::ApplyMica(m_hwnd);
                    break;
                case BlurType::Solid:
                    AcrylicHelper::RemoveBackdrop(m_hwnd);
                    break;
            }
        }
    }

    // Force redraw
    if (IsVisible()) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    // Watermark mode: show immediately with current desktop info
    if (settings.mode == OverlayMode::Watermark && settings.enabled) {
        Show(m_state.currentDesktopIndex, m_state.currentDesktopName);
    }
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* pThis = nullptr;

    if (msg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = static_cast<OverlayWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            Render();
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_OVERLAY_ANIMATION) {
                OnAnimationTimer();
            } else if (wParam == TIMER_OVERLAY_AUTOHIDE) {
                OnAutoHideTimer();
            } else if (wParam == TIMER_OVERLAY_DODGE) {
                OnDodgeTimer();
            }
            return 0;

        case WM_SIZE:
            DiscardRenderResources();
            CreateRenderResources();
            return 0;

        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            LOG_DEBUG("Display/DPI changed, repositioning watermark");
            DiscardRenderResources();
            CreateRenderResources();
            if (IsVisible()) {
                UpdateWindowPosition();
                // Re-render 
                InvalidateRect(m_hwnd, nullptr, TRUE);
                UpdateWindow(m_hwnd);
            }
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool OverlayWindow::CreateRenderResources() {
    if (!m_hwnd) return false;

    auto& renderer = D2DRenderer::Instance();

    // Create render target if needed
    if (!m_renderTarget) {
        ID2D1HwndRenderTarget* pRT = nullptr;
        if (!renderer.CreateRenderTarget(m_hwnd, &pRT)) {
            return false;
        }
        m_renderTarget.Attach(pRT);
    }

    // Create text brush - watermark mode uses specific opacity
    if (!m_textBrush) {
        float textOpacity = (m_settings.mode == OverlayMode::Watermark) 
            ? m_settings.watermarkOpacity : 1.0f;
        D2D1_COLOR_F textColor = D2DRenderer::ColorFromRGB(m_settings.text.color, textOpacity);
        m_renderTarget->CreateSolidColorBrush(textColor, m_textBrush.GetAddressOf());
    }

    // Create background brush (not used in watermark mode but create anyway)
    if (!m_backgroundBrush) {
        D2D1_COLOR_F bgColor = D2DRenderer::ColorFromRGB(
            m_settings.style.tintColor, 
            m_settings.style.tintOpacity
        );
        m_renderTarget->CreateSolidColorBrush(bgColor, m_backgroundBrush.GetAddressOf());
    }

    // Create border brush
    if (!m_borderBrush) {
        D2D1_COLOR_F borderColor = D2DRenderer::ColorFromRGB(m_settings.style.borderColor);
        m_renderTarget->CreateSolidColorBrush(borderColor, m_borderBrush.GetAddressOf());
    }

    // Create text format - watermark mode uses larger font
    if (!m_textFormat) {
        IDWriteTextFormat* pFormat = nullptr;
        DWRITE_FONT_WEIGHT weight = static_cast<DWRITE_FONT_WEIGHT>(m_settings.text.fontWeight);
        int fontSize = (m_settings.mode == OverlayMode::Watermark)
            ? m_settings.watermarkFontSize : m_settings.text.fontSize;
        if (renderer.CreateTextFormat(
            m_settings.text.fontFamily,
            static_cast<float>(fontSize),
            weight,
            &pFormat
        )) {
            m_textFormat.Attach(pFormat);
        }
    }

    return true;
}

void OverlayWindow::DiscardRenderResources() {
    m_textFormat.Reset();
    m_borderBrush.Reset();
    m_backgroundBrush.Reset();
    m_textBrush.Reset();
    m_renderTarget.Reset();
}

void OverlayWindow::Render() {
    if (!m_renderTarget) {
        if (!CreateRenderResources()) {
            return;
        }
    }

    m_renderTarget->BeginDraw();
    
    // Clear background - use black for watermark mode (will be color-keyed out)
    if (m_settings.mode == OverlayMode::Watermark) {
        m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 1.0f));  // Solid black (color key)
    } else {
        m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));  // Transparent
    }
    m_renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);

    D2D1_SIZE_F size = m_renderTarget->GetSize();
    float padding = static_cast<float>(m_settings.style.padding);
    float radius = static_cast<float>(m_settings.style.cornerRadius);

    // In watermark mode, skip background and border
    if (m_settings.mode != OverlayMode::Watermark) {
        // Background rounded rect
        D2D1_ROUNDED_RECT bgRect = D2D1::RoundedRect(
            D2D1::RectF(0, 0, size.width, size.height),
            radius, radius
        );

        // Draw background (if solid style or as fallback)
        if (m_backgroundBrush) {
            m_renderTarget->FillRoundedRectangle(bgRect, m_backgroundBrush.Get());
        }

        // Draw border
        if (m_borderBrush && m_settings.style.borderWidth > 0) {
            m_renderTarget->DrawRoundedRectangle(bgRect, m_borderBrush.Get(), 
                static_cast<float>(m_settings.style.borderWidth));
        }
    }

    // Draw text
    if (m_textBrush && m_textFormat) {
        std::wstring displayText = FormatDisplayText();
        
        D2D1_RECT_F textRect = D2D1::RectF(
            padding, 
            padding + m_state.slideOffset,  // Apply slide offset
            size.width - padding, 
            size.height - padding + m_state.slideOffset
        );

        // In watermark mode, optionally draw a thin outline for readability (not a shadow fill)
        if (m_settings.mode == OverlayMode::Watermark && m_settings.watermarkShadow) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> outlineBrush;
            m_renderTarget->CreateSolidColorBrush(
                D2D1::ColorF(0, 0, 0, m_settings.watermarkOpacity * 0.7f),
                outlineBrush.GetAddressOf()
            );
            if (outlineBrush) {
                // Draw thin outline offsets
                for (float dx = -1.0f; dx <= 1.0f; dx += 1.0f) {
                    for (float dy = -1.0f; dy <= 1.0f; dy += 1.0f) {
                        if (dx == 0 && dy == 0) continue;
                        D2D1_RECT_F outlineRect = textRect;
                        outlineRect.left += dx;
                        outlineRect.top += dy;
                        outlineRect.right += dx;
                        outlineRect.bottom += dy;
                        m_renderTarget->DrawText(
                            displayText.c_str(),
                            static_cast<UINT32>(displayText.length()),
                            m_textFormat.Get(),
                            outlineRect,
                            outlineBrush.Get()
                        );
                    }
                }
            }
        }

        m_renderTarget->DrawText(
            displayText.c_str(),
            static_cast<UINT32>(displayText.length()),
            m_textFormat.Get(),
            textRect,
            m_textBrush.Get()
        );
    }

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardRenderResources();
    }
}

void OverlayWindow::RenderWatermark() {
    // Use GDI+ or UpdateLayeredWindow for per-pixel alpha transparency
    // Create a 32-bit ARGB bitmap and render to it
    
    int width = m_windowWidth;
    int height = m_windowHeight;
    
    LOG_DEBUG("RenderWatermark: width=%d, height=%d", width, height);
    
    if (width <= 0 || height <= 0) {
        LOG_ERROR("RenderWatermark: Invalid dimensions");
        return;
    }
    
    // Create compatible DC
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    // Create 32-bit ARGB bitmap
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    void* pvBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!hBitmap) {
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }
    
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
    
    // Clear to transparent
    memset(pvBits, 0, width * height * 4);
    
    // Create D2D DC render target
    auto& renderer = D2DRenderer::Instance();
    ID2D1Factory* pFactory = renderer.GetD2DFactory();
    
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT
    );
    
    ComPtr<ID2D1DCRenderTarget> dcRT;
    HRESULT hr = pFactory->CreateDCRenderTarget(&rtProps, dcRT.GetAddressOf());
    if (FAILED(hr)) {
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }
    
    RECT rcBind = { 0, 0, width, height };
    hr = dcRT->BindDC(hdcMem, &rcBind);
    if (FAILED(hr)) {
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }
    
    // Create text format
    IDWriteTextFormat* pFormat = nullptr;
    DWRITE_FONT_WEIGHT weight = static_cast<DWRITE_FONT_WEIGHT>(m_settings.text.fontWeight);
    renderer.CreateTextFormat(
        m_settings.text.fontFamily,
        static_cast<float>(m_settings.watermarkFontSize),
        weight,
        &pFormat
    );
    ComPtr<IDWriteTextFormat> textFormat;
    textFormat.Attach(pFormat);
    
    // Create text brush with watermark color and opacity
    ComPtr<ID2D1SolidColorBrush> textBrush;
    D2D1_COLOR_F textColor = D2DRenderer::ColorFromRGB(m_settings.watermarkColor, m_settings.watermarkOpacity);
    dcRT->CreateSolidColorBrush(textColor, textBrush.GetAddressOf());
    
    // Render text
    dcRT->BeginDraw();
    dcRT->Clear(D2D1::ColorF(0, 0, 0, 0));  // Transparent
    
    std::wstring displayText = FormatDisplayText();
    float padding = static_cast<float>(m_settings.style.padding);
    D2D1_RECT_F textRect = D2D1::RectF(padding, padding, 
        static_cast<float>(width) - padding, static_cast<float>(height) - padding);
    
    if (textFormat && textBrush) {
        dcRT->DrawText(
            displayText.c_str(),
            static_cast<UINT32>(displayText.length()),
            textFormat.Get(),
            textRect,
            textBrush.Get()
        );
    }
    
    dcRT->EndDraw();
    
    // Get window position
    RECT rcWindow;
    GetWindowRect(m_hwnd, &rcWindow);
    POINT ptSrc = { 0, 0 };
    POINT ptDst = { rcWindow.left, rcWindow.top };
    SIZE sizeWnd = { width, height };
    
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    
    BOOL result = UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    if (!result) {
        LOG_ERROR("UpdateLayeredWindow failed: %lu", GetLastError());
    } else {
        LOG_DEBUG("UpdateLayeredWindow success: pos=(%ld,%ld) size=(%d,%d)", ptDst.x, ptDst.y, width, height);
    }
    
    // Cleanup
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void OverlayWindow::StartFadeIn() {
    m_state.state = OverlayState::FadeIn;
    m_state.stateStartTime = GetTickCount();
    m_state.opacity = 0.0f;
    m_state.slideOffset = m_settings.animation.slideIn ? 
        static_cast<float>(-m_settings.animation.slideDistance) : 0.0f;

    // Set layered window to use alpha blending for notification mode
    SetLayeredWindowAttributes(m_hwnd, 0, 0, LWA_ALPHA);

    // Show window
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    // Start animation timer
    SetTimer(m_hwnd, TIMER_OVERLAY_ANIMATION, TIMER_ANIMATION_INTERVAL_MS, nullptr);
}

void OverlayWindow::StartFadeOut() {
    m_state.state = OverlayState::FadeOut;
    m_state.stateStartTime = GetTickCount();

    KillTimer(m_hwnd, TIMER_OVERLAY_AUTOHIDE);
    SetTimer(m_hwnd, TIMER_OVERLAY_ANIMATION, TIMER_ANIMATION_INTERVAL_MS, nullptr);
}

void OverlayWindow::UpdateAnimation() {
    DWORD now = GetTickCount();
    DWORD elapsed = now - m_state.stateStartTime;

    if (m_state.state == OverlayState::FadeIn) {
        float duration = static_cast<float>(m_settings.animation.fadeInDurationMs);
        float progress = std::min(elapsed / duration, 1.0f);

        // Ease out quad: t * (2 - t)
        float easedProgress = progress * (2.0f - progress);

        m_state.opacity = easedProgress;
        
        if (m_settings.animation.slideIn) {
            float startOffset = static_cast<float>(-m_settings.animation.slideDistance);
            m_state.slideOffset = startOffset * (1.0f - easedProgress);
        }

        // Update window opacity
        SetLayeredWindowAttributes(m_hwnd, 0, 
            static_cast<BYTE>(m_state.opacity * 255.0f), LWA_ALPHA);
        InvalidateRect(m_hwnd, nullptr, FALSE);

        if (progress >= 1.0f) {
            // Fade-in complete
            KillTimer(m_hwnd, TIMER_OVERLAY_ANIMATION);
            m_state.state = OverlayState::Visible;
            m_state.opacity = 1.0f;
            m_state.slideOffset = 0.0f;
            m_state.visibleStartTime = now;

            // Start auto-hide timer
            if (m_settings.autoHide) {
                SetTimer(m_hwnd, TIMER_OVERLAY_AUTOHIDE, 
                    static_cast<UINT>(m_settings.autoHideDelayMs), nullptr);
            }
        }
    } else if (m_state.state == OverlayState::FadeOut) {
        float duration = static_cast<float>(m_settings.animation.fadeOutDurationMs);
        float progress = std::min(elapsed / duration, 1.0f);

        m_state.opacity = 1.0f - progress;

        SetLayeredWindowAttributes(m_hwnd, 0, 
            static_cast<BYTE>(m_state.opacity * 255.0f), LWA_ALPHA);

        if (progress >= 1.0f) {
            // Fade-out complete
            KillTimer(m_hwnd, TIMER_OVERLAY_ANIMATION);
            m_state.state = OverlayState::Hidden;
            m_state.opacity = 0.0f;
            ShowWindow(m_hwnd, SW_HIDE);
        }
    }
}

void OverlayWindow::OnAnimationTimer() {
    UpdateAnimation();
}

void OverlayWindow::OnAutoHideTimer() {
    KillTimer(m_hwnd, TIMER_OVERLAY_AUTOHIDE);
    
    if (m_state.state == OverlayState::Visible) {
        StartFadeOut();
    }
}

OverlayPosition OverlayWindow::GetOppositeHorizontalPosition(OverlayPosition pos) {
    switch (pos) {
        // Corner positions: horizontal dodge
        case OverlayPosition::TopLeft:     return OverlayPosition::TopRight;
        case OverlayPosition::TopRight:    return OverlayPosition::TopLeft;
        case OverlayPosition::BottomLeft:  return OverlayPosition::BottomRight;
        case OverlayPosition::BottomRight: return OverlayPosition::BottomLeft;
        // Center positions: vertical dodge
        case OverlayPosition::TopCenter:    return OverlayPosition::BottomCenter;
        case OverlayPosition::BottomCenter: return OverlayPosition::TopCenter;
        // True center: no dodge possible
        case OverlayPosition::Center:
        default: return pos;
    }
}

void OverlayWindow::OnDodgeTimer() {
    if (!m_settings.dodgeOnHover || !IsVisible()) {
        return;
    }
    
    // Get current mouse position
    POINT cursorPos;
    if (!GetCursorPos(&cursorPos)) {
        return;
    }
    
    int prox = m_settings.dodgeProximity;
    
    if (m_isDodging) {
        // When dodging, check if cursor is far from the ORIGINAL position
        // Temporarily calculate where original position would be
        OverlayPosition savedPos = m_settings.position;
        m_settings.position = m_originalPosition;
        int origX, origY;
        CalculateWindowPosition(origX, origY, m_windowWidth, m_windowHeight);
        m_settings.position = savedPos;  // Restore current position
        
        RECT origRect = { origX, origY, origX + m_windowWidth, origY + m_windowHeight };
        origRect.left -= prox;
        origRect.top -= prox;
        origRect.right += prox;
        origRect.bottom += prox;
        
        bool cursorNearOriginal = PtInRect(&origRect, cursorPos);
        
        if (!cursorNearOriginal) {
            // Cursor far from original position, stop dodging
            m_isDodging = false;
            m_dodgeMonitorRect = {};  // Clear stored monitor
            m_settings.position = m_originalPosition;
            UpdateWindowPosition();
            if (m_settings.mode == OverlayMode::Watermark) {
                RenderWatermark();
            } else {
                InvalidateRect(m_hwnd, nullptr, TRUE);
            }
            LOG_DEBUG("Dodge: returned to original position %d", 
                      static_cast<int>(m_settings.position));
        }
    } else {
        // Not dodging - check if cursor is near current position
        RECT overlayRect;
        if (!GetWindowRect(m_hwnd, &overlayRect)) {
            return;
        }
        
        RECT proximityRect = overlayRect;
        proximityRect.left -= prox;
        proximityRect.top -= prox;
        proximityRect.right += prox;
        proximityRect.bottom += prox;
        
        bool cursorNear = PtInRect(&proximityRect, cursorPos);
        
        if (cursorNear) {
            // Start dodging - move to opposite side
            m_isDodging = true;
            m_originalPosition = m_settings.position;
            
            // Store the current monitor rect to prevent jumping monitors during dodge
            HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
            if (hMon) {
                MONITORINFO mi = { sizeof(mi) };
                if (GetMonitorInfoW(hMon, &mi)) {
                    m_dodgeMonitorRect = mi.rcWork;
                }
            }
            
            m_settings.position = GetOppositeHorizontalPosition(m_originalPosition);
            UpdateWindowPosition();
            if (m_settings.mode == OverlayMode::Watermark) {
                RenderWatermark();
            } else {
                InvalidateRect(m_hwnd, nullptr, TRUE);
            }
            LOG_DEBUG("Dodge: moved from %d to %d", 
                      static_cast<int>(m_originalPosition), 
                      static_cast<int>(m_settings.position));
        }
    }
}

void OverlayWindow::CalculateWindowPosition(int& x, int& y, int width, int height) {
    LOG_DEBUG("CalculateWindowPosition: m_settings.position=%d", static_cast<int>(m_settings.position));
    
    // Get target monitor
    RECT monitorRect;
    
    // During dodge, use the stored monitor rect to prevent jumping monitors
    if (m_isDodging && (m_dodgeMonitorRect.right - m_dodgeMonitorRect.left) > 0) {
        monitorRect = m_dodgeMonitorRect;
    } else {
        const MonitorInfo* targetMonitor = nullptr;

        switch (m_settings.monitor) {
            case MonitorSelection::Cursor:
                targetMonitor = Monitor::Instance().GetAtCursor();
                break;
            case MonitorSelection::Primary:
                targetMonitor = Monitor::Instance().GetPrimary();
                break;
            case MonitorSelection::All:
                // For 'all', use primary for position calculation
                targetMonitor = Monitor::Instance().GetPrimary();
                break;
        }

        if (targetMonitor) {
            monitorRect = targetMonitor->workArea;
        } else {
            // Fallback to screen dimensions
            monitorRect.left = 0;
            monitorRect.top = 0;
            monitorRect.right = GetSystemMetrics(SM_CXSCREEN);
            monitorRect.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
    }

    int monitorWidth = monitorRect.right - monitorRect.left;
    int monitorHeight = monitorRect.bottom - monitorRect.top;

    // Calculate position based on setting
    int margin = 20;  // Margin from edges

    switch (m_settings.position) {
        case OverlayPosition::TopLeft:
            x = monitorRect.left + margin;
            y = monitorRect.top + margin;
            break;
        case OverlayPosition::TopCenter:
            x = monitorRect.left + (monitorWidth - width) / 2;
            y = monitorRect.top + margin;
            break;
        case OverlayPosition::TopRight:
            x = monitorRect.right - width - margin;
            y = monitorRect.top + margin;
            break;
        case OverlayPosition::Center:
            x = monitorRect.left + (monitorWidth - width) / 2;
            y = monitorRect.top + (monitorHeight - height) / 2;
            break;
        case OverlayPosition::BottomLeft:
            x = monitorRect.left + margin;
            y = monitorRect.bottom - height - margin;
            break;
        case OverlayPosition::BottomCenter:
            x = monitorRect.left + (monitorWidth - width) / 2;
            y = monitorRect.bottom - height - margin;
            break;
        case OverlayPosition::BottomRight:
            x = monitorRect.right - width - margin;
            y = monitorRect.bottom - height - margin;
            break;
        default:
            // Fallback to top center
            x = monitorRect.left + (monitorWidth - width) / 2;
            y = monitorRect.top + margin;
            LOG_WARN("Unknown position value %d, defaulting to top center", static_cast<int>(m_settings.position));
            break;
    }
    
    LOG_DEBUG("CalculateWindowPosition result: x=%d, y=%d (monitor top=%ld, bottom=%ld)",
              x, y, monitorRect.top, monitorRect.bottom);
}

void OverlayWindow::UpdateWindowPosition() {
    if (!m_hwnd) return;

    // Calculate content size based on mode and actual text
    int contentWidth, contentHeight;
    
    if (m_settings.mode == OverlayMode::Watermark) {
        // Watermark mode: measure actual text width
        std::wstring displayText = FormatDisplayText();
        size_t textLen = displayText.length();
        if (textLen == 0) textLen = 10;  // Default assumption
        
        // Approximate character width is about 60% of font size for most fonts
        float charWidth = m_settings.watermarkFontSize * 0.6f;
        contentWidth = static_cast<int>(charWidth * textLen) + m_settings.style.padding * 2;
        contentHeight = m_settings.watermarkFontSize + 20; // Font height + margin
    } else {
        // Notification mode: standard size
        contentWidth = 200;  // Minimum width
        contentHeight = 60;  // Minimum height
    }

    m_windowWidth = contentWidth + m_settings.style.padding * 2;
    m_windowHeight = contentHeight;

    int x, y;
    CalculateWindowPosition(x, y, m_windowWidth, m_windowHeight);

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, m_windowWidth, m_windowHeight, 
        SWP_NOACTIVATE);

    // Recreate render target for new size
    DiscardRenderResources();
    CreateRenderResources();
}

std::wstring OverlayWindow::FormatDisplayText() {
    std::wstring result = m_settings.format;

    // Replace {number} with desktop index
    std::wstring numberStr = std::to_wstring(m_state.currentDesktopIndex);
    size_t pos = result.find(L"{number}");
    while (pos != std::wstring::npos) {
        result.replace(pos, 8, numberStr);
        pos = result.find(L"{number}", pos + numberStr.length());
    }

    // Replace {name} with desktop name
    pos = result.find(L"{name}");
    while (pos != std::wstring::npos) {
        result.replace(pos, 6, m_state.currentDesktopName);
        pos = result.find(L"{name}", pos + m_state.currentDesktopName.length());
    }

    // If name is empty, clean up formatting
    if (m_state.currentDesktopName.empty()) {
        // Remove ": " if name was empty
        pos = result.find(L": ");
        if (pos != std::wstring::npos && pos + 2 == result.length()) {
            result = result.substr(0, pos);
        }
    }

    return result;
}

}  // namespace VirtualOverlay

#include "OverlayWindow.h"
#include "../utils/D2DRenderer.h"
#include "../utils/Logger.h"
#include "../utils/Monitor.h"
#include "../desktop/VirtualDesktop.h"
#include "../display/FlipManager.h"
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

    m_initialized = true;
    LOG_INFO("OverlayWindow initialized");
    return true;
}

void OverlayWindow::Shutdown() {
    if (!m_initialized) {
        return;
    }

    KillTimer(m_hwnd, TIMER_OVERLAY_DODGE);

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    m_initialized = false;
    LOG_INFO("OverlayWindow shutdown");
}

void OverlayWindow::Show(int desktopIndex, const std::wstring& desktopName) {
    LOG_DEBUG("OverlayWindow::Show called: index=%d, name=%ws, initialized=%d, enabled=%d",
              desktopIndex, desktopName.c_str(), m_initialized ? 1 : 0, m_settings.enabled ? 1 : 0);
    
    if (!m_initialized || !m_settings.enabled) {
        LOG_WARN("OverlayWindow::Show returning early: initialized=%d, enabled=%d",
                 m_initialized ? 1 : 0, m_settings.enabled ? 1 : 0);
        return;
    }

    m_state.currentDesktopIndex = desktopIndex;
    m_state.currentDesktopName = desktopName;

    // Move overlay window to the current virtual desktop so it's visible
    // even when the target desktop has no other windows.
    VirtualDesktop::Instance().MoveWindowToCurrentDesktop(m_hwnd);

    // Always update position (settings may have changed)
    UpdateWindowPosition();

    // Honor per-monitor opt-outs: on a screen that opted out, the overlay stays
    // hidden until the target moves to a screen that allows it again.
    const bool allowed = ResolveTargetMonitor() != nullptr;

    m_state.state = OverlayState::Visible;
    m_hiddenByPolicy = !allowed;
    if (allowed) {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RenderWatermark();
        LOG_DEBUG("Watermark Show: using per-pixel alpha");
    } else {
        // Keep the logical visible state so an allowed target can show it again.
        ShowWindow(m_hwnd, SW_HIDE);
        LOG_DEBUG("Watermark Show: target monitor opted out, staying hidden");
    }

    if (m_settings.dodgeOnHover) {
        SetTimer(m_hwnd, TIMER_OVERLAY_DODGE, TIMER_DODGE_INTERVAL_MS, nullptr);
    }
}

void OverlayWindow::Hide() {
    if (!m_initialized) {
        return;
    }

    if (m_state.state == OverlayState::Hidden) {
        return;
    }

    m_hiddenByPolicy = false;

    KillTimer(m_hwnd, TIMER_OVERLAY_DODGE);
    ShowWindow(m_hwnd, SW_HIDE);
    m_state.state = OverlayState::Hidden;
    if (m_isDodging) {
        m_settings.position = m_originalPosition;
        m_isDodging = false;
    }
    m_dodgeMonitorRect = {};
    LOG_DEBUG("Watermark hidden immediately");
}

void OverlayWindow::OnDisplayChanged() {
    if (!m_initialized || !IsVisible() || m_updatingPosition) {
        return;
    }

    LOG_DEBUG("OnDisplayChanged: repositioning overlay");

    if (m_isDodging) {
        m_settings.position = m_originalPosition;
        m_isDodging = false;
    }
    m_dodgeMonitorRect = {};

    // Update position for new display configuration
    UpdateWindowPosition();

    // Force redraw
    RenderWatermark();
}

void OverlayWindow::CheckPosition() {
    if (!m_initialized || !m_hwnd || !IsVisible() || m_updatingPosition) {
        return;
    }

    // Per-monitor opt-out: the overlay is never shown on screens that opted
    // out. The watermark keeps its logical "visible" state so it reappears as
    // soon as the target screen allows it again.
    if (ResolveTargetMonitor() == nullptr) {
        if (!m_hiddenByPolicy) {
            m_hiddenByPolicy = true;
            ShowWindow(m_hwnd, SW_HIDE);
            LOG_DEBUG("Watermark hidden: target monitor opted out");
        }
        return;
    }

    if (m_hiddenByPolicy) {
        m_hiddenByPolicy = false;
        UpdateWindowPosition();
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        RenderWatermark();
        LOG_DEBUG("Overlay reappeared: target monitor allows overlay again");
    }

    // If dodging, verify the dodge monitor rect is still valid and unchanged
    if (m_isDodging) {
        HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (!hMon || !GetMonitorInfoW(hMon, &mi) || !EqualRect(&mi.rcWork, &m_dodgeMonitorRect)) {
            m_settings.position = m_originalPosition;
            m_isDodging = false;
            m_dodgeMonitorRect = {};
        } else {
            return;
        }
    }

    int expectedX = 0, expectedY = 0;
    CalculateWindowPosition(expectedX, expectedY, m_windowWidth, m_windowHeight);

    RECT rcWindow = {};
    if (GetWindowRect(m_hwnd, &rcWindow)) {
        if (rcWindow.left != expectedX || rcWindow.top != expectedY ||
            (rcWindow.right - rcWindow.left) != m_windowWidth ||
            (rcWindow.bottom - rcWindow.top) != m_windowHeight) {
            LOG_INFO("OverlayWindow: correcting position from (%ld,%ld) to (%d,%d)",
                     rcWindow.left, rcWindow.top, expectedX, expectedY);
            UpdateWindowPosition();
            RenderWatermark();
        }
    }
}

void OverlayWindow::ApplySettings(const OverlaySettings& settings) {
    LOG_INFO("OverlayWindow::ApplySettings - position=%d, monitor=%d, enabled=%d",
             static_cast<int>(settings.position),
             static_cast<int>(settings.monitor), settings.enabled ? 1 : 0);
    
    if (m_hwnd) KillTimer(m_hwnd, TIMER_OVERLAY_DODGE);
    m_isDodging = false;
    m_dodgeMonitorRect = {};
    m_hiddenByPolicy = false;
    m_settings = settings;

    if (m_initialized) {
        if (settings.enabled) {
            Show(m_state.currentDesktopIndex, m_state.currentDesktopName);
        } else {
            Hide();
        }
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
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_OVERLAY_DODGE) {
                OnDodgeTimer();
            }
            return 0;

        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            // Display/DPI changes are handled centrally by App::OnDisplayChange and CheckPosition.
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
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
            RenderWatermark();
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
            RenderWatermark();
            LOG_DEBUG("Dodge: moved from %d to %d", 
                      static_cast<int>(m_originalPosition), 
                      static_cast<int>(m_settings.position));
        }
    }
}

HMONITOR OverlayWindow::ResolveTargetMonitor() const {
    HMONITOR hTargetMon = nullptr;

    switch (m_settings.monitor) {
        case MonitorSelection::Cursor: {
            POINT pt = {};
            GetCursorPos(&pt);
            hTargetMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
            break;
        }
        case MonitorSelection::Primary:
        case MonitorSelection::All:
        default: {
            POINT zero = { 0, 0 };
            hTargetMon = MonitorFromPoint(zero, MONITOR_DEFAULTTOPRIMARY);
            break;
        }
    }

    if (!hTargetMon) {
        return nullptr;
    }

    // Honor per-monitor opt-outs (and the no-watermark-on-mirrored-screens rule)
    // via the shared per-monitor settings registry.
    if (const MonitorInfo* mon = Monitor::Instance().GetByHandle(hTargetMon)) {
        if (!IsOverlayAllowedOn(Config::Instance().Get().monitors, *mon)) {
            return nullptr;
        }
    }

    return hTargetMon;
}

void OverlayWindow::CalculateWindowPosition(int& x, int& y, int width, int height) {
    // Get target monitor workArea directly from live Win32 monitor APIs
    RECT monitorRect = {};
    
    // During dodge, use the stored monitor rect to prevent jumping monitors
    if (m_isDodging && (m_dodgeMonitorRect.right - m_dodgeMonitorRect.left) > 0) {
        monitorRect = m_dodgeMonitorRect;
    } else {
        HMONITOR hTargetMon = ResolveTargetMonitor();
        if (!hTargetMon) {
            // Target screen is opted out (the window is hidden anyway): keep the
            // last known position instead of jumping monitors.
            RECT rcWindow = {};
            if (GetWindowRect(m_hwnd, &rcWindow)) {
                x = rcWindow.left;
                y = rcWindow.top;
                return;
            }
            POINT zero = { 0, 0 };
            hTargetMon = MonitorFromPoint(zero, MONITOR_DEFAULTTOPRIMARY);
        }

        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (hTargetMon && GetMonitorInfoW(hTargetMon, &mi)) {
            monitorRect = mi.rcWork;
        } else {
            // Fallback to primary screen dimensions
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
}

void OverlayWindow::UpdateWindowPosition() {
    if (!m_hwnd || m_updatingPosition) return;
    m_updatingPosition = true;

    // Measure the watermark text using the existing character-width estimate.
    int contentWidth, contentHeight;
    std::wstring displayText = FormatDisplayText();
    size_t textLen = displayText.length();
    if (textLen == 0) textLen = 10;

    float charWidth = m_settings.watermarkFontSize * 0.6f;
    contentWidth = static_cast<int>(charWidth * textLen) + m_settings.style.padding * 2;
    contentHeight = m_settings.watermarkFontSize + 20;

    m_windowWidth = contentWidth + m_settings.style.padding * 2;
    m_windowHeight = contentHeight;

    int x, y;
    CalculateWindowPosition(x, y, m_windowWidth, m_windowHeight);
    LOG_DEBUG("UpdateWindowPosition: pos=(%d,%d) size=(%d,%d)", x, y, m_windowWidth, m_windowHeight);

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, m_windowWidth, m_windowHeight, 
        SWP_NOACTIVATE);

    m_updatingPosition = false;
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

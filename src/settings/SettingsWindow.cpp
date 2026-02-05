#include "SettingsWindow.h"
#include "SettingsPages.h"
#include "../utils/Logger.h"
#include "../overlay/OverlayWindow.h"
#include "../overlay/OverlayConfig.h"
#include <windowsx.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

namespace VirtualOverlay {

constexpr wchar_t SETTINGS_WINDOW_CLASS[] = L"VirtualOverlaySettings";

SettingsWindow& SettingsWindow::Instance() {
    static SettingsWindow instance;
    return instance;
}

SettingsWindow::SettingsWindow() = default;

SettingsWindow::~SettingsWindow() {
    Shutdown();
}

bool SettingsWindow::Init(HINSTANCE hInstance, HWND hParentWnd) {
    if (m_initialized) {
        return true;
    }

    m_hInstance = hInstance;
    m_hParentWnd = hParentWnd;

    // Initialize common controls
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS;
    InitCommonControlsEx(&icex);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = SETTINGS_WINDOW_CLASS;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("Failed to register settings window class: %lu", error);
            return false;
        }
    }

    m_initialized = true;
    LOG_INFO("SettingsWindow initialized");
    return true;
}

void SettingsWindow::Shutdown() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_initialized = false;
}

void SettingsWindow::Open() {
    if (!m_initialized) {
        LOG_ERROR("SettingsWindow not initialized");
        return;
    }

    // If already open, bring to front
    if (m_hwnd && IsWindow(m_hwnd)) {
        SetForegroundWindow(m_hwnd);
        return;
    }

    // Load current config as working copy
    m_workingConfig = Config::Instance().Get();

    // Create the window
    if (!CreateMainWindow()) {
        LOG_ERROR("Failed to create settings window");
        return;
    }

    // Create controls
    if (!CreateTabControl() || !CreatePages() || !CreateButtons()) {
        LOG_ERROR("Failed to create settings controls");
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return;
    }

    // Load settings to UI
    LoadSettingsToUI();

    // Show first tab
    ShowPage(SettingsTab::General);

    // Show window
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
}

void SettingsWindow::Close() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool SettingsWindow::CreateMainWindow() {
    // Center on screen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - SETTINGS_WIDTH) / 2;
    int y = (screenHeight - SETTINGS_HEIGHT) / 2;

    m_hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        SETTINGS_WINDOW_CLASS,
        L"Virtual Overlay Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, SETTINGS_WIDTH, SETTINGS_HEIGHT,
        m_hParentWnd,
        nullptr,
        m_hInstance,
        this
    );

    return m_hwnd != nullptr;
}

bool SettingsWindow::CreateTabControl() {
    RECT rcClient;
    GetClientRect(m_hwnd, &rcClient);

    m_hTabControl = CreateWindowExW(
        0,
        WC_TABCONTROLW,
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS,
        MARGIN, MARGIN,
        rcClient.right - 2 * MARGIN,
        TAB_HEIGHT,
        m_hwnd,
        reinterpret_cast<HMENU>(IDC_TAB_CONTROL),
        m_hInstance,
        nullptr
    );

    if (!m_hTabControl) {
        return false;
    }

    // Set font
    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(m_hTabControl, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Add tabs
    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;

    tie.pszText = const_cast<LPWSTR>(L"General");
    TabCtrl_InsertItem(m_hTabControl, 0, &tie);

    tie.pszText = const_cast<LPWSTR>(L"Overlay");
    TabCtrl_InsertItem(m_hTabControl, 1, &tie);

    tie.pszText = const_cast<LPWSTR>(L"Zoom");
    TabCtrl_InsertItem(m_hTabControl, 2, &tie);

    tie.pszText = const_cast<LPWSTR>(L"About");
    TabCtrl_InsertItem(m_hTabControl, 3, &tie);

    return true;
}

bool SettingsWindow::CreatePages() {
    // Get tab display area
    RECT rcTab;
    GetWindowRect(m_hTabControl, &rcTab);
    MapWindowPoints(HWND_DESKTOP, m_hwnd, reinterpret_cast<LPPOINT>(&rcTab), 2);
    TabCtrl_AdjustRect(m_hTabControl, FALSE, &rcTab);

    // Extend page area down to buttons
    RECT rcClient;
    GetClientRect(m_hwnd, &rcClient);
    rcTab.bottom = rcClient.bottom - BUTTON_HEIGHT - MARGIN * 2;

    LOG_DEBUG("Page rect: left=%d, top=%d, right=%d, bottom=%d",
              rcTab.left, rcTab.top, rcTab.right, rcTab.bottom);

    // Create page windows
    m_hPageGeneral = SettingsPages::CreateGeneralPage(m_hwnd, m_hInstance, rcTab);
    m_hPageOverlay = SettingsPages::CreateOverlayPage(m_hwnd, m_hInstance, rcTab);
    m_hPageZoom = SettingsPages::CreateZoomPage(m_hwnd, m_hInstance, rcTab);
    m_hPageAbout = SettingsPages::CreateAboutPage(m_hwnd, m_hInstance, rcTab);

    LOG_DEBUG("Pages created: General=%p, Overlay=%p, Zoom=%p, About=%p",
              (void*)m_hPageGeneral, (void*)m_hPageOverlay, (void*)m_hPageZoom, (void*)m_hPageAbout);

    return m_hPageGeneral && m_hPageOverlay && m_hPageZoom && m_hPageAbout;
}

bool SettingsWindow::CreateButtons() {
    RECT rcClient;
    GetClientRect(m_hwnd, &rcClient);

    int btnY = rcClient.bottom - BUTTON_HEIGHT - MARGIN;
    int btnX = rcClient.right - MARGIN;

    // Apply button
    btnX -= BUTTON_WIDTH;
    m_hBtnApply = CreateWindowExW(
        0, L"BUTTON", L"Apply",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        btnX, btnY, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_APPLY),
        m_hInstance, nullptr
    );

    // Cancel button
    btnX -= BUTTON_WIDTH + MARGIN;
    m_hBtnCancel = CreateWindowExW(
        0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        btnX, btnY, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_CANCEL),
        m_hInstance, nullptr
    );

    // OK button
    btnX -= BUTTON_WIDTH + MARGIN;
    m_hBtnOK = CreateWindowExW(
        0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        btnX, btnY, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_OK),
        m_hInstance, nullptr
    );

    // Set font
    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(m_hBtnOK, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    SendMessageW(m_hBtnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    SendMessageW(m_hBtnApply, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    return m_hBtnOK && m_hBtnCancel && m_hBtnApply;
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* pThis = nullptr;

    if (msg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = static_cast<SettingsWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDC_BTN_OK:
                    OnOK();
                    return 0;
                case IDC_BTN_CANCEL:
                    OnCancel();
                    return 0;
                case IDC_BTN_APPLY:
                    OnApply();
                    return 0;
                case IDC_OVL_PREVIEW:
                    SaveSettingsFromUI();
                    PreviewOverlay();
                    return 0;
            }
            break;
        }

        case WM_NOTIFY: {
            auto* pnmh = reinterpret_cast<NMHDR*>(lParam);
            if (pnmh->idFrom == IDC_TAB_CONTROL) {
                if (pnmh->code == TCN_SELCHANGE) {
                    int tabIndex = TabCtrl_GetCurSel(m_hTabControl);
                    OnTabChanged(tabIndex);
                }
            }
            break;
        }

        case WM_CLOSE:
            OnCancel();
            return 0;

        case WM_DESTROY:
            m_hwnd = nullptr;
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void SettingsWindow::OnTabChanged(int tabIndex) {
    ShowPage(static_cast<SettingsTab>(tabIndex));
}

void SettingsWindow::ShowPage(SettingsTab tab) {
    HideAllPages();

    HWND hPage = nullptr;
    switch (tab) {
        case SettingsTab::General:
            hPage = m_hPageGeneral;
            break;
        case SettingsTab::Overlay:
            hPage = m_hPageOverlay;
            break;
        case SettingsTab::Zoom:
            hPage = m_hPageZoom;
            break;
        case SettingsTab::About:
            hPage = m_hPageAbout;
            break;
    }

    if (hPage) {
        ShowWindow(hPage, SW_SHOW);
        // Bring page to front (above tab control)
        SetWindowPos(hPage, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        InvalidateRect(hPage, nullptr, TRUE);
        UpdateWindow(hPage);
    }

    m_currentTab = tab;
}

void SettingsWindow::HideAllPages() {
    if (m_hPageGeneral) ShowWindow(m_hPageGeneral, SW_HIDE);
    if (m_hPageOverlay) ShowWindow(m_hPageOverlay, SW_HIDE);
    if (m_hPageZoom) ShowWindow(m_hPageZoom, SW_HIDE);
    if (m_hPageAbout) ShowWindow(m_hPageAbout, SW_HIDE);
}

void SettingsWindow::OnApply() {
    SaveSettingsFromUI();

    if (!ValidateSettings()) {
        MessageBoxW(m_hwnd, L"Invalid settings. Please check your values.",
                    L"Settings Error", MB_OK | MB_ICONWARNING);
        return;
    }

    // Apply to global config
    Config::Instance().GetMutable() = m_workingConfig;
    Config::Instance().Save();

    LOG_INFO("Settings applied");

    // Notify callback
    if (m_applyCallback) {
        m_applyCallback();
    }
}

void SettingsWindow::OnCancel() {
    Close();
}

void SettingsWindow::OnOK() {
    OnApply();
    Close();
}

void SettingsWindow::LoadSettingsToUI() {
    SettingsPages::LoadGeneralSettings(m_hPageGeneral, m_workingConfig.general);
    SettingsPages::LoadOverlaySettings(m_hPageOverlay, m_workingConfig.overlay);
    SettingsPages::LoadZoomSettings(m_hPageZoom, m_workingConfig.zoom);
}

void SettingsWindow::SaveSettingsFromUI() {
    LOG_DEBUG("SaveSettingsFromUI: hPageOverlay=%p", (void*)m_hPageOverlay);
    SettingsPages::SaveGeneralSettings(m_hPageGeneral, m_workingConfig.general);
    SettingsPages::SaveOverlaySettings(m_hPageOverlay, m_workingConfig.overlay);
    SettingsPages::SaveZoomSettings(m_hPageZoom, m_workingConfig.zoom);
    LOG_DEBUG("SaveSettingsFromUI done: overlay.position=%d", static_cast<int>(m_workingConfig.overlay.position));
}

bool SettingsWindow::ValidateSettings() {
    return Config::Instance().Validate(m_workingConfig);
}

void SettingsWindow::PreviewOverlay() {
    LOG_INFO("PreviewOverlay called");
    
    // Temporarily apply overlay settings and show
    const auto& overlayConfig = m_workingConfig.overlay;
    
    LOG_INFO("PreviewOverlay: mode=%d, position=%d, enabled=%d",
             static_cast<int>(overlayConfig.mode),
             static_cast<int>(overlayConfig.position),
             overlayConfig.enabled ? 1 : 0);
    
    OverlaySettings previewSettings;
    previewSettings.enabled = true;
    previewSettings.mode = overlayConfig.mode;
    previewSettings.position = overlayConfig.position;
    previewSettings.monitor = overlayConfig.monitor;
    previewSettings.format = overlayConfig.format;
    
    // For preview: notification mode auto-hides, watermark doesn't
    if (overlayConfig.mode == OverlayMode::Notification) {
        previewSettings.autoHide = true;
        previewSettings.autoHideDelayMs = 3000;  // 3 seconds for preview
    } else {
        previewSettings.autoHide = false;
    }
    
    // Watermark settings
    previewSettings.watermarkFontSize = overlayConfig.watermarkFontSize;
    previewSettings.watermarkOpacity = overlayConfig.watermarkOpacity;
    previewSettings.watermarkShadow = overlayConfig.watermarkShadow;
    previewSettings.watermarkColor = overlayConfig.watermarkColor;
    
    // Dodge settings
    previewSettings.dodgeOnHover = overlayConfig.dodgeOnHover;
    previewSettings.dodgeProximity = overlayConfig.dodgeProximity;
    
    previewSettings.style.backdrop = overlayConfig.style.blur;
    previewSettings.style.tintColor = overlayConfig.style.tintColor;
    previewSettings.style.tintOpacity = overlayConfig.style.tintOpacity;
    previewSettings.style.cornerRadius = overlayConfig.style.cornerRadius;
    previewSettings.style.padding = overlayConfig.style.padding;
    previewSettings.style.borderWidth = overlayConfig.style.borderWidth;
    previewSettings.style.borderColor = overlayConfig.style.borderColor;
    
    previewSettings.text.fontFamily = overlayConfig.text.fontFamily;
    previewSettings.text.fontSize = overlayConfig.text.fontSize;
    previewSettings.text.fontWeight = overlayConfig.text.fontWeight;
    previewSettings.text.color = overlayConfig.text.color;
    
    previewSettings.animation.fadeInDurationMs = overlayConfig.animation.fadeInDurationMs;
    previewSettings.animation.fadeOutDurationMs = overlayConfig.animation.fadeOutDurationMs;
    previewSettings.animation.slideIn = overlayConfig.animation.slideIn;
    previewSettings.animation.slideDistance = overlayConfig.animation.slideDistance;
    
    OverlayWindow::Instance().ApplySettings(previewSettings);
    OverlayWindow::Instance().Show(1, L"Preview Desktop");
}

}  // namespace VirtualOverlay

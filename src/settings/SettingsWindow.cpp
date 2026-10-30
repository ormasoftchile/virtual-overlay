#include "SettingsWindow.h"
#include "SettingsPages.h"
#include "../utils/Logger.h"
#include "../overlay/OverlayWindow.h"
#include "../overlay/OverlayConfig.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>

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
    icex.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS | ICC_BAR_CLASSES;
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
    // Capture page geometry in 96-DPI units before applying the monitor's font.
    if (!UpdateFont(USER_DEFAULT_SCREEN_DPI) ||
        !CreateTabControl() || !CreatePages() || !CreateButtons()) {
        LOG_ERROR("Failed to create settings controls");
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return;
    }

    // Load settings to UI
    LoadSettingsToUI();
    CapturePageLayout();
    if (!UpdateFont(GetDpiForWindow(m_hwnd))) {
        Close();
        return;
    }
    LayoutControls();

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
    POINT cursor = {};
    if (!GetCursorPos(&cursor)) {
        LOG_ERROR("Failed to locate settings monitor: %lu", GetLastError());
        return false;
    }
    MONITORINFO monitor = { sizeof(monitor) };
    if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
        LOG_ERROR("Failed to query settings monitor: %lu", GetLastError());
        return false;
    }
    const RECT& workArea = monitor.rcWork;
    int x = workArea.left + (workArea.right - workArea.left - SETTINGS_WIDTH) / 2;
    int y = workArea.top + (workArea.bottom - workArea.top - SETTINGS_HEIGHT) / 2;

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

    if (!m_hwnd) return false;

    UINT dpi = GetDpiForWindow(m_hwnd);
    RECT bounds = { 0, 0, MulDiv(SETTINGS_WIDTH, dpi, USER_DEFAULT_SCREEN_DPI),
                         MulDiv(SETTINGS_HEIGHT, dpi, USER_DEFAULT_SCREEN_DPI) };
    if (!AdjustWindowRectExForDpi(&bounds, static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE)),
                                  FALSE, static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE)), dpi)) {
        LOG_ERROR("Failed to size settings window: %lu", GetLastError());
        Close();
        return false;
    }
    int width = bounds.right - bounds.left;
    int height = bounds.bottom - bounds.top;
    x = std::max(workArea.left, workArea.left + (workArea.right - workArea.left - width) / 2);
    y = std::max(workArea.top, workArea.top + (workArea.bottom - workArea.top - height) / 2);
    SetWindowPos(m_hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
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
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB_CONTROL)),
        m_hInstance,
        nullptr
    );

    if (!m_hTabControl) {
        return false;
    }

    // Set font
    SendMessageW(m_hTabControl, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);

    // Add tabs
    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;

    tie.pszText = const_cast<LPWSTR>(L"General");
    TabCtrl_InsertItem(m_hTabControl, 0, &tie);

    tie.pszText = const_cast<LPWSTR>(L"Overlay");
    TabCtrl_InsertItem(m_hTabControl, 1, &tie);

    tie.pszText = const_cast<LPWSTR>(L"Flip Screen");
    TabCtrl_InsertItem(m_hTabControl, 2, &tie);

    tie.pszText = const_cast<LPWSTR>(L"Presenter");
    TabCtrl_InsertItem(m_hTabControl, 3, &tie);

    tie.pszText = const_cast<LPWSTR>(L"About");
    TabCtrl_InsertItem(m_hTabControl, 4, &tie);

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

    // Create page windows
    m_hPageGeneral = SettingsPages::CreateGeneralPage(m_hwnd, m_hInstance, rcTab);
    m_hPageOverlay = SettingsPages::CreateOverlayPage(m_hwnd, m_hInstance, rcTab);
    m_hPageFlip = SettingsPages::CreateFlipPage(m_hwnd, m_hInstance, rcTab);
    m_hPagePresenter = SettingsPages::CreatePresenterPage(m_hwnd, m_hInstance, rcTab);
    m_hPageAbout = SettingsPages::CreateAboutPage(m_hwnd, m_hInstance, rcTab);

    return m_hPageGeneral && m_hPageOverlay && m_hPageFlip && m_hPagePresenter && m_hPageAbout;
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
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_APPLY)),
        m_hInstance, nullptr
    );

    // Cancel button
    btnX -= BUTTON_WIDTH + MARGIN;
    m_hBtnCancel = CreateWindowExW(
        0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        btnX, btnY, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_CANCEL)),
        m_hInstance, nullptr
    );

    // OK button
    btnX -= BUTTON_WIDTH + MARGIN;
    m_hBtnOK = CreateWindowExW(
        0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        btnX, btnY, BUTTON_WIDTH, BUTTON_HEIGHT,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_OK)),
        m_hInstance, nullptr
    );

    // Set font
    SendMessageW(m_hBtnOK, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    SendMessageW(m_hBtnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);
    SendMessageW(m_hBtnApply, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), TRUE);

    return m_hBtnOK && m_hBtnCancel && m_hBtnApply;
}

bool SettingsWindow::UpdateFont(UINT dpi) {
    NONCLIENTMETRICSW metrics = { sizeof(metrics) };
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) {
        LOG_ERROR("Failed to query settings font: %lu", GetLastError());
        return false;
    }
    HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
    if (!font) {
        LOG_ERROR("Failed to create settings font: %lu", GetLastError());
        return false;
    }
    HFONT previous = m_hFont;
    m_hFont = font;
    m_dpi = dpi;
    EnumChildWindows(m_hwnd, [](HWND control, LPARAM data) -> BOOL {
        SendMessageW(control, WM_SETFONT, static_cast<WPARAM>(data), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
    if (previous) DeleteObject(previous);
    return true;
}

void SettingsWindow::CapturePageLayout() {
    m_pageControls.clear();
    for (HWND page : { m_hPageGeneral, m_hPageOverlay, m_hPageFlip, m_hPagePresenter, m_hPageAbout }) {
        for (HWND control = GetWindow(page, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT)) {
            RECT bounds = {};
            GetWindowRect(control, &bounds);
            MapWindowPoints(HWND_DESKTOP, page, reinterpret_cast<LPPOINT>(&bounds), 2);
            wchar_t className[32] = {};
            GetClassNameW(control, className, 32);
            if (wcscmp(className, WC_COMBOBOXW) == 0) {
                RECT dropped = {};
                SendMessageW(control, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropped));
                bounds.bottom = bounds.top + dropped.bottom - dropped.top;
            }
            m_pageControls.push_back({ control, bounds });
        }
    }
}

int SettingsWindow::Scale(int value) const {
    return MulDiv(value, m_dpi, USER_DEFAULT_SCREEN_DPI);
}

void SettingsWindow::LayoutControls() {
    if (!m_hTabControl || !m_hBtnOK) return;

    RECT client = {};
    GetClientRect(m_hwnd, &client);
    int margin = Scale(MARGIN);
    int buttonHeight = Scale(BUTTON_HEIGHT);
    int buttonWidth = Scale(BUTTON_WIDTH);
    int buttonY = client.bottom - margin - buttonHeight;
    int tabWidth = client.right - margin * 2;
    int tabHeight = buttonY - margin * 2;
    SetWindowPos(m_hTabControl, nullptr, margin, margin, tabWidth, tabHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    RECT pageBounds = { 0, 0, tabWidth, tabHeight };
    TabCtrl_AdjustRect(m_hTabControl, FALSE, &pageBounds);
    MapWindowPoints(m_hTabControl, m_hwnd, reinterpret_cast<LPPOINT>(&pageBounds), 2);
    for (HWND page : { m_hPageGeneral, m_hPageOverlay, m_hPageFlip, m_hPagePresenter, m_hPageAbout }) {
        if (page) {
            SetWindowPos(page, nullptr, pageBounds.left, pageBounds.top,
                         pageBounds.right - pageBounds.left, pageBounds.bottom - pageBounds.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    for (const auto& control : m_pageControls) {
        const RECT& bounds = control.bounds;
        SetWindowPos(control.hwnd, nullptr, Scale(bounds.left), Scale(bounds.top),
                     Scale(bounds.right - bounds.left), Scale(bounds.bottom - bounds.top),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    int buttonX = client.right - margin - buttonWidth;
    for (HWND button : { m_hBtnApply, m_hBtnCancel, m_hBtnOK }) {
        SetWindowPos(button, nullptr, buttonX, buttonY, buttonWidth, buttonHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        buttonX -= buttonWidth + margin;
    }
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
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
        case WM_GETFONT:
            return reinterpret_cast<LRESULT>(m_hFont);

        case WM_SIZE:
            LayoutControls();
            return 0;

        case WM_DPICHANGED: {
            UINT dpi = HIWORD(wParam);
            if (m_hFont && !UpdateFont(dpi)) return 0;
            m_dpi = dpi;
            auto* bounds = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, bounds->left, bounds->top,
                         bounds->right - bounds->left, bounds->bottom - bounds->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            LayoutControls();
            return 0;
        }

        case WM_SETTINGCHANGE:
            if (wParam == SPI_SETNONCLIENTMETRICS && m_hFont && UpdateFont(m_dpi)) {
                LayoutControls();
            }
            break;

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

        case WM_NCDESTROY:
            m_pageControls.clear();
            if (m_hFont) DeleteObject(m_hFont);
            m_hFont = nullptr;
            m_hTabControl = nullptr;
            m_hBtnOK = m_hBtnCancel = m_hBtnApply = nullptr;
            m_hPageGeneral = m_hPageOverlay = m_hPageFlip = m_hPagePresenter = m_hPageAbout = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;

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
        case SettingsTab::Flip:
            hPage = m_hPageFlip;
            break;
        case SettingsTab::Presenter:
            hPage = m_hPagePresenter;
            break;
        case SettingsTab::About:
            hPage = m_hPageAbout;
            break;
    }

    if (hPage) {
        ShowWindow(hPage, SW_SHOW);
        SetWindowPos(hPage, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        InvalidateRect(hPage, nullptr, TRUE);
        UpdateWindow(hPage);
    }

    m_currentTab = tab;
}

void SettingsWindow::HideAllPages() {
    if (m_hPageGeneral) ShowWindow(m_hPageGeneral, SW_HIDE);
    if (m_hPageOverlay) ShowWindow(m_hPageOverlay, SW_HIDE);
    if (m_hPageFlip) ShowWindow(m_hPageFlip, SW_HIDE);
    if (m_hPagePresenter) ShowWindow(m_hPagePresenter, SW_HIDE);
    if (m_hPageAbout) ShowWindow(m_hPageAbout, SW_HIDE);
}

void SettingsWindow::OnApply() {
    SaveSettingsFromUI();

    if (!ValidateSettings()) {
        MessageBoxW(m_hwnd, L"Invalid settings. Please check your values.",
                    L"Settings Error", MB_OK | MB_ICONWARNING);
        return;
    }

    // Preserve monitorAnchors that may have been updated via Alt-drag while Settings was open
    const auto& liveAnchors = Config::Instance().Get().presenter.monitorAnchors;
    for (const auto& [k, v] : liveAnchors) {
        m_workingConfig.presenter.monitorAnchors[k] = v;
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
    SettingsPages::LoadOverlaySettings(m_hPageOverlay, m_workingConfig.overlay, m_workingConfig.monitors);
    SettingsPages::LoadFlipSettings(m_hPageFlip, m_workingConfig.flip, m_workingConfig.monitors);
    SettingsPages::LoadPresenterSettings(m_hPagePresenter, m_workingConfig.presenter);
}

void SettingsWindow::SaveSettingsFromUI() {
    SettingsPages::SaveGeneralSettings(m_hPageGeneral, m_workingConfig.general);
    SettingsPages::SaveOverlaySettings(m_hPageOverlay, m_workingConfig.overlay, m_workingConfig.monitors);
    SettingsPages::SaveFlipSettings(m_hPageFlip, m_workingConfig.flip, m_workingConfig.monitors);
    SettingsPages::SavePresenterSettings(m_hPagePresenter, m_workingConfig.presenter);
}

bool SettingsWindow::ValidateSettings() {
    return Config::Instance().Validate(m_workingConfig);
}

void SettingsWindow::PreviewOverlay() {
    LOG_INFO("PreviewOverlay called");
    
    const auto& overlayConfig = m_workingConfig.overlay;
    
    OverlaySettings previewSettings;
    previewSettings.enabled = true;
    previewSettings.position = overlayConfig.position;
    previewSettings.monitor = overlayConfig.monitor;
    previewSettings.format = overlayConfig.format;
    
    previewSettings.watermarkFontSize = overlayConfig.watermarkFontSize;
    previewSettings.watermarkOpacity = overlayConfig.watermarkOpacity;
    previewSettings.watermarkShadow = overlayConfig.watermarkShadow;
    previewSettings.watermarkColor = overlayConfig.watermarkColor;
    
    previewSettings.dodgeOnHover = overlayConfig.dodgeOnHover;
    previewSettings.dodgeProximity = overlayConfig.dodgeProximity;
    previewSettings.dodgeSticky = overlayConfig.dodgeSticky;
    
    previewSettings.style.padding = overlayConfig.style.padding;
    
    previewSettings.text.fontFamily = overlayConfig.text.fontFamily;
    previewSettings.text.fontWeight = overlayConfig.text.fontWeight;
    
    OverlayWindow::Instance().ApplySettings(previewSettings);
    OverlayWindow::Instance().Show(1, L"Preview Desktop");
}

}  // namespace VirtualOverlay

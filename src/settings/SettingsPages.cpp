#include "SettingsPages.h"
#include "SettingsWindow.h"
#include "../config/Defaults.h"
#include "../utils/Logger.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace VirtualOverlay {

constexpr wchar_t PAGE_CLASS[] = L"VirtualOverlaySettingsPage";
static bool s_pageClassRegistered = false;
static HBRUSH s_hBrushBackground = nullptr;
static uint32_t s_watermarkColor = 0xFFFFFF;  // Current watermark color for UI

// Page window procedure to handle control backgrounds
static LRESULT CALLBACK PageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            // Make static controls and checkboxes use window background
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            if (!s_hBrushBackground) {
                s_hBrushBackground = GetSysColorBrush(COLOR_WINDOW);
            }
            return reinterpret_cast<LRESULT>(s_hBrushBackground);
        }
        case WM_HSCROLL: {
            HWND hSlider = reinterpret_cast<HWND>(lParam);
            int id = GetDlgCtrlID(hSlider);
            int pos = static_cast<int>(SendMessageW(hSlider, TBM_GETPOS, 0, 0));
            wchar_t text[16];
            if (id == IDC_OVL_WATERMARK_SIZE) {
                swprintf_s(text, L"%d", pos);
                SetDlgItemTextW(hwnd, IDC_OVL_WATERMARK_SIZE_LABEL, text);
            } else if (id == IDC_OVL_WATERMARK_OPACITY) {
                swprintf_s(text, L"%d%%", pos);
                SetDlgItemTextW(hwnd, IDC_OVL_WATERMARK_OPACITY_LABEL, text);
            } else if (id == IDC_OVL_DODGE_PROXIMITY) {
                swprintf_s(text, L"%dpx", pos);
                SetDlgItemTextW(hwnd, IDC_OVL_DODGE_PROXIMITY_LABEL, text);
            } else if (id == IDC_ZOOM_STEP) {
                swprintf_s(text, L"%d%%", pos);
                SetDlgItemTextW(hwnd, IDC_ZOOM_STEP_LABEL, text);
            } else if (id == IDC_ZOOM_MAX) {
                swprintf_s(text, L"%dx", pos);
                SetDlgItemTextW(hwnd, IDC_ZOOM_MAX_LABEL, text);
            }
            return 0;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);
            
            // Handle mode combo change - show/hide controls based on mode
            if (wmId == IDC_OVL_MODE && wmEvent == CBN_SELCHANGE) {
                LRESULT mode = SendDlgItemMessageW(hwnd, IDC_OVL_MODE, CB_GETCURSEL, 0, 0);
                bool isWatermark = (mode == 1); // 0=Notification, 1=Watermark
                int showCmd = isWatermark ? SW_HIDE : SW_SHOW;
                ShowWindow(GetDlgItem(hwnd, IDC_OVL_AUTOHIDE), showCmd);
                ShowWindow(GetDlgItem(hwnd, IDC_OVL_AUTOHIDE_DELAY), showCmd);
                ShowWindow(GetDlgItem(hwnd, IDC_OVL_AUTOHIDE_MS_LABEL), showCmd);
                return 0;
            }
            
            // Handle color button click
            if (wmId == IDC_OVL_WATERMARK_COLOR_BTN && wmEvent == BN_CLICKED) {
                CHOOSECOLORW cc = {};
                static COLORREF acrCustClr[16] = {};
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd;
                cc.rgbResult = RGB(
                    (s_watermarkColor >> 16) & 0xFF,
                    (s_watermarkColor >> 8) & 0xFF,
                    s_watermarkColor & 0xFF
                );
                cc.lpCustColors = acrCustClr;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                
                if (ChooseColorW(&cc)) {
                    // Convert COLORREF (BGR) to our format (RGB)
                    s_watermarkColor = 
                        ((GetRValue(cc.rgbResult)) << 16) |
                        ((GetGValue(cc.rgbResult)) << 8) |
                        (GetBValue(cc.rgbResult));
                    // Invalidate color preview
                    HWND hPreview = GetDlgItem(hwnd, IDC_OVL_WATERMARK_COLOR);
                    if (hPreview) {
                        InvalidateRect(hPreview, nullptr, TRUE);
                    }
                }
                return 0;
            }
            
            // Forward other button clicks to parent (settings window)
            HWND hParent = GetParent(hwnd);
            if (hParent) {
                return SendMessageW(hParent, msg, wParam, lParam);
            }
            break;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pDIS = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
            if (pDIS && pDIS->CtlID == IDC_OVL_WATERMARK_COLOR) {
                // Draw color preview
                HBRUSH hBrush = CreateSolidBrush(RGB(
                    (s_watermarkColor >> 16) & 0xFF,
                    (s_watermarkColor >> 8) & 0xFF,
                    s_watermarkColor & 0xFF
                ));
                FillRect(pDIS->hDC, &pDIS->rcItem, hBrush);
                DeleteObject(hBrush);
                // Draw border
                FrameRect(pDIS->hDC, &pDIS->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
                return TRUE;
            }
            break;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void RegisterPageClass(HINSTANCE hInstance) {
    if (s_pageClassRegistered) return;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PageWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = PAGE_CLASS;

    if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        s_pageClassRegistered = true;
    }
}

HWND SettingsPages::CreateLabel(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h) {
    HWND hCtrl = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                  x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
    SetControlFont(hCtrl);
    return hCtrl;
}

HWND SettingsPages::CreateCheckbox(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h) {
    HWND hCtrl = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                  x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
    SetControlFont(hCtrl);
    return hCtrl;
}

HWND SettingsPages::CreateComboBox(HWND hParent, HINSTANCE hInstance, int id, int x, int y, int w, int h) {
    HWND hCtrl = CreateWindowExW(0, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                  x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
    SetControlFont(hCtrl);
    return hCtrl;
}

HWND SettingsPages::CreateEdit(HWND hParent, HINSTANCE hInstance, int id, int x, int y, int w, int h) {
    HWND hCtrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
    SetControlFont(hCtrl);
    return hCtrl;
}

HWND SettingsPages::CreateSlider(HWND hParent, HINSTANCE hInstance, int id, int x, int y, int w, int h, int min, int max) {
    HWND hCtrl = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                                  x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
    SendMessageW(hCtrl, TBM_SETRANGE, TRUE, MAKELPARAM(min, max));
    return hCtrl;
}

HWND SettingsPages::CreateButton(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h) {
    HWND hCtrl = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
    SetControlFont(hCtrl);
    return hCtrl;
}

HWND SettingsPages::CreateLink(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h) {
    HWND hCtrl = CreateWindowExW(0, WC_LINK, text, WS_CHILD | WS_VISIBLE,
                                  x, y, w, h, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
    SetControlFont(hCtrl);
    return hCtrl;
}

void SettingsPages::SetControlFont(HWND hControl) {
    if (hControl) {
        HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(hControl, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    }
}

// =============================================================================
// General Page
// =============================================================================

HWND SettingsPages::CreateGeneralPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage) {
    RegisterPageClass(hInstance);

    int pageW = rcPage.right - rcPage.left;
    int pageH = rcPage.bottom - rcPage.top;

    HWND hPage = CreateWindowExW(
        WS_EX_CONTROLPARENT, PAGE_CLASS, nullptr,
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        rcPage.left, rcPage.top,
        pageW, pageH,
        hParent, nullptr, hInstance, nullptr
    );

    if (!hPage) {
        OutputDebugStringW(L"CreateGeneralPage: Failed to create page window\n");
        return nullptr;
    }

    wchar_t dbg[256];
    swprintf_s(dbg, L"CreateGeneralPage: hPage=%p, x=%d, y=%d, w=%d, h=%d\n",
               (void*)hPage, rcPage.left, rcPage.top, pageW, pageH);
    OutputDebugStringW(dbg);

    int y = 20;
    int x = 20;
    int labelW = 200;
    int ctrlH = 22;
    int spacing = 30;

    // Start with Windows checkbox
    HWND hChk1 = CreateCheckbox(hPage, hInstance, IDC_GEN_START_WINDOWS, L"Start with Windows", x, y, labelW, ctrlH);
    swprintf_s(dbg, L"  Checkbox 1: %p\n", (void*)hChk1);
    OutputDebugStringW(dbg);
    y += spacing;

    // Show tray icon checkbox
    HWND hChk2 = CreateCheckbox(hPage, hInstance, IDC_GEN_SHOW_TRAY, L"Show tray icon", x, y, labelW, ctrlH);
    swprintf_s(dbg, L"  Checkbox 2: %p\n", (void*)hChk2);
    OutputDebugStringW(dbg);
    y += spacing;

    // Settings hotkey label
    CreateLabel(hPage, hInstance, 0, L"Settings hotkey:", x, y, 100, ctrlH);
    CreateLabel(hPage, hInstance, IDC_GEN_HOTKEY_LABEL, L"Ctrl+Shift+O", x + 110, y, 100, ctrlH);

    return hPage;
}

void SettingsPages::LoadGeneralSettings(HWND hPage, const GeneralConfig& config) {
    if (!hPage) return;

    CheckDlgButton(hPage, IDC_GEN_START_WINDOWS, config.startWithWindows ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hPage, IDC_GEN_SHOW_TRAY, config.showTrayIcon ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(hPage, IDC_GEN_HOTKEY_LABEL, config.settingsHotkey.c_str());
}

void SettingsPages::SaveGeneralSettings(HWND hPage, GeneralConfig& config) {
    if (!hPage) return;

    config.startWithWindows = IsDlgButtonChecked(hPage, IDC_GEN_START_WINDOWS) == BST_CHECKED;
    config.showTrayIcon = IsDlgButtonChecked(hPage, IDC_GEN_SHOW_TRAY) == BST_CHECKED;
    // Hotkey is read-only for now
}

// =============================================================================
// Overlay Page
// =============================================================================

HWND SettingsPages::CreateOverlayPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage) {
    RegisterPageClass(hInstance);

    HWND hPage = CreateWindowExW(
        WS_EX_CONTROLPARENT, PAGE_CLASS, nullptr,
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        rcPage.left, rcPage.top,
        rcPage.right - rcPage.left,
        rcPage.bottom - rcPage.top,
        hParent, nullptr, hInstance, nullptr
    );

    if (!hPage) return nullptr;

    int y = 12;
    int x = 20;
    int labelW = 100;
    int ctrlW = 150;
    int ctrlH = 20;
    int spacing = 24;

    // Enable checkbox
    CreateCheckbox(hPage, hInstance, IDC_OVL_ENABLE, L"Enable overlay", x, y, 150, ctrlH);
    y += spacing;

    // Mode dropdown
    CreateLabel(hPage, hInstance, 0, L"Mode:", x, y + 2, labelW, ctrlH);
    HWND hMode = CreateComboBox(hPage, hInstance, IDC_OVL_MODE, x + labelW, y, ctrlW, 100);
    SendMessageW(hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Notification"));
    SendMessageW(hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Watermark"));
    y += spacing;

    // Position dropdown
    CreateLabel(hPage, hInstance, 0, L"Position:", x, y + 2, labelW, ctrlH);
    HWND hPos = CreateComboBox(hPage, hInstance, IDC_OVL_POSITION, x + labelW, y, ctrlW, 200);
    SendMessageW(hPos, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Top Left"));
    SendMessageW(hPos, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Top Center"));
    SendMessageW(hPos, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Top Right"));
    SendMessageW(hPos, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Center"));
    SendMessageW(hPos, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Bottom Left"));
    SendMessageW(hPos, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Bottom Center"));
    SendMessageW(hPos, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Bottom Right"));
    y += spacing;

    // Monitor dropdown
    CreateLabel(hPage, hInstance, 0, L"Monitor:", x, y + 2, labelW, ctrlH);
    HWND hMon = CreateComboBox(hPage, hInstance, IDC_OVL_MONITOR, x + labelW, y, ctrlW, 150);
    SendMessageW(hMon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cursor Monitor"));
    SendMessageW(hMon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Primary Monitor"));
    SendMessageW(hMon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Monitors"));
    y += spacing;

    // Format text
    CreateLabel(hPage, hInstance, 0, L"Format:", x, y + 2, labelW, ctrlH);
    CreateEdit(hPage, hInstance, IDC_OVL_FORMAT, x + labelW, y, ctrlW, ctrlH);
    y += spacing;

    // Watermark font size slider
    CreateLabel(hPage, hInstance, 0, L"WM Size:", x, y + 2, labelW, ctrlH);
    CreateSlider(hPage, hInstance, IDC_OVL_WATERMARK_SIZE, x + labelW, y, ctrlW, 25, 24, 144);
    CreateLabel(hPage, hInstance, IDC_OVL_WATERMARK_SIZE_LABEL, L"72", x + labelW + ctrlW + 10, y + 2, 40, ctrlH);
    y += spacing;

    // Watermark opacity slider (10% to 100%)
    CreateLabel(hPage, hInstance, 0, L"WM Opacity:", x, y + 2, labelW, ctrlH);
    CreateSlider(hPage, hInstance, IDC_OVL_WATERMARK_OPACITY, x + labelW, y, ctrlW, 25, 10, 100);
    CreateLabel(hPage, hInstance, IDC_OVL_WATERMARK_OPACITY_LABEL, L"25%", x + labelW + ctrlW + 10, y + 2, 40, ctrlH);
    y += spacing;

    // Watermark color button
    CreateLabel(hPage, hInstance, 0, L"WM Color:", x, y + 2, labelW, ctrlH);
    HWND hColorBtn = CreateButton(hPage, hInstance, IDC_OVL_WATERMARK_COLOR_BTN, L"Choose...", x + labelW, y, 80, ctrlH);
    // Color preview static control
    HWND hColorPreview = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", 
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        x + labelW + 85, y, 30, ctrlH, hPage, 
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OVL_WATERMARK_COLOR)), hInstance, nullptr);
    y += spacing;

    // Auto-hide checkbox and delay (notification mode only)
    CreateCheckbox(hPage, hInstance, IDC_OVL_AUTOHIDE, L"Auto-hide after", x, y, 100, ctrlH);
    CreateEdit(hPage, hInstance, IDC_OVL_AUTOHIDE_DELAY, x + 105, y, 50, ctrlH);
    CreateLabel(hPage, hInstance, IDC_OVL_AUTOHIDE_MS_LABEL, L"ms", x + 160, y + 2, 30, ctrlH);
    y += spacing;
    
    // Dodge on hover checkbox and proximity
    CreateCheckbox(hPage, hInstance, IDC_OVL_DODGE, L"Dodge cursor", x, y, 100, ctrlH);
    CreateSlider(hPage, hInstance, IDC_OVL_DODGE_PROXIMITY, x + 105, y, 100, ctrlH, 20, 200);
    CreateLabel(hPage, hInstance, IDC_OVL_DODGE_PROXIMITY_LABEL, L"100px", x + 210, y + 2, 40, ctrlH);
    y += spacing + 5;

    // Preview button
    CreateButton(hPage, hInstance, IDC_OVL_PREVIEW, L"Preview", x, y, 80, 25);

    return hPage;
}

void SettingsPages::LoadOverlaySettings(HWND hPage, const OverlayConfig& config) {
    if (!hPage) return;

    CheckDlgButton(hPage, IDC_OVL_ENABLE, config.enabled ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(hPage, IDC_OVL_MODE, CB_SETCURSEL, static_cast<int>(config.mode), 0);
    SendDlgItemMessageW(hPage, IDC_OVL_POSITION, CB_SETCURSEL, static_cast<int>(config.position), 0);
    SendDlgItemMessageW(hPage, IDC_OVL_MONITOR, CB_SETCURSEL, static_cast<int>(config.monitor), 0);
    SetDlgItemTextW(hPage, IDC_OVL_FORMAT, config.format.c_str());

    // Watermark size slider
    SendDlgItemMessageW(hPage, IDC_OVL_WATERMARK_SIZE, TBM_SETPOS, TRUE, config.watermarkFontSize);
    wchar_t sizeText[16];
    swprintf_s(sizeText, L"%d", config.watermarkFontSize);
    SetDlgItemTextW(hPage, IDC_OVL_WATERMARK_SIZE_LABEL, sizeText);

    // Watermark opacity slider (stored as 0.0-1.0, display as 0-100)
    int wmOpacity = static_cast<int>(config.watermarkOpacity * 100.0f);
    SendDlgItemMessageW(hPage, IDC_OVL_WATERMARK_OPACITY, TBM_SETPOS, TRUE, wmOpacity);
    wchar_t opacityText[16];
    swprintf_s(opacityText, L"%d%%", wmOpacity);
    SetDlgItemTextW(hPage, IDC_OVL_WATERMARK_OPACITY_LABEL, opacityText);

    // Watermark color
    s_watermarkColor = config.watermarkColor;
    HWND hColorPreview = GetDlgItem(hPage, IDC_OVL_WATERMARK_COLOR);
    if (hColorPreview) {
        InvalidateRect(hColorPreview, nullptr, TRUE);
    }

    CheckDlgButton(hPage, IDC_OVL_AUTOHIDE, config.autoHide ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(hPage, IDC_OVL_AUTOHIDE_DELAY, config.autoHideDelayMs, FALSE);
    
    // Dodge settings
    CheckDlgButton(hPage, IDC_OVL_DODGE, config.dodgeOnHover ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(hPage, IDC_OVL_DODGE_PROXIMITY, TBM_SETPOS, TRUE, config.dodgeProximity);
    wchar_t proxText[16];
    swprintf_s(proxText, L"%dpx", config.dodgeProximity);
    SetDlgItemTextW(hPage, IDC_OVL_DODGE_PROXIMITY_LABEL, proxText);
    
    // Show/hide auto-hide controls based on mode
    bool isWatermark = (config.mode == OverlayMode::Watermark);
    int showCmd = isWatermark ? SW_HIDE : SW_SHOW;
    ShowWindow(GetDlgItem(hPage, IDC_OVL_AUTOHIDE), showCmd);
    ShowWindow(GetDlgItem(hPage, IDC_OVL_AUTOHIDE_DELAY), showCmd);
    ShowWindow(GetDlgItem(hPage, IDC_OVL_AUTOHIDE_MS_LABEL), showCmd);
}

void SettingsPages::SaveOverlaySettings(HWND hPage, OverlayConfig& config) {
    if (!hPage) {
        OutputDebugStringW(L"SaveOverlaySettings: hPage is NULL!\n");
        return;
    }

    config.enabled = IsDlgButtonChecked(hPage, IDC_OVL_ENABLE) == BST_CHECKED;

    // Mode
    LRESULT modeResult = SendDlgItemMessageW(hPage, IDC_OVL_MODE, CB_GETCURSEL, 0, 0);
    if (modeResult != CB_ERR && modeResult >= 0) {
        config.mode = static_cast<OverlayMode>(modeResult);
    }

    // Position
    LRESULT posResult = SendDlgItemMessageW(hPage, IDC_OVL_POSITION, CB_GETCURSEL, 0, 0);
    if (posResult != CB_ERR && posResult >= 0) {
        config.position = static_cast<OverlayPosition>(posResult);
    }

    // Monitor
    LRESULT monResult = SendDlgItemMessageW(hPage, IDC_OVL_MONITOR, CB_GETCURSEL, 0, 0);
    if (monResult != CB_ERR && monResult >= 0) {
        config.monitor = static_cast<MonitorSelection>(monResult);
    }

    // Format
    wchar_t format[256] = {};
    GetDlgItemTextW(hPage, IDC_OVL_FORMAT, format, 255);
    config.format = format;

    // Watermark size
    int wmSize = static_cast<int>(SendDlgItemMessageW(hPage, IDC_OVL_WATERMARK_SIZE, TBM_GETPOS, 0, 0));
    config.watermarkFontSize = wmSize;

    // Watermark opacity
    int wmOpacity = static_cast<int>(SendDlgItemMessageW(hPage, IDC_OVL_WATERMARK_OPACITY, TBM_GETPOS, 0, 0));
    config.watermarkOpacity = wmOpacity / 100.0f;

    // Watermark color
    config.watermarkColor = s_watermarkColor;

    config.autoHide = IsDlgButtonChecked(hPage, IDC_OVL_AUTOHIDE) == BST_CHECKED;
    config.autoHideDelayMs = GetDlgItemInt(hPage, IDC_OVL_AUTOHIDE_DELAY, nullptr, FALSE);
    
    // Dodge settings
    config.dodgeOnHover = IsDlgButtonChecked(hPage, IDC_OVL_DODGE) == BST_CHECKED;
    config.dodgeProximity = static_cast<int>(SendDlgItemMessageW(hPage, IDC_OVL_DODGE_PROXIMITY, TBM_GETPOS, 0, 0));
}

// =============================================================================
// Zoom Page
// =============================================================================

HWND SettingsPages::CreateZoomPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage) {
    RegisterPageClass(hInstance);

    HWND hPage = CreateWindowExW(
        WS_EX_CONTROLPARENT, PAGE_CLASS, nullptr,
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        rcPage.left, rcPage.top,
        rcPage.right - rcPage.left,
        rcPage.bottom - rcPage.top,
        hParent, nullptr, hInstance, nullptr
    );

    if (!hPage) return nullptr;

    int y = 15;
    int x = 20;
    int labelW = 120;
    int ctrlW = 150;
    int ctrlH = 22;
    int spacing = 28;

    // Enable checkbox
    CreateCheckbox(hPage, hInstance, IDC_ZOOM_ENABLE, L"Enable zoom", x, y, 150, ctrlH);
    y += spacing;

    // Modifier key dropdown
    CreateLabel(hPage, hInstance, 0, L"Modifier key:", x, y + 2, labelW, ctrlH);
    HWND hMod = CreateComboBox(hPage, hInstance, IDC_ZOOM_MODIFIER, x + labelW, y, ctrlW, 150);
    SendMessageW(hMod, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ctrl"));
    SendMessageW(hMod, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Alt"));
    SendMessageW(hMod, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Shift"));
    SendMessageW(hMod, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Win"));
    y += spacing;

    // Zoom step slider
    CreateLabel(hPage, hInstance, 0, L"Zoom step:", x, y + 2, labelW, ctrlH);
    CreateSlider(hPage, hInstance, IDC_ZOOM_STEP, x + labelW, y, ctrlW, 25, 10, 100);  // 0.1 to 1.0
    CreateLabel(hPage, hInstance, IDC_ZOOM_STEP_LABEL, L"0.50", x + labelW + ctrlW + 10, y + 2, 40, ctrlH);
    y += spacing;

    // Max zoom slider
    CreateLabel(hPage, hInstance, 0, L"Max zoom:", x, y + 2, labelW, ctrlH);
    CreateSlider(hPage, hInstance, IDC_ZOOM_MAX, x + labelW, y, ctrlW, 25, 2, 20);  // 2x to 20x
    CreateLabel(hPage, hInstance, IDC_ZOOM_MAX_LABEL, L"10x", x + labelW + ctrlW + 10, y + 2, 40, ctrlH);
    y += spacing;

    // Smoothing checkbox
    CreateCheckbox(hPage, hInstance, IDC_ZOOM_SMOOTHING, L"Smooth zoom animation", x, y, 200, ctrlH);
    y += spacing;

    // Double-tap to reset checkbox
    CreateCheckbox(hPage, hInstance, IDC_ZOOM_DOUBLETAP, L"Double-tap modifier to reset", x, y, 200, ctrlH);
    y += spacing;

    // Touchpad pinch checkbox
    CreateCheckbox(hPage, hInstance, IDC_ZOOM_PINCH, L"Touchpad pinch-to-zoom", x, y, 200, ctrlH);

    return hPage;
}

void SettingsPages::LoadZoomSettings(HWND hPage, const ZoomConfig& config) {
    if (!hPage) return;

    CheckDlgButton(hPage, IDC_ZOOM_ENABLE, config.enabled ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(hPage, IDC_ZOOM_MODIFIER, CB_SETCURSEL, static_cast<int>(config.modifierKey), 0);

    int step = static_cast<int>(config.zoomStep * 100.0f);
    SendDlgItemMessageW(hPage, IDC_ZOOM_STEP, TBM_SETPOS, TRUE, step);
    wchar_t stepText[16];
    swprintf_s(stepText, L"%d%%", step);
    SetDlgItemTextW(hPage, IDC_ZOOM_STEP_LABEL, stepText);

    int maxZoom = static_cast<int>(config.maxZoom);
    SendDlgItemMessageW(hPage, IDC_ZOOM_MAX, TBM_SETPOS, TRUE, maxZoom);
    wchar_t maxText[16];
    swprintf_s(maxText, L"%dx", maxZoom);
    SetDlgItemTextW(hPage, IDC_ZOOM_MAX_LABEL, maxText);

    CheckDlgButton(hPage, IDC_ZOOM_SMOOTHING, config.smoothing ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hPage, IDC_ZOOM_DOUBLETAP, config.doubleTapToReset ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hPage, IDC_ZOOM_PINCH, config.touchpadPinch ? BST_CHECKED : BST_UNCHECKED);
}

void SettingsPages::SaveZoomSettings(HWND hPage, ZoomConfig& config) {
    if (!hPage) return;

    config.enabled = IsDlgButtonChecked(hPage, IDC_ZOOM_ENABLE) == BST_CHECKED;

    int mod = static_cast<int>(SendDlgItemMessageW(hPage, IDC_ZOOM_MODIFIER, CB_GETCURSEL, 0, 0));
    if (mod >= 0) config.modifierKey = static_cast<ModifierKey>(mod);

    int step = static_cast<int>(SendDlgItemMessageW(hPage, IDC_ZOOM_STEP, TBM_GETPOS, 0, 0));
    config.zoomStep = step / 100.0f;

    int maxZoom = static_cast<int>(SendDlgItemMessageW(hPage, IDC_ZOOM_MAX, TBM_GETPOS, 0, 0));
    config.maxZoom = static_cast<float>(maxZoom);

    config.smoothing = IsDlgButtonChecked(hPage, IDC_ZOOM_SMOOTHING) == BST_CHECKED;
    config.doubleTapToReset = IsDlgButtonChecked(hPage, IDC_ZOOM_DOUBLETAP) == BST_CHECKED;
    config.touchpadPinch = IsDlgButtonChecked(hPage, IDC_ZOOM_PINCH) == BST_CHECKED;
}

// =============================================================================
// About Page
// =============================================================================

HWND SettingsPages::CreateAboutPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage) {
    RegisterPageClass(hInstance);

    HWND hPage = CreateWindowExW(
        WS_EX_CONTROLPARENT, PAGE_CLASS, nullptr,
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        rcPage.left, rcPage.top,
        rcPage.right - rcPage.left,
        rcPage.bottom - rcPage.top,
        hParent, nullptr, hInstance, nullptr
    );

    if (!hPage) return nullptr;

    int y = 30;
    int x = 20;
    int ctrlH = 20;
    int spacing = 25;

    // App name
    CreateLabel(hPage, hInstance, 0, L"Virtual Overlay", x, y, 200, 30);
    y += 35;

    // Version
    std::wstring version = L"Version: ";
    version += L"1.0.0";
    CreateLabel(hPage, hInstance, IDC_ABOUT_VERSION, version.c_str(), x, y, 200, ctrlH);
    y += spacing;

    // Description
    CreateLabel(hPage, hInstance, 0, L"A Windows utility for virtual desktop overlay", x, y, 350, ctrlH);
    y += spacing;
    CreateLabel(hPage, hInstance, 0, L"and macOS-style screen zoom.", x, y, 350, ctrlH);
    y += spacing * 2;

    // GitHub link
    CreateLink(hPage, hInstance, IDC_ABOUT_GITHUB, 
               L"<a href=\"https://github.com/your-repo/virtual-overlay\">GitHub Repository</a>", 
               x, y, 200, ctrlH);
    y += spacing * 2;

    // Copyright
    CreateLabel(hPage, hInstance, 0, L"© 2026 Virtual Overlay Contributors", x, y, 300, ctrlH);

    return hPage;
}

}  // namespace VirtualOverlay

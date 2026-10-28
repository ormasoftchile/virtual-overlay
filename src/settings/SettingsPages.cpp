#include "SettingsPages.h"
#include "SettingsWindow.h"
#include "../utils/Monitor.h"
#include "../presenter/AudioInputManager.h"
#include "../presenter/OfflineSpeechRecognizer.h"
#include "../presenter/PresenterController.h"
#include <algorithm>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace VirtualOverlay {

constexpr wchar_t PAGE_CLASS[] = L"VirtualOverlaySettingsPage";
static bool s_pageClassRegistered = false;
static HBRUSH s_hBrushBackground = nullptr;
static uint32_t s_watermarkColor = 0xFFFFFF;  // Current watermark color for UI
static std::vector<AudioEndpointInfo> s_presenterAudioDevices;
static std::wstring s_presenterVoskModelPath;

static void RefreshPresenterModelLabel(HWND hPage) {
    if (!hPage) return;
    std::wstring found = OfflineSpeechRecognizer::LocateVoskModel(s_presenterVoskModelPath);
    if (!found.empty()) {
        std::filesystem::path p(found);
        std::wstring label = L"Installed: " + p.filename().wstring();
        SetDlgItemTextW(hPage, IDC_PRES_MODEL_STATUS, label.c_str());
    } else {
        SetDlgItemTextW(hPage, IDC_PRES_MODEL_STATUS, L"Not installed (click Download or Browse...)");
    }
}

static void PopulatePresenterChannelCombo(HWND hPage, int channels, int selectedChannel) {
    HWND hCh = GetDlgItem(hPage, IDC_PRES_CHANNEL_COMBO);
    if (!hCh) return;

    SendMessageW(hCh, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCh, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Channels (Mono Mixdown)"));
    int maxCh = std::max(1, channels);
    for (int c = 1; c <= maxCh && c <= 16; ++c) {
        wchar_t buf[64] = {};
        if (c == 1) swprintf_s(buf, L"Channel 1 (Left / Input 1)");
        else if (c == 2) swprintf_s(buf, L"Channel 2 (Right / Input 2)");
        else swprintf_s(buf, L"Channel %d", c);
        SendMessageW(hCh, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
    }

    int sel = (selectedChannel >= 0 && selectedChannel <= maxCh) ? selectedChannel : 0;
    SendMessageW(hCh, CB_SETCURSEL, sel, 0);
}

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
            } else if (id == IDC_PRES_FONT_SIZE) {
                swprintf_s(text, L"%dpt", pos);
                SetDlgItemTextW(hwnd, IDC_PRES_FONT_SIZE_LABEL, text);
            } else if (id == IDC_PRES_WIDTH) {
                swprintf_s(text, L"%dpx", pos);
                SetDlgItemTextW(hwnd, IDC_PRES_WIDTH_LABEL, text);
            } else if (id == IDC_PRES_BG_OPACITY) {
                swprintf_s(text, L"%d%%", pos);
                SetDlgItemTextW(hwnd, IDC_PRES_BG_OPACITY_LABEL, text);
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

            // Handle Presenter microphone selection change -> update channel combo
            if (wmId == IDC_PRES_MIC_COMBO && wmEvent == CBN_SELCHANGE) {
                LRESULT sel = SendDlgItemMessageW(hwnd, IDC_PRES_MIC_COMBO, CB_GETCURSEL, 0, 0);
                int chCount = 1;
                if (sel != CB_ERR && static_cast<size_t>(sel) < s_presenterAudioDevices.size()) {
                    chCount = s_presenterAudioDevices[static_cast<size_t>(sel)].channels;
                }
                PopulatePresenterChannelCombo(hwnd, chCount, 0);
                return 0;
            }

            if (wmId == IDC_PRES_PASTE_SCRIPT && wmEvent == BN_CLICKED) {
                if (PresenterController::Instance().PasteScriptFromClipboard()) {
                    MessageBoxW(hwnd, L"Script loaded from clipboard.", L"Presenter Script", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, L"Clipboard does not contain text.", L"Presenter Script", MB_OK | MB_ICONWARNING);
                }
                return 0;
            }

            if (wmId == IDC_PRES_LOAD_FILE && wmEvent == BN_CLICKED) {
                PresenterController::Instance().PromptAndLoadScriptFile(hwnd);
                return 0;
            }

            if (wmId == IDC_PRES_RESET_POS && wmEvent == BN_CLICKED) {
                PresenterController::Instance().ResetToStart();
                return 0;
            }

            if (wmId == IDC_PRES_BROWSE_MODEL && wmEvent == BN_CLICKED) {
                BROWSEINFOW bi = {};
                bi.hwndOwner = hwnd;
                bi.lpszTitle = L"Select Vosk Speech Model Folder (e.g. vosk-model-small-en-us-0.15)";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    wchar_t folderPath[MAX_PATH] = {};
                    if (SHGetPathFromIDListW(pidl, folderPath)) {
                        if (OfflineSpeechRecognizer::IsValidVoskModelDirectory(folderPath) ||
                            !OfflineSpeechRecognizer::LocateVoskModel(folderPath).empty()) {
                            s_presenterVoskModelPath = folderPath;
                            RefreshPresenterModelLabel(hwnd);
                        } else {
                            MessageBoxW(hwnd,
                                        L"The selected folder does not appear to contain a valid Vosk model (expected am/final.mdl and conf/mfcc.conf).",
                                        L"Invalid Vosk Model", MB_OK | MB_ICONWARNING);
                        }
                    }
                    CoTaskMemFree(pidl);
                }
                return 0;
            }

            if (wmId == IDC_PRES_DOWNLOAD_MODEL && wmEvent == BN_CLICKED) {
                int ans = MessageBoxW(
                    hwnd,
                    L"Download the offline English Vosk speech model (vosk-model-small-en-us-0.15, ~40 MB) from alphacephei.com to your local AppData folder?\n\n"
                    L"Once installed, speech recognition runs 100% offline on your machine.",
                    L"Download Offline Speech Model",
                    MB_YESNO | MB_ICONQUESTION
                );
                if (ans == IDYES) {
                    std::wstring destRoot = OfflineSpeechRecognizer::GetDefaultUserModelsDirectory();
                    std::wstring psCmd =
                        L"-NoProfile -ExecutionPolicy Bypass -Command \""
                        L"$ErrorActionPreference='Stop'; "
                        L"$dir='" + destRoot + L"'; "
                        L"New-Item -ItemType Directory -Force -Path $dir | Out-Null; "
                        L"$zip=Join-Path $dir 'vosk-model-small-en-us-0.15.zip'; "
                        L"Write-Host 'Downloading vosk-model-small-en-us-0.15 (~40 MB)...'; "
                        L"Invoke-WebRequest -Uri 'https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip' -OutFile $zip; "
                        L"Write-Host 'Extracting model...'; "
                        L"Expand-Archive -Path $zip -DestinationPath $dir -Force; "
                        L"Remove-Item $zip -Force; "
                        L"Write-Host 'Done!'\"";
                    SHELLEXECUTEINFOW sei = {};
                    sei.cbSize = sizeof(sei);
                    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                    sei.hwnd = hwnd;
                    sei.lpVerb = L"open";
                    sei.lpFile = L"powershell.exe";
                    sei.lpParameters = psCmd.c_str();
                    sei.nShow = SW_SHOWNORMAL;
                    if (ShellExecuteExW(&sei) && sei.hProcess) {
                        WaitForSingleObject(sei.hProcess, INFINITE);
                        CloseHandle(sei.hProcess);
                        RefreshPresenterModelLabel(hwnd);
                    }
                }
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
        return nullptr;
    }

    int y = 20;
    int x = 20;
    int labelW = 200;
    int ctrlH = 22;
    int spacing = 30;

    CreateCheckbox(hPage, hInstance, IDC_GEN_START_WINDOWS, L"Start with Windows", x, y, labelW, ctrlH);
    y += spacing;

    CreateCheckbox(hPage, hInstance, IDC_GEN_SHOW_TRAY, L"Show tray icon", x, y, labelW, ctrlH);
    y += spacing;

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

    CreateCheckbox(hPage, hInstance, IDC_OVL_ENABLE, L"Enable overlay", x, y, 150, ctrlH);
    y += spacing;

    CreateLabel(hPage, hInstance, 0, L"Mode:", x, y + 2, labelW, ctrlH);
    HWND hMode = CreateComboBox(hPage, hInstance, IDC_OVL_MODE, x + labelW, y, ctrlW, 100);
    SendMessageW(hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Notification"));
    SendMessageW(hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Watermark"));
    y += spacing;

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

    CreateLabel(hPage, hInstance, 0, L"Monitor:", x, y + 2, labelW, ctrlH);
    HWND hMon = CreateComboBox(hPage, hInstance, IDC_OVL_MONITOR, x + labelW, y, ctrlW, 150);
    SendMessageW(hMon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cursor Monitor"));
    SendMessageW(hMon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Primary Monitor"));
    SendMessageW(hMon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Monitors"));
    y += spacing;

    CreateLabel(hPage, hInstance, 0, L"Format:", x, y + 2, labelW, ctrlH);
    CreateEdit(hPage, hInstance, IDC_OVL_FORMAT, x + labelW, y, ctrlW, ctrlH);
    y += spacing;

    CreateLabel(hPage, hInstance, 0, L"WM Size:", x, y + 2, labelW, ctrlH);
    CreateSlider(hPage, hInstance, IDC_OVL_WATERMARK_SIZE, x + labelW, y, ctrlW, 25, 24, 144);
    CreateLabel(hPage, hInstance, IDC_OVL_WATERMARK_SIZE_LABEL, L"72", x + labelW + ctrlW + 10, y + 2, 40, ctrlH);
    y += spacing;

    CreateLabel(hPage, hInstance, 0, L"WM Opacity:", x, y + 2, labelW, ctrlH);
    CreateSlider(hPage, hInstance, IDC_OVL_WATERMARK_OPACITY, x + labelW, y, ctrlW, 25, 10, 100);
    CreateLabel(hPage, hInstance, IDC_OVL_WATERMARK_OPACITY_LABEL, L"25%", x + labelW + ctrlW + 10, y + 2, 40, ctrlH);
    y += spacing;

    CreateLabel(hPage, hInstance, 0, L"WM Color:", x, y + 2, labelW, ctrlH);
    CreateButton(hPage, hInstance, IDC_OVL_WATERMARK_COLOR_BTN, L"Choose...", x + labelW, y, 80, ctrlH);
    CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", 
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        x + labelW + 85, y, 30, ctrlH, hPage, 
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OVL_WATERMARK_COLOR)), hInstance, nullptr);
    y += spacing;

    CreateCheckbox(hPage, hInstance, IDC_OVL_AUTOHIDE, L"Auto-hide after", x, y, 100, ctrlH);
    CreateEdit(hPage, hInstance, IDC_OVL_AUTOHIDE_DELAY, x + 105, y, 50, ctrlH);
    CreateLabel(hPage, hInstance, IDC_OVL_AUTOHIDE_MS_LABEL, L"ms", x + 160, y + 2, 30, ctrlH);
    y += spacing;
    
    CreateCheckbox(hPage, hInstance, IDC_OVL_DODGE, L"Dodge cursor", x, y, 100, ctrlH);
    CreateSlider(hPage, hInstance, IDC_OVL_DODGE_PROXIMITY, x + 105, y, 100, ctrlH, 20, 200);
    CreateLabel(hPage, hInstance, IDC_OVL_DODGE_PROXIMITY_LABEL, L"100px", x + 210, y + 2, 40, ctrlH);
    y += spacing + 5;

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

    SendDlgItemMessageW(hPage, IDC_OVL_WATERMARK_SIZE, TBM_SETPOS, TRUE, config.watermarkFontSize);
    wchar_t sizeText[16];
    swprintf_s(sizeText, L"%d", config.watermarkFontSize);
    SetDlgItemTextW(hPage, IDC_OVL_WATERMARK_SIZE_LABEL, sizeText);

    int wmOpacity = static_cast<int>(config.watermarkOpacity * 100.0f);
    SendDlgItemMessageW(hPage, IDC_OVL_WATERMARK_OPACITY, TBM_SETPOS, TRUE, wmOpacity);
    wchar_t opacityText[16];
    swprintf_s(opacityText, L"%d%%", wmOpacity);
    SetDlgItemTextW(hPage, IDC_OVL_WATERMARK_OPACITY_LABEL, opacityText);

    s_watermarkColor = config.watermarkColor;
    HWND hColorPreview = GetDlgItem(hPage, IDC_OVL_WATERMARK_COLOR);
    if (hColorPreview) {
        InvalidateRect(hColorPreview, nullptr, TRUE);
    }

    CheckDlgButton(hPage, IDC_OVL_AUTOHIDE, config.autoHide ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(hPage, IDC_OVL_AUTOHIDE_DELAY, config.autoHideDelayMs, FALSE);
    
    CheckDlgButton(hPage, IDC_OVL_DODGE, config.dodgeOnHover ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(hPage, IDC_OVL_DODGE_PROXIMITY, TBM_SETPOS, TRUE, config.dodgeProximity);
    wchar_t proxText[16];
    swprintf_s(proxText, L"%dpx", config.dodgeProximity);
    SetDlgItemTextW(hPage, IDC_OVL_DODGE_PROXIMITY_LABEL, proxText);
    
    bool isWatermark = (config.mode == OverlayMode::Watermark);
    int showCmd = isWatermark ? SW_HIDE : SW_SHOW;
    ShowWindow(GetDlgItem(hPage, IDC_OVL_AUTOHIDE), showCmd);
    ShowWindow(GetDlgItem(hPage, IDC_OVL_AUTOHIDE_DELAY), showCmd);
    ShowWindow(GetDlgItem(hPage, IDC_OVL_AUTOHIDE_MS_LABEL), showCmd);
}

void SettingsPages::SaveOverlaySettings(HWND hPage, OverlayConfig& config) {
    if (!hPage) return;

    config.enabled = IsDlgButtonChecked(hPage, IDC_OVL_ENABLE) == BST_CHECKED;

    LRESULT modeResult = SendDlgItemMessageW(hPage, IDC_OVL_MODE, CB_GETCURSEL, 0, 0);
    if (modeResult != CB_ERR && modeResult >= 0) {
        config.mode = static_cast<OverlayMode>(modeResult);
    }

    LRESULT posResult = SendDlgItemMessageW(hPage, IDC_OVL_POSITION, CB_GETCURSEL, 0, 0);
    if (posResult != CB_ERR && posResult >= 0) {
        config.position = static_cast<OverlayPosition>(posResult);
    }

    LRESULT monResult = SendDlgItemMessageW(hPage, IDC_OVL_MONITOR, CB_GETCURSEL, 0, 0);
    if (monResult != CB_ERR && monResult >= 0) {
        config.monitor = static_cast<MonitorSelection>(monResult);
    }

    wchar_t format[256] = {};
    GetDlgItemTextW(hPage, IDC_OVL_FORMAT, format, 255);
    config.format = format;

    int wmSize = static_cast<int>(SendDlgItemMessageW(hPage, IDC_OVL_WATERMARK_SIZE, TBM_GETPOS, 0, 0));
    config.watermarkFontSize = wmSize;

    int wmOpacity = static_cast<int>(SendDlgItemMessageW(hPage, IDC_OVL_WATERMARK_OPACITY, TBM_GETPOS, 0, 0));
    config.watermarkOpacity = wmOpacity / 100.0f;

    config.watermarkColor = s_watermarkColor;

    config.autoHide = IsDlgButtonChecked(hPage, IDC_OVL_AUTOHIDE) == BST_CHECKED;
    config.autoHideDelayMs = GetDlgItemInt(hPage, IDC_OVL_AUTOHIDE_DELAY, nullptr, FALSE);
    
    config.dodgeOnHover = IsDlgButtonChecked(hPage, IDC_OVL_DODGE) == BST_CHECKED;
    config.dodgeProximity = static_cast<int>(SendDlgItemMessageW(hPage, IDC_OVL_DODGE_PROXIMITY, TBM_GETPOS, 0, 0));
}

// =============================================================================
// Flip Screen Page (Teleprompter Mirror)
// =============================================================================

HWND SettingsPages::CreateFlipPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage) {
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

    int y = 14;
    int x = 20;
    int ctrlH = 20;
    int spacing = 24;

    CreateCheckbox(hPage, hInstance, IDC_FLIP_ENABLE,
                   L"Enable horizontal flip (teleprompter mirror)", x, y, 350, ctrlH);
    y += spacing;

    CreateLabel(hPage, hInstance, 0, L"Select screen(s) to flip:", x, y, 300, ctrlH);
    y += 22;

    const auto& monitors = Monitor::Instance().GetMonitors();
    if (monitors.empty()) {
        CreateLabel(hPage, hInstance, 0, L"No monitors detected", x + 15, y, 200, ctrlH);
        y += 22;
    } else {
        for (size_t i = 0; i < monitors.size() && i < IDC_FLIP_MONITOR_MAX; ++i) {
            const auto& mon = monitors[i];
            std::wstring label = mon.GetDisplayName(i);

            CreateCheckbox(hPage, hInstance,
                           IDC_FLIP_MONITOR_BASE + static_cast<int>(i),
                           label.c_str(), x + 15, y, 420, ctrlH);
            y += 22;
        }
    }
    y += 4;

    CreateCheckbox(hPage, hInstance, IDC_FLIP_INVERT_MOUSE,
                   L"Mirror pointer movement (natural through the glass)", x, y, 420, ctrlH);
    y += spacing;

    CreateLabel(hPage, hInstance, 0, L"Pointer:", x, y + 2, 100, ctrlH);
    HWND hCursorMode = CreateComboBox(hPage, hInstance, IDC_FLIP_CURSOR_MODE, x + 105, y, 200, 100);
    SendMessageW(hCursorMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Mirrored (click-accurate)"));
    SendMessageW(hCursorMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"System (unmirrored)"));
    y += spacing;

    CreateLabel(hPage, hInstance, 0, L"Toggle hotkey:", x, y + 2, 100, ctrlH);
    CreateEdit(hPage, hInstance, IDC_FLIP_HOTKEY, x + 105, y, 160, ctrlH);
    y += spacing + 4;

    CreateLabel(hPage, hInstance, 0,
                L"Tip: Press Ctrl+Shift+F to toggle flip on the screen under your mouse,\n"
                L"or right-click the Virtual Overlay tray icon -> 'Flip screen'.",
                x, y, 420, 36);

    return hPage;
}

void SettingsPages::LoadFlipSettings(HWND hPage, const FlipConfig& config) {
    if (!hPage) return;

    CheckDlgButton(hPage, IDC_FLIP_ENABLE, config.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hPage, IDC_FLIP_INVERT_MOUSE, config.invertMouse ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(hPage, IDC_FLIP_HOTKEY, config.toggleHotkey.c_str());

    const int cursorSel = (config.cursorMode == FlipCursorMode::None) ? 1 : 0;
    SendDlgItemMessageW(hPage, IDC_FLIP_CURSOR_MODE, CB_SETCURSEL, cursorSel, 0);

    const auto& monitors = Monitor::Instance().GetMonitors();
    for (size_t i = 0; i < monitors.size() && i < IDC_FLIP_MONITOR_MAX; ++i) {
        bool isFlipped = std::any_of(config.monitors.begin(), config.monitors.end(),
            [&](const std::wstring& id) { return monitors[i].MatchesId(id); });
        CheckDlgButton(hPage, IDC_FLIP_MONITOR_BASE + static_cast<int>(i),
                       isFlipped ? BST_CHECKED : BST_UNCHECKED);
    }
}

void SettingsPages::SaveFlipSettings(HWND hPage, FlipConfig& config) {
    if (!hPage) return;

    config.enabled = (IsDlgButtonChecked(hPage, IDC_FLIP_ENABLE) == BST_CHECKED);
    config.invertMouse = (IsDlgButtonChecked(hPage, IDC_FLIP_INVERT_MOUSE) == BST_CHECKED);

    wchar_t hotkeyBuf[64] = {};
    GetDlgItemTextW(hPage, IDC_FLIP_HOTKEY, hotkeyBuf, 64);
    if (wcslen(hotkeyBuf) > 0) {
        config.toggleHotkey = hotkeyBuf;
    }

    LRESULT curSel = SendDlgItemMessageW(hPage, IDC_FLIP_CURSOR_MODE, CB_GETCURSEL, 0, 0);
    if (curSel == 0) config.cursorMode = FlipCursorMode::Magnified;
    else if (curSel == 1) config.cursorMode = FlipCursorMode::None;

    const auto& monitors = Monitor::Instance().GetMonitors();
    std::vector<std::wstring> updatedMonitors;

    for (const auto& existingId : config.monitors) {
        bool connectedNow = std::any_of(monitors.begin(), monitors.end(),
            [&](const MonitorInfo& m) { return m.MatchesId(existingId); });
        if (!connectedNow) {
            updatedMonitors.push_back(existingId);
        }
    }

    bool anyCheckedNow = false;
    for (size_t i = 0; i < monitors.size() && i < IDC_FLIP_MONITOR_MAX; ++i) {
        if (IsDlgButtonChecked(hPage, IDC_FLIP_MONITOR_BASE + static_cast<int>(i)) == BST_CHECKED) {
            updatedMonitors.push_back(monitors[i].GetId());
            anyCheckedNow = true;
        }
    }
    config.monitors = std::move(updatedMonitors);
    if (anyCheckedNow) {
        config.enabled = true;
    }
}

// =============================================================================
// Presenter Page (Native Speech-Following Teleprompter)
// =============================================================================

HWND SettingsPages::CreatePresenterPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage) {
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

    int y = 10;
    int x = 18;
    int labelW = 105;
    int ctrlW = 230;
    int ctrlH = 20;
    int spacing = 26;

    // Enable Presenter checkbox
    CreateCheckbox(hPage, hInstance, IDC_PRES_ENABLE,
                   L"Show Presenter caption surface (Ctrl+Shift+P)", x, y, 360, ctrlH);
    y += spacing;

    // Microphone selector
    CreateLabel(hPage, hInstance, 0, L"Microphone:", x, y + 2, labelW, ctrlH);
    CreateComboBox(hPage, hInstance, IDC_PRES_MIC_COMBO, x + labelW, y, 320, 200);
    y += spacing;

    // Input channel selector
    CreateLabel(hPage, hInstance, 0, L"Input channel:", x, y + 2, labelW, ctrlH);
    CreateComboBox(hPage, hInstance, IDC_PRES_CHANNEL_COMBO, x + labelW, y, 220, 150);
    y += spacing;

    // Font size slider
    CreateLabel(hPage, hInstance, 0, L"Font size:", x, y + 2, labelW, ctrlH);
    CreateSlider(hPage, hInstance, IDC_PRES_FONT_SIZE, x + labelW, y, ctrlW, 24, 18, 72);
    CreateLabel(hPage, hInstance, IDC_PRES_FONT_SIZE_LABEL, L"32pt", x + labelW + ctrlW + 8, y + 2, 50, ctrlH);
    y += spacing;

    // Caption width slider
    CreateLabel(hPage, hInstance, 0, L"Caption width:", x, y + 2, labelW, ctrlH);
    CreateSlider(hPage, hInstance, IDC_PRES_WIDTH, x + labelW, y, ctrlW, 24, 360, 1600);
    CreateLabel(hPage, hInstance, IDC_PRES_WIDTH_LABEL, L"840px", x + labelW + ctrlW + 8, y + 2, 55, ctrlH);
    y += spacing;

    // Background checkbox & opacity slider
    CreateCheckbox(hPage, hInstance, IDC_PRES_SHOW_BG, L"Background:", x, y, labelW, ctrlH);
    CreateSlider(hPage, hInstance, IDC_PRES_BG_OPACITY, x + labelW, y, ctrlW, 24, 10, 100);
    CreateLabel(hPage, hInstance, IDC_PRES_BG_OPACITY_LABEL, L"76%", x + labelW + ctrlW + 8, y + 2, 50, ctrlH);
    y += spacing;

    // Lines layout
    CreateLabel(hPage, hInstance, 0, L"Visible lines:", x, y + 2, labelW, ctrlH);
    HWND hLines = CreateComboBox(hPage, hInstance, IDC_PRES_LINES_COMBO, x + labelW, y, 220, 150);
    SendMessageW(hLines, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current + 1 Upcoming"));
    SendMessageW(hLines, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current + 2 Upcoming"));
    SendMessageW(hLines, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1 Previous + Current + 1 Upcoming"));
    SendMessageW(hLines, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1 Previous + Current + 2 Upcoming"));
    y += spacing;

    // Script controls
    CreateLabel(hPage, hInstance, 0, L"Script:", x, y + 3, labelW, ctrlH);
    CreateButton(hPage, hInstance, IDC_PRES_PASTE_SCRIPT, L"Paste Clipboard", x + labelW, y, 108, 23);
    CreateButton(hPage, hInstance, IDC_PRES_LOAD_FILE, L"Load File...", x + labelW + 114, y, 95, 23);
    CreateButton(hPage, hInstance, IDC_PRES_RESET_POS, L"Reset Top", x + labelW + 215, y, 85, 23);
    y += spacing + 2;

    // Offline Vosk Model status & actions
    CreateLabel(hPage, hInstance, 0, L"Speech model:", x, y + 2, labelW, ctrlH);
    CreateLabel(hPage, hInstance, IDC_PRES_MODEL_STATUS, L"Checking...", x + labelW, y + 2, 330, ctrlH);
    y += 22;

    CreateButton(hPage, hInstance, IDC_PRES_DOWNLOAD_MODEL, L"Download English Model (~40 MB)...", x + labelW, y, 215, 23);
    CreateButton(hPage, hInstance, IDC_PRES_BROWSE_MODEL, L"Browse Folder...", x + labelW + 222, y, 105, 23);
    y += 28;

    CreateLabel(hPage, hInstance, 0,
                L"Tip: Hold Alt + left-drag anywhere on the caption to position the reading line right above your camera lens.",
                x, y, 440, 32);

    return hPage;
}

void SettingsPages::LoadPresenterSettings(HWND hPage, const PresenterConfig& config) {
    if (!hPage) return;

    CheckDlgButton(hPage, IDC_PRES_ENABLE, config.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hPage, IDC_PRES_SHOW_BG, config.showBackground ? BST_CHECKED : BST_UNCHECKED);

    // Populate microphone dropdown
    s_presenterAudioDevices = AudioInputManager::Instance().EnumerateCaptureDevices();
    HWND hMic = GetDlgItem(hPage, IDC_PRES_MIC_COMBO);
    int selectedMicIdx = 0;
    int selectedMicChannels = 1;
    if (hMic) {
        SendMessageW(hMic, CB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < s_presenterAudioDevices.size(); ++i) {
            SendMessageW(hMic, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s_presenterAudioDevices[i].friendlyName.c_str()));
            if (s_presenterAudioDevices[i].id == config.audioDeviceId) {
                selectedMicIdx = static_cast<int>(i);
                selectedMicChannels = s_presenterAudioDevices[i].channels;
            }
        }
        SendMessageW(hMic, CB_SETCURSEL, selectedMicIdx, 0);
    }

    PopulatePresenterChannelCombo(hPage, selectedMicChannels, config.audioChannel);

    // Sliders
    SendDlgItemMessageW(hPage, IDC_PRES_FONT_SIZE, TBM_SETPOS, TRUE, config.fontSize);
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%dpt", config.fontSize);
    SetDlgItemTextW(hPage, IDC_PRES_FONT_SIZE_LABEL, buf);

    SendDlgItemMessageW(hPage, IDC_PRES_WIDTH, TBM_SETPOS, TRUE, config.captionWidth);
    swprintf_s(buf, L"%dpx", config.captionWidth);
    SetDlgItemTextW(hPage, IDC_PRES_WIDTH_LABEL, buf);

    int bgOp = static_cast<int>(std::round(config.bgOpacity * 100.0f));
    SendDlgItemMessageW(hPage, IDC_PRES_BG_OPACITY, TBM_SETPOS, TRUE, bgOp);
    swprintf_s(buf, L"%d%%", bgOp);
    SetDlgItemTextW(hPage, IDC_PRES_BG_OPACITY_LABEL, buf);

    int linesIdx = 3;
    if (config.previousLines == 0 && config.upcomingLines == 1) linesIdx = 0;
    else if (config.previousLines == 0 && config.upcomingLines >= 2) linesIdx = 1;
    else if (config.previousLines >= 1 && config.upcomingLines == 1) linesIdx = 2;
    else linesIdx = 3;
    SendDlgItemMessageW(hPage, IDC_PRES_LINES_COMBO, CB_SETCURSEL, linesIdx, 0);

    s_presenterVoskModelPath = config.voskModelPath;
    RefreshPresenterModelLabel(hPage);
}

void SettingsPages::SavePresenterSettings(HWND hPage, PresenterConfig& config) {
    if (!hPage) return;

    config.enabled = (IsDlgButtonChecked(hPage, IDC_PRES_ENABLE) == BST_CHECKED);
    config.showBackground = (IsDlgButtonChecked(hPage, IDC_PRES_SHOW_BG) == BST_CHECKED);

    LRESULT micSel = SendDlgItemMessageW(hPage, IDC_PRES_MIC_COMBO, CB_GETCURSEL, 0, 0);
    if (micSel != CB_ERR && static_cast<size_t>(micSel) < s_presenterAudioDevices.size()) {
        const auto& dev = s_presenterAudioDevices[static_cast<size_t>(micSel)];
        config.audioDeviceId = dev.id;
        config.audioDeviceName = (dev.id == L"default") ? L"" : dev.friendlyName;
    }

    LRESULT chSel = SendDlgItemMessageW(hPage, IDC_PRES_CHANNEL_COMBO, CB_GETCURSEL, 0, 0);
    if (chSel != CB_ERR && chSel >= 0) {
        config.audioChannel = static_cast<int>(chSel);
    }

    config.fontSize = static_cast<int>(SendDlgItemMessageW(hPage, IDC_PRES_FONT_SIZE, TBM_GETPOS, 0, 0));
    config.captionWidth = static_cast<int>(SendDlgItemMessageW(hPage, IDC_PRES_WIDTH, TBM_GETPOS, 0, 0));
    int bgOp = static_cast<int>(SendDlgItemMessageW(hPage, IDC_PRES_BG_OPACITY, TBM_GETPOS, 0, 0));
    config.bgOpacity = static_cast<float>(bgOp) / 100.0f;

    LRESULT linesSel = SendDlgItemMessageW(hPage, IDC_PRES_LINES_COMBO, CB_GETCURSEL, 0, 0);
    switch (linesSel) {
        case 0: config.previousLines = 0; config.upcomingLines = 1; break;
        case 1: config.previousLines = 0; config.upcomingLines = 2; break;
        case 2: config.previousLines = 1; config.upcomingLines = 1; break;
        case 3:
        default: config.previousLines = 1; config.upcomingLines = 2; break;
    }

    config.voskModelPath = s_presenterVoskModelPath;
    // Preserve live script text & reading position from PresenterController
    config.scriptText = PresenterController::Instance().GetScriptText();
    config.savedTokenIndex = PresenterController::Instance().GetCurrentTokenIndex();
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

    int y = 24;
    int x = 20;
    int ctrlH = 20;
    int spacing = 24;

    CreateLabel(hPage, hInstance, 0, L"Virtual Overlay", x, y, 200, 26);
    y += 30;

    std::wstring version = L"Version: 1.2.0";
    CreateLabel(hPage, hInstance, IDC_ABOUT_VERSION, version.c_str(), x, y, 200, ctrlH);
    y += spacing;

    CreateLabel(hPage, hInstance, 0, L"A Windows utility for virtual desktop overlay,", x, y, 380, ctrlH);
    y += 20;
    CreateLabel(hPage, hInstance, 0, L"teleprompter screen flipping, and offline speech-following presenter.", x, y, 420, ctrlH);
    y += spacing + 8;

    CreateLink(hPage, hInstance, IDC_ABOUT_GITHUB, 
               L"<a href=\"https://github.com/ormasoftchile/virtual-overlay\">GitHub Repository</a>", 
               x, y, 200, ctrlH);
    y += spacing + 8;

    CreateLabel(hPage, hInstance, 0, L"\u00A9 2026 Virtual Overlay Contributors", x, y, 350, ctrlH);
    y += 20;
    CreateLabel(hPage, hInstance, 0, L"Speech alignment adapted from TalkPrompter (\u00A9 2026 Sven Bosau, MIT).", x, y, 440, ctrlH);

    return hPage;
}

}  // namespace VirtualOverlay

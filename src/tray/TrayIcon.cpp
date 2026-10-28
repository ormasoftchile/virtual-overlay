#include "TrayIcon.h"
#include "../utils/Logger.h"
#include "../utils/Monitor.h"
#include "../config/Defaults.h"
#include "../config/Config.h"
#include "../presenter/PresenterController.h"
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")

namespace VirtualOverlay {

// Registry key for auto-start
constexpr wchar_t AUTO_START_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t AUTO_START_VALUE[] = L"VirtualOverlay";

TrayIcon& TrayIcon::Instance() {
    static TrayIcon instance;
    return instance;
}

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    if (m_initialized) {
        Shutdown();
    }
}

bool TrayIcon::Init(HINSTANCE hInstance, HWND hParentWnd) {
    LOG_INFO("TrayIcon::Init called with hInstance=%p, hParentWnd=%p", 
             (void*)hInstance, (void*)hParentWnd);
    
    if (m_initialized) {
        LOG_INFO("TrayIcon already initialized");
        return true;
    }

    m_hInstance = hInstance;
    m_hParentWnd = hParentWnd;

    // Allow messages across UIPI in case running elevated
    static const UINT s_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(hParentWnd, WM_TRAYICON, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hParentWnd, s_wmTaskbarCreated, MSGFLT_ALLOW, nullptr);

    // Initialize NOTIFYICONDATA
    m_nid = {};
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = hParentWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.uVersion = NOTIFYICON_VERSION_4;

    // Load small icon sized for the notification area (16x16 / DPI-scaled)
    m_nid.hIcon = static_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    if (!m_nid.hIcon) {
        // Fallback to default application icon
        m_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    // Set tooltip
    wcscpy_s(m_nid.szTip, L"Virtual Overlay");

    // Create context menu
    BuildContextMenu();

    m_initialized = true;
    LOG_INFO("TrayIcon initialized");
    return true;
}

void TrayIcon::Shutdown() {
    if (!m_initialized) {
        return;
    }

    Hide();

    if (m_hMenu) {
        DestroyMenu(m_hMenu);
        m_hMenu = nullptr;
    }

    if (m_nid.hIcon && m_nid.hIcon != LoadIconW(nullptr, IDI_APPLICATION)) {
        DestroyIcon(m_nid.hIcon);
    }

    m_initialized = false;
    LOG_INFO("TrayIcon shutdown");
}

void TrayIcon::BuildContextMenu() {
    if (m_hMenu) {
        DestroyMenu(m_hMenu);
        m_hMenu = nullptr;
    }

    m_hMenu = CreatePopupMenu();
    if (!m_hMenu) {
        return;
    }

    AppendMenuW(m_hMenu, MF_STRING, IDM_TRAY_SETTINGS, L"&Settings...");
    AppendMenuW(m_hMenu, MF_SEPARATOR, 0, nullptr);

    // Presenter (speech-following teleprompter) submenu
    HMENU hPresMenu = CreatePopupMenu();
    if (hPresMenu) {
        auto& pres = PresenterController::Instance();
        UINT openFlags = MF_STRING | (pres.IsOpen() ? MF_CHECKED : 0);
        AppendMenuW(hPresMenu, openFlags, IDM_TRAY_PRES_TOGGLE, L"&Show Presenter\tCtrl+Shift+P");

        UINT pauseFlags = MF_STRING | (pres.IsPaused() ? MF_CHECKED : 0);
        if (!pres.IsOpen()) pauseFlags |= MF_GRAYED;
        AppendMenuW(hPresMenu, pauseFlags, IDM_TRAY_PRES_PAUSE, L"&Pause Speech Tracking");

        AppendMenuW(hPresMenu, MF_STRING, IDM_TRAY_PRES_RESET, L"&Reset to Beginning");
        AppendMenuW(hPresMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hPresMenu, MF_STRING, IDM_TRAY_PRES_PASTE, L"Paste Script from &Clipboard");
        AppendMenuW(hPresMenu, MF_STRING, IDM_TRAY_PRES_LOAD, L"&Load Script File (.txt / .md)...");

        // Microphone submenu inside Presenter menu
        HMENU hMicMenu = CreatePopupMenu();
        if (hMicMenu) {
            const auto& selId = Config::Instance().Get().presenter.audioDeviceId;
            auto devices = pres.GetAudioDevices();
            for (size_t i = 0; i < devices.size() && i < IDM_TRAY_PRES_MIC_MAX; ++i) {
                UINT mFlags = MF_STRING | (devices[i].id == selId ? MF_CHECKED : 0);
                AppendMenuW(hMicMenu, mFlags,
                            IDM_TRAY_PRES_MIC_BASE + static_cast<UINT>(i),
                            devices[i].friendlyName.c_str());
            }
            AppendMenuW(hPresMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hPresMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hMicMenu), L"&Microphone");
        }

        AppendMenuW(m_hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hPresMenu),
                    L"&Presenter (Speech Following)");
    }

    // Refresh monitors so newly attached/detached displays are current
    Monitor::Instance().Refresh();

    // "Flip screen" submenu listing each monitor (teleprompter mirror).
    const auto& monitors = Monitor::Instance().GetMonitors();
    if (!monitors.empty()) {
        HMENU hFlipMenu = CreatePopupMenu();
        if (hFlipMenu) {
            for (size_t i = 0; i < monitors.size() && i < IDM_TRAY_FLIP_MAX; ++i) {
                const auto& mon = monitors[i];
                std::wstring label = mon.GetDisplayName(i);

                UINT flags = MF_STRING;
                if (m_isFlipped && m_isFlipped(mon.GetId())) {
                    flags |= MF_CHECKED;
                }

                AppendMenuW(hFlipMenu, flags,
                            IDM_TRAY_FLIP_BASE + static_cast<UINT>(i), label.c_str());
            }
            AppendMenuW(m_hMenu, MF_POPUP,
                        reinterpret_cast<UINT_PTR>(hFlipMenu),
                        L"&Flip screen (teleprompter)");
        }
    }

    AppendMenuW(m_hMenu, MF_SEPARATOR, 0, nullptr);

    UINT autoStartFlags = MF_STRING;
    if (IsAutoStartEnabled()) {
        autoStartFlags |= MF_CHECKED;
    }
    AppendMenuW(m_hMenu, autoStartFlags, IDM_TRAY_AUTOSTART, L"Start with &Windows");

    AppendMenuW(m_hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_hMenu, MF_STRING, IDM_TRAY_ABOUT, L"&About...");
    AppendMenuW(m_hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_hMenu, MF_STRING, IDM_TRAY_EXIT, L"E&xit");
}

// In Windows 11, taskbar icons can be hidden in an overflow menu or completely suppressed
// if "Hidden icon menu" is off. Marking IsPromoted = 1 ensures Windows 11 pins the icon
// directly onto the visible taskbar next to the clock.
static void PromoteInNotifyIconSettings() {
    wchar_t currentExe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, currentExe, MAX_PATH);

    HKEY hRoot = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings", 0, KEY_READ, &hRoot) != ERROR_SUCCESS) {
        return;
    }

    wchar_t subKeyName[256];
    DWORD index = 0;
    DWORD subKeyNameLen = 256;

    while (RegEnumKeyExW(hRoot, index, subKeyName, &subKeyNameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        HKEY hSub = nullptr;
        if (RegOpenKeyExW(hRoot, subKeyName, 0, KEY_READ | KEY_WRITE, &hSub) == ERROR_SUCCESS) {
            wchar_t exePath[MAX_PATH] = {};
            DWORD exePathSize = sizeof(exePath);
            DWORD type = 0;
            if (RegQueryValueExW(hSub, L"ExecutablePath", nullptr, &type, reinterpret_cast<LPBYTE>(exePath), &exePathSize) == ERROR_SUCCESS) {
                if (_wcsicmp(exePath, currentExe) == 0) {
                    DWORD isPromoted = 1;
                    RegSetValueExW(hSub, L"IsPromoted", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&isPromoted), sizeof(isPromoted));
                    RegCloseKey(hSub);
                    break;
                }
            }
            RegCloseKey(hSub);
        }
        index++;
        subKeyNameLen = 256;
    }
    RegCloseKey(hRoot);
}

bool TrayIcon::AddIcon() {
    if (!Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        DWORD err = GetLastError();
        LOG_WARN("Shell_NotifyIcon NIM_ADD failed (attempt %d/%d): %lu",
                 m_addRetryCount + 1, MAX_TRAY_RETRIES, err);
        if (m_addRetryCount < MAX_TRAY_RETRIES) {
            SetTimer(m_hParentWnd, TIMER_TRAY_RETRY, TRAY_RETRY_INTERVAL_MS, nullptr);
        } else {
            KillTimer(m_hParentWnd, TIMER_TRAY_RETRY);
            LOG_ERROR("Tray icon failed after %d retries — giving up", MAX_TRAY_RETRIES);
        }
        return false;
    }

    KillTimer(m_hParentWnd, TIMER_TRAY_RETRY);
    m_addRetryCount = 0;

    m_nid.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &m_nid)) {
        LOG_WARN("Failed to set tray icon version: %lu", GetLastError());
    }

    // Ensure Windows 11 promotes icon to visible taskbar
    PromoteInNotifyIconSettings();

    m_visible = true;
    LOG_INFO("Tray icon added successfully");
    return true;
}

void TrayIcon::Show() {
    if (!m_initialized || m_visible) {
        return;
    }

    if (AddIcon()) {
        LOG_DEBUG("Tray icon shown");
    }
}

void TrayIcon::Restore() {
    if (!m_initialized) {
        return;
    }

    if (AddIcon()) {
        LOG_INFO("Tray icon restored after shell restart");
    }
}

void TrayIcon::OnRetryTimer() {
    m_addRetryCount++;
    LOG_INFO("Retrying tray icon add (attempt %d/%d)", m_addRetryCount, MAX_TRAY_RETRIES);
    AddIcon();
}

void TrayIcon::Hide() {
    if (!m_initialized || !m_visible) {
        return;
    }

    Shell_NotifyIconW(NIM_DELETE, &m_nid);
    m_visible = false;
    LOG_DEBUG("Tray icon hidden");
}

void TrayIcon::SetTooltip(const wchar_t* tooltip) {
    if (!m_initialized) {
        return;
    }

    wcscpy_s(m_nid.szTip, tooltip);
    
    if (m_visible) {
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
}

void TrayIcon::HandleMessage(WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(wParam);

    switch (LOWORD(lParam)) {
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            ShowContextMenu();
            break;

        case WM_LBUTTONDBLCLK:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            // Left click, Enter key, or double-click opens settings
            if (m_onSettings) {
                m_onSettings();
            }
            break;

        default:
            break;
    }
}

void TrayIcon::ShowContextMenu() {
    // Rebuild so the monitor list and flip check states are current.
    BuildContextMenu();
    if (!m_hMenu) {
        return;
    }

    // Get cursor position
    POINT pt;
    GetCursorPos(&pt);

    // Required for menu to work correctly
    SetForegroundWindow(m_hParentWnd);

    // Show menu
    UINT cmd = TrackPopupMenu(
        m_hMenu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        pt.x, pt.y,
        0,
        m_hParentWnd,
        nullptr
    );

    // Handle menu selection
    switch (cmd) {
        case IDM_TRAY_SETTINGS:
            if (m_onSettings) {
                m_onSettings();
            }
            break;

        case IDM_TRAY_ABOUT:
            if (m_onAbout) {
                m_onAbout();
            }
            break;

        case IDM_TRAY_EXIT:
            if (m_onExit) {
                m_onExit();
            }
            break;

        case IDM_TRAY_AUTOSTART:
            SetAutoStart(!IsAutoStartEnabled());
            break;

        case IDM_TRAY_PRES_TOGGLE:
            PresenterController::Instance().Toggle();
            break;

        case IDM_TRAY_PRES_PAUSE:
            PresenterController::Instance().TogglePause();
            break;

        case IDM_TRAY_PRES_RESET:
            PresenterController::Instance().ResetToStart();
            break;

        case IDM_TRAY_PRES_PASTE:
            PresenterController::Instance().PasteScriptFromClipboard();
            break;

        case IDM_TRAY_PRES_LOAD:
            PresenterController::Instance().PromptAndLoadScriptFile(m_hParentWnd);
            break;

        default:
            if (cmd >= IDM_TRAY_FLIP_BASE && cmd < IDM_TRAY_FLIP_BASE + IDM_TRAY_FLIP_MAX) {
                const auto& monitors = Monitor::Instance().GetMonitors();
                const size_t index = cmd - IDM_TRAY_FLIP_BASE;
                if (index < monitors.size() && m_onFlipToggle) {
                    m_onFlipToggle(monitors[index].GetId());
                }
            } else if (cmd >= IDM_TRAY_PRES_MIC_BASE && cmd < IDM_TRAY_PRES_MIC_BASE + IDM_TRAY_PRES_MIC_MAX) {
                auto devices = PresenterController::Instance().GetAudioDevices();
                const size_t index = cmd - IDM_TRAY_PRES_MIC_BASE;
                if (index < devices.size()) {
                    PresenterController::Instance().SelectAudioDevice(devices[index].id, devices[index].friendlyName, 0);
                }
            }
            break;
    }

    // Required for menu to work correctly
    PostMessageW(m_hParentWnd, WM_NULL, 0, 0);
}

bool TrayIcon::IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTO_START_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH] = {};
    DWORD size = sizeof(value);
    DWORD type = 0;
    bool exists = RegQueryValueExW(hKey, AUTO_START_VALUE, nullptr, &type, 
                                   reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS;
    
    RegCloseKey(hKey);
    return exists && type == REG_SZ;
}

void TrayIcon::SetAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTO_START_KEY, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        LOG_ERROR("Failed to open registry key for auto-start");
        return;
    }

    if (enable) {
        // Get executable path
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        // Set registry value
        LSTATUS status = RegSetValueExW(hKey, AUTO_START_VALUE, 0, REG_SZ,
                                        reinterpret_cast<const BYTE*>(exePath),
                                        static_cast<DWORD>((wcslen(exePath) + 1) * sizeof(wchar_t)));
        if (status == ERROR_SUCCESS) {
            LOG_INFO("Auto-start enabled");
        } else {
            LOG_ERROR("Failed to set auto-start registry value: %ld", status);
        }
    } else {
        // Delete registry value
        RegDeleteValueW(hKey, AUTO_START_VALUE);
        LOG_INFO("Auto-start disabled");
    }

    RegCloseKey(hKey);
}

}  // namespace VirtualOverlay

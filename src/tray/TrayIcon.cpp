#include "TrayIcon.h"
#include "../utils/Logger.h"
#include "../config/Defaults.h"
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

    // Initialize NOTIFYICONDATA
    m_nid = {};
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = hParentWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.uVersion = NOTIFYICON_VERSION_4;

    // Load icon
    m_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    if (!m_nid.hIcon) {
        // Fallback to default application icon
        m_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    // Set tooltip
    wcscpy_s(m_nid.szTip, L"Virtual Overlay");

    // Create context menu
    m_hMenu = CreatePopupMenu();
    if (m_hMenu) {
        AppendMenuW(m_hMenu, MF_STRING, IDM_TRAY_SETTINGS, L"&Settings...");
        AppendMenuW(m_hMenu, MF_SEPARATOR, 0, nullptr);
        
        // Auto-start item with check
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

void TrayIcon::Show() {
    if (!m_initialized || m_visible) {
        return;
    }

    if (Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        Shell_NotifyIconW(NIM_SETVERSION, &m_nid);
        m_visible = true;
        LOG_DEBUG("Tray icon shown");
    } else {
        LOG_ERROR("Failed to add tray icon: %lu", GetLastError());
    }
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
            // Double-click opens settings
            if (m_onSettings) {
                m_onSettings();
            }
            break;

        case NIN_SELECT:
        case NIN_KEYSELECT:
            // Single click/keyboard - could show a popup or do nothing
            break;

        default:
            break;
    }
}

void TrayIcon::ShowContextMenu() {
    if (!m_hMenu) {
        return;
    }

    // Update auto-start check state
    CheckMenuItem(m_hMenu, IDM_TRAY_AUTOSTART, 
                  IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);

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

        default:
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

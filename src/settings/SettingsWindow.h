#pragma once

#include <windows.h>
#include <commctrl.h>
#include <memory>
#include <functional>
#include "../config/Config.h"

namespace VirtualOverlay {

// Tab page identifiers
enum class SettingsTab {
    General = 0,
    Overlay,
    Zoom,
    About
};

// Forward declarations
class SettingsPage;

// Settings window manager
class SettingsWindow {
public:
    static SettingsWindow& Instance();

    // Lifecycle
    bool Init(HINSTANCE hInstance, HWND hParentWnd);
    void Shutdown();

    // Show/hide
    void Open();
    void Close();
    bool IsOpen() const { return m_hwnd != nullptr && IsWindowVisible(m_hwnd); }

    // Get window handle
    HWND GetHwnd() const { return m_hwnd; }

    // Callback for settings applied
    using SettingsAppliedCallback = std::function<void()>;
    void SetApplyCallback(SettingsAppliedCallback callback) { m_applyCallback = callback; }

    // Preview overlay with current settings
    void PreviewOverlay();

private:
    SettingsWindow();
    ~SettingsWindow();
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // Window procedure
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Window creation
    bool CreateMainWindow();
    bool CreateTabControl();
    bool CreatePages();
    bool CreateButtons();

    // Tab handling
    void OnTabChanged(int tabIndex);
    void ShowPage(SettingsTab tab);
    void HideAllPages();

    // Button handlers
    void OnApply();
    void OnCancel();
    void OnOK();

    // Load/save settings to UI
    void LoadSettingsToUI();
    void SaveSettingsFromUI();

    // Validation
    bool ValidateSettings();

    // Members
    HINSTANCE m_hInstance = nullptr;
    HWND m_hParentWnd = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_hTabControl = nullptr;

    // Buttons
    HWND m_hBtnApply = nullptr;
    HWND m_hBtnCancel = nullptr;
    HWND m_hBtnOK = nullptr;

    // Page windows
    HWND m_hPageGeneral = nullptr;
    HWND m_hPageOverlay = nullptr;
    HWND m_hPageZoom = nullptr;
    HWND m_hPageAbout = nullptr;

    // Current tab
    SettingsTab m_currentTab = SettingsTab::General;

    // Working copy of config
    AppConfig m_workingConfig;

    // Callback
    SettingsAppliedCallback m_applyCallback;

    // Initialized flag
    bool m_initialized = false;
};

// Control IDs
constexpr int IDC_TAB_CONTROL = 100;
constexpr int IDC_BTN_OK = 101;
constexpr int IDC_BTN_CANCEL = 102;
constexpr int IDC_BTN_APPLY = 103;

// Page dimensions
constexpr int SETTINGS_WIDTH = 500;
constexpr int SETTINGS_HEIGHT = 450;
constexpr int TAB_HEIGHT = 350;
constexpr int BUTTON_HEIGHT = 30;
constexpr int BUTTON_WIDTH = 80;
constexpr int MARGIN = 10;

}  // namespace VirtualOverlay

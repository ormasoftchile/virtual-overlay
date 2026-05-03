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
    About
};

class SettingsWindow {
public:
    static SettingsWindow& Instance();

    bool Init(HINSTANCE hInstance, HWND hParentWnd);
    void Shutdown();

    void Open();
    void Close();
    bool IsOpen() const { return m_hwnd != nullptr && IsWindowVisible(m_hwnd); }

    HWND GetHwnd() const { return m_hwnd; }

    using SettingsAppliedCallback = std::function<void()>;
    void SetApplyCallback(SettingsAppliedCallback callback) { m_applyCallback = callback; }

    void PreviewOverlay();

private:
    SettingsWindow();
    ~SettingsWindow();
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool CreateMainWindow();
    bool CreateTabControl();
    bool CreatePages();
    bool CreateButtons();

    void OnTabChanged(int tabIndex);
    void ShowPage(SettingsTab tab);
    void HideAllPages();

    void OnApply();
    void OnCancel();
    void OnOK();

    void LoadSettingsToUI();
    void SaveSettingsFromUI();

    bool ValidateSettings();

    HINSTANCE m_hInstance = nullptr;
    HWND m_hParentWnd = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_hTabControl = nullptr;

    HWND m_hBtnApply = nullptr;
    HWND m_hBtnCancel = nullptr;
    HWND m_hBtnOK = nullptr;

    HWND m_hPageGeneral = nullptr;
    HWND m_hPageOverlay = nullptr;
    HWND m_hPageAbout = nullptr;

    SettingsTab m_currentTab = SettingsTab::General;
    AppConfig m_workingConfig;
    SettingsAppliedCallback m_applyCallback;
    bool m_initialized = false;
};

constexpr int IDC_TAB_CONTROL = 100;
constexpr int IDC_BTN_OK = 101;
constexpr int IDC_BTN_CANCEL = 102;
constexpr int IDC_BTN_APPLY = 103;

constexpr int SETTINGS_WIDTH = 500;
constexpr int SETTINGS_HEIGHT = 450;
constexpr int TAB_HEIGHT = 350;
constexpr int BUTTON_HEIGHT = 30;
constexpr int BUTTON_WIDTH = 80;
constexpr int MARGIN = 10;

}  // namespace VirtualOverlay

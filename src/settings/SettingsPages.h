#pragma once

#include <windows.h>
#include "../config/Config.h"

namespace VirtualOverlay {

// Control IDs for General page
constexpr int IDC_GEN_START_WINDOWS = 200;
constexpr int IDC_GEN_SHOW_TRAY = 201;
constexpr int IDC_GEN_HOTKEY_LABEL = 202;

// Control IDs for Overlay page
constexpr int IDC_OVL_ENABLE = 300;
constexpr int IDC_OVL_MODE = 301;
constexpr int IDC_OVL_POSITION = 302;
constexpr int IDC_OVL_FORMAT = 303;
constexpr int IDC_OVL_FONT = 304;
constexpr int IDC_OVL_FONT_SIZE = 305;
constexpr int IDC_OVL_TEXT_COLOR = 306;
constexpr int IDC_OVL_OPACITY = 307;
constexpr int IDC_OVL_OPACITY_LABEL = 308;
constexpr int IDC_OVL_AUTOHIDE = 309;
constexpr int IDC_OVL_AUTOHIDE_DELAY = 310;
constexpr int IDC_OVL_PREVIEW = 311;
constexpr int IDC_OVL_BLUR_STYLE = 312;
constexpr int IDC_OVL_MONITOR = 313;
constexpr int IDC_OVL_WATERMARK_SIZE = 314;
constexpr int IDC_OVL_WATERMARK_SIZE_LABEL = 315;
constexpr int IDC_OVL_WATERMARK_OPACITY = 316;
constexpr int IDC_OVL_WATERMARK_OPACITY_LABEL = 317;
constexpr int IDC_OVL_WATERMARK_COLOR = 318;
constexpr int IDC_OVL_WATERMARK_COLOR_BTN = 319;
constexpr int IDC_OVL_AUTOHIDE_MS_LABEL = 320;
constexpr int IDC_OVL_DODGE = 321;
constexpr int IDC_OVL_DODGE_PROXIMITY = 322;
constexpr int IDC_OVL_DODGE_PROXIMITY_LABEL = 323;

// Control IDs for Zoom page
constexpr int IDC_ZOOM_ENABLE = 400;
constexpr int IDC_ZOOM_MODIFIER = 401;
constexpr int IDC_ZOOM_STEP = 402;
constexpr int IDC_ZOOM_STEP_LABEL = 403;
constexpr int IDC_ZOOM_MAX = 404;
constexpr int IDC_ZOOM_MAX_LABEL = 405;
constexpr int IDC_ZOOM_SMOOTHING = 406;
constexpr int IDC_ZOOM_DOUBLETAP = 407;
constexpr int IDC_ZOOM_PINCH = 408;

// Control IDs for About page
constexpr int IDC_ABOUT_VERSION = 500;
constexpr int IDC_ABOUT_GITHUB = 501;
constexpr int IDC_ABOUT_AUTHOR = 502;

class SettingsPages {
public:
    // Create page windows
    static HWND CreateGeneralPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage);
    static HWND CreateOverlayPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage);
    static HWND CreateZoomPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage);
    static HWND CreateAboutPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage);

    // Load settings to UI
    static void LoadGeneralSettings(HWND hPage, const GeneralConfig& config);
    static void LoadOverlaySettings(HWND hPage, const OverlayConfig& config);
    static void LoadZoomSettings(HWND hPage, const ZoomConfig& config);

    // Save settings from UI
    static void SaveGeneralSettings(HWND hPage, GeneralConfig& config);
    static void SaveOverlaySettings(HWND hPage, OverlayConfig& config);
    static void SaveZoomSettings(HWND hPage, ZoomConfig& config);

private:
    // Page window procedures
    static LRESULT CALLBACK GeneralPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK OverlayPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ZoomPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK AboutPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Helper to create controls
    static HWND CreateLabel(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h);
    static HWND CreateCheckbox(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h);
    static HWND CreateComboBox(HWND hParent, HINSTANCE hInstance, int id, int x, int y, int w, int h);
    static HWND CreateEdit(HWND hParent, HINSTANCE hInstance, int id, int x, int y, int w, int h);
    static HWND CreateSlider(HWND hParent, HINSTANCE hInstance, int id, int x, int y, int w, int h, int min, int max);
    static HWND CreateButton(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h);
    static HWND CreateLink(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h);

    // Set default font
    static void SetControlFont(HWND hControl);
};

}  // namespace VirtualOverlay

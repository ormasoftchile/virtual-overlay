#pragma once

#include <windows.h>
#include "../config/Config.h"

namespace VirtualOverlay {

constexpr int IDC_GEN_START_WINDOWS = 200;
constexpr int IDC_GEN_SHOW_TRAY = 201;
constexpr int IDC_GEN_HOTKEY_LABEL = 202;

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

constexpr int IDC_FLIP_ENABLE = 400;
constexpr int IDC_FLIP_HOTKEY = 401;
constexpr int IDC_FLIP_INVERT_MOUSE = 402;
constexpr int IDC_FLIP_CURSOR_MODE = 403;
constexpr int IDC_FLIP_MONITOR_BASE = 410;
constexpr int IDC_FLIP_MONITOR_MAX = 32;

constexpr int IDC_PRES_ENABLE = 600;
constexpr int IDC_PRES_MIC_COMBO = 601;
constexpr int IDC_PRES_CHANNEL_COMBO = 602;
constexpr int IDC_PRES_FONT_SIZE = 603;
constexpr int IDC_PRES_FONT_SIZE_LABEL = 604;
constexpr int IDC_PRES_WIDTH = 605;
constexpr int IDC_PRES_WIDTH_LABEL = 606;
constexpr int IDC_PRES_BG_OPACITY = 607;
constexpr int IDC_PRES_BG_OPACITY_LABEL = 608;
constexpr int IDC_PRES_SHOW_BG = 609;
constexpr int IDC_PRES_LINES_COMBO = 610;
constexpr int IDC_PRES_HOTKEY = 611;
constexpr int IDC_PRES_MODEL_STATUS = 612;
constexpr int IDC_PRES_BROWSE_MODEL = 613;
constexpr int IDC_PRES_DOWNLOAD_MODEL = 614;
constexpr int IDC_PRES_PASTE_SCRIPT = 615;
constexpr int IDC_PRES_LOAD_FILE = 616;
constexpr int IDC_PRES_RESET_POS = 617;

constexpr int IDC_ABOUT_VERSION = 500;
constexpr int IDC_ABOUT_GITHUB = 501;
constexpr int IDC_ABOUT_AUTHOR = 502;

class SettingsPages {
public:
    static HWND CreateGeneralPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage);
    static HWND CreateOverlayPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage);
    static HWND CreateFlipPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage);
    static HWND CreatePresenterPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage);
    static HWND CreateAboutPage(HWND hParent, HINSTANCE hInstance, const RECT& rcPage);

    static void LoadGeneralSettings(HWND hPage, const GeneralConfig& config);
    static void LoadOverlaySettings(HWND hPage, const OverlayConfig& config);
    static void LoadFlipSettings(HWND hPage, const FlipConfig& config);
    static void LoadPresenterSettings(HWND hPage, const PresenterConfig& config);

    static void SaveGeneralSettings(HWND hPage, GeneralConfig& config);
    static void SaveOverlaySettings(HWND hPage, OverlayConfig& config);
    static void SaveFlipSettings(HWND hPage, FlipConfig& config);
    static void SavePresenterSettings(HWND hPage, PresenterConfig& config);

private:
    static HWND CreateLabel(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h);
    static HWND CreateCheckbox(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h);
    static HWND CreateComboBox(HWND hParent, HINSTANCE hInstance, int id, int x, int y, int w, int h);
    static HWND CreateEdit(HWND hParent, HINSTANCE hInstance, int id, int x, int y, int w, int h);
    static HWND CreateSlider(HWND hParent, HINSTANCE hInstance, int id, int x, int y, int w, int h, int min, int max);
    static HWND CreateButton(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h);
    static HWND CreateLink(HWND hParent, HINSTANCE hInstance, int id, const wchar_t* text, int x, int y, int w, int h);

    static void SetControlFont(HWND hControl);
};

}  // namespace VirtualOverlay

#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <vector>
#include <map>

namespace VirtualOverlay {

// Enums for configuration options
enum class OverlayPosition {
    TopLeft,
    TopCenter,
    TopRight,
    Center,
    BottomLeft,
    BottomCenter,
    BottomRight
};

enum class MonitorSelection {
    Cursor,
    Primary,
    All
};

enum class BlurType {
    Acrylic,
    Mica,
    Solid
};

enum class OverlayMode {
    Notification,  // Shows briefly on desktop switch, then fades
    Watermark      // Always visible, transparent text only
};

// General settings
struct GeneralConfig {
    bool startWithWindows = true;
    bool showTrayIcon = true;
    std::wstring settingsHotkey = L"Ctrl+Shift+O";
    std::wstring overlayToggleHotkey = L"Ctrl+Shift+D";
    bool forcePollingMode = true;
};

// Overlay style settings
struct OverlayStyleConfig {
    BlurType blur = BlurType::Acrylic;
    uint32_t tintColor = 0x000000;  // RGB
    float tintOpacity = 0.6f;
    int cornerRadius = 8;
    uint32_t borderColor = 0x404040;  // RGB
    int borderWidth = 1;
    bool shadowEnabled = true;
    int padding = 16;
};

// Overlay text settings
struct OverlayTextConfig {
    std::wstring fontFamily = L"Segoe UI Variable";
    int fontSize = 20;
    int fontWeight = 600;
    uint32_t color = 0xFFFFFF;  // RGB
};

// Overlay animation settings
struct OverlayAnimationConfig {
    int fadeInDurationMs = 150;
    int fadeOutDurationMs = 200;
    bool slideIn = true;
    int slideDistance = 10;
};

// Overlay settings
struct OverlayConfig {
    bool enabled = true;
    OverlayMode mode = OverlayMode::Watermark;
    OverlayPosition position = OverlayPosition::TopRight;
    bool showDesktopNumber = true;
    bool showDesktopName = true;
    std::wstring format = L"{number}: {name}";
    bool autoHide = true;
    int autoHideDelayMs = 2000;
    MonitorSelection monitor = MonitorSelection::Cursor;

    // Watermark-specific settings
    int watermarkFontSize = 120;
    float watermarkOpacity = 0.25f;
    bool watermarkShadow = false;
    uint32_t watermarkColor = 0x00FF00;  // Lime by default

    // Dodge mode - move overlay when mouse approaches
    bool dodgeOnHover = false;
    int dodgeProximity = 100;

    OverlayStyleConfig style;
    OverlayTextConfig text;
    OverlayAnimationConfig animation;
};

// How the mouse cursor is handled on a mirrored monitor
enum class FlipCursorMode {
    None,        // No cursor assistance; the real system cursor stays as-is
    Magnified,   // Hide the real cursor and draw a mirrored surrogate cursor
                 // into the flipped content (correct clicks/hover/drag)
    Inverted     // Same surrogate-cursor behaviour as Magnified (kept for
                 // config compatibility)
};

// Per-monitor horizontal flip (teleprompter mirror) settings
struct FlipConfig {
    bool enabled = false;
    bool invertMouse = true;                   // Visible pointer moves mirrored to the hand (natural through glass)
    std::wstring toggleHotkey = L"Ctrl+Shift+F";
    FlipCursorMode cursorMode = FlipCursorMode::Magnified;
    // Monitors to mirror, identified by their stable device name (e.g. "\\.\DISPLAY2")
    std::vector<std::wstring> monitors;
};

// Per-monitor reading-baseline anchor in display-relative normalized coordinates [0.0, 1.0]
struct PresenterMonitorAnchor {
    float normX = 0.50f;   // Horizontal center relative to monitor width
    float normY = 0.14f;   // Vertical reading baseline relative to monitor height (just above camera lens)
};

// Native speech-following Presenter settings
struct PresenterConfig {
    bool enabled = false;
    std::wstring toggleHotkey = L"Ctrl+Shift+P";
    std::wstring fontFamily = L"Segoe UI Variable";
    int fontSize = 32;
    int captionWidth = 840;
    uint32_t textColor = 0xFFFFFF;     // RGB
    float textOpacity = 0.96f;
    bool showBackground = true;
    uint32_t bgColor = 0x0E121A;       // RGB
    float bgOpacity = 0.76f;
    int previousLines = 1;             // 0 or 1
    int upcomingLines = 2;             // 1 or 2
    float smoothTime = 0.28f;
    std::wstring targetMonitor = L"";  // Stable monitor ID (e.g. "MNN"), or "" for auto (teleprompter/primary)
    std::wstring audioDeviceId = L"default";
    std::wstring audioDeviceName = L"";
    int audioChannel = 0;              // 0 = all channels (mono mixdown), 1+ = 1-based channel
    std::wstring voskModelPath = L"";
    std::wstring scriptText =
        L"Welcome to Virtual Overlay Presenter.\n"
        L"Hold Alt and drag anywhere on this caption to place the reading line right above your camera lens.\n"
        L"As you speak into your selected microphone, the script advances smoothly at your natural speaking pace.\n"
        L"You can pause, resume, switch microphones, or load any text or markdown script from the tray menu or settings.";
    std::wstring scriptFilePath = L"";
    int savedTokenIndex = -1;
    std::map<std::wstring, PresenterMonitorAnchor> monitorAnchors;
};

// Complete configuration
struct AppConfig {
    std::wstring schema = L"virtual-overlay-config-v1";
    GeneralConfig general;
    OverlayConfig overlay;
    FlipConfig flip;
    PresenterConfig presenter;
};

// Configuration manager
class Config {
public:
    static Config& Instance();

    bool Load();
    bool Load(const std::wstring& filePath);

    bool Save();
    bool Save(const std::wstring& filePath);

    void Reset();

    const AppConfig& Get() const;
    AppConfig& GetMutable();

    bool Apply();

    static std::wstring GetDefaultConfigPath();

    bool Validate() const;
    bool Validate(const AppConfig& config) const;

    static std::string PositionToString(OverlayPosition pos);
    static OverlayPosition StringToPosition(const std::string& str);
    static std::string MonitorToString(MonitorSelection mon);
    static MonitorSelection StringToMonitor(const std::string& str);
    static std::string BlurToString(BlurType blur);
    static BlurType StringToBlur(const std::string& str);
    static std::string ModeToString(OverlayMode mode);
    static OverlayMode StringToMode(const std::string& str);
    static std::string FlipCursorModeToString(FlipCursorMode mode);
    static FlipCursorMode StringToFlipCursorMode(const std::string& str);

private:
    Config();
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    static uint32_t ParseColor(const std::string& hex);
    static std::string ColorToHex(uint32_t color);

    void ClampValues(AppConfig& config);

    AppConfig m_config;
    std::wstring m_configPath;
    bool m_dirty = false;
};

}  // namespace VirtualOverlay

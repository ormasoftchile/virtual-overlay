#pragma once

#include <string>
#include <optional>
#include <cstdint>

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

// Complete configuration
struct AppConfig {
    std::wstring schema = L"virtual-overlay-config-v1";
    GeneralConfig general;
    OverlayConfig overlay;
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

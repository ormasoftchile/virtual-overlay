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

enum class ModifierKey {
    Ctrl,
    Alt,
    Shift,
    Win
};

// General settings
struct GeneralConfig {
    bool startWithWindows = true;
    bool showTrayIcon = true;
    std::wstring settingsHotkey = L"Ctrl+Shift+O";
    std::wstring overlayToggleHotkey = L"Ctrl+Shift+D";  // Toggle overlay visibility
};

// Zoom settings
struct ZoomConfig {
    bool enabled = true;
    ModifierKey modifierKey = ModifierKey::Ctrl;
    float zoomStep = 0.5f;              // Increased for faster zoom
    float minZoom = 1.0f;
    float maxZoom = 10.0f;
    bool smoothing = true;
    float smoothingFactor = 0.08f;      // Reduced for snappier response
    int animationDurationMs = 50;       // Reduced for faster animation
    bool doubleTapToReset = true;
    int doubleTapWindowMs = 300;
    bool touchpadPinch = true;
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
    OverlayMode mode = OverlayMode::Notification;
    OverlayPosition position = OverlayPosition::TopCenter;
    bool showDesktopNumber = true;
    bool showDesktopName = true;
    std::wstring format = L"{number}: {name}";
    bool autoHide = true;
    int autoHideDelayMs = 2000;
    MonitorSelection monitor = MonitorSelection::Cursor;
    
    // Watermark-specific settings
    int watermarkFontSize = 72;
    float watermarkOpacity = 0.25f;
    bool watermarkShadow = false;
    uint32_t watermarkColor = 0xFFFFFF;  // White by default
    
    // Dodge mode - move overlay when mouse approaches
    bool dodgeOnHover = false;
    int dodgeProximity = 100;  // pixels - how close mouse must be to trigger dodge
    
    OverlayStyleConfig style;
    OverlayTextConfig text;
    OverlayAnimationConfig animation;
};

// Complete configuration
struct AppConfig {
    std::wstring schema = L"virtual-overlay-config-v1";
    GeneralConfig general;
    ZoomConfig zoom;
    OverlayConfig overlay;
};

// Configuration manager
class Config {
public:
    static Config& Instance();

    // Load configuration from file
    // Returns true if loaded successfully, false if defaults were used
    bool Load();
    bool Load(const std::wstring& filePath);

    // Save configuration to file
    bool Save();
    bool Save(const std::wstring& filePath);

    // Reset to defaults
    void Reset();

    // Get the current configuration (read-only)
    const AppConfig& Get() const;

    // Get mutable configuration for editing
    AppConfig& GetMutable();

    // Apply changes and save
    bool Apply();

    // Get default config path
    static std::wstring GetDefaultConfigPath();

    // Validation
    bool Validate() const;
    bool Validate(const AppConfig& config) const;

    // Convert enums to/from strings
    static std::string PositionToString(OverlayPosition pos);
    static OverlayPosition StringToPosition(const std::string& str);
    static std::string MonitorToString(MonitorSelection mon);
    static MonitorSelection StringToMonitor(const std::string& str);
    static std::string BlurToString(BlurType blur);
    static BlurType StringToBlur(const std::string& str);
    static std::string ModeToString(OverlayMode mode);
    static OverlayMode StringToMode(const std::string& str);
    static std::string ModifierToString(ModifierKey key);
    static ModifierKey StringToModifier(const std::string& str);

private:
    Config();
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    // Helper to parse hex color string
    static uint32_t ParseColor(const std::string& hex);
    static std::string ColorToHex(uint32_t color);

    // Clamp values to valid ranges
    void ClampValues(AppConfig& config);

    AppConfig m_config;
    std::wstring m_configPath;
    bool m_dirty = false;
};

}  // namespace VirtualOverlay

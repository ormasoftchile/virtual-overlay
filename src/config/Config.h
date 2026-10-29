#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <vector>
#include <map>

namespace VirtualOverlay {

struct MonitorInfo;  // defined in utils/Monitor.h

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
    int padding = 16;
};

// Overlay text settings
struct OverlayTextConfig {
    std::wstring fontFamily = L"Segoe UI Variable";
    int fontWeight = 600;
};

// Overlay settings
struct OverlayConfig {
    bool enabled = true;
    OverlayPosition position = OverlayPosition::TopRight;
    bool showDesktopNumber = true;
    bool showDesktopName = true;
    std::wstring format = L"{number}: {name}";
    MonitorSelection monitor = MonitorSelection::Cursor;

    // Watermark appearance
    int watermarkFontSize = 120;
    float watermarkOpacity = 0.25f;
    bool watermarkShadow = false;
    uint32_t watermarkColor = 0x00FF00;  // Lime by default

    // Dodge mode - move overlay when mouse approaches
    bool dodgeOnHover = false;
    int dodgeProximity = 100;

    OverlayStyleConfig style;
    OverlayTextConfig text;
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
};

// Per-monitor feature toggles, keyed by stable monitor ID (MonitorInfo::GetId()).
// Monitors without an entry use the defaults below.
struct PerMonitorSettings {
    bool flip = false;        // Horizontal teleprompter mirror on this screen
    bool showOverlay = true;  // Watermark may appear on this screen

    bool operator==(const PerMonitorSettings&) const = default;
};

using MonitorSettingsMap = std::map<std::wstring, PerMonitorSettings>;

// Returns the stored settings for a monitor. The string overload matches the
// map key exactly (case-insensitive); the MonitorInfo overload also accepts
// legacy keys (device path or GDI device name). Returns nullptr when the
// monitor has no entry.
const PerMonitorSettings* FindMonitorSettings(const MonitorSettingsMap& map, const std::wstring& id);
const PerMonitorSettings* FindMonitorSettings(const MonitorSettingsMap& map, const MonitorInfo& mon);

// Returns the stored settings, or the defaults when the monitor has no entry.
PerMonitorSettings GetMonitorSettings(const MonitorSettingsMap& map, const MonitorInfo& mon);

// Stores settings under id / mon.GetId(). Legacy-keyed duplicates for the same
// monitor are dropped, and entries equal to the defaults are removed.
void SetMonitorSettings(MonitorSettingsMap& map, const std::wstring& id, const PerMonitorSettings& settings);
void SetMonitorSettings(MonitorSettingsMap& map, const MonitorInfo& mon, const PerMonitorSettings& settings);

// Whether the watermark may be shown on this monitor. Explicit opt-outs
// (showOverlay == false) always apply. Mirrored (flipped)
// non-primary screens never show the watermark, because MirrorWindow captures
// and re-renders it mirrored.
bool IsOverlayAllowedOn(const MonitorSettingsMap& map, const MonitorInfo& mon);

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
    // Per-monitor feature toggles (flip, watermark opt-out), keyed by stable ID
    MonitorSettingsMap monitors;
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

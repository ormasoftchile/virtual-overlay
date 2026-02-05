#include "Config.h"
#include "../utils/Logger.h"
#include "../../lib/json.hpp"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace VirtualOverlay {

// Helper functions for string conversion
static std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
    return result;
}

static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), size);
    return result;
}

Config& Config::Instance() {
    static Config instance;
    return instance;
}

Config::Config() {
    m_configPath = GetDefaultConfigPath();
    Reset();
}

std::wstring Config::GetDefaultConfigPath() {
    wchar_t* appDataPath = nullptr;
    std::wstring configPath;
    
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appDataPath))) {
        configPath = appDataPath;
        CoTaskMemFree(appDataPath);
        configPath += L"\\VirtualOverlay\\config.json";
    } else {
        // Fallback to current directory
        configPath = L"config.json";
    }
    
    return configPath;
}

void Config::Reset() {
    m_config = AppConfig{};
    m_dirty = true;
}

bool Config::Load() {
    return Load(m_configPath);
}

bool Config::Load(const std::wstring& filePath) {
    m_configPath = filePath;
    
    // Check if file exists
    if (!fs::exists(filePath)) {
        LOG_INFO("Config file not found, using defaults: {}", 
                 WideToUtf8(filePath));
        Reset();
        return false;
    }
    
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            LOG_WARN("Failed to open config file, using defaults");
            Reset();
            return false;
        }
        
        json j;
        file >> j;
        
        // Parse schema
        if (j.contains("$schema")) {
            std::string schema = j["$schema"].get<std::string>();
            m_config.schema = Utf8ToWide(schema);
        }
        
        // Parse general settings
        if (j.contains("general")) {
            auto& g = j["general"];
            if (g.contains("startWithWindows")) m_config.general.startWithWindows = g["startWithWindows"].get<bool>();
            if (g.contains("showTrayIcon")) m_config.general.showTrayIcon = g["showTrayIcon"].get<bool>();
            if (g.contains("settingsHotkey")) {
                std::string hotkey = g["settingsHotkey"].get<std::string>();
                m_config.general.settingsHotkey = Utf8ToWide(hotkey);
            }
            if (g.contains("overlayToggleHotkey")) {
                std::string hotkey = g["overlayToggleHotkey"].get<std::string>();
                m_config.general.overlayToggleHotkey = Utf8ToWide(hotkey);
            }
        }
        
        // Parse zoom settings
        if (j.contains("zoom")) {
            auto& z = j["zoom"];
            if (z.contains("enabled")) m_config.zoom.enabled = z["enabled"].get<bool>();
            if (z.contains("modifierKey")) m_config.zoom.modifierKey = StringToModifier(z["modifierKey"].get<std::string>());
            if (z.contains("zoomStep")) m_config.zoom.zoomStep = z["zoomStep"].get<float>();
            if (z.contains("minZoom")) m_config.zoom.minZoom = z["minZoom"].get<float>();
            if (z.contains("maxZoom")) m_config.zoom.maxZoom = z["maxZoom"].get<float>();
            if (z.contains("smoothing")) m_config.zoom.smoothing = z["smoothing"].get<bool>();
            if (z.contains("smoothingFactor")) m_config.zoom.smoothingFactor = z["smoothingFactor"].get<float>();
            if (z.contains("animationDurationMs")) m_config.zoom.animationDurationMs = z["animationDurationMs"].get<int>();
            if (z.contains("doubleTapToReset")) m_config.zoom.doubleTapToReset = z["doubleTapToReset"].get<bool>();
            if (z.contains("doubleTapWindowMs")) m_config.zoom.doubleTapWindowMs = z["doubleTapWindowMs"].get<int>();
            if (z.contains("touchpadPinch")) m_config.zoom.touchpadPinch = z["touchpadPinch"].get<bool>();
        }
        
        // Parse overlay settings
        if (j.contains("overlay")) {
            auto& o = j["overlay"];
            if (o.contains("enabled")) m_config.overlay.enabled = o["enabled"].get<bool>();
            if (o.contains("mode")) m_config.overlay.mode = StringToMode(o["mode"].get<std::string>());
            if (o.contains("position")) m_config.overlay.position = StringToPosition(o["position"].get<std::string>());
            if (o.contains("showDesktopNumber")) m_config.overlay.showDesktopNumber = o["showDesktopNumber"].get<bool>();
            if (o.contains("showDesktopName")) m_config.overlay.showDesktopName = o["showDesktopName"].get<bool>();
            if (o.contains("format")) {
                std::string fmt = o["format"].get<std::string>();
                m_config.overlay.format = Utf8ToWide(fmt);
            }
            if (o.contains("autoHide")) m_config.overlay.autoHide = o["autoHide"].get<bool>();
            if (o.contains("autoHideDelayMs")) m_config.overlay.autoHideDelayMs = o["autoHideDelayMs"].get<int>();
            if (o.contains("monitor")) m_config.overlay.monitor = StringToMonitor(o["monitor"].get<std::string>());
            
            // Watermark settings
            if (o.contains("watermarkFontSize")) m_config.overlay.watermarkFontSize = o["watermarkFontSize"].get<int>();
            if (o.contains("watermarkOpacity")) m_config.overlay.watermarkOpacity = o["watermarkOpacity"].get<float>();
            if (o.contains("watermarkShadow")) m_config.overlay.watermarkShadow = o["watermarkShadow"].get<bool>();
            if (o.contains("watermarkColor")) m_config.overlay.watermarkColor = ParseColor(o["watermarkColor"].get<std::string>());
            
            // Dodge settings
            if (o.contains("dodgeOnHover")) m_config.overlay.dodgeOnHover = o["dodgeOnHover"].get<bool>();
            if (o.contains("dodgeProximity")) m_config.overlay.dodgeProximity = o["dodgeProximity"].get<int>();
            
            // Parse style
            if (o.contains("style")) {
                auto& s = o["style"];
                if (s.contains("blur")) m_config.overlay.style.blur = StringToBlur(s["blur"].get<std::string>());
                if (s.contains("tintColor")) m_config.overlay.style.tintColor = ParseColor(s["tintColor"].get<std::string>());
                if (s.contains("tintOpacity")) m_config.overlay.style.tintOpacity = s["tintOpacity"].get<float>();
                if (s.contains("cornerRadius")) m_config.overlay.style.cornerRadius = s["cornerRadius"].get<int>();
                if (s.contains("borderColor")) m_config.overlay.style.borderColor = ParseColor(s["borderColor"].get<std::string>());
                if (s.contains("borderWidth")) m_config.overlay.style.borderWidth = s["borderWidth"].get<int>();
                if (s.contains("shadowEnabled")) m_config.overlay.style.shadowEnabled = s["shadowEnabled"].get<bool>();
                if (s.contains("padding")) m_config.overlay.style.padding = s["padding"].get<int>();
            }
            
            // Parse text
            if (o.contains("text")) {
                auto& t = o["text"];
                if (t.contains("fontFamily")) {
                    std::string font = t["fontFamily"].get<std::string>();
                    m_config.overlay.text.fontFamily = Utf8ToWide(font);
                }
                if (t.contains("fontSize")) m_config.overlay.text.fontSize = t["fontSize"].get<int>();
                if (t.contains("fontWeight")) m_config.overlay.text.fontWeight = t["fontWeight"].get<int>();
                if (t.contains("color")) m_config.overlay.text.color = ParseColor(t["color"].get<std::string>());
            }
            
            // Parse animation
            if (o.contains("animation")) {
                auto& a = o["animation"];
                if (a.contains("fadeInDurationMs")) m_config.overlay.animation.fadeInDurationMs = a["fadeInDurationMs"].get<int>();
                if (a.contains("fadeOutDurationMs")) m_config.overlay.animation.fadeOutDurationMs = a["fadeOutDurationMs"].get<int>();
                if (a.contains("slideIn")) m_config.overlay.animation.slideIn = a["slideIn"].get<bool>();
                if (a.contains("slideDistance")) m_config.overlay.animation.slideDistance = a["slideDistance"].get<int>();
            }
        }
        
        // Clamp values to valid ranges
        ClampValues(m_config);
        
        LOG_INFO("Configuration loaded successfully");
        m_dirty = false;
        return true;
        
    } catch (const json::exception& e) {
        LOG_ERROR("Failed to parse config file: {}", e.what());
        Reset();
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error loading config: {}", e.what());
        Reset();
        return false;
    }
}

bool Config::Save() {
    return Save(m_configPath);
}

bool Config::Save(const std::wstring& filePath) {
    try {
        // Ensure directory exists
        fs::path path(filePath);
        fs::path dir = path.parent_path();
        if (!dir.empty() && !fs::exists(dir)) {
            fs::create_directories(dir);
        }
        
        // Build JSON
        json j;
        
        j["$schema"] = WideToUtf8(m_config.schema);
        
        // General
        j["general"]["startWithWindows"] = m_config.general.startWithWindows;
        j["general"]["showTrayIcon"] = m_config.general.showTrayIcon;
        j["general"]["settingsHotkey"] = WideToUtf8(m_config.general.settingsHotkey);
        j["general"]["overlayToggleHotkey"] = WideToUtf8(m_config.general.overlayToggleHotkey);
        
        // Zoom
        j["zoom"]["enabled"] = m_config.zoom.enabled;
        j["zoom"]["modifierKey"] = ModifierToString(m_config.zoom.modifierKey);
        j["zoom"]["zoomStep"] = m_config.zoom.zoomStep;
        j["zoom"]["minZoom"] = m_config.zoom.minZoom;
        j["zoom"]["maxZoom"] = m_config.zoom.maxZoom;
        j["zoom"]["smoothing"] = m_config.zoom.smoothing;
        j["zoom"]["smoothingFactor"] = m_config.zoom.smoothingFactor;
        j["zoom"]["animationDurationMs"] = m_config.zoom.animationDurationMs;
        j["zoom"]["doubleTapToReset"] = m_config.zoom.doubleTapToReset;
        j["zoom"]["doubleTapWindowMs"] = m_config.zoom.doubleTapWindowMs;
        j["zoom"]["touchpadPinch"] = m_config.zoom.touchpadPinch;
        
        // Overlay
        j["overlay"]["enabled"] = m_config.overlay.enabled;
        j["overlay"]["mode"] = ModeToString(m_config.overlay.mode);
        j["overlay"]["position"] = PositionToString(m_config.overlay.position);
        j["overlay"]["showDesktopNumber"] = m_config.overlay.showDesktopNumber;
        j["overlay"]["showDesktopName"] = m_config.overlay.showDesktopName;
        j["overlay"]["format"] = WideToUtf8(m_config.overlay.format);
        j["overlay"]["autoHide"] = m_config.overlay.autoHide;
        j["overlay"]["autoHideDelayMs"] = m_config.overlay.autoHideDelayMs;
        j["overlay"]["monitor"] = MonitorToString(m_config.overlay.monitor);
        
        // Watermark settings
        j["overlay"]["watermarkFontSize"] = m_config.overlay.watermarkFontSize;
        j["overlay"]["watermarkOpacity"] = m_config.overlay.watermarkOpacity;
        j["overlay"]["watermarkShadow"] = m_config.overlay.watermarkShadow;
        j["overlay"]["watermarkColor"] = ColorToHex(m_config.overlay.watermarkColor);
        
        // Dodge settings
        j["overlay"]["dodgeOnHover"] = m_config.overlay.dodgeOnHover;
        j["overlay"]["dodgeProximity"] = m_config.overlay.dodgeProximity;
        
        // Style
        j["overlay"]["style"]["blur"] = BlurToString(m_config.overlay.style.blur);
        j["overlay"]["style"]["tintColor"] = ColorToHex(m_config.overlay.style.tintColor);
        j["overlay"]["style"]["tintOpacity"] = m_config.overlay.style.tintOpacity;
        j["overlay"]["style"]["cornerRadius"] = m_config.overlay.style.cornerRadius;
        j["overlay"]["style"]["borderColor"] = ColorToHex(m_config.overlay.style.borderColor);
        j["overlay"]["style"]["borderWidth"] = m_config.overlay.style.borderWidth;
        j["overlay"]["style"]["shadowEnabled"] = m_config.overlay.style.shadowEnabled;
        j["overlay"]["style"]["padding"] = m_config.overlay.style.padding;
        
        // Text
        j["overlay"]["text"]["fontFamily"] = WideToUtf8(m_config.overlay.text.fontFamily);
        j["overlay"]["text"]["fontSize"] = m_config.overlay.text.fontSize;
        j["overlay"]["text"]["fontWeight"] = m_config.overlay.text.fontWeight;
        j["overlay"]["text"]["color"] = ColorToHex(m_config.overlay.text.color);
        
        // Animation
        j["overlay"]["animation"]["fadeInDurationMs"] = m_config.overlay.animation.fadeInDurationMs;
        j["overlay"]["animation"]["fadeOutDurationMs"] = m_config.overlay.animation.fadeOutDurationMs;
        j["overlay"]["animation"]["slideIn"] = m_config.overlay.animation.slideIn;
        j["overlay"]["animation"]["slideDistance"] = m_config.overlay.animation.slideDistance;
        
        // Write to temp file first, then rename (atomic save)
        fs::path tempPath = path;
        tempPath += L".tmp";
        
        std::ofstream file(tempPath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to create temp config file");
            return false;
        }
        
        file << j.dump(2);
        file.close();
        
        // Backup existing config
        if (fs::exists(path)) {
            fs::path backupPath = path;
            backupPath += L".bak";
            fs::copy_file(path, backupPath, fs::copy_options::overwrite_existing);
        }
        
        // Rename temp to final
        fs::rename(tempPath, path);
        
        LOG_INFO("Configuration saved successfully");
        m_dirty = false;
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save config: {}", e.what());
        return false;
    }
}

const AppConfig& Config::Get() const {
    return m_config;
}

AppConfig& Config::GetMutable() {
    m_dirty = true;
    return m_config;
}

bool Config::Apply() {
    ClampValues(m_config);
    return Save();
}

bool Config::Validate() const {
    return Validate(m_config);
}

bool Config::Validate(const AppConfig& config) const {
    // Zoom validation
    if (config.zoom.zoomStep < 0.1f || config.zoom.zoomStep > 1.0f) return false;
    if (config.zoom.maxZoom < 2.0f || config.zoom.maxZoom > 20.0f) return false;
    if (config.zoom.smoothingFactor < 0.05f || config.zoom.smoothingFactor > 0.5f) return false;
    if (config.zoom.animationDurationMs < 0 || config.zoom.animationDurationMs > 500) return false;
    if (config.zoom.doubleTapWindowMs < 100 || config.zoom.doubleTapWindowMs > 1000) return false;
    
    // Overlay validation
    if (config.overlay.autoHideDelayMs < 500 || config.overlay.autoHideDelayMs > 10000) return false;
    if (config.overlay.style.tintOpacity < 0.0f || config.overlay.style.tintOpacity > 1.0f) return false;
    if (config.overlay.style.cornerRadius < 0 || config.overlay.style.cornerRadius > 32) return false;
    if (config.overlay.style.padding < 0 || config.overlay.style.padding > 64) return false;
    if (config.overlay.text.fontSize < 8 || config.overlay.text.fontSize > 72) return false;
    if (config.overlay.text.fontWeight < 100 || config.overlay.text.fontWeight > 900) return false;
    
    return true;
}

void Config::ClampValues(AppConfig& config) {
    // Clamp zoom values
    config.zoom.zoomStep = std::clamp(config.zoom.zoomStep, 0.1f, 1.0f);
    config.zoom.minZoom = 1.0f;  // Fixed
    config.zoom.maxZoom = std::clamp(config.zoom.maxZoom, 2.0f, 20.0f);
    config.zoom.smoothingFactor = std::clamp(config.zoom.smoothingFactor, 0.05f, 0.5f);
    config.zoom.animationDurationMs = std::clamp(config.zoom.animationDurationMs, 0, 500);
    config.zoom.doubleTapWindowMs = std::clamp(config.zoom.doubleTapWindowMs, 100, 1000);
    
    // Clamp overlay values
    config.overlay.autoHideDelayMs = std::clamp(config.overlay.autoHideDelayMs, 500, 10000);
    config.overlay.style.tintOpacity = std::clamp(config.overlay.style.tintOpacity, 0.0f, 1.0f);
    config.overlay.style.cornerRadius = std::clamp(config.overlay.style.cornerRadius, 0, 32);
    config.overlay.style.borderWidth = std::clamp(config.overlay.style.borderWidth, 0, 4);
    config.overlay.style.padding = std::clamp(config.overlay.style.padding, 0, 64);
    config.overlay.text.fontSize = std::clamp(config.overlay.text.fontSize, 8, 72);
    config.overlay.text.fontWeight = std::clamp(config.overlay.text.fontWeight, 100, 900);
    config.overlay.animation.fadeInDurationMs = std::clamp(config.overlay.animation.fadeInDurationMs, 0, 500);
    config.overlay.animation.fadeOutDurationMs = std::clamp(config.overlay.animation.fadeOutDurationMs, 0, 500);
    config.overlay.animation.slideDistance = std::clamp(config.overlay.animation.slideDistance, 0, 50);
}

// Enum conversion helpers
std::string Config::PositionToString(OverlayPosition pos) {
    switch (pos) {
        case OverlayPosition::TopLeft: return "top-left";
        case OverlayPosition::TopCenter: return "top-center";
        case OverlayPosition::TopRight: return "top-right";
        case OverlayPosition::Center: return "center";
        case OverlayPosition::BottomLeft: return "bottom-left";
        case OverlayPosition::BottomCenter: return "bottom-center";
        case OverlayPosition::BottomRight: return "bottom-right";
        default: return "top-center";
    }
}

OverlayPosition Config::StringToPosition(const std::string& str) {
    if (str == "top-left") return OverlayPosition::TopLeft;
    if (str == "top-center") return OverlayPosition::TopCenter;
    if (str == "top-right") return OverlayPosition::TopRight;
    if (str == "center") return OverlayPosition::Center;
    if (str == "bottom-left") return OverlayPosition::BottomLeft;
    if (str == "bottom-center") return OverlayPosition::BottomCenter;
    if (str == "bottom-right") return OverlayPosition::BottomRight;
    return OverlayPosition::TopCenter;  // Default
}

std::string Config::MonitorToString(MonitorSelection mon) {
    switch (mon) {
        case MonitorSelection::Cursor: return "cursor";
        case MonitorSelection::Primary: return "primary";
        case MonitorSelection::All: return "all";
        default: return "cursor";
    }
}

MonitorSelection Config::StringToMonitor(const std::string& str) {
    if (str == "cursor") return MonitorSelection::Cursor;
    if (str == "primary") return MonitorSelection::Primary;
    if (str == "all") return MonitorSelection::All;
    return MonitorSelection::Cursor;  // Default
}

std::string Config::BlurToString(BlurType blur) {
    switch (blur) {
        case BlurType::Acrylic: return "acrylic";
        case BlurType::Mica: return "mica";
        case BlurType::Solid: return "solid";
        default: return "acrylic";
    }
}

BlurType Config::StringToBlur(const std::string& str) {
    if (str == "acrylic") return BlurType::Acrylic;
    if (str == "mica") return BlurType::Mica;
    if (str == "solid") return BlurType::Solid;
    return BlurType::Acrylic;  // Default
}

std::string Config::ModeToString(OverlayMode mode) {
    switch (mode) {
        case OverlayMode::Notification: return "notification";
        case OverlayMode::Watermark: return "watermark";
        default: return "notification";
    }
}

OverlayMode Config::StringToMode(const std::string& str) {
    if (str == "notification") return OverlayMode::Notification;
    if (str == "watermark") return OverlayMode::Watermark;
    return OverlayMode::Notification;  // Default
}

std::string Config::ModifierToString(ModifierKey key) {
    switch (key) {
        case ModifierKey::Ctrl: return "ctrl";
        case ModifierKey::Alt: return "alt";
        case ModifierKey::Shift: return "shift";
        case ModifierKey::Win: return "win";
        default: return "ctrl";
    }
}

ModifierKey Config::StringToModifier(const std::string& str) {
    if (str == "ctrl") return ModifierKey::Ctrl;
    if (str == "alt") return ModifierKey::Alt;
    if (str == "shift") return ModifierKey::Shift;
    if (str == "win") return ModifierKey::Win;
    return ModifierKey::Ctrl;  // Default
}

uint32_t Config::ParseColor(const std::string& hex) {
    if (hex.empty()) return 0;
    
    std::string h = hex;
    if (h[0] == '#') h = h.substr(1);
    
    try {
        return static_cast<uint32_t>(std::stoul(h, nullptr, 16));
    } catch (...) {
        return 0;
    }
}

std::string Config::ColorToHex(uint32_t color) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%06X", color & 0xFFFFFF);
    return buf;
}

}  // namespace VirtualOverlay

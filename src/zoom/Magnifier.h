#pragma once

#include <windows.h>

namespace VirtualOverlay {

// Wrapper for Windows Magnification API
// Provides fullscreen zoom functionality
class Magnifier {
public:
    static Magnifier& Instance();

    // Initialize magnification subsystem
    // Must be called before any other magnification functions
    bool Init();

    // Shutdown magnification subsystem
    // Resets any active magnification
    void Shutdown();

    // Check if magnification is available and initialized
    bool IsInitialized() const;

    // Set fullscreen magnification
    // level: Magnification factor (1.0 = no zoom, 2.0 = 2x zoom, etc.)
    // centerX, centerY: Screen coordinates to center zoom on
    bool SetFullscreenMagnification(float level, int centerX, int centerY);

    // Get current magnification level
    float GetMagnificationLevel() const;
    
    // Reset magnification to 1.0 (no zoom)
    bool ResetMagnification();

    // Check if Windows Magnifier is running (conflict detection)
    static bool IsWindowsMagnifierActive();

private:
    Magnifier();
    ~Magnifier();
    Magnifier(const Magnifier&) = delete;
    Magnifier& operator=(const Magnifier&) = delete;

    bool m_initialized = false;
    float m_currentLevel = 1.0f;
};

}  // namespace VirtualOverlay

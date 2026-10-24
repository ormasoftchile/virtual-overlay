#pragma once

#include <windows.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "MirrorWindow.h"
#include "MouseMirror.h"
#include "../config/Config.h"

namespace VirtualOverlay {

// Timer id used to keep the system cursor hidden while it is over a mirrored
// monitor and to verify mirror window/monitor health. Handled by MainWndProc.
constexpr UINT_PTR TIMER_FLIP_CURSOR = 3;
constexpr UINT TIMER_FLIP_CURSOR_MS = 30;

// Timer id used to debounce/settle display configuration changes (e.g. KVM switches).
constexpr UINT_PTR TIMER_FLIP_DISPLAY_SETTLE = 4;
constexpr UINT TIMER_FLIP_DISPLAY_SETTLE_MS = 100;

// Manages per-monitor horizontal mirroring (teleprompter flip).
class FlipManager {
public:
    static FlipManager& Instance();

    bool Init(HINSTANCE hInstance, HWND hMainWnd);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    bool IsFlipped(const std::wstring& deviceName) const;
    bool IsAnyFlipped() const { return !m_windows.empty(); }

    // Enable/disable mirroring for a monitor (persisted to config).
    void SetFlipped(const std::wstring& deviceName, bool flipped);
    void Toggle(const std::wstring& deviceName);

    // Toggle mirroring for the monitor under the cursor (used by the hotkey).
    void ToggleUnderCursor();

    // Apply the persisted configuration to the current monitor set.
    void ApplySettings();

    // Recreate mirrors to match after a display configuration change.
    void OnDisplayChanged();

    // Called when the display-change settle timer fires.
    void OnDisplaySettleTimer();

    // Called on the cursor/watchdog timer.
    void OnCursorTimer();

private:
    FlipManager();
    ~FlipManager();
    FlipManager(const FlipManager&) = delete;
    FlipManager& operator=(const FlipManager&) = delete;

    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                      LONG idObject, LONG idChild, DWORD thread, DWORD time);
    void OnWindowShown(HWND hwnd);
    void UpdateZOrderHook();

    std::vector<HWINEVENTHOOK> m_zOrderHooks;

    bool EnsureMagnifier();
    void EnsureSystemCursorVisible();
    void CreateMirror(const std::wstring& deviceName, const RECT& bounds, HMONITOR hMonitor);
    void DestroyMirror(const std::wstring& deviceName);
    void DestroyAllMirrors();
    void Persist();
    void RefreshCursorHandling();
    void UpdateCursorVisibility();
    bool WantSurrogateCursor() const;
    bool UseInversionHook() const;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hMainWnd = nullptr;
    bool m_initialized = false;
    bool m_magInit = false;
    bool m_cursorTimerActive = false;
    bool m_systemCursorHidden = false;
    bool m_inUpdate = false;
    bool m_pendingTeardown = false;
    int m_settleChecksRemaining = 0;
    unsigned int m_watchdogTick = 0;

    std::map<std::wstring, std::unique_ptr<MirrorWindow>> m_windows;
    std::set<std::wstring> m_allowPrimaryFlip;
    MouseMirror m_mouseMirror;
    ULONGLONG m_lastToggleTick = 0;
};

}  // namespace VirtualOverlay

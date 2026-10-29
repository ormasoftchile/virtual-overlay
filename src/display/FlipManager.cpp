#include "FlipManager.h"
#include "../overlay/OverlayWindow.h"
#include "../utils/Logger.h"
#include "../utils/Monitor.h"
#include <magnification.h>
#include <algorithm>
#include <memory>

#pragma comment(lib, "magnification.lib")

namespace VirtualOverlay {

namespace {
struct ScopedUpdateGuard {
    bool& flag;
    explicit ScopedUpdateGuard(bool& f) : flag(f) { flag = true; }
    ~ScopedUpdateGuard() { flag = false; }
};
}  // namespace

FlipManager& FlipManager::Instance() {
    static FlipManager instance;
    return instance;
}

FlipManager::FlipManager() = default;

FlipManager::~FlipManager() {
    if (m_initialized) {
        Shutdown();
    }
}

bool FlipManager::EnsureMagnifier() {
    if (m_magInit) {
        return true;
    }
    if (!MagInitialize()) {
        LOG_ERROR("FlipManager: MagInitialize failed: %lu", GetLastError());
        return false;
    }
    m_magInit = true;
    return true;
}

void FlipManager::EnsureSystemCursorVisible() {
    if (m_magInit) {
        MagShowSystemCursor(TRUE);
    } else if (MagInitialize()) {
        MagShowSystemCursor(TRUE);
        m_magInit = true;
    }
    m_systemCursorHidden = false;
}

bool FlipManager::Init(HINSTANCE hInstance, HWND hMainWnd) {
    if (m_initialized) {
        return true;
    }

    m_hInstance = hInstance;
    m_hMainWnd = hMainWnd;
    m_initialized = true;

    // Ensure the system cursor starts visible even if a previous session left it hidden.
    EnsureSystemCursorVisible();

    // Apply persisted flips.
    ApplySettings();

    LOG_INFO("FlipManager initialized (%zu monitor(s) mirrored)", m_windows.size());
    return true;
}

void FlipManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_hMainWnd) {
        if (m_cursorTimerActive) {
            KillTimer(m_hMainWnd, TIMER_FLIP_CURSOR);
            m_cursorTimerActive = false;
        }
        KillTimer(m_hMainWnd, TIMER_FLIP_DISPLAY_SETTLE);
        m_settleChecksRemaining = 0;
    }

    m_mouseMirror.Stop();
    EnsureSystemCursorVisible();

    DestroyAllMirrors();
    UpdateZOrderHook();

    if (m_magInit) {
        MagShowSystemCursor(TRUE);
        MagUninitialize();
        m_magInit = false;
    }

    m_initialized = false;
    LOG_INFO("FlipManager shutdown");
}

void FlipManager::DestroyAllMirrors() {
    EnsureSystemCursorVisible();

    // Move map to local before destroying so re-entrant STA message pumping
    // inside Windows.Graphics.Capture cannot touch the same MirrorWindow twice.
    auto windows = std::move(m_windows);
    m_windows.clear();
    for (auto& [name, mirror] : windows) {
        if (mirror) {
            mirror->Destroy();
        }
    }
}

bool FlipManager::IsFlipped(const std::wstring& deviceName) const {
    if (deviceName.empty()) {
        return false;
    }
    for (const auto& [key, mirror] : m_windows) {
        if (_wcsicmp(key.c_str(), deviceName.c_str()) == 0) {
            return true;
        }
    }
    if (const MonitorInfo* mon = Monitor::Instance().GetByName(deviceName)) {
        for (const auto& [key, mirror] : m_windows) {
            if (mon->MatchesId(key)) {
                return true;
            }
        }
    }
    return false;
}

void FlipManager::ApplySettings() {
    if (!m_initialized || m_inUpdate) {
        return;
    }
    ScopedUpdateGuard guard(m_inUpdate);

    // Migrate any legacy "\\.\DISPLAYx" keys in the per-monitor registry to stable
    // EDID friendly names (e.g. "MNN") when the corresponding monitor is connected.
    {
        auto& registry = Config::Instance().GetMutable().monitors;
        std::vector<std::wstring> legacyKeys;
        for (const auto& [key, settings] : registry) {
            if (key.rfind(L"\\\\.\\DISPLAY", 0) == 0 && Monitor::Instance().GetByName(key)) {
                const std::wstring stableId = Monitor::Instance().GetByName(key)->GetId();
                if (!stableId.empty() && _wcsicmp(stableId.c_str(), key.c_str()) != 0) {
                    legacyKeys.push_back(key);
                }
            }
        }
        for (const auto& key : legacyKeys) {
            LOG_INFO("FlipManager: migrating configured monitor '%ws' -> stable id", key.c_str());
            const MonitorInfo* mon = Monitor::Instance().GetByName(key);
            PerMonitorSettings settings;
            if (const PerMonitorSettings* found = FindMonitorSettings(registry, key)) {
                settings = *found;
            }
            SetMonitorSettings(registry, key, PerMonitorSettings{});  // drops the legacy key
            if (mon) {
                PerMonitorSettings merged = GetMonitorSettings(registry, *mon);
                merged.flip = merged.flip || settings.flip;
                merged.showOverlay = merged.showOverlay && settings.showOverlay;
                SetMonitorSettings(registry, *mon, merged);
            }
        }
        if (!legacyKeys.empty()) {
            Persist();
        }
    }

    const auto& flip = Config::Instance().Get().flip;
    const auto& registry = Config::Instance().Get().monitors;

    // Resolve which configured monitors are currently connected and eligible to mirror.
    std::map<std::wstring, const MonitorInfo*> activeTargets;
    bool anyConfigured = false;
    for (const auto& [cfgId, settings] : registry) {
        if (!settings.flip) {
            continue;
        }
        anyConfigured = true;
        const MonitorInfo* mon = Monitor::Instance().GetByName(cfgId);
        if (!mon) {
            LOG_WARN("FlipManager: configured monitor %ws not found", cfgId.c_str());
            continue;
        }
        const std::wstring id = mon->GetId();
        // Never mirror a secondary/teleprompter monitor while it is temporarily promoted
        // to the Primary display at (0,0) during a KVM switch (while main monitors are
        // disconnected or still training DisplayPort links).
        if (mon->isPrimary && m_allowPrimaryFlip.find(id) == m_allowPrimaryFlip.end()) {
            LOG_INFO("FlipManager: skipping mirror for %ws while it is the primary display at (%ld,%ld)",
                     id.c_str(), mon->bounds.left, mon->bounds.top);
            continue;
        }
        activeTargets[id] = mon;
    }

    // If flipping is globally disabled or no monitors are configured, tear down everything.
    if (!flip.enabled || !anyConfigured) {
        if (!m_windows.empty()) {
            DestroyAllMirrors();
        }
        RefreshCursorHandling();
        return;
    }

    // Remove mirrors that are no longer configured OR whose monitor is not currently eligible.
    // Move out of m_windows before calling Destroy() to prevent re-entrancy issues.
    std::vector<std::unique_ptr<MirrorWindow>> toDestroy;
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        if (activeTargets.find(it->first) == activeTargets.end()) {
            toDestroy.push_back(std::move(it->second));
            it = m_windows.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& mirror : toDestroy) {
        if (mirror) {
            mirror->Destroy();
        }
    }

    // Create/refresh mirrors for configured monitors that currently exist.
    for (const auto& [canonicalId, mon] : activeTargets) {
        CreateMirror(canonicalId, mon->bounds, mon->handle);
    }

    RefreshCursorHandling();
}

void FlipManager::CreateMirror(const std::wstring& deviceName, const RECT& bounds, HMONITOR hMonitor) {
    if (!EnsureMagnifier()) {
        return;
    }

    auto it = m_windows.find(deviceName);
    if (it != m_windows.end() && it->second && it->second->IsValid()) {
        it->second->UpdateBounds(bounds, hMonitor);
        it->second->SetMagnifiedCursor(WantSurrogateCursor());
        return;
    }

    auto mirror = std::make_unique<MirrorWindow>();
    if (!mirror->Create(m_hInstance, deviceName, bounds, hMonitor)) {
        LOG_WARN("FlipManager: failed to create mirror for %ws", deviceName.c_str());
        return;
    }
    mirror->SetMagnifiedCursor(WantSurrogateCursor());
    m_windows[deviceName] = std::move(mirror);
}

void FlipManager::DestroyMirror(const std::wstring& deviceName) {
    std::vector<std::unique_ptr<MirrorWindow>> toDestroy;
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        if (_wcsicmp(it->first.c_str(), deviceName.c_str()) == 0) {
            toDestroy.push_back(std::move(it->second));
            it = m_windows.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& mirror : toDestroy) {
        if (mirror) {
            mirror->Destroy();
        }
    }
}

void FlipManager::SetFlipped(const std::wstring& deviceName, bool flipped) {
    if (!m_initialized) {
        return;
    }

    Monitor::Instance().Refresh();
    const MonitorInfo* mon = Monitor::Instance().GetByName(deviceName);
    const std::wstring targetId = mon ? mon->GetId() : deviceName;

    if (mon && mon->isPrimary) {
        if (flipped) {
            m_allowPrimaryFlip.insert(targetId);
        } else {
            m_allowPrimaryFlip.erase(targetId);
        }
    }

    auto& registry = Config::Instance().GetMutable().monitors;
    PerMonitorSettings settings;
    if (mon) {
        settings = GetMonitorSettings(registry, *mon);
    } else if (const PerMonitorSettings* found = FindMonitorSettings(registry, targetId)) {
        settings = *found;
    }
    settings.flip = flipped;
    if (mon) {
        // Also drops any legacy-keyed (device path / GDI name) duplicates.
        SetMonitorSettings(registry, *mon, settings);
    } else {
        SetMonitorSettings(registry, targetId, settings);
    }

    if (!flipped) {
        DestroyMirror(targetId);
        DestroyMirror(deviceName);
    }

    // Keep the global "enabled" flag in sync with whether anything is mirrored.
    bool anyFlipped = false;
    for (const auto& [id, s] : registry) {
        if (s.flip) {
            anyFlipped = true;
            break;
        }
    }
    Config::Instance().GetMutable().flip.enabled = anyFlipped;

    Persist();
    ApplySettings();
}

void FlipManager::Toggle(const std::wstring& deviceName) {
    const ULONGLONG now = GetTickCount64();
    if (now - m_lastToggleTick < 400) {
        LOG_DEBUG("FlipManager: ignoring toggle for %ws (debounced)", deviceName.c_str());
        return;
    }
    m_lastToggleTick = now;

    SetFlipped(deviceName, !IsFlipped(deviceName));
}

void FlipManager::ToggleUnderCursor() {
    if (!m_initialized) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (now - m_lastToggleTick < 400) {
        LOG_DEBUG("FlipManager: ignoring toggle under cursor (debounced)");
        return;
    }

    Monitor::Instance().Refresh();
    const MonitorInfo* mon = Monitor::Instance().GetAtCursor();
    if (!mon) {
        LOG_WARN("FlipManager: no monitor under cursor for hotkey toggle");
        return;
    }
    const std::wstring id = mon->GetId();
    LOG_INFO("FlipManager: toggling flip for monitor %ws (%ws) [%ld,%ld - %ld,%ld]",
             id.c_str(), mon->name.c_str(),
             mon->bounds.left, mon->bounds.top, mon->bounds.right, mon->bounds.bottom);
    Toggle(id);
}

void FlipManager::OnDisplayChanged() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("FlipManager: display change detected, hiding mirrors, restoring cursor, and scheduling settle");

    // Pure Win32 immediate actions (safe inside sent WM_DISPLAYCHANGE without pumping COM STA):
    EnsureSystemCursorVisible();
    m_mouseMirror.Stop();
    if (m_cursorTimerActive && m_hMainWnd) {
        KillTimer(m_hMainWnd, TIMER_FLIP_CURSOR);
        m_cursorTimerActive = false;
    }
    for (auto& [name, mirror] : m_windows) {
        if (mirror && mirror->GetHwnd()) {
            ShowWindow(mirror->GetHwnd(), SW_HIDE);
        }
    }

    // Defer WGC COM teardown and monitor re-evaluation to the posted timer loop
    // so we never enter Windows.Graphics.Capture.dll inside a sent WM_DISPLAYCHANGE message.
    m_pendingTeardown = true;
    m_settleChecksRemaining = 10;  // 10 * 500ms = 5 seconds of settle checks for KVM link training
    if (m_hMainWnd) {
        SetTimer(m_hMainWnd, TIMER_FLIP_DISPLAY_SETTLE, TIMER_FLIP_DISPLAY_SETTLE_MS, nullptr);
    }
}

void FlipManager::OnDisplaySettleTimer() {
    if (!m_initialized || m_inUpdate) {
        return;
    }

    if (m_pendingTeardown) {
        m_pendingTeardown = false;
        EnsureSystemCursorVisible();
        m_mouseMirror.Stop();
        DestroyAllMirrors();
    }

    Monitor::Instance().Refresh();
    OverlayWindow::Instance().CheckPosition();
    ApplySettings();

    if (m_windows.empty()) {
        EnsureSystemCursorVisible();
    }

    if (m_settleChecksRemaining > 0) {
        --m_settleChecksRemaining;
        if (m_hMainWnd) {
            SetTimer(m_hMainWnd, TIMER_FLIP_DISPLAY_SETTLE, 500, nullptr);
        }
    } else if (m_hMainWnd) {
        KillTimer(m_hMainWnd, TIMER_FLIP_DISPLAY_SETTLE);
    }
}

void FlipManager::Persist() {
    Config::Instance().Apply();
}

bool FlipManager::WantSurrogateCursor() const {
    // Any mode other than "None" hides the real cursor over a mirror and draws
    // a mirrored surrogate cursor into the flipped content. The real (hidden)
    // cursor then sits on the element shown under the surrogate, so clicks,
    // hover and dragging land on what the user sees.
    return Config::Instance().Get().flip.cursorMode != FlipCursorMode::None;
}

bool FlipManager::UseInversionHook() const {
    // invertMouse means "the visible pointer moves mirrored relative to the
    // hand" (natural when viewed through the teleprompter glass).
    // - Mirrored pointer: the surrogate is drawn at reflect(real), so it is
    //   already mirrored; the hook is needed only to cancel that.
    // - System pointer: the real cursor is visible, so the hook provides it.
    const auto& flip = Config::Instance().Get().flip;
    return WantSurrogateCursor() != flip.invertMouse;
}

void FlipManager::UpdateZOrderHook() {
    const bool want = !m_windows.empty();
    if (want && m_zOrderHooks.empty()) {
        // Popups (e.g. the Windows 11 XAML context menu) are often shown with
        // an empty rect or cloaked, then positioned/uncloaked, so watch all of
        // these events rather than just SHOW.
        const DWORD events[][2] = {
            { EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW },
            { EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE },
            { EVENT_OBJECT_UNCLOAKED, EVENT_OBJECT_UNCLOAKED },
            { EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_MENUPOPUPSTART },
        };
        for (const auto& range : events) {
            if (HWINEVENTHOOK h = SetWinEventHook(range[0], range[1], nullptr, WinEventProc, 0, 0,
                                                  WINEVENT_OUTOFCONTEXT)) {
                m_zOrderHooks.push_back(h);
            }
        }
    } else if (!want && !m_zOrderHooks.empty()) {
        for (HWINEVENTHOOK h : m_zOrderHooks) {
            UnhookWinEvent(h);
        }
        m_zOrderHooks.clear();
    }
}

void CALLBACK FlipManager::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                        LONG idObject, LONG idChild, DWORD, DWORD) {
    if (!hwnd) {
        return;
    }
    if (event != EVENT_SYSTEM_FOREGROUND && event != EVENT_SYSTEM_MENUPOPUPSTART &&
        (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)) {
        return;
    }
    Instance().OnWindowShown(hwnd);
}

void FlipManager::OnWindowShown(HWND hwnd) {
    // Menus, tooltips and other topmost popups opened after the mirror sit
    // above it and appear un-flipped (on top of the flipped copy captured
    // beneath). Re-raise the mirror over them; it is click-through, so they
    // still receive input.
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root) {
        root = hwnd;
    }
    for (const auto& [name, mirror] : m_windows) {
        if (mirror && mirror->GetHwnd() == root) {
            return;
        }
    }
    if (!IsWindowVisible(root) || !(GetWindowLongPtrW(root, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
        return;
    }

    RECT wr = {};
    if (!GetWindowRect(root, &wr) || IsRectEmpty(&wr)) {
        return;
    }
    for (const auto& [name, mirror] : m_windows) {
        RECT overlap = {};
        if (!mirror || !mirror->IsValid() || !IntersectRect(&overlap, &wr, &mirror->Bounds())) {
            continue;
        }
        // Only raise when the popup is actually above the mirror.
        const HWND mirrorHwnd = mirror->GetHwnd();
        for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT)) {
            if (w == mirrorHwnd) {
                break;
            }
            if (w == root) {
                mirror->RaiseToTop();
                break;
            }
        }
    }
}

void FlipManager::RefreshCursorHandling() {
    // Gather the bounds of the currently mirrored monitors.
    std::vector<RECT> rects;
    for (const auto& [name, mirror] : m_windows) {
        if (mirror && mirror->IsValid()) {
            rects.push_back(mirror->Bounds());
        }
    }

    // Mouse inversion hook.
    if (UseInversionHook() && !rects.empty()) {
        if (m_mouseMirror.IsActive()) {
            m_mouseMirror.UpdateRects(rects);
        } else {
            m_mouseMirror.Start(rects);
        }
    } else {
        m_mouseMirror.Stop();
    }

    // Cursor visibility & mirror health watchdog timer (active whenever any monitor is mirrored).
    const bool needTimer = !rects.empty();
    if (needTimer && !m_cursorTimerActive && m_hMainWnd) {
        SetTimer(m_hMainWnd, TIMER_FLIP_CURSOR, TIMER_FLIP_CURSOR_MS, nullptr);
        m_cursorTimerActive = true;
    } else if (!needTimer && m_cursorTimerActive && m_hMainWnd) {
        KillTimer(m_hMainWnd, TIMER_FLIP_CURSOR);
        m_cursorTimerActive = false;
    }

    UpdateCursorVisibility();
    UpdateZOrderHook();
}

void FlipManager::UpdateCursorVisibility() {
    // The real system cursor must be hidden while over a mirror so only the
    // surrogate (mirrored) cursor drawn into the flipped content is visible.
    if (!WantSurrogateCursor() || m_windows.empty()) {
        if (m_systemCursorHidden) {
            EnsureSystemCursorVisible();
        }
        return;
    }

    POINT pt = {};
    GetCursorPos(&pt);

    bool overMirrored = false;
    for (const auto& [name, mirror] : m_windows) {
        if (mirror && mirror->IsValid() && PtInRect(&mirror->Bounds(), pt)) {
            overMirrored = true;
            break;
        }
    }

    if (overMirrored && !m_systemCursorHidden) {
        EnsureMagnifier();
        MagShowSystemCursor(FALSE);
        m_systemCursorHidden = true;
    } else if (!overMirrored && m_systemCursorHidden) {
        EnsureSystemCursorVisible();
    }
}

void FlipManager::OnCursorTimer() {
    if (m_inUpdate || m_pendingTeardown) {
        return;
    }

    UpdateCursorVisibility();

    if (m_windows.empty()) {
        return;
    }

    bool needsResync = false;
    ++m_watchdogTick;
    const bool checkTopology = (m_watchdogTick % 20 == 0);  // Every ~600ms
    if (checkTopology) {
        Monitor::Instance().Refresh();
        // Periodically reinforce system cursor visibility when not over a mirror
        if (!m_systemCursorHidden && m_magInit) {
            MagShowSystemCursor(TRUE);
        }
    }

    for (const auto& [id, mirror] : m_windows) {
        if (!mirror || !mirror->IsValid() || mirror->IsCaptureLost()) {
            LOG_WARN("FlipManager: capture lost or invalid mirror for %ws, resyncing", id.c_str());
            needsResync = true;
            break;
        }

        // Verify Windows hasn't migrated/resized our top-level mirror window (e.g. to (0,0) during KVM switch)
        RECT actualRect = {};
        if (!GetWindowRect(mirror->GetHwnd(), &actualRect) || !EqualRect(&actualRect, &mirror->Bounds())) {
            LOG_WARN("FlipManager: mirror window for %ws moved from [%ld,%ld-%ld,%ld] to [%ld,%ld-%ld,%ld], resyncing",
                     id.c_str(),
                     mirror->Bounds().left, mirror->Bounds().top, mirror->Bounds().right, mirror->Bounds().bottom,
                     actualRect.left, actualRect.top, actualRect.right, actualRect.bottom);
            needsResync = true;
            break;
        }

        // Verify the HMONITOR is still valid and has the same bounds, and is not Primary unless allowed
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(mirror->MonitorHandle(), &mi) || !EqualRect(&mi.rcMonitor, &mirror->Bounds())) {
            LOG_WARN("FlipManager: HMONITOR bounds changed or invalidated for %ws, resyncing", id.c_str());
            needsResync = true;
            break;
        }
        if ((mi.dwFlags & MONITORINFOF_PRIMARY) != 0 && m_allowPrimaryFlip.find(id) == m_allowPrimaryFlip.end()) {
            LOG_WARN("FlipManager: %ws became primary display during transition, resyncing", id.c_str());
            needsResync = true;
            break;
        }

        // Periodically verify via QueryDisplayConfig that the target monitor by friendlyName/ID
        // still maps to this exact HMONITOR and bounds.
        if (checkTopology) {
            const MonitorInfo* mon = Monitor::Instance().GetByName(id);
            if (!mon || mon->handle != mirror->MonitorHandle() || !EqualRect(&mon->bounds, &mirror->Bounds())) {
                LOG_WARN("FlipManager: display topology changed for %ws, resyncing", id.c_str());
                needsResync = true;
                break;
            }
        }
    }

    if (needsResync) {
        OnDisplayChanged();
    }
}

}  // namespace VirtualOverlay

namespace VirtualOverlay {
void FlipManager::RaiseMirrorsToTop() {
    for (const auto& [id, mirror] : m_windows) {
        if (mirror && mirror->IsValid()) {
            mirror->RaiseToTop();
        }
    }
}
}  // namespace VirtualOverlay

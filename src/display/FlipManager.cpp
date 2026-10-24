#include "FlipManager.h"
#include "../utils/Logger.h"
#include "../utils/Monitor.h"
#include <magnification.h>
#include <algorithm>
#include <memory>

#pragma comment(lib, "magnification.lib")

namespace VirtualOverlay {

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

bool FlipManager::Init(HINSTANCE hInstance, HWND hMainWnd) {
    if (m_initialized) {
        return true;
    }

    m_hInstance = hInstance;
    m_hMainWnd = hMainWnd;
    m_initialized = true;

    // Apply persisted flips.
    ApplySettings();

    LOG_INFO("FlipManager initialized (%zu monitor(s) mirrored)", m_windows.size());
    return true;
}

void FlipManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_cursorTimerActive && m_hMainWnd) {
        KillTimer(m_hMainWnd, TIMER_FLIP_CURSOR);
        m_cursorTimerActive = false;
    }

    m_mouseMirror.Stop();

    m_windows.clear();
    UpdateZOrderHook();

    if (m_systemCursorHidden) {
        MagShowSystemCursor(TRUE);
        m_systemCursorHidden = false;
    }

    if (m_magInit) {
        MagUninitialize();
        m_magInit = false;
    }

    m_initialized = false;
    LOG_INFO("FlipManager shutdown");
}

bool FlipManager::IsFlipped(const std::wstring& deviceName) const {
    return m_windows.find(deviceName) != m_windows.end();
}

void FlipManager::ApplySettings() {
    if (!m_initialized) {
        return;
    }

    const auto& flip = Config::Instance().Get().flip;

    // Remove mirrors that are no longer configured.
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        const bool stillConfigured =
            std::find(flip.monitors.begin(), flip.monitors.end(), it->first) != flip.monitors.end();
        if (!stillConfigured) {
            it = m_windows.erase(it);
        } else {
            ++it;
        }
    }

    // Create/refresh mirrors for configured monitors that currently exist.
    for (const auto& deviceName : flip.monitors) {
        const MonitorInfo* mon = Monitor::Instance().GetByName(deviceName);
        if (!mon) {
            LOG_WARN("FlipManager: configured monitor %ws not found", deviceName.c_str());
            continue;
        }
        CreateMirror(deviceName, mon->bounds, mon->handle);
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
    auto it = m_windows.find(deviceName);
    if (it != m_windows.end()) {
        it->second->Destroy();
        m_windows.erase(it);
    }
}

void FlipManager::SetFlipped(const std::wstring& deviceName, bool flipped) {
    if (!m_initialized) {
        return;
    }

    auto& monitors = Config::Instance().GetMutable().flip.monitors;
    auto found = std::find(monitors.begin(), monitors.end(), deviceName);

    if (flipped) {
        if (found == monitors.end()) {
            monitors.push_back(deviceName);
        }
        if (const MonitorInfo* mon = Monitor::Instance().GetByName(deviceName)) {
            CreateMirror(deviceName, mon->bounds, mon->handle);
        } else {
            LOG_WARN("FlipManager: cannot mirror %ws - monitor not found", deviceName.c_str());
        }
    } else {
        if (found != monitors.end()) {
            monitors.erase(found);
        }
        DestroyMirror(deviceName);
    }

    // Keep the global "enabled" flag in sync with whether anything is mirrored.
    Config::Instance().GetMutable().flip.enabled = !monitors.empty();

    Persist();
    RefreshCursorHandling();
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
    LOG_INFO("FlipManager: toggling flip for monitor %ws [%ld,%ld - %ld,%ld]",
             mon->name.c_str(), mon->bounds.left, mon->bounds.top, mon->bounds.right, mon->bounds.bottom);
    Toggle(mon->name);
}

void FlipManager::OnDisplayChanged() {
    if (!m_initialized) {
        return;
    }
    Monitor::Instance().Refresh();
    ApplySettings();
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

    // Cursor visibility timer (needed whenever we draw a surrogate cursor so we
    // can hide/show the real system cursor as it enters/leaves the mirror).
    const bool needTimer = WantSurrogateCursor() && !rects.empty();
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
            MagShowSystemCursor(TRUE);
            m_systemCursorHidden = false;
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
        MagShowSystemCursor(FALSE);
        m_systemCursorHidden = true;
    } else if (!overMirrored && m_systemCursorHidden) {
        MagShowSystemCursor(TRUE);
        m_systemCursorHidden = false;
    }
}

void FlipManager::OnCursorTimer() {
    UpdateCursorVisibility();
}

}  // namespace VirtualOverlay

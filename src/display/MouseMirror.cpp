#include "MouseMirror.h"
#include "../utils/Logger.h"
#include <algorithm>

namespace VirtualOverlay {

MouseMirror* MouseMirror::s_instance = nullptr;

MouseMirror::~MouseMirror() {
    Stop();
}

bool MouseMirror::PointInRect(const RECT& rect, const POINT& pt) {
    return pt.x >= rect.left && pt.x < rect.right &&
           pt.y >= rect.top && pt.y < rect.bottom;
}

bool MouseMirror::OnDesktop(const POINT& pt) {
    return MonitorFromPoint(pt, MONITOR_DEFAULTTONULL) != nullptr;
}

bool MouseMirror::Start(const std::vector<RECT>& monitorRects) {
    if (monitorRects.empty()) {
        return false;
    }
    UpdateRects(monitorRects);

    if (m_thread.joinable()) {
        return true;
    }

    s_instance = this;
    m_hasAnchor.store(false);
    m_hookOk = false;
    m_started = false;

    m_thread = std::thread(&MouseMirror::ThreadMain, this);

    // Wait until the hook thread reports whether installation succeeded.
    while (!m_started.load()) {
        Sleep(1);
    }
    if (!m_hookOk) {
        m_thread.join();
        m_threadId = 0;
        s_instance = nullptr;
        return false;
    }
    return true;
}

void MouseMirror::UpdateRects(const std::vector<RECT>& monitorRects) {
    {
        std::lock_guard<std::mutex> lock(m_rectsMutex);
        m_rects = monitorRects;
    }
    // Force re-anchoring on next mouse move when monitor geometry changes.
    m_hasAnchor.store(false);
    if (monitorRects.empty()) {
        Stop();
    }
}

void MouseMirror::Stop() {
    if (m_thread.joinable()) {
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
        m_thread.join();
        m_threadId = 0;
        LOG_INFO("MouseMirror: mouse inversion hook removed");
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
    m_hasAnchor.store(false);
}

void MouseMirror::ThreadMain() {
    m_threadId = GetCurrentThreadId();

    // The hook must never be delayed, otherwise Windows skips it and lets the
    // un-inverted movement through.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // Force creation of this thread's message queue before signalling.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, HookProc, GetModuleHandleW(nullptr), 0);
    m_hookOk = (hook != nullptr);
    if (!hook) {
        LOG_ERROR("MouseMirror: failed to install mouse hook: %lu", GetLastError());
    } else {
        LOG_INFO("MouseMirror: mouse inversion hook installed on dedicated thread");
    }
    m_started = true;
    if (!hook) {
        return;
    }

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(hook);
}

POINT MouseMirror::Reflect(const POINT& virt) const {
    POINT target = virt;
    std::lock_guard<std::mutex> lock(m_rectsMutex);
    for (const auto& rect : m_rects) {
        if (PointInRect(rect, virt)) {
            target.x = rect.left + rect.right - 1 - virt.x;
            target.x = std::clamp(target.x, rect.left, rect.right - 1);
            break;
        }
    }
    return target;
}

// Returns true if the original event must be swallowed (we moved the cursor).
bool MouseMirror::HandleMove(const POINT& physical) {
    // Where the real cursor actually is right now (before this event applies).
    POINT current = {};
    GetCursorPos(&current);

    // (Re)anchor when starting, or when something else moved the cursor
    // (another app's SetCursorPos, touch, pen, display change, ...). Reflection
    // is its own inverse, so the virtual position is reflect(real).
    if (!m_hasAnchor.load() || current.x != m_lastSet.x || current.y != m_lastSet.y) {
        m_virtual = Reflect(current);
        m_lastSet = current;
        m_hasAnchor.store(true);
    }

    const LONG dx = physical.x - current.x;
    const LONG dy = physical.y - current.y;
    if (dx == 0 && dy == 0) {
        return false;
    }

    // Advance the user-perceived position in the physical direction, never
    // letting it leave the desktop (otherwise it silently desyncs from the
    // real cursor, e.g. when pushing against an outer screen edge).
    POINT next = { m_virtual.x + dx, m_virtual.y + dy };
    if (!OnDesktop(next)) {
        const POINT onlyX = { m_virtual.x + dx, m_virtual.y };
        const POINT onlyY = { m_virtual.x, m_virtual.y + dy };
        if (OnDesktop(onlyX)) next = onlyX;
        else if (OnDesktop(onlyY)) next = onlyY;
        else next = m_virtual;
    }
    m_virtual = next;

    POINT target = Reflect(m_virtual);

    // The system clips movement at the outer desktop edge, which would freeze
    // the reflected cursor there. Keep the real cursor one pixel inside so the
    // outward movement is still observable.
    if (target.x != m_virtual.x) {
        if (!OnDesktop({ target.x - 1, target.y })) target.x += 1;
        else if (!OnDesktop({ target.x + 1, target.y })) target.x -= 1;
    }

    if (target.x == physical.x && target.y == physical.y) {
        m_lastSet = physical;
        return false;
    }

    SetCursorPos(target.x, target.y);
    GetCursorPos(&m_lastSet);
    return true;
}

LRESULT CALLBACK MouseMirror::HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    MouseMirror* self = s_instance;
    if (nCode != HC_ACTION || self == nullptr || wParam != WM_MOUSEMOVE) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

    // Leave injected events (remote desktop, automation, other tools) alone;
    // the next physical event re-anchors if they moved the cursor.
    if (info->flags & LLMHF_INJECTED) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    if (self->HandleMove(info->pt)) {
        return 1;
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace VirtualOverlay

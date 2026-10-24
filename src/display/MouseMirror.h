#pragma once

#include <windows.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace VirtualOverlay {

// Low-level mouse hook that inverts horizontal mouse movement while the
// user-perceived ("virtual") cursor is over a mirrored (flipped) monitor.
//
// The mirror overlay hides the real cursor and draws a mirrored surrogate at
// reflect(real). This hook keeps real = reflect(virtual) so the surrogate moves
// in the physical direction, while the real (hidden) cursor sits exactly over
// the element the user sees under the surrogate -> clicks/hover/drag land right.
//
// The hook runs on its own dedicated thread with its own message loop. A
// low-level hook that is delayed by a busy thread is skipped (and eventually
// silently removed) by Windows, which would let un-inverted movement through.
class MouseMirror {
public:
    MouseMirror() = default;
    ~MouseMirror();

    MouseMirror(const MouseMirror&) = delete;
    MouseMirror& operator=(const MouseMirror&) = delete;

    // Install the hook. Returns true on success.
    bool Start(const std::vector<RECT>& monitorRects);

    // Update the set of mirrored monitors while the hook stays installed.
    void UpdateRects(const std::vector<RECT>& monitorRects);

    void Stop();

    bool IsActive() const { return m_thread.joinable(); }

private:
    static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam);
    static bool PointInRect(const RECT& rect, const POINT& pt);
    static bool OnDesktop(const POINT& pt);

    void ThreadMain();
    bool HandleMove(const POINT& physical);
    POINT Reflect(const POINT& virt) const;

    std::thread m_thread;
    DWORD m_threadId = 0;
    std::atomic<bool> m_started{ false };
    bool m_hookOk = false;

    mutable std::mutex m_rectsMutex;
    std::vector<RECT> m_rects;

    // Touched by the hook thread; m_hasAnchor is atomic so UpdateRects can force re-anchoring.
    POINT m_virtual = {LONG_MIN, LONG_MIN};  // user-perceived position
    POINT m_lastSet = {LONG_MIN, LONG_MIN};  // last real position (actual)
    std::atomic<bool> m_hasAnchor{ false };

    static MouseMirror* s_instance;
};

}  // namespace VirtualOverlay

#pragma once

#include "../config/Config.h"
#include "../utils/Monitor.h"
#include "ScriptAlignmentEngine.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <functional>
#include <string>
#include <vector>

namespace VirtualOverlay {

using Microsoft::WRL::ComPtr;

constexpr UINT_PTR TIMER_PRESENTER_FRAME = 50;
constexpr UINT_PTR TIMER_PRESENTER_INTERACTION = 51;
constexpr UINT TIMER_PRESENTER_FRAME_MS = 16;        // ~60 FPS smooth scroll & audio meter
constexpr UINT TIMER_PRESENTER_INTERACTION_MS = 30;  // ~33 Hz hover & Alt-key detection

enum class ToolbarButton {
    None = 0,
    ResetTop,
    ScrollUp,
    ScrollDown,
    JumpEnd,
    Move,
    PauseResume,
    Microphone,
    Close
};

struct LogicalScriptBlock {
    ComPtr<IDWriteTextLayout> textLayout;
    uint32_t textStart = 0;       // Start index in m_model.Text()
    uint32_t textLength = 0;      // Character count in m_model.Text()
    float layoutTopY = 0.0f;      // Y offset of this block's layout in virtual scroll space
    float indentX = 0.0f;         // Left indent (in pixels) relative to m_paddingX
    bool isBullet = false;        // True if this block is a bullet item (-, *, +, •)
    int bulletDepth = 0;          // 0 = top-level bullet, 1+ = nested bullet
    bool hasParagraphBreakBefore = false;
    bool hasLineBreakBefore = false;
};

struct VisualScriptLine {
    int lineIndex = 0;
    int blockIndex = 0;
    uint32_t textStart = 0;
    uint32_t textLength = 0;
    float topY = 0.0f;
    float height = 0.0f;
    float baselineY = 0.0f;
    float indentX = 0.0f;
    bool showBullet = false;
    int bulletDepth = 0;
    bool hasParagraphGapBefore = false;
    bool isHardLineEnd = false;
    int firstTokenIndex = -1;
    int lastTokenIndex = -1;
};

class PresenterWindow {
public:
    using VoidCallback = std::function<void()>;
    using PointCallback = std::function<void(POINT screenPt)>;
    using TokenSeekCallback = std::function<void(int tokenIndex)>;
    using AnchorChangedCallback = std::function<void(const std::wstring& monitorId, PresenterMonitorAnchor anchor)>;

    PresenterWindow();
    ~PresenterWindow();
    PresenterWindow(const PresenterWindow&) = delete;
    PresenterWindow& operator=(const PresenterWindow&) = delete;

    bool Init(HINSTANCE hInstance);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    void Show();
    void Hide();
    bool IsVisible() const { return m_visible; }
    HWND GetHwnd() const { return m_hwnd; }

    // Apply configuration and script model
    void ApplyConfig(const PresenterConfig& config);
    void SetScriptModel(const ScriptModel& model, int currentTokenIndex);
    void SetActiveToken(int tokenIndex, TrackingState trackingState, bool immediateJump = false);

    // Runtime status updates for toolbar / status pill
    void SetPaused(bool paused);
    bool IsPaused() const { return m_paused; }
    void SetAudioStatus(float liveLevel, bool deviceAvailable, const std::wstring& deviceLabel);
    void SetRecognizerStatus(const std::wstring& statusText, bool isReady);

    // Line navigation helpers
    int GetLineCount() const { return static_cast<int>(m_lines.size()); }
    int GetActiveLineIndex() const { return m_activeLineIndex; }
    std::vector<std::wstring> GetLinePreviews() const;
    int GetFirstTokenForLine(int lineIndex) const;

    // Multi-monitor & DPI handling
    void OnDisplayChanged();
    std::wstring GetCurrentMonitorId() const { return m_currentMonitorId; }
    PresenterMonitorAnchor GetCurrentAnchor() const { return m_currentAnchor; }
    float GetReadingBaselineLocalY() const { return m_readingBaselineLocalY; }
    void SetAnchorForMonitor(const std::wstring& monitorId, PresenterMonitorAnchor anchor);

    // Callbacks to PresenterController
    void SetResetTopCallback(VoidCallback cb) { m_onResetTop = std::move(cb); }
    void SetJumpEndCallback(VoidCallback cb) { m_onJumpEnd = std::move(cb); }
    void SetPauseResumeCallback(VoidCallback cb) { m_onPauseResume = std::move(cb); }
    void SetMicrophoneMenuCallback(std::function<void(POINT)> cb) { m_onMicrophoneMenu = std::move(cb); }
    void SetCloseCallback(VoidCallback cb) { m_onClose = std::move(cb); }
    void SetTokenSeekCallback(TokenSeekCallback cb) { m_onTokenSeek = std::move(cb); }
    void SetAnchorChangedCallback(AnchorChangedCallback cb) { m_onAnchorChanged = std::move(cb); }

    void StepLine(int deltaLines);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void RebuildTextLayout();
    void RecalculateWindowGeometry();
    void UpdateWindowPositionFromAnchor();
    void RenderLayered();

    void OnFrameTimer();
    void OnInteractionTimer();
    void SetClickThrough(bool clickThrough);

    ToolbarButton HitTestToolbar(POINT clientPt) const;
    int HitTestLineIndex(POINT clientPt) const;
    void BeginAnchorDrag(POINT screenPt);
    void UpdateAnchorDrag(POINT screenPt);
    void EndAnchorDrag();
    void AdjustAnchorVerticalPixels(int deltaPixels);

    const MonitorInfo* ResolveTargetMonitor() const;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;
    bool m_initialized = false;
    bool m_visible = false;
    bool m_clickThrough = true;

    PresenterConfig m_config;
    ScriptModel m_model;

    // DirectWrite layout & line mapping
    ComPtr<IDWriteTextFormat> m_textFormat;
    ComPtr<IDWriteTextLayout> m_textLayout;
    std::vector<LogicalScriptBlock> m_blocks;
    std::vector<VisualScriptLine> m_lines;
    std::vector<int> m_tokenToLine;

    int m_activeTokenIndex = -1;
    int m_activeLineIndex = 0;
    TrackingState m_trackingState = TrackingState::Idle;

    // Smooth vertical scroll state (in layout DIP/pixel coordinates)
    double m_currentScrollBaselineY = 0.0;
    double m_targetScrollBaselineY = 0.0;
    double m_scrollVelocity = 0.0;
    ULONGLONG m_lastFrameTick = 0;

    // Window & reading-line geometry (in physical screen pixels)
    std::wstring m_currentMonitorId;
    PresenterMonitorAnchor m_currentAnchor{ 0.50f, 0.14f };
    float m_dpiScale = 1.0f;
    int m_windowWidth = 840;
    int m_windowHeight = 200;
    float m_readingBaselineLocalY = 90.0f;  // Fixed reading baseline Y within window
    float m_nominalLineHeight = 42.0f;
    float m_nominalBaseline = 32.0f;
    float m_contentTopY = 44.0f;
    float m_contentBottomY = 186.0f;
    float m_paddingX = 28.0f;

    // Toolbar & interaction state
    bool m_mouseHovered = false;
    bool m_altHeld = false;
    bool m_isDraggingAnchor = false;
    bool m_pendingBodyDrag = false;
    POINT m_dragStartScreenPt = {};
    POINT m_downClientPt = {};
    int m_dragStartAnchorScreenX = 0;
    int m_dragStartAnchorScreenY = 0;

    RECT m_toolbarLocalRect = {};
    RECT m_btnTopRect = {};
    RECT m_btnUpRect = {};
    RECT m_btnDownRect = {};
    RECT m_btnEndRect = {};
    RECT m_btnMoveRect = {};
    RECT m_btnPauseRect = {};
    RECT m_btnMicRect = {};
    RECT m_btnCloseRect = {};
    ToolbarButton m_hoveredButton = ToolbarButton::None;

    // Runtime status
    bool m_paused = false;
    float m_audioLevel = 0.0f;
    float m_displayedAudioLevel = 0.0f;
    bool m_audioAvailable = true;
    std::wstring m_audioDeviceLabel = L"Default";
    std::wstring m_recognizerStatusText = L"Ready";
    bool m_recognizerReady = false;

    // Callbacks
    VoidCallback m_onResetTop;
    VoidCallback m_onJumpEnd;
    VoidCallback m_onPauseResume;
    std::function<void(POINT)> m_onMicrophoneMenu;
    VoidCallback m_onClose;
    TokenSeekCallback m_onTokenSeek;
    AnchorChangedCallback m_onAnchorChanged;
};

}  // namespace VirtualOverlay

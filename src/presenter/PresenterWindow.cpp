#include "PresenterWindow.h"
#include "../utils/D2DRenderer.h"
#include "../utils/Logger.h"
#include "../desktop/VirtualDesktop.h"
#include "../display/FlipManager.h"

#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace VirtualOverlay {

constexpr wchar_t PRESENTER_WINDOW_CLASS[] = L"VirtualOverlayPresenterWindow";

PresenterWindow::PresenterWindow() = default;

PresenterWindow::~PresenterWindow() {
    Shutdown();
}

bool PresenterWindow::Init(HINSTANCE hInstance) {
    if (m_initialized) {
        return true;
    }

    m_hInstance = hInstance;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = PRESENTER_WINDOW_CLASS;

    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("PresenterWindow: Failed to register window class (%lu)", err);
            return false;
        }
    }

    // Start as layered, topmost, non-activating, toolwindow, and click-through by default
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT;
    m_hwnd = CreateWindowExW(
        exStyle,
        PRESENTER_WINDOW_CLASS,
        L"Virtual Overlay Presenter",
        WS_POPUP,
        0, 0, m_windowWidth, m_windowHeight,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd) {
        LOG_ERROR("PresenterWindow: CreateWindowExW failed (%lu)", GetLastError());
        return false;
    }

    m_clickThrough = true;
    D2DRenderer::Instance().Init();
    m_initialized = true;
    LOG_INFO("PresenterWindow initialized");
    return true;
}

void PresenterWindow::Shutdown() {
    if (!m_initialized) {
        return;
    }

    Hide();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    m_blocks.clear();
    m_textLayout.Reset();
    m_textFormat.Reset();
    m_initialized = false;
    LOG_INFO("PresenterWindow shutdown");
}

void PresenterWindow::Show() {
    if (!m_initialized || !m_hwnd) {
        return;
    }

    Monitor::Instance().Refresh();
    RecalculateWindowGeometry();
    RebuildTextLayout();
    UpdateWindowPositionFromAnchor();

    VirtualDesktop::Instance().MoveWindowToCurrentDesktop(m_hwnd);

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    m_visible = true;
    m_lastFrameTick = GetTickCount64();

    RenderLayered();

    SetTimer(m_hwnd, TIMER_PRESENTER_FRAME, TIMER_PRESENTER_FRAME_MS, nullptr);
    SetTimer(m_hwnd, TIMER_PRESENTER_INTERACTION, TIMER_PRESENTER_INTERACTION_MS, nullptr);
}

void PresenterWindow::Hide() {
    if (!m_initialized || !m_hwnd) {
        return;
    }

    KillTimer(m_hwnd, TIMER_PRESENTER_FRAME);
    KillTimer(m_hwnd, TIMER_PRESENTER_INTERACTION);

    if (m_isDraggingAnchor) {
        ReleaseCapture();
        m_isDraggingAnchor = false;
    }

    SetClickThrough(true);
    ShowWindow(m_hwnd, SW_HIDE);
    m_visible = false;
    m_mouseHovered = false;
    m_altHeld = false;
}

const MonitorInfo* PresenterWindow::ResolveTargetMonitor() const {
    auto& monMgr = Monitor::Instance();

    // 1. Current monitor ID if still connected (takes priority during/after dragging across screens)
    if (!m_currentMonitorId.empty()) {
        if (const MonitorInfo* mon = monMgr.GetByName(m_currentMonitorId)) {
            return mon;
        }
    }

    // 2. Explicit target monitor in presenter config
    if (!m_config.targetMonitor.empty()) {
        if (const MonitorInfo* mon = monMgr.GetByName(m_config.targetMonitor)) {
            return mon;
        }
    }

    // 3. If a monitor is currently flipped for teleprompter (e.g. MNN), prefer it!
    const auto& flipMonitors = Config::Instance().Get().flip.monitors;
    for (const auto& fm : flipMonitors) {
        if (const MonitorInfo* mon = monMgr.GetByName(fm)) {
            if (FlipManager::Instance().IsFlipped(mon->GetId()) || !mon->isPrimary) {
                return mon;
            }
        }
    }

    // 4. Fallback to Primary monitor
    if (const MonitorInfo* primary = monMgr.GetPrimary()) {
        return primary;
    }

    const auto& all = monMgr.GetMonitors();
    return all.empty() ? nullptr : &all.front();
}

void PresenterWindow::ApplyConfig(const PresenterConfig& config) {
    m_config = config;

    const MonitorInfo* mon = ResolveTargetMonitor();
    if (mon) {
        m_currentMonitorId = mon->GetId();
        auto it = m_config.monitorAnchors.find(m_currentMonitorId);
        if (it != m_config.monitorAnchors.end()) {
            m_currentAnchor = it->second;
        }
    }

    RecalculateWindowGeometry();
    RebuildTextLayout();

    // Snap scroll baseline to the current active line so font/width changes never shift the reading line
    if (m_activeLineIndex >= 0 && m_activeLineIndex < static_cast<int>(m_lines.size())) {
        m_targetScrollBaselineY = m_lines[static_cast<size_t>(m_activeLineIndex)].baselineY;
        m_currentScrollBaselineY = m_targetScrollBaselineY;
        m_scrollVelocity = 0.0;
    }

    if (m_visible) {
        UpdateWindowPositionFromAnchor();
        RenderLayered();
    }
}

void PresenterWindow::SetScriptModel(const ScriptModel& model, int currentTokenIndex) {
    m_model = model;
    m_activeTokenIndex = currentTokenIndex;
    RebuildTextLayout();

    if (m_activeTokenIndex >= 0 && m_activeTokenIndex < static_cast<int>(m_tokenToLine.size())) {
        m_activeLineIndex = m_tokenToLine[static_cast<size_t>(m_activeTokenIndex)];
    } else {
        m_activeLineIndex = 0;
    }

    if (!m_lines.empty()) {
        int clampedLine = std::clamp(m_activeLineIndex, 0, static_cast<int>(m_lines.size()) - 1);
        m_targetScrollBaselineY = m_lines[static_cast<size_t>(clampedLine)].baselineY;
        m_currentScrollBaselineY = m_targetScrollBaselineY;
        m_scrollVelocity = 0.0;
    } else {
        m_targetScrollBaselineY = 0.0;
        m_currentScrollBaselineY = 0.0;
        m_scrollVelocity = 0.0;
    }

    if (m_visible) {
        RenderLayered();
    }
}

void PresenterWindow::SetActiveToken(int tokenIndex, TrackingState trackingState, bool immediateJump) {
    bool changed = (m_activeTokenIndex != tokenIndex || m_trackingState != trackingState);
    m_activeTokenIndex = tokenIndex;
    m_trackingState = trackingState;

    if (m_activeTokenIndex >= 0 && m_activeTokenIndex < static_cast<int>(m_tokenToLine.size())) {
        int baseLine = m_tokenToLine[static_cast<size_t>(m_activeTokenIndex)];

        // During live speech (!immediateJump), once the speaker finishes the
        // last word of a line/bullet (m_activeTokenIndex == curLine.lastTokenIndex),
        // advance the reading baseline to the line of the next word (m_activeTokenIndex + 1)
        // so the next line or bullet glides onto the reading baseline immediately.
        if (!immediateJump && trackingState == TrackingState::Tracking &&
            baseLine >= 0 && baseLine < static_cast<int>(m_lines.size())) {
            const auto& curLine = m_lines[static_cast<size_t>(baseLine)];
            if (curLine.lastTokenIndex >= 0 &&
                m_activeTokenIndex >= curLine.lastTokenIndex &&
                (m_activeTokenIndex + 1) < static_cast<int>(m_tokenToLine.size())) {
                baseLine = m_tokenToLine[static_cast<size_t>(m_activeTokenIndex + 1)];
            }
        }
        m_activeLineIndex = baseLine;
    } else if (m_activeTokenIndex < 0) {
        m_activeLineIndex = 0;
    }

    if (!m_lines.empty()) {
        int clampedLine = std::clamp(m_activeLineIndex, 0, static_cast<int>(m_lines.size()) - 1);
        m_targetScrollBaselineY = m_lines[static_cast<size_t>(clampedLine)].baselineY;
        if (immediateJump) {
            m_currentScrollBaselineY = m_targetScrollBaselineY;
            m_scrollVelocity = 0.0;
        }
    }

    // Always redraw immediately when the active token changes so the word highlight
    // tracks the speaker's voice in real time across every word of the line.
    if (m_visible && (changed || immediateJump)) {
        RenderLayered();
    }
}

void PresenterWindow::SetPaused(bool paused) {
    if (m_paused != paused) {
        m_paused = paused;
        if (m_visible) {
            RenderLayered();
        }
    }
}

void PresenterWindow::SetAudioStatus(float liveLevel, bool deviceAvailable, const std::wstring& deviceLabel) {
    m_audioLevel = std::clamp(liveLevel, 0.0f, 1.0f);
    bool statusChanged = (m_audioAvailable != deviceAvailable || m_audioDeviceLabel != deviceLabel);
    m_audioAvailable = deviceAvailable;
    m_audioDeviceLabel = deviceLabel;
    if (statusChanged && m_visible) {
        RenderLayered();
    }
}

void PresenterWindow::SetRecognizerStatus(const std::wstring& statusText, bool isReady) {
    bool changed = (m_recognizerStatusText != statusText || m_recognizerReady != isReady);
    m_recognizerStatusText = statusText;
    m_recognizerReady = isReady;
    if (changed && m_visible) {
        RenderLayered();
    }
}

std::vector<std::wstring> PresenterWindow::GetLinePreviews() const {
    std::vector<std::wstring> previews;
    const std::wstring& fullText = m_model.Text();
    previews.reserve(m_lines.size());

    for (const auto& line : m_lines) {
        if (line.textStart < fullText.size()) {
            size_t len = std::min<size_t>(line.textLength, fullText.size() - line.textStart);
            std::wstring slice = fullText.substr(line.textStart, len);
            while (!slice.empty() && (slice.back() == L'\r' || slice.back() == L'\n' || slice.back() == L' ')) {
                slice.pop_back();
            }
            if (line.showBullet) {
                slice = (line.bulletDepth > 0 ? L"  \u25E6 " : L"\u2022 ") + slice;
            }
            previews.push_back(std::move(slice));
        } else {
            previews.emplace_back(L"");
        }
    }
    return previews;
}

int PresenterWindow::GetFirstTokenForLine(int lineIndex) const {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(m_lines.size())) {
        return -1;
    }
    return m_lines[static_cast<size_t>(lineIndex)].firstTokenIndex;
}

void PresenterWindow::SetAnchorForMonitor(const std::wstring& monitorId, PresenterMonitorAnchor anchor) {
    anchor.normX = std::clamp(anchor.normX, 0.05f, 0.95f);
    anchor.normY = std::clamp(anchor.normY, 0.02f, 0.95f);
    m_config.monitorAnchors[monitorId] = anchor;
    if (monitorId == m_currentMonitorId) {
        m_currentAnchor = anchor;
        if (m_visible) {
            UpdateWindowPositionFromAnchor();
            RenderLayered();
        }
    }
}

void PresenterWindow::OnDisplayChanged() {
    Monitor::Instance().Refresh();
    const MonitorInfo* mon = ResolveTargetMonitor();
    if (mon) {
        m_currentMonitorId = mon->GetId();
        auto it = m_config.monitorAnchors.find(m_currentMonitorId);
        if (it != m_config.monitorAnchors.end()) {
            m_currentAnchor = it->second;
        }
    }
    RecalculateWindowGeometry();
    RebuildTextLayout();
    if (m_visible) {
        UpdateWindowPositionFromAnchor();
        RenderLayered();
    }
}

void PresenterWindow::RecalculateWindowGeometry() {
    const MonitorInfo* mon = ResolveTargetMonitor();
    UINT dpi = (mon && mon->dpi > 0) ? mon->dpi : 96;
    m_dpiScale = static_cast<float>(dpi) / 96.0f;

    float scaledFontSize = static_cast<float>(m_config.fontSize) * m_dpiScale;
    m_nominalLineHeight = std::round(scaledFontSize * 1.38f);
    m_nominalBaseline = std::round(scaledFontSize * 1.05f);
    m_paddingX = std::round(26.0f * m_dpiScale);

    float toolbarStripHeight = std::round(36.0f * m_dpiScale);
    float padTop = std::round(12.0f * m_dpiScale);
    float padBottom = std::round(16.0f * m_dpiScale);

    int prevCount = std::clamp(m_config.previousLines, 0, 1);
    int nextCount = std::clamp(m_config.upcomingLines, 1, 2);
    int totalVisibleLines = prevCount + 1 + nextCount;

    m_contentTopY = toolbarStripHeight + padTop;
    m_readingBaselineLocalY = m_contentTopY + static_cast<float>(prevCount) * m_nominalLineHeight + m_nominalBaseline;
    m_contentBottomY = m_contentTopY + (static_cast<float>(totalVisibleLines) + 0.55f) * m_nominalLineHeight;

    int monWidth = mon ? (mon->bounds.right - mon->bounds.left) : GetSystemMetrics(SM_CXSCREEN);
    int desiredWidth = static_cast<int>(std::round(static_cast<float>(m_config.captionWidth) * m_dpiScale));
    m_windowWidth = std::clamp(desiredWidth, 320, std::max(360, monWidth - 20));
    m_windowHeight = static_cast<int>(std::ceil(m_contentBottomY + padBottom));

    // Layout toolbar buttons at top-right of the window
    int tbHeight = static_cast<int>(std::round(28.0f * m_dpiScale));
    int tbTop = static_cast<int>(std::round(6.0f * m_dpiScale));
    int gap = static_cast<int>(std::round(5.0f * m_dpiScale));
    int rightEdge = m_windowWidth - static_cast<int>(std::round(10.0f * m_dpiScale));

    int closeW = static_cast<int>(std::round(30.0f * m_dpiScale));
    int micW = static_cast<int>(std::round(96.0f * m_dpiScale));
    int pauseW = static_cast<int>(std::round(76.0f * m_dpiScale));
    int moveW = static_cast<int>(std::round(62.0f * m_dpiScale));
    int endW = static_cast<int>(std::round(48.0f * m_dpiScale));
    int downW = static_cast<int>(std::round(30.0f * m_dpiScale));
    int upW = static_cast<int>(std::round(30.0f * m_dpiScale));
    int topW = static_cast<int>(std::round(48.0f * m_dpiScale));

    m_btnCloseRect = { rightEdge - closeW, tbTop, rightEdge, tbTop + tbHeight };
    rightEdge = m_btnCloseRect.left - gap;

    m_btnMicRect = { rightEdge - micW, tbTop, rightEdge, tbTop + tbHeight };
    rightEdge = m_btnMicRect.left - gap;

    m_btnPauseRect = { rightEdge - pauseW, tbTop, rightEdge, tbTop + tbHeight };
    rightEdge = m_btnPauseRect.left - gap;

    m_btnMoveRect = { rightEdge - moveW, tbTop, rightEdge, tbTop + tbHeight };
    rightEdge = m_btnMoveRect.left - gap;

    m_btnEndRect = { rightEdge - endW, tbTop, rightEdge, tbTop + tbHeight };
    rightEdge = m_btnEndRect.left - gap;

    m_btnDownRect = { rightEdge - downW, tbTop, rightEdge, tbTop + tbHeight };
    rightEdge = m_btnDownRect.left - gap;

    m_btnUpRect = { rightEdge - upW, tbTop, rightEdge, tbTop + tbHeight };
    rightEdge = m_btnUpRect.left - gap;

    m_btnTopRect = { rightEdge - topW, tbTop, rightEdge, tbTop + tbHeight };

    m_toolbarLocalRect = {
        0,
        0,
        m_windowWidth,
        tbTop + tbHeight + gap
    };
}

void PresenterWindow::RebuildTextLayout() {
    m_lines.clear();
    m_tokenToLine.clear();
    m_blocks.clear();
    m_textLayout.Reset();
    m_textFormat.Reset();

    auto& renderer = D2DRenderer::Instance();
    if (!renderer.IsInitialized() && !renderer.Init()) {
        return;
    }

    IDWriteFactory* dwFactory = renderer.GetDWriteFactory();
    if (!dwFactory) {
        return;
    }

    float scaledFontSize = static_cast<float>(m_config.fontSize) * m_dpiScale;
    HRESULT hr = dwFactory->CreateTextFormat(
        m_config.fontFamily.c_str(),
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        scaledFontSize,
        L"en-US",
        m_textFormat.GetAddressOf()
    );

    if (FAILED(hr) || !m_textFormat) {
        return;
    }

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    m_textFormat->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, m_nominalLineHeight, m_nominalBaseline);

    const std::wstring& scriptText = m_model.Text();
    std::wstring fallbackStr = L"No script loaded. Paste or load a script from the Presenter menu.";
    const std::wstring& displayStr = scriptText.empty() ? fallbackStr : scriptText;

    float maxTextWidth = std::max(120.0f, static_cast<float>(m_windowWidth) - 2.0f * m_paddingX);

    auto isBulletChar = [](wchar_t c) -> bool {
        return c == L'-' || c == L'*' || c == L'+' ||
               c == L'\u2022' || c == L'\u2023' || c == L'\u25E6' ||
               c == L'\u25AA' || c == L'\u2013' || c == L'\u2014';
    };

    const uint32_t totalLen = static_cast<uint32_t>(displayStr.size());
    uint32_t pos = 0;
    bool pendingBlankLine = false;
    float curTop = 0.0f;

    while (pos <= totalLen) {
        uint32_t lineEnd = pos;
        while (lineEnd < totalLen &&
               displayStr[lineEnd] != L'\n' &&
               displayStr[lineEnd] != L'\u2028' &&
               displayStr[lineEnd] != L'\u2029') {
            lineEnd++;
        }

        bool isParaSeparatorChar = (lineEnd < totalLen && displayStr[lineEnd] == L'\u2029');
        uint32_t contentEnd = lineEnd;
        while (contentEnd > pos && (displayStr[contentEnd - 1] == L'\r' || displayStr[contentEnd - 1] == L' ' || displayStr[contentEnd - 1] == L'\t')) {
            contentEnd--;
        }

        uint32_t cur = pos;
        int leadingSpaces = 0;
        while (cur < contentEnd && (displayStr[cur] == L' ' || displayStr[cur] == L'\t' || displayStr[cur] == L'\u00A0')) {
            leadingSpaces += (displayStr[cur] == L'\t') ? 4 : 1;
            cur++;
        }

        if (cur >= contentEnd) {
            if (!m_blocks.empty()) {
                pendingBlankLine = true;
            }
            if (lineEnd >= totalLen) {
                break;
            }
            pos = lineEnd + 1;
            continue;
        }

        bool isBullet = false;
        int bulletDepth = 0;
        if (cur + 1 < contentEnd && isBulletChar(displayStr[cur]) &&
            (displayStr[cur + 1] == L' ' || displayStr[cur + 1] == L'\t' || displayStr[cur + 1] == L'\u00A0')) {
            uint32_t afterBullet = cur + 1;
            while (afterBullet < contentEnd &&
                   (displayStr[afterBullet] == L' ' || displayStr[afterBullet] == L'\t' || displayStr[afterBullet] == L'\u00A0')) {
                afterBullet++;
            }
            if (afterBullet < contentEnd) {
                isBullet = true;
                bulletDepth = (leadingSpaces >= 2) ? 1 : 0;
                cur = afterBullet;
            }
        } else if (displayStr[cur] == L'#') {
            uint32_t afterHashes = cur;
            while (afterHashes < contentEnd && displayStr[afterHashes] == L'#') {
                afterHashes++;
            }
            if (afterHashes < contentEnd && (displayStr[afterHashes] == L' ' || displayStr[afterHashes] == L'\t')) {
                while (afterHashes < contentEnd && (displayStr[afterHashes] == L' ' || displayStr[afterHashes] == L'\t')) {
                    afterHashes++;
                }
                if (afterHashes < contentEnd) {
                    cur = afterHashes;
                }
            }
        }

        LogicalScriptBlock blk;
        blk.textStart = cur;
        blk.textLength = contentEnd - cur;
        blk.isBullet = isBullet;
        blk.bulletDepth = bulletDepth;
        blk.indentX = isBullet ? std::round((bulletDepth > 0 ? 44.0f : 26.0f) * m_dpiScale) : 0.0f;

        if (!m_blocks.empty()) {
            const auto& prevBlk = m_blocks.back();
            blk.hasParagraphBreakBefore = pendingBlankLine || (prevBlk.isBullet != blk.isBullet);
            blk.hasLineBreakBefore = !blk.hasParagraphBreakBefore;

            if (blk.hasParagraphBreakBefore) {
                curTop += std::round(m_nominalLineHeight * 0.38f);
            } else {
                curTop += std::round(m_nominalLineHeight * 0.14f);
            }
        }

        pendingBlankLine = isParaSeparatorChar;
        blk.layoutTopY = curTop;

        float blockMaxWidth = std::max(100.0f, maxTextWidth - blk.indentX);
        hr = dwFactory->CreateTextLayout(
            displayStr.c_str() + blk.textStart,
            blk.textLength,
            m_textFormat.Get(),
            blockMaxWidth,
            100000.0f,
            blk.textLayout.GetAddressOf()
        );

        if (SUCCEEDED(hr) && blk.textLayout) {
            UINT32 lineCount = 0;
            blk.textLayout->GetLineMetrics(nullptr, 0, &lineCount);
            if (lineCount > 0) {
                std::vector<DWRITE_LINE_METRICS> dwLines(lineCount);
                blk.textLayout->GetLineMetrics(dwLines.data(), lineCount, &lineCount);

                int blkIdx = static_cast<int>(m_blocks.size());
                uint32_t relCharPos = 0;
                float localTop = 0.0f;

                for (UINT32 i = 0; i < lineCount; ++i) {
                    VisualScriptLine vl;
                    vl.lineIndex = static_cast<int>(m_lines.size());
                    vl.blockIndex = blkIdx;
                    vl.textStart = blk.textStart + relCharPos;
                    vl.textLength = dwLines[i].length;
                    vl.topY = blk.layoutTopY + localTop;
                    vl.height = dwLines[i].height;
                    vl.baselineY = vl.topY + dwLines[i].baseline;
                    vl.indentX = blk.indentX;
                    vl.showBullet = (blk.isBullet && i == 0);
                    vl.bulletDepth = blk.bulletDepth;
                    vl.hasParagraphGapBefore = (i == 0 && blk.hasParagraphBreakBefore);
                    vl.isHardLineEnd = (i + 1 == lineCount);
                    m_lines.push_back(vl);

                    relCharPos += dwLines[i].length;
                    localTop += dwLines[i].height;
                }

                curTop = blk.layoutTopY + localTop;
                m_blocks.push_back(std::move(blk));
            }
        }

        if (lineEnd >= totalLen) {
            break;
        }
        pos = lineEnd + 1;
    }

    if (m_lines.empty()) {
        return;
    }

    if (!m_blocks.empty()) {
        m_textLayout = m_blocks.front().textLayout;
    }

    // Map each ScriptToken to its wrapped VisualScriptLine
    const auto& tokens = m_model.Tokens();
    m_tokenToLine.assign(tokens.size(), 0);

    size_t lineCursor = 0;
    for (size_t t = 0; t < tokens.size(); ++t) {
        uint32_t tokStart = static_cast<uint32_t>(std::max(0, tokens[t].start));
        while (lineCursor + 1 < m_lines.size() &&
               tokStart >= (m_lines[lineCursor].textStart + m_lines[lineCursor].textLength)) {
            lineCursor++;
        }
        m_tokenToLine[t] = static_cast<int>(lineCursor);
        auto& lineObj = m_lines[lineCursor];
        if (lineObj.firstTokenIndex < 0) {
            lineObj.firstTokenIndex = static_cast<int>(t);
        }
        lineObj.lastTokenIndex = static_cast<int>(t);
    }
}

void PresenterWindow::UpdateWindowPositionFromAnchor() {
    if (!m_hwnd) return;

    const MonitorInfo* mon = ResolveTargetMonitor();
    RECT monBounds = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (mon) {
        monBounds = mon->bounds;
        m_currentMonitorId = mon->GetId();
        auto it = m_config.monitorAnchors.find(m_currentMonitorId);
        if (it != m_config.monitorAnchors.end()) {
            m_currentAnchor = it->second;
        }
    }

    int monW = std::max(1, static_cast<int>(monBounds.right - monBounds.left));
    int monH = std::max(1, static_cast<int>(monBounds.bottom - monBounds.top));

    int anchorScreenX = monBounds.left + static_cast<int>(std::round(m_currentAnchor.normX * static_cast<float>(monW)));
    int anchorScreenY = monBounds.top + static_cast<int>(std::round(m_currentAnchor.normY * static_cast<float>(monH)));

    int winX = anchorScreenX - m_windowWidth / 2;
    int winY = anchorScreenY - static_cast<int>(std::round(m_readingBaselineLocalY));

    // Keep window horizontally within monitor bounds while allowing reading baseline near top/bottom edges
    winX = std::clamp(winX, static_cast<int>(monBounds.left), std::max(static_cast<int>(monBounds.left), static_cast<int>(monBounds.right) - m_windowWidth));
    winY = std::clamp(winY, static_cast<int>(monBounds.top) - static_cast<int>(m_contentTopY),
                      static_cast<int>(monBounds.bottom) - static_cast<int>(m_nominalLineHeight));

    SetWindowPos(
        m_hwnd,
        HWND_TOPMOST,
        winX, winY,
        m_windowWidth, m_windowHeight,
        SWP_NOACTIVATE
    );
    FlipManager::Instance().RaiseMirrorsToTop();
}

void PresenterWindow::SetClickThrough(bool clickThrough) {
    if (!m_hwnd || m_clickThrough == clickThrough) {
        return;
    }
    LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (clickThrough) {
        exStyle |= WS_EX_TRANSPARENT;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);
    m_clickThrough = clickThrough;
}

ToolbarButton PresenterWindow::HitTestToolbar(POINT clientPt) const {
    if (PtInRect(&m_btnCloseRect, clientPt)) return ToolbarButton::Close;
    if (PtInRect(&m_btnMicRect, clientPt))   return ToolbarButton::Microphone;
    if (PtInRect(&m_btnPauseRect, clientPt)) return ToolbarButton::PauseResume;
    if (PtInRect(&m_btnMoveRect, clientPt))  return ToolbarButton::Move;
    if (PtInRect(&m_btnEndRect, clientPt))   return ToolbarButton::JumpEnd;
    if (PtInRect(&m_btnDownRect, clientPt))  return ToolbarButton::ScrollDown;
    if (PtInRect(&m_btnUpRect, clientPt))    return ToolbarButton::ScrollUp;
    if (PtInRect(&m_btnTopRect, clientPt))   return ToolbarButton::ResetTop;
    return ToolbarButton::None;
}

void PresenterWindow::StepLine(int deltaLines) {
    if (m_lines.empty() || deltaLines == 0) return;

    int nextLine = std::clamp(m_activeLineIndex + deltaLines, 0, static_cast<int>(m_lines.size()) - 1);
    if (nextLine <= 0 && deltaLines < 0) {
        if (m_onResetTop) {
            m_onResetTop();
        } else {
            SetActiveToken(-1, TrackingState::Idle, true);
        }
        return;
    }
    if (nextLine >= static_cast<int>(m_lines.size()) - 1 && deltaLines > 0) {
        if (m_onJumpEnd) {
            m_onJumpEnd();
            return;
        }
    }

    int tok = GetFirstTokenForLine(nextLine);
    // If target line is blank, search nearest line in scroll direction that has a token
    for (int step = 1; tok < 0 && step < static_cast<int>(m_lines.size()); ++step) {
        int probe = std::clamp(nextLine + (deltaLines > 0 ? step : -step), 0, static_cast<int>(m_lines.size()) - 1);
        tok = GetFirstTokenForLine(probe);
    }

    if (tok >= 0 && m_onTokenSeek) {
        m_onTokenSeek(tok);
    }
}

int PresenterWindow::HitTestLineIndex(POINT clientPt) const {
    if (m_lines.empty()) return -1;
    if (clientPt.y < static_cast<LONG>(m_contentTopY) || clientPt.y > static_cast<LONG>(m_contentBottomY)) {
        return -1;
    }

    float layoutOriginY = m_readingBaselineLocalY - static_cast<float>(m_currentScrollBaselineY);
    float layoutY = static_cast<float>(clientPt.y) - layoutOriginY;

    for (const auto& line : m_lines) {
        if (layoutY >= line.topY && layoutY <= (line.topY + line.height)) {
            return line.lineIndex;
        }
    }
    return -1;
}

void PresenterWindow::BeginAnchorDrag(POINT screenPt) {
    RECT rcWnd = {};
    if (m_hwnd && GetWindowRect(m_hwnd, &rcWnd)) {
        m_dragStartAnchorScreenX = rcWnd.left + m_windowWidth / 2;
        m_dragStartAnchorScreenY = rcWnd.top + static_cast<int>(std::round(m_readingBaselineLocalY));
    } else {
        const MonitorInfo* mon = Monitor::Instance().GetAtPoint(screenPt);
        if (!mon) mon = ResolveTargetMonitor();
        if (!mon) return;
        int monW = std::max(1, static_cast<int>(mon->bounds.right - mon->bounds.left));
        int monH = std::max(1, static_cast<int>(mon->bounds.bottom - mon->bounds.top));
        m_dragStartAnchorScreenX = mon->bounds.left + static_cast<int>(std::round(m_currentAnchor.normX * static_cast<float>(monW)));
        m_dragStartAnchorScreenY = mon->bounds.top + static_cast<int>(std::round(m_currentAnchor.normY * static_cast<float>(monH)));
    }

    m_dragStartScreenPt = screenPt;
    m_isDraggingAnchor = true;
    m_pendingBodyDrag = false;
    SetClickThrough(false);
    SetCapture(m_hwnd);
    RenderLayered();
}

void PresenterWindow::UpdateAnchorDrag(POINT screenPt) {
    if (!m_isDraggingAnchor) return;

    int newAnchorX = m_dragStartAnchorScreenX + (screenPt.x - m_dragStartScreenPt.x);
    int newAnchorY = m_dragStartAnchorScreenY + (screenPt.y - m_dragStartScreenPt.y);

    // Use the cursor position first so dragging onto another monitor immediately targets that screen
    const MonitorInfo* mon = Monitor::Instance().GetAtPoint(screenPt);
    if (!mon) {
        POINT anchorPt = { newAnchorX, newAnchorY };
        mon = Monitor::Instance().GetAtPoint(anchorPt);
    }
    if (!mon) mon = ResolveTargetMonitor();
    if (!mon) return;

    std::wstring newMonId = mon->GetId();
    bool monitorChanged = (newMonId != m_currentMonitorId);
    m_currentMonitorId = newMonId;
    m_config.targetMonitor = newMonId;

    if (monitorChanged) {
        UINT newDpi = (mon->dpi > 0) ? mon->dpi : 96;
        float newScale = static_cast<float>(newDpi) / 96.0f;
        if (std::fabs(newScale - m_dpiScale) > 0.01f) {
            RecalculateWindowGeometry();
            RebuildTextLayout();
        }
    }

    int monW = std::max(1, static_cast<int>(mon->bounds.right - mon->bounds.left));
    int monH = std::max(1, static_cast<int>(mon->bounds.bottom - mon->bounds.top));

    m_currentAnchor.normX = std::clamp(
        static_cast<float>(newAnchorX - mon->bounds.left) / static_cast<float>(monW),
        0.02f, 0.98f
    );
    m_currentAnchor.normY = std::clamp(
        static_cast<float>(newAnchorY - mon->bounds.top) / static_cast<float>(monH),
        0.02f, 0.98f
    );
    m_config.monitorAnchors[m_currentMonitorId] = m_currentAnchor;

    // Move window directly to the live drag coordinates so it crosses between monitors without edge clamping
    int winX = newAnchorX - m_windowWidth / 2;
    int winY = newAnchorY - static_cast<int>(std::round(m_readingBaselineLocalY));
    SetWindowPos(
        m_hwnd,
        HWND_TOPMOST,
        winX, winY,
        m_windowWidth, m_windowHeight,
        SWP_NOACTIVATE
    );
    FlipManager::Instance().RaiseMirrorsToTop();
    RenderLayered();
}

void PresenterWindow::EndAnchorDrag() {
    if (!m_isDraggingAnchor) return;

    m_isDraggingAnchor = false;
    m_pendingBodyDrag = false;
    ReleaseCapture();

    UpdateWindowPositionFromAnchor();

    if (m_onAnchorChanged && !m_currentMonitorId.empty()) {
        m_onAnchorChanged(m_currentMonitorId, m_currentAnchor);
    }

    RenderLayered();
}

void PresenterWindow::AdjustAnchorVerticalPixels(int deltaPixels) {
    const MonitorInfo* mon = ResolveTargetMonitor();
    if (!mon) return;

    int monH = std::max(1, static_cast<int>(mon->bounds.bottom - mon->bounds.top));
    m_currentAnchor.normY = std::clamp(
        m_currentAnchor.normY + static_cast<float>(deltaPixels) / static_cast<float>(monH),
        0.02f, 0.95f
    );
    m_config.monitorAnchors[m_currentMonitorId] = m_currentAnchor;

    UpdateWindowPositionFromAnchor();
    RenderLayered();

    if (m_onAnchorChanged && !m_currentMonitorId.empty()) {
        m_onAnchorChanged(m_currentMonitorId, m_currentAnchor);
    }
}

void PresenterWindow::OnInteractionTimer() {
    if (!m_visible || !m_hwnd) return;

    POINT pt = {};
    if (!GetCursorPos(&pt)) return;

    RECT rcWnd = {};
    if (!GetWindowRect(m_hwnd, &rcWnd)) return;

    bool insideWindow = PtInRect(&rcWnd, pt) != FALSE;
    bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    POINT clientPt = { pt.x - rcWnd.left, pt.y - rcWnd.top };
    bool insideToolbar = insideWindow && (PtInRect(&m_toolbarLocalRect, clientPt) != FALSE);
    ToolbarButton newHoveredBtn = insideToolbar ? HitTestToolbar(clientPt) : ToolbarButton::None;

    // Make window interactive when dragging or when the cursor is inside the presenter overlay
    // so the user can mouse-wheel scroll up/down, click Top/Up/Down/End/Move/Pause/Mic/Close,
    // click a script line to jump, or drag the presenter to any monitor.
    bool wantInteractive = m_isDraggingAnchor || m_pendingBodyDrag || insideWindow;
    SetClickThrough(!wantInteractive);

    bool needRedraw = false;
    if (m_mouseHovered != insideWindow) {
        m_mouseHovered = insideWindow;
        needRedraw = true;
    }
    if (m_altHeld != (insideWindow && altDown)) {
        m_altHeld = (insideWindow && altDown);
        needRedraw = true;
    }
    if (m_hoveredButton != newHoveredBtn) {
        m_hoveredButton = newHoveredBtn;
        needRedraw = true;
    }

    if (needRedraw) {
        RenderLayered();
    }
}

void PresenterWindow::OnFrameTimer() {
    if (!m_visible || !m_hwnd) return;

    ULONGLONG now = GetTickCount64();
    double dt = (m_lastFrameTick == 0) ? (1.0 / 60.0) : (static_cast<double>(now - m_lastFrameTick) / 1000.0);
    m_lastFrameTick = now;
    dt = std::clamp(dt, 0.001, 0.1);

    bool needsRender = false;

    if (std::fabs(m_targetScrollBaselineY - m_currentScrollBaselineY) > 0.25) {
        double effectiveSmooth = std::clamp(static_cast<double>(m_config.smoothTime) * 0.5, 0.06, 0.14);
        m_currentScrollBaselineY = SmoothDamp::Step(
            m_currentScrollBaselineY,
            m_targetScrollBaselineY,
            m_scrollVelocity,
            effectiveSmooth,
            dt
        );
        needsRender = true;
    } else if (std::fabs(m_targetScrollBaselineY - m_currentScrollBaselineY) > 0.001) {
        m_currentScrollBaselineY = m_targetScrollBaselineY;
        m_scrollVelocity = 0.0;
        needsRender = true;
    }

    if (m_mouseHovered && std::fabs(m_audioLevel - m_displayedAudioLevel) > 0.015f) {
        m_displayedAudioLevel = m_displayedAudioLevel * 0.6f + m_audioLevel * 0.4f;
        needsRender = true;
    }

    if (needsRender) {
        RenderLayered();
    }
}

void PresenterWindow::RenderLayered() {
    if (!m_hwnd || m_windowWidth <= 0 || m_windowHeight <= 0) {
        return;
    }

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_windowWidth;
    bmi.bmiHeader.biHeight = -m_windowHeight;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!hBitmap) {
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    HBITMAP hOldBmp = static_cast<HBITMAP>(SelectObject(hdcMem, hBitmap));
    memset(pvBits, 0, static_cast<size_t>(m_windowWidth) * static_cast<size_t>(m_windowHeight) * 4);

    auto& renderer = D2DRenderer::Instance();
    ID2D1Factory* d2dFactory = renderer.GetD2DFactory();
    IDWriteFactory* dwFactory = renderer.GetDWriteFactory();
    if (!d2dFactory || !dwFactory) {
        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    ComPtr<ID2D1DCRenderTarget> dcRT;
    if (FAILED(d2dFactory->CreateDCRenderTarget(&rtProps, dcRT.GetAddressOf()))) {
        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    RECT rcBind = { 0, 0, m_windowWidth, m_windowHeight };
    if (FAILED(dcRT->BindDC(hdcMem, &rcBind))) {
        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    dcRT->BeginDraw();
    dcRT->Clear(D2D1::ColorF(0, 0, 0, 0.0f));

    const float w = static_cast<float>(m_windowWidth);
    const float h = static_cast<float>(m_windowHeight);
    const float cornerRad = 12.0f * m_dpiScale;

    // 1. Optional translucent caption background
    float bgTop = m_contentTopY - 8.0f * m_dpiScale;
    D2D1_ROUNDED_RECT captionBgRect = D2D1::RoundedRect(
        D2D1::RectF(4.0f * m_dpiScale, bgTop, w - 4.0f * m_dpiScale, h - 4.0f * m_dpiScale),
        cornerRad, cornerRad
    );

    if (m_config.showBackground && m_config.bgOpacity > 0.01f) {
        ComPtr<ID2D1SolidColorBrush> bgBrush;
        dcRT->CreateSolidColorBrush(
            D2DRenderer::ColorFromRGB(m_config.bgColor, m_config.bgOpacity),
            bgBrush.GetAddressOf()
        );
        if (bgBrush) {
            dcRT->FillRoundedRectangle(captionBgRect, bgBrush.Get());
        }

        ComPtr<ID2D1SolidColorBrush> borderBrush;
        float borderAlpha = (m_isDraggingAnchor || m_altHeld) ? 0.65f : 0.22f;
        uint32_t borderRgb = (m_isDraggingAnchor || m_altHeld) ? 0x4DA3FF : 0x506078;
        dcRT->CreateSolidColorBrush(
            D2DRenderer::ColorFromRGB(borderRgb, borderAlpha),
            borderBrush.GetAddressOf()
        );
        if (borderBrush) {
            dcRT->DrawRoundedRectangle(captionBgRect, borderBrush.Get(), 1.5f * m_dpiScale);
        }
    }

    // 2. Fixed reading line anchor ticks & active line highlight band
    float readingLineTop = m_readingBaselineLocalY - m_nominalBaseline;
    float readingLineBottom = readingLineTop + m_nominalLineHeight;

    {
        ComPtr<ID2D1SolidColorBrush> bandBrush;
        dcRT->CreateSolidColorBrush(D2D1::ColorF(0.28f, 0.62f, 1.0f, 0.11f), bandBrush.GetAddressOf());
        if (bandBrush) {
            D2D1_ROUNDED_RECT bandRect = D2D1::RoundedRect(
                D2D1::RectF(10.0f * m_dpiScale, readingLineTop, w - 10.0f * m_dpiScale, readingLineBottom),
                6.0f * m_dpiScale, 6.0f * m_dpiScale
            );
            dcRT->FillRoundedRectangle(bandRect, bandBrush.Get());
        }

        // Left & right reading-line anchor marker bars
        ComPtr<ID2D1SolidColorBrush> tickBrush;
        uint32_t tickColor = m_paused ? 0xF5A623 : (m_recognizerReady && m_audioAvailable ? 0x38E58B : 0x4DA3FF);
        dcRT->CreateSolidColorBrush(D2DRenderer::ColorFromRGB(tickColor, 0.90f), tickBrush.GetAddressOf());
        if (tickBrush) {
            float barW = 4.0f * m_dpiScale;
            dcRT->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(8.0f * m_dpiScale, readingLineTop + 4.0f * m_dpiScale,
                                              8.0f * m_dpiScale + barW, readingLineBottom - 4.0f * m_dpiScale),
                                  barW * 0.5f, barW * 0.5f),
                tickBrush.Get()
            );
        }
    }

    // 3. While dragging (or holding Alt), draw a subtle horizontal reading-baseline guide across the caption
    if (m_isDraggingAnchor || m_altHeld) {
        ComPtr<ID2D1SolidColorBrush> guideBrush;
        dcRT->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.72f, 1.0f, 0.80f), guideBrush.GetAddressOf());
        if (guideBrush) {
            dcRT->DrawLine(
                D2D1::Point2F(8.0f * m_dpiScale, m_readingBaselineLocalY),
                D2D1::Point2F(w - 8.0f * m_dpiScale, m_readingBaselineLocalY),
                guideBrush.Get(),
                1.5f * m_dpiScale
            );
        }
    }

    // 4. Render script blocks, bullets, and paragraph breaks clipped to the caption content region
    if (!m_blocks.empty() && !m_lines.empty()) {
        D2D1_RECT_F clipRect = D2D1::RectF(
            m_paddingX - 6.0f * m_dpiScale,
            m_contentTopY,
            w - m_paddingX + 6.0f * m_dpiScale,
            m_contentBottomY
        );
        dcRT->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        ComPtr<ID2D1SolidColorBrush> activeTextBrush;
        ComPtr<ID2D1SolidColorBrush> dimmedTextBrush;
        ComPtr<ID2D1SolidColorBrush> spokenWordBrush;
        ComPtr<ID2D1SolidColorBrush> activeWordBrush;
        ComPtr<ID2D1SolidColorBrush> dividerBrush;

        dcRT->CreateSolidColorBrush(
            D2DRenderer::ColorFromRGB(m_config.textColor, m_config.textOpacity),
            activeTextBrush.GetAddressOf()
        );
        dcRT->CreateSolidColorBrush(
            D2DRenderer::ColorFromRGB(m_config.textColor, m_config.textOpacity * 0.38f),
            dimmedTextBrush.GetAddressOf()
        );
        dcRT->CreateSolidColorBrush(
            D2D1::ColorF(0.62f, 0.84f, 0.98f, m_config.textOpacity * 0.62f),
            spokenWordBrush.GetAddressOf()
        );
        dcRT->CreateSolidColorBrush(
            D2D1::ColorF(0.30f, 0.92f, 1.0f, m_config.textOpacity),
            activeWordBrush.GetAddressOf()
        );
        dcRT->CreateSolidColorBrush(
            D2D1::ColorF(0.55f, 0.72f, 0.95f, m_config.textOpacity * 0.22f),
            dividerBrush.GetAddressOf()
        );

        // Reset all logical blocks to dimmed text brush
        if (dimmedTextBrush) {
            for (auto& blk : m_blocks) {
                if (blk.textLayout && blk.textLength > 0) {
                    DWRITE_TEXT_RANGE allRange = { 0, blk.textLength };
                    blk.textLayout->SetDrawingEffect(dimmedTextBrush.Get(), allRange);
                }
            }
        }

        // Style the active line and its words inside its logical block
        const auto& tokens = m_model.Tokens();
        if (m_activeLineIndex >= 0 && m_activeLineIndex < static_cast<int>(m_lines.size()) && activeTextBrush) {
            const auto& activeLine = m_lines[static_cast<size_t>(m_activeLineIndex)];
            if (activeLine.blockIndex >= 0 && activeLine.blockIndex < static_cast<int>(m_blocks.size())) {
                auto& activeBlk = m_blocks[static_cast<size_t>(activeLine.blockIndex)];
                if (activeBlk.textLayout && activeLine.textLength > 0 && activeLine.textStart >= activeBlk.textStart) {
                    uint32_t lineRelStart = activeLine.textStart - activeBlk.textStart;
                    DWRITE_TEXT_RANGE lineRange = { lineRelStart, activeLine.textLength };
                    activeBlk.textLayout->SetDrawingEffect(activeTextBrush.Get(), lineRange);

                    // Determine which token on activeLine is the next word to read vs already spoken
                    int highlightTokIdx = -1;
                    if (m_activeTokenIndex < 0) {
                        highlightTokIdx = activeLine.firstTokenIndex;
                    } else if (m_activeTokenIndex < activeLine.firstTokenIndex) {
                        // Line just advanced as previous line finished: highlight first word of this new line
                        highlightTokIdx = activeLine.firstTokenIndex;
                    } else if (m_activeTokenIndex >= activeLine.firstTokenIndex &&
                               m_activeTokenIndex <= activeLine.lastTokenIndex) {
                        // Style words up through m_activeTokenIndex (or before highlightTokIdx) as spoken
                        int nextTok = (m_activeTokenIndex + 1 <= activeLine.lastTokenIndex)
                            ? (m_activeTokenIndex + 1)
                            : m_activeTokenIndex;
                        highlightTokIdx = nextTok;

                        if (spokenWordBrush && highlightTokIdx > activeLine.firstTokenIndex &&
                            highlightTokIdx < static_cast<int>(tokens.size())) {
                            uint32_t nextTokStart = static_cast<uint32_t>(std::max(0, tokens[static_cast<size_t>(highlightTokIdx)].start));
                            if (nextTokStart > activeLine.textStart) {
                                uint32_t spokenLen = std::min(nextTokStart - activeLine.textStart, activeLine.textLength);
                                if (spokenLen > 0) {
                                    DWRITE_TEXT_RANGE spokenRange = { lineRelStart, spokenLen };
                                    activeBlk.textLayout->SetDrawingEffect(spokenWordBrush.Get(), spokenRange);
                                }
                            }
                        }
                    }

                    // Highlight the active/next word in bright cyan
                    if (activeWordBrush && highlightTokIdx >= 0 && highlightTokIdx < static_cast<int>(tokens.size())) {
                        const auto& tok = tokens[static_cast<size_t>(highlightTokIdx)];
                        uint32_t tokStart = static_cast<uint32_t>(std::max(0, tok.start));
                        if (tok.length > 0 && tokStart >= activeBlk.textStart &&
                            tokStart < (activeBlk.textStart + activeBlk.textLength)) {
                            DWRITE_TEXT_RANGE wordRange = {
                                tokStart - activeBlk.textStart,
                                static_cast<UINT32>(tok.length)
                            };
                            activeBlk.textLayout->SetDrawingEffect(activeWordBrush.Get(), wordRange);
                        }
                    }
                }
            }
        }

        float layoutOriginY = m_readingBaselineLocalY - static_cast<float>(m_currentScrollBaselineY);

        // Draw paragraph divider rules and bullet markers for visible lines
        for (const auto& line : m_lines) {
            float lineScreenTop = layoutOriginY + line.topY;
            float lineScreenBottom = lineScreenTop + line.height;
            if (lineScreenBottom < m_contentTopY - m_nominalLineHeight ||
                lineScreenTop > m_contentBottomY + m_nominalLineHeight) {
                continue;
            }

            if (line.hasParagraphGapBefore && dividerBrush) {
                float ruleY = std::round(lineScreenTop - m_nominalLineHeight * 0.19f);
                dcRT->DrawLine(
                    D2D1::Point2F(m_paddingX + line.indentX, ruleY),
                    D2D1::Point2F(m_paddingX + line.indentX + 110.0f * m_dpiScale, ruleY),
                    dividerBrush.Get(),
                    1.2f * m_dpiScale
                );
            }

            if (line.showBullet) {
                float bulletCenterY = layoutOriginY + line.baselineY - m_nominalBaseline * 0.35f;
                float bulletCenterX = m_paddingX + line.indentX - 13.0f * m_dpiScale;
                float bulletRadius = std::max(3.2f * m_dpiScale, m_nominalLineHeight * 0.095f);
                D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(bulletCenterX, bulletCenterY), bulletRadius, bulletRadius);

                ID2D1Brush* bulletBrush = (line.lineIndex == m_activeLineIndex && activeWordBrush)
                    ? static_cast<ID2D1Brush*>(activeWordBrush.Get())
                    : static_cast<ID2D1Brush*>(dimmedTextBrush.Get());

                if (bulletBrush) {
                    if (line.bulletDepth == 0) {
                        dcRT->FillEllipse(ellipse, bulletBrush);
                    } else {
                        dcRT->DrawEllipse(ellipse, bulletBrush, 1.6f * m_dpiScale);
                    }
                }
            }
        }

        // Draw each visible logical block's text layout with its hanging indent
        for (const auto& blk : m_blocks) {
            if (!blk.textLayout) continue;
            float blkScreenTop = layoutOriginY + blk.layoutTopY;
            if (blkScreenTop > m_contentBottomY + m_nominalLineHeight) {
                break;
            }
            dcRT->DrawTextLayout(
                D2D1::Point2F(m_paddingX + blk.indentX, blkScreenTop),
                blk.textLayout.Get(),
                dimmedTextBrush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_NONE
            );
        }

        dcRT->PopAxisAlignedClip();
    }

    // 5. Render Hover Toolbar (or status banner if model/mic is missing) when hovered or dragging
    bool showDiagnosticBanner = (!m_recognizerReady || !m_audioAvailable);
    if (m_mouseHovered || m_isDraggingAnchor || m_altHeld || showDiagnosticBanner) {
        ComPtr<IDWriteTextFormat> uiFont;
        dwFactory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            11.5f * m_dpiScale,
            L"en-US",
            uiFont.GetAddressOf()
        );
        if (uiFont) {
            uiFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            uiFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        // Diagnostic pill on top-left when model or mic needs attention (or when hovered)
        if ((showDiagnosticBanner || m_mouseHovered) && uiFont) {
            std::wstring statusLabel;
            uint32_t pillColor = 0x1A2436;
            if (!m_audioAvailable) {
                statusLabel = L"Mic Disconnected";
                pillColor = 0x6B1D1D;
            } else if (!m_recognizerReady) {
                statusLabel = m_recognizerStatusText.empty() ? L"Vosk Model Not Loaded" : m_recognizerStatusText;
                pillColor = 0x5C3D10;
            } else if (m_paused) {
                statusLabel = L"Paused — Alt+Drag to move";
            } else {
                statusLabel = L"Listening (" + m_audioDeviceLabel + L")";
            }

            if (statusLabel.size() > 48) {
                statusLabel = statusLabel.substr(0, 45) + L"...";
            }

            float pillRight = std::max(80.0f * m_dpiScale, static_cast<float>(m_btnTopRect.left) - 8.0f * m_dpiScale);
            if (pillRight > 90.0f * m_dpiScale) {
                D2D1_ROUNDED_RECT statusRect = D2D1::RoundedRect(
                    D2D1::RectF(10.0f * m_dpiScale, 6.0f * m_dpiScale,
                                pillRight,
                                34.0f * m_dpiScale),
                    6.0f * m_dpiScale, 6.0f * m_dpiScale
                );
                ComPtr<ID2D1SolidColorBrush> pillBg;
                ComPtr<ID2D1SolidColorBrush> pillText;
                dcRT->CreateSolidColorBrush(D2DRenderer::ColorFromRGB(pillColor, 0.88f), pillBg.GetAddressOf());
                dcRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.95f, 1.0f, 0.95f), pillText.GetAddressOf());
                if (pillBg && pillText) {
                    dcRT->FillRoundedRectangle(statusRect, pillBg.Get());
                    dcRT->DrawText(
                        statusLabel.c_str(),
                        static_cast<UINT32>(statusLabel.size()),
                        uiFont.Get(),
                        statusRect.rect,
                        pillText.Get()
                    );
                }
            }
        }

        // Hover toolbar controls: Top, Up, Down, End, Move, Pause/Resume, Microphone, Close
        if ((m_mouseHovered || m_isDraggingAnchor || m_altHeld) && uiFont) {
            auto drawButton = [&](const RECT& rc, ToolbarButton btnId, const wchar_t* label, uint32_t baseColor) {
                bool hovered = (m_hoveredButton == btnId);
                float alpha = hovered ? 0.96f : 0.85f;
                uint32_t rgb = hovered ? 0x2E476B : baseColor;
                if (btnId == ToolbarButton::Close && hovered) {
                    rgb = 0xB82E2E;
                }

                D2D1_ROUNDED_RECT bRect = D2D1::RoundedRect(
                    D2D1::RectF(static_cast<float>(rc.left), static_cast<float>(rc.top),
                                static_cast<float>(rc.right), static_cast<float>(rc.bottom)),
                    6.0f * m_dpiScale, 6.0f * m_dpiScale
                );

                ComPtr<ID2D1SolidColorBrush> bBrush;
                ComPtr<ID2D1SolidColorBrush> bBorder;
                ComPtr<ID2D1SolidColorBrush> tBrush;
                dcRT->CreateSolidColorBrush(D2DRenderer::ColorFromRGB(rgb, alpha), bBrush.GetAddressOf());
                dcRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, hovered ? 0.45f : 0.20f), bBorder.GetAddressOf());
                dcRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), tBrush.GetAddressOf());

                if (bBrush) dcRT->FillRoundedRectangle(bRect, bBrush.Get());
                if (bBorder) dcRT->DrawRoundedRectangle(bRect, bBorder.Get(), 1.0f * m_dpiScale);

                if (btnId == ToolbarButton::Microphone) {
                    // Draw live audio level meter bar at the bottom of the Microphone button
                    float meterW = static_cast<float>(rc.right - rc.left) - 12.0f * m_dpiScale;
                    float fillW = meterW * std::clamp(m_displayedAudioLevel * 2.5f, 0.0f, 1.0f);
                    D2D1_RECT_F meterBg = D2D1::RectF(
                        static_cast<float>(rc.left) + 6.0f * m_dpiScale,
                        static_cast<float>(rc.bottom) - 5.0f * m_dpiScale,
                        static_cast<float>(rc.right) - 6.0f * m_dpiScale,
                        static_cast<float>(rc.bottom) - 2.5f * m_dpiScale
                    );
                    D2D1_RECT_F meterFg = meterBg;
                    meterFg.right = meterFg.left + fillW;

                    ComPtr<ID2D1SolidColorBrush> mBgBrush;
                    ComPtr<ID2D1SolidColorBrush> mFgBrush;
                    dcRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f), mBgBrush.GetAddressOf());
                    uint32_t mCol = m_audioAvailable ? 0x38E58B : 0xFF4D4D;
                    dcRT->CreateSolidColorBrush(D2DRenderer::ColorFromRGB(mCol, 0.95f), mFgBrush.GetAddressOf());
                    if (mBgBrush) dcRT->FillRectangle(meterBg, mBgBrush.Get());
                    if (mFgBrush && fillW > 0.5f) dcRT->FillRectangle(meterFg, mFgBrush.Get());
                }

                if (tBrush) {
                    dcRT->DrawText(
                        label,
                        static_cast<UINT32>(wcslen(label)),
                        uiFont.Get(),
                        bRect.rect,
                        tBrush.Get()
                    );
                }
            };

            drawButton(m_btnTopRect, ToolbarButton::ResetTop, L"|<< Top", 0x1B2638);
            drawButton(m_btnUpRect, ToolbarButton::ScrollUp, L"\u25B2", 0x1B2638);
            drawButton(m_btnDownRect, ToolbarButton::ScrollDown, L"\u25BC", 0x1B2638);
            drawButton(m_btnEndRect, ToolbarButton::JumpEnd, L"End >>|", 0x1B2638);
            drawButton(m_btnMoveRect, ToolbarButton::Move, L":: Move", 0x1B2638);
            drawButton(m_btnPauseRect, ToolbarButton::PauseResume,
                       m_paused ? L"|> Resume" : L"|| Pause",
                       m_paused ? 0x6E4715 : 0x1B2638);
            drawButton(m_btnMicRect, ToolbarButton::Microphone,
                       m_audioAvailable ? L"Mic v" : L"Mic [!] v",
                       m_audioAvailable ? 0x1B2638 : 0x6E1F1F);
            drawButton(m_btnCloseRect, ToolbarButton::Close, L"X", 0x2B2028);
        }
    }

    dcRT->EndDraw();

    RECT rcWindow = {};
    GetWindowRect(m_hwnd, &rcWindow);
    POINT ptSrc = { 0, 0 };
    POINT ptDst = { rcWindow.left, rcWindow.top };
    SIZE sizeWnd = { m_windowWidth, m_windowHeight };

    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

LRESULT CALLBACK PresenterWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PresenterWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<PresenterWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<PresenterWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PresenterWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            // Never steal keyboard focus from the user's active application
            return MA_NOACTIVATE;

        case WM_NCHITTEST: {
            return HTCLIENT;
        }

        case WM_SETCURSOR: {
            if (m_isDraggingAnchor || (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 || m_hoveredButton == ToolbarButton::Move) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                return TRUE;
            }
            if (m_hoveredButton != ToolbarButton::None) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }

        case WM_LBUTTONDOWN: {
            POINT clientPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            POINT screenPt = clientPt;
            ClientToScreen(hwnd, &screenPt);

            ToolbarButton btn = HitTestToolbar(clientPt);
            if (btn == ToolbarButton::ResetTop) {
                if (m_onResetTop) m_onResetTop();
                return 0;
            }
            if (btn == ToolbarButton::ScrollUp) {
                StepLine(-1);
                return 0;
            }
            if (btn == ToolbarButton::ScrollDown) {
                StepLine(1);
                return 0;
            }
            if (btn == ToolbarButton::JumpEnd) {
                if (m_onJumpEnd) m_onJumpEnd();
                return 0;
            }
            if (btn == ToolbarButton::Move) {
                BeginAnchorDrag(screenPt);
                return 0;
            }
            if (btn == ToolbarButton::PauseResume) {
                if (m_onPauseResume) m_onPauseResume();
                return 0;
            }
            if (btn == ToolbarButton::Microphone) {
                if (m_onMicrophoneMenu) {
                    POINT menuPt = { m_btnMicRect.left, m_btnMicRect.bottom };
                    ClientToScreen(hwnd, &menuPt);
                    m_onMicrophoneMenu(menuPt);
                }
                return 0;
            }
            if (btn == ToolbarButton::Close) {
                if (m_onClose) m_onClose();
                return 0;
            }

            // Alt + left drag OR dragging the top bar starts anchor drag immediately
            if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0 || PtInRect(&m_toolbarLocalRect, clientPt)) {
                BeginAnchorDrag(screenPt);
                return 0;
            }

            // Left-mousedown on the caption body: dragging moves the presenter across screens;
            // releasing without dragging jumps to the clicked line.
            m_pendingBodyDrag = true;
            m_dragStartScreenPt = screenPt;
            m_downClientPt = clientPt;
            SetCapture(hwnd);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (m_pendingBodyDrag && !m_isDraggingAnchor) {
                POINT screenPt = {};
                if (GetCursorPos(&screenPt)) {
                    if (std::abs(screenPt.x - m_dragStartScreenPt.x) >= 5 ||
                        std::abs(screenPt.y - m_dragStartScreenPt.y) >= 5) {
                        BeginAnchorDrag(m_dragStartScreenPt);
                        UpdateAnchorDrag(screenPt);
                    }
                }
                return 0;
            }
            if (m_isDraggingAnchor) {
                POINT screenPt = {};
                if (GetCursorPos(&screenPt)) {
                    UpdateAnchorDrag(screenPt);
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (m_pendingBodyDrag && !m_isDraggingAnchor) {
                m_pendingBodyDrag = false;
                ReleaseCapture();
                int clickedLine = HitTestLineIndex(m_downClientPt);
                if (clickedLine >= 0) {
                    if (clickedLine == 0 && m_onResetTop) {
                        m_onResetTop();
                    } else {
                        int tok = GetFirstTokenForLine(clickedLine);
                        if (tok >= 0 && m_onTokenSeek) {
                            m_onTokenSeek(tok);
                        }
                    }
                }
                return 0;
            }
            if (m_isDraggingAnchor) {
                EndAnchorDrag();
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int steps = delta / WHEEL_DELTA;
            if (steps == 0) {
                steps = (delta > 0) ? 1 : -1;
            }

            if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) {
                // Alt + mouse wheel: fine vertical positioning of the reading anchor
                AdjustAnchorVerticalPixels(-steps * 4);
            } else {
                // Mouse wheel anywhere on the overlay: scroll script up/down
                StepLine(-steps);
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == TIMER_PRESENTER_FRAME) {
                OnFrameTimer();
            } else if (wParam == TIMER_PRESENTER_INTERACTION) {
                OnInteractionTimer();
            }
            return 0;
        }

        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace VirtualOverlay

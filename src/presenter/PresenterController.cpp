#include "PresenterController.h"
#include "../utils/Logger.h"
#include "../utils/Monitor.h"
#include "../display/FlipManager.h"

#include <commdlg.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace VirtualOverlay {

// Popup menu command IDs for Presenter hover toolbar Microphone menu
constexpr UINT IDM_PRES_MIC_BASE       = 3000;
constexpr UINT IDM_PRES_MIC_MAX        = 64;
constexpr UINT IDM_PRES_CH_BASE        = 3100;
constexpr UINT IDM_PRES_CH_MAX         = 16;
constexpr UINT IDM_PRES_PASTE_SCRIPT   = 3200;
constexpr UINT IDM_PRES_LOAD_FILE      = 3201;
constexpr UINT IDM_PRES_RESET_SCRIPT   = 3202;
constexpr UINT IDM_PRES_OPEN_SETTINGS  = 3203;
constexpr UINT IDM_PRES_LINE_BASE      = 3300;
constexpr UINT IDM_PRES_LINE_MAX       = 200;

PresenterController& PresenterController::Instance() {
    static PresenterController instance;
    return instance;
}

PresenterController::PresenterController() = default;

PresenterController::~PresenterController() {
    Shutdown();
}

bool PresenterController::Init(HINSTANCE hInstance, HWND hMainWnd) {
    if (m_initialized) {
        return true;
    }

    m_hInstance = hInstance;
    m_hMainWnd = hMainWnd;

    AudioInputManager::Instance().Init();

    if (!m_window.Init(hInstance)) {
        LOG_ERROR("PresenterController: Failed to initialize PresenterWindow");
        return false;
    }

    // Wire PresenterWindow callbacks
    m_window.SetResetTopCallback([this]() {
        ResetToStart();
    });

    m_window.SetJumpEndCallback([this]() {
        JumpToEnd();
    });

    m_window.SetPauseResumeCallback([this]() {
        TogglePause();
    });

    m_window.SetMicrophoneMenuCallback([this](POINT screenPt) {
        ShowMicrophonePopupMenu(screenPt);
    });

    m_window.SetCloseCallback([this]() {
        Close();
    });

    m_window.SetTokenSeekCallback([this](int tokenIndex) {
        SeekToToken(tokenIndex);
    });

    m_window.SetAnchorChangedCallback([this](const std::wstring& monitorId, PresenterMonitorAnchor anchor) {
        auto& cfg = Config::Instance().GetMutable().presenter;
        cfg.monitorAnchors[monitorId] = anchor;
        cfg.targetMonitor = monitorId;
        PersistConfig();
    });

    // Wire AudioInputManager callbacks
    AudioInputManager::Instance().SetAudioDataCallback([this](const int16_t* samples, size_t count) {
        if (!m_paused && m_window.IsVisible()) {
            m_recognizer.FeedAudio(samples, count);
        }
        if (m_hMainWnd && m_window.IsVisible()) {
            static std::atomic<ULONGLONG> s_lastLevelTick{ 0 };
            ULONGLONG now = GetTickCount64();
            ULONGLONG prev = s_lastLevelTick.load();
            if (now - prev >= 50) {
                s_lastLevelTick.store(now);
                PostMessageW(m_hMainWnd, WM_APP_PRESENTER_AUDIO_LVL, 0, 0);
            }
        }
    });

    AudioInputManager::Instance().SetStateChangeCallback([this](AudioCaptureState, const std::wstring&) {
        if (m_hMainWnd) {
            PostMessageW(m_hMainWnd, WM_APP_PRESENTER_STATUS, 0, 0);
        }
    });

    // Wire OfflineSpeechRecognizer callbacks
    // Partials are cumulative: replace any unconsumed partial so the UI only processes the newest one.
    m_recognizer.SetHypothesisCallback([this](const std::string& hypothesisUtf8, bool isFinal) {
        bool shouldPost = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            shouldPost = m_pendingHypotheses.empty();
            if (!isFinal && !m_pendingHypotheses.empty() && !m_pendingHypotheses.back().second) {
                m_pendingHypotheses.back().first = hypothesisUtf8;
            } else {
                m_pendingHypotheses.emplace_back(hypothesisUtf8, isFinal);
            }
        }
        if (m_hMainWnd && shouldPost) {
            PostMessageW(m_hMainWnd, WM_APP_PRESENTER_HYPOTHESIS, 0, 0);
        }
    });

    m_recognizer.SetStatusCallback([this](RecognizerStatus, const std::wstring&) {
        if (m_hMainWnd) {
            PostMessageW(m_hMainWnd, WM_APP_PRESENTER_STATUS, 0, 0);
        }
    });

    // Load initial script & config
    const auto& presCfg = Config::Instance().Get().presenter;
    m_recognizer.SetConfiguredModelPath(presCfg.voskModelPath);
    AudioInputManager::Instance().SetSelectedDevice(
        presCfg.audioDeviceId, presCfg.audioDeviceName, presCfg.audioChannel);

    m_window.ApplyConfig(presCfg);

    ScriptModel initialModel = ScriptModel::Build(presCfg.scriptText, true);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_alignment.SetModel(initialModel);
        if (presCfg.savedTokenIndex >= 0 && presCfg.savedTokenIndex < initialModel.TokenCount()) {
            m_alignment.SeekToToken(presCfg.savedTokenIndex);
        }
    }
    m_window.SetScriptModel(initialModel, GetCurrentTokenIndex());

    m_initialized = true;

    if (presCfg.enabled) {
        Open();
    }

    LOG_INFO("PresenterController initialized");
    return true;
}

void PresenterController::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Stop capture and recognizer without clearing the user's enabled preference on app exit
    AudioInputManager::Instance().StopCapture();
    m_recognizer.Stop();
    m_window.Shutdown();
    AudioInputManager::Instance().Shutdown();

    m_initialized = false;
    LOG_INFO("PresenterController shutdown");
}

void PresenterController::Open() {
    if (!m_initialized) {
        return;
    }

    auto& presCfg = Config::Instance().GetMutable().presenter;
    presCfg.enabled = true;
    PersistConfig();

    m_window.ApplyConfig(presCfg);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_window.SetScriptModel(m_alignment.Model(), m_alignment.CurrentTokenIndex());
    }
    m_window.SetPaused(m_paused);
    m_window.Show();
    FlipManager::Instance().RaiseMirrorsToTop();

    // Start offline Vosk recognizer with current script vocabulary
    std::vector<std::string> vocab;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        vocab = m_alignment.Model().DistinctVocabulary();
    }
    m_recognizer.SetConfiguredModelPath(presCfg.voskModelPath);
    m_recognizer.Start(vocab);

    // Start WASAPI capture on selected audio device
    AudioInputManager::Instance().SetSelectedDevice(
        presCfg.audioDeviceId, presCfg.audioDeviceName, presCfg.audioChannel);
    AudioInputManager::Instance().StartCapture();

    SyncStatusToWindow();
    LOG_INFO("Presenter opened");
}

void PresenterController::Close() {
    if (!m_initialized) {
        return;
    }

    // Stop audio capture and release Vosk recognizer/model resources immediately
    AudioInputManager::Instance().StopCapture();
    m_recognizer.Stop();
    m_window.Hide();

    // Preserve current script and reading position in config
    auto& presCfg = Config::Instance().GetMutable().presenter;
    presCfg.enabled = false;
    presCfg.savedTokenIndex = GetCurrentTokenIndex();
    PersistConfig();

    LOG_INFO("Presenter closed (script and reading position preserved at token %d)", presCfg.savedTokenIndex);
}

void PresenterController::Toggle() {
    if (IsOpen()) {
        Close();
    } else {
        Open();
    }
}

void PresenterController::SetPaused(bool paused) {
    m_paused = paused;
    m_window.SetPaused(paused);
    if (!paused) {
        m_recognizer.ResetUtterance();
    }
    SyncStatusToWindow();
}

void PresenterController::TogglePause() {
    SetPaused(!m_paused);
}

void PresenterController::SetScriptText(const std::wstring& scriptText, bool resetPosition) {
    ScriptModel newModel = ScriptModel::Build(scriptText, true);
    std::vector<std::string> vocab = newModel.DistinctVocabulary();

    int tokIdx = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        int prevTok = m_alignment.CurrentTokenIndex();
        m_alignment.SetModel(newModel);
        if (!resetPosition && prevTok >= 0 && prevTok < newModel.TokenCount()) {
            m_alignment.SeekToToken(prevTok);
        }
        tokIdx = m_alignment.CurrentTokenIndex();
    }

    auto& presCfg = Config::Instance().GetMutable().presenter;
    presCfg.scriptText = scriptText;
    presCfg.savedTokenIndex = tokIdx;
    PersistConfig();

    m_window.SetScriptModel(newModel, tokIdx);
    if (IsOpen()) {
        m_recognizer.UpdateVocabulary(vocab);
    }
}

bool PresenterController::LoadScriptFromFile(const std::wstring& filePath) {
    if (filePath.empty()) return false;

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        LOG_WARN("PresenterController: Failed to open script file '%ws'", filePath.c_str());
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string rawBytes = ss.str();

    // Strip UTF-8 BOM if present
    if (rawBytes.size() >= 3 &&
        static_cast<unsigned char>(rawBytes[0]) == 0xEF &&
        static_cast<unsigned char>(rawBytes[1]) == 0xBB &&
        static_cast<unsigned char>(rawBytes[2]) == 0xBF) {
        rawBytes.erase(0, 3);
    }

    std::wstring wideText = TextNormalizer::Utf8ToWide(rawBytes);
    auto& presCfg = Config::Instance().GetMutable().presenter;
    presCfg.scriptFilePath = filePath;

    SetScriptText(wideText, true);
    LOG_INFO("Loaded presenter script from '%ws' (%zu chars)", filePath.c_str(), wideText.size());
    return true;
}

bool PresenterController::PromptAndLoadScriptFile(HWND hOwner) {
    wchar_t fileBuf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hOwner ? hOwner : m_hMainWnd;
    ofn.lpstrFilter = L"Script Files (*.txt;*.md)\0*.txt;*.md\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Load Presenter Script (UTF-8 .txt / .md)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        return LoadScriptFromFile(fileBuf);
    }
    return false;
}

bool PresenterController::PasteScriptFromClipboard() {
    if (!OpenClipboard(m_hMainWnd)) {
        return false;
    }

    bool ok = false;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* pText = static_cast<const wchar_t*>(GlobalLock(hData));
        if (pText) {
            std::wstring text(pText);
            GlobalUnlock(hData);
            if (!text.empty()) {
                SetScriptText(text, true);
                ok = true;
            }
        }
    }
    CloseClipboard();
    return ok;
}

void PresenterController::ResetToStart() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingHypotheses.clear();
        m_alignment.Reset();
    }
    m_recognizer.ResetUtterance();
    m_window.SetActiveToken(-1, TrackingState::Idle, true);

    auto& presCfg = Config::Instance().GetMutable().presenter;
    presCfg.savedTokenIndex = -1;
    PersistConfig();
}

void PresenterController::JumpToEnd() {
    int lastLine = std::max(0, m_window.GetLineCount() - 1);
    int tok = m_window.GetFirstTokenForLine(lastLine);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingHypotheses.clear();
        if (tok < 0 && m_alignment.Model().TokenCount() > 0) {
            tok = m_alignment.Model().TokenCount() - 1;
        }
        if (tok >= 0) {
            m_alignment.SeekToToken(tok);
            tok = m_alignment.CurrentTokenIndex();
        }
    }
    m_recognizer.ResetUtterance();
    m_window.SetActiveToken(tok, TrackingState::Paused, true);

    auto& presCfg = Config::Instance().GetMutable().presenter;
    presCfg.savedTokenIndex = tok;
    PersistConfig();
}

void PresenterController::SeekToToken(int tokenIndex) {
    int newTok = -1;
    TrackingState st = TrackingState::Idle;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingHypotheses.clear();
        m_alignment.SeekToToken(tokenIndex);
        newTok = m_alignment.CurrentTokenIndex();
        st = m_alignment.State();
    }
    m_recognizer.ResetUtterance();
    m_window.SetActiveToken(newTok, st, true);

    auto& presCfg = Config::Instance().GetMutable().presenter;
    presCfg.savedTokenIndex = newTok;
    PersistConfig();
}

void PresenterController::SeekToLine(int lineIndex) {
    int firstTok = m_window.GetFirstTokenForLine(lineIndex);
    if (firstTok >= 0) {
        SeekToToken(firstTok);
    }
}

std::wstring PresenterController::GetScriptText() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_alignment.Model().Text();
}

int PresenterController::GetCurrentTokenIndex() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_alignment.CurrentTokenIndex();
}

TrackingState PresenterController::GetTrackingState() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_alignment.State();
}

std::vector<AudioEndpointInfo> PresenterController::GetAudioDevices() const {
    return AudioInputManager::Instance().EnumerateCaptureDevices();
}

void PresenterController::SelectAudioDevice(
    const std::wstring& deviceId,
    const std::wstring& friendlyName,
    int channel
) {
    auto& presCfg = Config::Instance().GetMutable().presenter;
    presCfg.audioDeviceId = deviceId.empty() ? L"default" : deviceId;
    if (!friendlyName.empty()) {
        presCfg.audioDeviceName = friendlyName;
    } else if (presCfg.audioDeviceId == L"default") {
        presCfg.audioDeviceName.clear();
    }
    if (channel >= 0) {
        presCfg.audioChannel = channel;
    }
    PersistConfig();

    AudioInputManager::Instance().SetSelectedDevice(
        presCfg.audioDeviceId, presCfg.audioDeviceName, presCfg.audioChannel);
    SyncStatusToWindow();
}

void PresenterController::SelectAudioChannel(int channel) {
    auto& presCfg = Config::Instance().GetMutable().presenter;
    presCfg.audioChannel = std::max(0, channel);
    PersistConfig();

    AudioInputManager::Instance().SetSelectedDevice(
        presCfg.audioDeviceId, presCfg.audioDeviceName, presCfg.audioChannel);
    SyncStatusToWindow();
}

void PresenterController::ApplySettings() {
    const auto& presCfg = Config::Instance().Get().presenter;

    m_recognizer.SetConfiguredModelPath(presCfg.voskModelPath);
    AudioInputManager::Instance().SetSelectedDevice(
        presCfg.audioDeviceId, presCfg.audioDeviceName, presCfg.audioChannel);

    bool scriptChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        scriptChanged = (m_alignment.Model().Text() != presCfg.scriptText);
    }

    m_window.ApplyConfig(presCfg);

    if (scriptChanged) {
        SetScriptText(presCfg.scriptText, false);
    }

    if (presCfg.enabled && !IsOpen()) {
        Open();
    } else if (!presCfg.enabled && IsOpen()) {
        Close();
    } else if (IsOpen()) {
        FlipManager::Instance().RaiseMirrorsToTop();
        SyncStatusToWindow();
    }
}

void PresenterController::OnDisplayChanged() {
    m_window.OnDisplayChanged();
    if (IsOpen()) {
        FlipManager::Instance().RaiseMirrorsToTop();
    }
}

MatchUpdate PresenterController::ProcessHypothesisForTest(const std::string& hypothesisUtf8, bool isFinal) {
    UNREFERENCED_PARAMETER(isFinal);
    if (m_paused) {
        return MatchUpdate{ GetCurrentTokenIndex(), 0.0, TrackingState::Paused, false };
    }
    MatchUpdate upd;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        upd = m_alignment.Process(hypothesisUtf8);
    }
    m_window.SetActiveToken(upd.tokenIndex, upd.state, false);
    Config::Instance().GetMutable().presenter.savedTokenIndex = upd.tokenIndex;
    return upd;
}

void PresenterController::OnHypothesisMessage() {
    std::vector<std::pair<std::string, bool>> batch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        batch.swap(m_pendingHypotheses);
    }

    if (m_paused || batch.empty()) {
        return;
    }

    MatchUpdate lastUpdate;
    bool anyProcessed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [hyp, isFinal] : batch) {
            UNREFERENCED_PARAMETER(isFinal);
            if (!hyp.empty()) {
                lastUpdate = m_alignment.Process(hyp);
                anyProcessed = true;
            }
        }
    }

    if (anyProcessed) {
        m_window.SetActiveToken(lastUpdate.tokenIndex, lastUpdate.state, false);
        Config::Instance().GetMutable().presenter.savedTokenIndex = lastUpdate.tokenIndex;
    }
}

void PresenterController::OnStatusMessage() {
    SyncStatusToWindow();
}

void PresenterController::OnAudioLevelMessage() {
    auto& audio = AudioInputManager::Instance();
    bool avail = (audio.GetState() == AudioCaptureState::Capturing);
    m_window.SetAudioStatus(audio.GetLiveLevel(), avail, audio.GetStatusMessage());
}

void PresenterController::SyncStatusToWindow() {
    auto& audio = AudioInputManager::Instance();
    bool avail = (audio.GetState() == AudioCaptureState::Capturing || audio.GetState() == AudioCaptureState::Stopped);
    std::wstring devLabel = audio.GetStatusMessage();
    if (devLabel.empty()) {
        devLabel = L"Default";
    }
    m_window.SetAudioStatus(audio.GetLiveLevel(), avail, devLabel);

    RecognizerStatus recSt = m_recognizer.GetStatus();
    bool recReady = (recSt == RecognizerStatus::Ready || recSt == RecognizerStatus::LoadingModel);
    m_window.SetRecognizerStatus(m_recognizer.GetDiagnosticMessage(), recReady);
}

void PresenterController::PersistConfig() {
    Config::Instance().Save();
}

void PresenterController::ShowMicrophonePopupMenu(POINT screenPt) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    auto& audio = AudioInputManager::Instance();
    const auto& presCfg = Config::Instance().Get().presenter;
    auto devices = audio.EnumerateCaptureDevices();

    // Live audio level & recognizer status header items (disabled informational items)
    int levelPct = static_cast<int>(std::round(audio.GetLiveLevel() * 100.0f));
    wchar_t levelBuf[128] = {};
    swprintf_s(levelBuf, L"Live Audio Level: %d%%", levelPct);
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, levelBuf);

    std::wstring recDiag = m_recognizer.GetDiagnosticMessage();
    if (!recDiag.empty()) {
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, (L"Speech: " + recDiag).c_str());
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Check if explicitly selected device is currently disconnected
    bool selectedFound = false;
    int selectedDeviceChannels = 1;
    for (size_t i = 0; i < devices.size() && i < IDM_PRES_MIC_MAX; ++i) {
        UINT flags = MF_STRING;
        if (devices[i].id == presCfg.audioDeviceId) {
            flags |= MF_CHECKED;
            selectedFound = true;
            selectedDeviceChannels = devices[i].channels;
        }
        AppendMenuW(hMenu, flags, IDM_PRES_MIC_BASE + static_cast<UINT>(i), devices[i].friendlyName.c_str());
    }

    if (!selectedFound && presCfg.audioDeviceId != L"default" && !presCfg.audioDeviceId.empty()) {
        std::wstring missingLabel = (presCfg.audioDeviceName.empty() ? presCfg.audioDeviceId : presCfg.audioDeviceName)
                                    + L" [Disconnected]";
        AppendMenuW(hMenu, MF_STRING | MF_CHECKED | MF_GRAYED, 0, missingLabel.c_str());
    }

    // Channel selector submenu if device has >= 2 channels
    if (selectedDeviceChannels >= 2) {
        HMENU hChMenu = CreatePopupMenu();
        if (hChMenu) {
            UINT mixFlags = MF_STRING | (presCfg.audioChannel == 0 ? MF_CHECKED : 0);
            AppendMenuW(hChMenu, mixFlags, IDM_PRES_CH_BASE, L"All Channels (Mono Mixdown)");
            for (int ch = 1; ch <= selectedDeviceChannels && ch < static_cast<int>(IDM_PRES_CH_MAX); ++ch) {
                wchar_t chLabel[64] = {};
                if (ch == 1) swprintf_s(chLabel, L"Channel 1 (Left / Input 1)");
                else if (ch == 2) swprintf_s(chLabel, L"Channel 2 (Right / Input 2)");
                else swprintf_s(chLabel, L"Channel %d", ch);

                UINT chFlags = MF_STRING | (presCfg.audioChannel == ch ? MF_CHECKED : 0);
                AppendMenuW(hChMenu, chFlags, IDM_PRES_CH_BASE + static_cast<UINT>(ch), chLabel);
            }
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hChMenu), L"Input &Channel");
        }
    }

    // Script & Line navigation actions
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_PRES_PASTE_SCRIPT, L"&Paste Script from Clipboard");
    AppendMenuW(hMenu, MF_STRING, IDM_PRES_LOAD_FILE, L"&Load Script File (.txt / .md)...");
    AppendMenuW(hMenu, MF_STRING, IDM_PRES_RESET_SCRIPT, L"&Reset to Beginning");

    // Jump to Line submenu
    auto linePreviews = m_window.GetLinePreviews();
    if (!linePreviews.empty()) {
        HMENU hLineMenu = CreatePopupMenu();
        if (hLineMenu) {
            int activeLine = m_window.GetActiveLineIndex();
            size_t count = std::min<size_t>(linePreviews.size(), IDM_PRES_LINE_MAX);
            for (size_t i = 0; i < count; ++i) {
                std::wstring preview = linePreviews[i];
                if (preview.size() > 42) {
                    preview = preview.substr(0, 39) + L"...";
                }
                if (preview.empty()) {
                    preview = L"(blank line)";
                }
                wchar_t itemText[128] = {};
                swprintf_s(itemText, L"%zu: %ws", i + 1, preview.c_str());
                UINT lFlags = MF_STRING | (static_cast<int>(i) == activeLine ? MF_CHECKED : 0);
                AppendMenuW(hLineMenu, lFlags, IDM_PRES_LINE_BASE + static_cast<UINT>(i), itemText);
            }
            AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hLineMenu), L"&Jump to Line");
        }
    }

    SetForegroundWindow(m_hMainWnd);
    UINT cmd = TrackPopupMenu(
        hMenu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        screenPt.x, screenPt.y,
        0,
        m_hMainWnd,
        nullptr
    );
    PostMessageW(m_hMainWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);

    if (cmd >= IDM_PRES_MIC_BASE && cmd < IDM_PRES_MIC_BASE + IDM_PRES_MIC_MAX) {
        size_t idx = cmd - IDM_PRES_MIC_BASE;
        if (idx < devices.size()) {
            SelectAudioDevice(devices[idx].id, devices[idx].friendlyName, 0);
        }
    } else if (cmd >= IDM_PRES_CH_BASE && cmd < IDM_PRES_CH_BASE + IDM_PRES_CH_MAX) {
        int ch = static_cast<int>(cmd - IDM_PRES_CH_BASE);
        SelectAudioChannel(ch);
    } else if (cmd == IDM_PRES_PASTE_SCRIPT) {
        PasteScriptFromClipboard();
    } else if (cmd == IDM_PRES_LOAD_FILE) {
        PromptAndLoadScriptFile(m_hMainWnd);
    } else if (cmd == IDM_PRES_RESET_SCRIPT) {
        ResetToStart();
    } else if (cmd >= IDM_PRES_LINE_BASE && cmd < IDM_PRES_LINE_BASE + IDM_PRES_LINE_MAX) {
        int lineIdx = static_cast<int>(cmd - IDM_PRES_LINE_BASE);
        SeekToLine(lineIdx);
    }
}

}  // namespace VirtualOverlay

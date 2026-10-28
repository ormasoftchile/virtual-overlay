#pragma once

#include "../config/Config.h"
#include "AudioInputManager.h"
#include "OfflineSpeechRecognizer.h"
#include "PresenterWindow.h"
#include "ScriptAlignmentEngine.h"

#include <windows.h>
#include <mutex>
#include <string>
#include <vector>

namespace VirtualOverlay {

constexpr UINT WM_APP_PRESENTER_HYPOTHESIS = WM_APP + 50;
constexpr UINT WM_APP_PRESENTER_STATUS     = WM_APP + 51;
constexpr UINT WM_APP_PRESENTER_AUDIO_LVL  = WM_APP + 52;

class PresenterController {
public:
    static PresenterController& Instance();

    bool Init(HINSTANCE hInstance, HWND hMainWnd);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Open / Close / Toggle Presenter caption surface
    void Open();
    void Close();
    void Toggle();
    bool IsOpen() const { return m_window.IsVisible(); }

    // Pause / Resume speech-driven advancement
    void SetPaused(bool paused);
    void TogglePause();
    bool IsPaused() const { return m_paused; }

    // Script management
    void SetScriptText(const std::wstring& scriptText, bool resetPosition = true);
    bool LoadScriptFromFile(const std::wstring& filePath);
    bool PromptAndLoadScriptFile(HWND hOwner = nullptr);
    bool PasteScriptFromClipboard();
    void ResetToStart();
    void JumpToEnd();
    void SeekToToken(int tokenIndex);
    void SeekToLine(int lineIndex);

    std::wstring GetScriptText() const;
    int GetCurrentTokenIndex() const;
    int GetCurrentLineIndex() const { return m_window.GetActiveLineIndex(); }
    int GetLineCount() const { return m_window.GetLineCount(); }
    std::vector<std::wstring> GetLinePreviews() const { return m_window.GetLinePreviews(); }
    TrackingState GetTrackingState() const;

    // Audio input selection
    std::vector<AudioEndpointInfo> GetAudioDevices() const;
    void SelectAudioDevice(const std::wstring& deviceId, const std::wstring& friendlyName = L"", int channel = -1);
    void SelectAudioChannel(int channel);

    // Recognition model diagnostics
    RecognizerStatus GetRecognizerStatus() const { return m_recognizer.GetStatus(); }
    std::wstring GetRecognizerDiagnostic() const { return m_recognizer.GetDiagnosticMessage(); }
    std::wstring GetActiveModelPath() const { return m_recognizer.GetActiveModelPath(); }

    // Configuration & Display notifications
    void ApplySettings();
    void OnDisplayChanged();

    // Main-thread message handlers (posted from worker threads)
    void OnHypothesisMessage();
    void OnStatusMessage();
    void OnAudioLevelMessage();

    // Deterministically process a recognized hypothesis (used by both live Vosk and integration tests)
    MatchUpdate ProcessHypothesisForTest(const std::string& hypothesisUtf8, bool isFinal = false);

private:
    PresenterController();
    ~PresenterController();
    PresenterController(const PresenterController&) = delete;
    PresenterController& operator=(const PresenterController&) = delete;

    void ShowMicrophonePopupMenu(POINT screenPt);
    void ShowJumpToLineMenu(POINT screenPt);
    void SyncStatusToWindow();
    void PersistConfig();

    HINSTANCE m_hInstance = nullptr;
    HWND m_hMainWnd = nullptr;
    bool m_initialized = false;
    bool m_paused = false;

    PresenterWindow m_window;
    OfflineSpeechRecognizer m_recognizer;

    mutable std::mutex m_mutex;
    ScriptAlignmentEngine m_alignment;
    std::vector<std::pair<std::string, bool>> m_pendingHypotheses;
};

}  // namespace VirtualOverlay

// Portions adapted from TalkPrompter (https://github.com/Sven-Bo/talkprompter)
// MIT License — Copyright (c) 2026 Sven Bosau (Bosau Digital LLC)

#pragma once

#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../lib/vosk/vosk_api.h"

namespace VirtualOverlay {

enum class RecognizerStatus {
    Uninitialized,
    MissingRuntimeDll,
    MissingModel,
    InvalidModel,
    LoadingModel,
    Ready,
    Error
};

class OfflineSpeechRecognizer {
public:
    static constexpr int kSampleRate = 16000;
    static constexpr size_t kGrammarWordLimit = 1200;

    using HypothesisCallback = std::function<void(const std::string& hypothesisUtf8, bool isFinal)>;
    using StatusCallback = std::function<void(RecognizerStatus status, const std::wstring& diagnostic)>;

    OfflineSpeechRecognizer();
    ~OfflineSpeechRecognizer();
    OfflineSpeechRecognizer(const OfflineSpeechRecognizer&) = delete;
    OfflineSpeechRecognizer& operator=(const OfflineSpeechRecognizer&) = delete;

    // Model discovery and validation helpers
    static bool IsValidVoskModelDirectory(const std::wstring& dirPath);
    static std::wstring LocateVoskModel(const std::wstring& configuredPath = L"");
    static std::wstring GetDefaultUserModelsDirectory();

    // Grammar builder & JSON result parser (exposed for deterministic unit tests)
    static std::string BuildGrammarJson(const std::vector<std::string>& vocabularyWords);
    static std::string ExtractFieldFromVoskJson(const std::string& jsonStr, const char* fieldName);

    // Configure model path and script vocabulary
    void SetConfiguredModelPath(const std::wstring& modelPath);
    std::wstring GetConfiguredModelPath() const;
    std::wstring GetActiveModelPath() const;

    // Start worker thread and load model + recognizer with optional script vocabulary
    bool Start(const std::vector<std::string>& scriptVocabulary = {});

    // Update recognizer grammar when script changes while running
    void UpdateVocabulary(const std::vector<std::string>& scriptVocabulary);

    // Reset recognizer utterance state
    void ResetUtterance();

    // Feed 16 kHz mono 16-bit PCM samples from audio capture thread (non-blocking)
    void FeedAudio(const int16_t* samples, size_t count);

    // Stop worker thread and release VoskRecognizer and VoskModel resources
    void Stop();

    RecognizerStatus GetStatus() const { return m_status.load(); }
    std::wstring GetDiagnosticMessage() const;
    bool IsUsingGrammar() const { return m_usingGrammar.load(); }

    void SetHypothesisCallback(HypothesisCallback cb);
    void SetStatusCallback(StatusCallback cb);

private:
    struct VoskApiTable {
        HMODULE hModule = nullptr;
        decltype(&vosk_set_log_level) set_log_level = nullptr;
        decltype(&vosk_model_new) model_new = nullptr;
        decltype(&vosk_model_free) model_free = nullptr;
        decltype(&vosk_recognizer_new) recognizer_new = nullptr;
        decltype(&vosk_recognizer_new_grm) recognizer_new_grm = nullptr;
        decltype(&vosk_recognizer_free) recognizer_free = nullptr;
        decltype(&vosk_recognizer_set_words) recognizer_set_words = nullptr;
        decltype(&vosk_recognizer_set_partial_words) recognizer_set_partial_words = nullptr;
        decltype(&vosk_recognizer_accept_waveform_s) recognizer_accept_waveform_s = nullptr;
        decltype(&vosk_recognizer_result) recognizer_result = nullptr;
        decltype(&vosk_recognizer_partial_result) recognizer_partial_result = nullptr;
        decltype(&vosk_recognizer_final_result) recognizer_final_result = nullptr;
        decltype(&vosk_recognizer_reset) recognizer_reset = nullptr;

        bool IsLoaded() const { return hModule != nullptr && model_new != nullptr && recognizer_new != nullptr; }
    };

    bool EnsureVoskDllLoaded();
    void UnloadVoskDll();
    void WorkerThreadMain();
    VoskRecognizer* CreateRecognizerLocked(VoskModel* model, const std::vector<std::string>& vocab);
    void UpdateStatus(RecognizerStatus status, const std::wstring& diagnostic);

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;

    VoskApiTable m_api;
    std::wstring m_configuredModelPath;
    std::wstring m_activeModelPath;
    std::vector<std::string> m_vocabulary;

    std::deque<std::vector<int16_t>> m_audioQueue;
    bool m_stopRequested = false;
    bool m_rebuildRecognizerRequested = false;
    bool m_resetUtteranceRequested = false;
    std::thread m_workerThread;

    std::atomic<RecognizerStatus> m_status{ RecognizerStatus::Uninitialized };
    std::atomic<bool> m_usingGrammar{ false };
    std::wstring m_diagnosticMessage;

    HypothesisCallback m_onHypothesis;
    StatusCallback m_onStatus;
};

}  // namespace VirtualOverlay

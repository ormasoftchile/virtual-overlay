// Portions adapted from TalkPrompter (https://github.com/Sven-Bo/talkprompter)
// MIT License — Copyright (c) 2026 Sven Bosau (Bosau Digital LLC)

#include "OfflineSpeechRecognizer.h"
#include "ScriptAlignmentEngine.h"
#include "../utils/Logger.h"
#include "../../lib/json.hpp"

#include <shlobj.h>
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace VirtualOverlay {

OfflineSpeechRecognizer::OfflineSpeechRecognizer() = default;

OfflineSpeechRecognizer::~OfflineSpeechRecognizer() {
    Stop();
    UnloadVoskDll();
}

static bool HasVoskModelFiles(const fs::path& modelDir, const fs::path& confDir, const fs::path& graphDir) {
    std::error_code ec;
    if (!fs::exists(modelDir / "final.mdl", ec)) return false;
    if (!fs::exists(confDir / "mfcc.conf", ec)) return false;
    if (fs::exists(graphDir / "HCLG.fst", ec)) return true;
    if (fs::exists(graphDir / "HCLr.fst", ec) && fs::exists(graphDir / "Gr.fst", ec)) return true;
    return false;
}

bool OfflineSpeechRecognizer::IsValidVoskModelDirectory(const std::wstring& dirPath) {
    if (dirPath.empty()) return false;
    std::error_code ec;
    fs::path p(dirPath);
    if (!fs::is_directory(p, ec)) return false;

    return HasVoskModelFiles(p / "am", p / "conf", p / "graph")
        || HasVoskModelFiles(p, p, p);
}

std::wstring OfflineSpeechRecognizer::GetDefaultUserModelsDirectory() {
    wchar_t* localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)) && localAppData) {
        std::wstring path = localAppData;
        CoTaskMemFree(localAppData);
        path += L"\\VirtualOverlay\\models";
        return path;
    }
    return L"models";
}

static std::wstring FindBestModelInRoot(const fs::path& root) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return {};

    // If root itself is a model directory, return it directly
    if (OfflineSpeechRecognizer::IsValidVoskModelDirectory(root.wstring())) {
        return root.wstring();
    }

    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;
        std::wstring folderName = entry.path().filename().wstring();
        if (!folderName.empty() && folderName[0] == L'.') continue;
        if (OfflineSpeechRecognizer::IsValidVoskModelDirectory(entry.path().wstring())) {
            candidates.push_back(entry.path());
        }
    }

    if (candidates.empty()) return {};

    // Prefer English small model ("en" + "small"), then any "en" model, then alphabetical
    auto rankModel = [](const fs::path& p) -> int {
        std::wstring name = p.filename().wstring();
        std::transform(name.begin(), name.end(), name.begin(), ::towlower);
        bool isEn = (name.find(L"en-us") != std::wstring::npos || name.find(L"-en-") != std::wstring::npos || name.find(L"en_") != std::wstring::npos);
        bool isSmall = (name.find(L"small") != std::wstring::npos);
        if (isEn && isSmall) return 0;
        if (isEn) return 1;
        if (isSmall) return 2;
        return 3;
    };

    std::sort(candidates.begin(), candidates.end(), [&](const fs::path& a, const fs::path& b) {
        int ra = rankModel(a);
        int rb = rankModel(b);
        if (ra != rb) return ra < rb;
        return a.filename().wstring() < b.filename().wstring();
    });

    return candidates.front().wstring();
}

std::wstring OfflineSpeechRecognizer::LocateVoskModel(const std::wstring& configuredPath) {
    // 1. Explicit configured path
    if (!configuredPath.empty()) {
        std::wstring found = FindBestModelInRoot(fs::path(configuredPath));
        if (!found.empty()) return found;
    }

    // 2. VOSK_MODEL_PATH environment variable
    wchar_t envBuf[MAX_PATH] = {};
    DWORD envLen = GetEnvironmentVariableW(L"VOSK_MODEL_PATH", envBuf, MAX_PATH);
    if (envLen > 0 && envLen < MAX_PATH) {
        std::wstring found = FindBestModelInRoot(fs::path(envBuf));
        if (!found.empty()) return found;
    }

    // 3. Per-user models folder (%LOCALAPPDATA%\VirtualOverlay\models)
    std::wstring userModels = GetDefaultUserModelsDirectory();
    if (!userModels.empty()) {
        std::wstring found = FindBestModelInRoot(fs::path(userModels));
        if (!found.empty()) return found;
    }

    // 4. Also check %LOCALAPPDATA%\TalkPrompter\models in case the user already installed TalkPrompter models
    wchar_t* localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)) && localAppData) {
        fs::path tpModels = fs::path(localAppData) / L"TalkPrompter" / L"models";
        CoTaskMemFree(localAppData);
        std::wstring found = FindBestModelInRoot(tpModels);
        if (!found.empty()) return found;
    }

    // 5. Walk up from executable directory looking for "models/"
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path dir = fs::path(exePath).parent_path();
    for (int i = 0; i < 6 && !dir.empty(); ++i) {
        std::wstring found = FindBestModelInRoot(dir / L"models");
        if (!found.empty()) return found;
        if (dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }

    return {};
}

std::string OfflineSpeechRecognizer::BuildGrammarJson(const std::vector<std::string>& vocabularyWords) {
    if (vocabularyWords.empty()) {
        return {};
    }

    std::vector<std::string> distinct;
    std::unordered_set<std::string> seen;
    distinct.reserve(vocabularyWords.size());

    for (const auto& raw : vocabularyWords) {
        std::string w = TextNormalizer::NormalizeWordUtf8(raw);
        if (!w.empty() && seen.insert(w).second) {
            distinct.push_back(std::move(w));
            if (distinct.size() > kGrammarWordLimit) {
                return {};
            }
        }
    }

    if (distinct.empty()) {
        return {};
    }

    // Build JSON array writing UTF-8 characters directly without \uXXXX escaping
    // because Vosk's internal JSON parser does not unescape \uXXXX sequences.
    std::string out = "[";
    for (size_t i = 0; i < distinct.size(); ++i) {
        if (i > 0) out += ", ";
        out += "\"";
        for (char c : distinct[i]) {
            if (c == '\"' || c == '\\') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        out += "\"";
    }
    out += ", \"[unk]\"]";
    return out;
}

static std::string StripUnkTokens(const std::string& text) {
    if (text.find("[unk]") == std::string::npos) {
        return text;
    }
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 5, "[unk]") == 0) {
            i += 5;
            while (i < text.size() && text[i] == ' ') i++;
            continue;
        }
        out.push_back(text[i]);
        i++;
    }
    // Trim trailing spaces
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string OfflineSpeechRecognizer::ExtractFieldFromVoskJson(const std::string& jsonStr, const char* fieldName) {
    if (jsonStr.empty() || !fieldName) return {};

    try {
        auto j = json::parse(jsonStr, nullptr, false);
        if (!j.is_discarded() && j.contains(fieldName) && j[fieldName].is_string()) {
            return StripUnkTokens(j[fieldName].get<std::string>());
        }
    } catch (...) {
    }
    return {};
}

bool OfflineSpeechRecognizer::EnsureVoskDllLoaded() {
    if (m_api.IsLoaded()) {
        return true;
    }

    std::vector<fs::path> candidateDirs;

    // 1. Executable directory and its vosk/ subdirectory
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    candidateDirs.push_back(exeDir);
    candidateDirs.push_back(exeDir / L"vosk");

    // 2. %LOCALAPPDATA%\VirtualOverlay\vosk
    wchar_t* localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)) && localAppData) {
        fs::path appDataDir(localAppData);
        CoTaskMemFree(localAppData);
        candidateDirs.push_back(appDataDir / L"VirtualOverlay" / L"vosk");
    }

    // 3. Repository lib/vosk/win-x64 and NuGet cache fallback
    fs::path walkDir = exeDir;
    for (int i = 0; i < 5 && !walkDir.empty(); ++i) {
        candidateDirs.push_back(walkDir / L"lib" / L"vosk" / L"win-x64");
        if (walkDir == walkDir.parent_path()) break;
        walkDir = walkDir.parent_path();
    }

    wchar_t* userProfile = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &userProfile)) && userProfile) {
        fs::path nugetVosk = fs::path(userProfile) / L".nuget" / L"packages" / L"vosk" / L"0.3.38" / L"build" / L"lib" / L"win-x64";
        CoTaskMemFree(userProfile);
        candidateDirs.push_back(nugetVosk);
    }

    HMODULE hMod = nullptr;
    std::error_code ec;
    for (const auto& dir : candidateDirs) {
        fs::path dllPath = dir / L"libvosk.dll";
        if (fs::exists(dllPath, ec)) {
            hMod = LoadLibraryExW(dllPath.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (hMod) {
                LOG_INFO("Loaded libvosk.dll from '%ws'", dllPath.wstring().c_str());
                break;
            }
        }
    }

    if (!hMod) {
        hMod = LoadLibraryW(L"libvosk.dll");
    }

    if (!hMod) {
        LOG_WARN("OfflineSpeechRecognizer: libvosk.dll not found");
        return false;
    }

    m_api.hModule = hMod;
    m_api.set_log_level = reinterpret_cast<decltype(&vosk_set_log_level)>(GetProcAddress(hMod, "vosk_set_log_level"));
    m_api.model_new = reinterpret_cast<decltype(&vosk_model_new)>(GetProcAddress(hMod, "vosk_model_new"));
    m_api.model_free = reinterpret_cast<decltype(&vosk_model_free)>(GetProcAddress(hMod, "vosk_model_free"));
    m_api.recognizer_new = reinterpret_cast<decltype(&vosk_recognizer_new)>(GetProcAddress(hMod, "vosk_recognizer_new"));
    m_api.recognizer_new_grm = reinterpret_cast<decltype(&vosk_recognizer_new_grm)>(GetProcAddress(hMod, "vosk_recognizer_new_grm"));
    m_api.recognizer_free = reinterpret_cast<decltype(&vosk_recognizer_free)>(GetProcAddress(hMod, "vosk_recognizer_free"));
    m_api.recognizer_set_words = reinterpret_cast<decltype(&vosk_recognizer_set_words)>(GetProcAddress(hMod, "vosk_recognizer_set_words"));
    m_api.recognizer_set_partial_words = reinterpret_cast<decltype(&vosk_recognizer_set_partial_words)>(GetProcAddress(hMod, "vosk_recognizer_set_partial_words"));
    m_api.recognizer_accept_waveform_s = reinterpret_cast<decltype(&vosk_recognizer_accept_waveform_s)>(GetProcAddress(hMod, "vosk_recognizer_accept_waveform_s"));
    m_api.recognizer_result = reinterpret_cast<decltype(&vosk_recognizer_result)>(GetProcAddress(hMod, "vosk_recognizer_result"));
    m_api.recognizer_partial_result = reinterpret_cast<decltype(&vosk_recognizer_partial_result)>(GetProcAddress(hMod, "vosk_recognizer_partial_result"));
    m_api.recognizer_final_result = reinterpret_cast<decltype(&vosk_recognizer_final_result)>(GetProcAddress(hMod, "vosk_recognizer_final_result"));
    m_api.recognizer_reset = reinterpret_cast<decltype(&vosk_recognizer_reset)>(GetProcAddress(hMod, "vosk_recognizer_reset"));

    if (!m_api.IsLoaded()) {
        LOG_ERROR("OfflineSpeechRecognizer: libvosk.dll missing required entry points");
        UnloadVoskDll();
        return false;
    }

    if (m_api.set_log_level) {
        m_api.set_log_level(-1);
    }

    return true;
}

void OfflineSpeechRecognizer::UnloadVoskDll() {
    if (m_api.hModule) {
        FreeLibrary(m_api.hModule);
        m_api = VoskApiTable{};
    }
}

void OfflineSpeechRecognizer::SetConfiguredModelPath(const std::wstring& modelPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_configuredModelPath = modelPath;
}

std::wstring OfflineSpeechRecognizer::GetConfiguredModelPath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_configuredModelPath;
}

std::wstring OfflineSpeechRecognizer::GetActiveModelPath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeModelPath;
}

std::wstring OfflineSpeechRecognizer::GetDiagnosticMessage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_diagnosticMessage;
}

void OfflineSpeechRecognizer::SetHypothesisCallback(HypothesisCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onHypothesis = std::move(cb);
}

void OfflineSpeechRecognizer::SetStatusCallback(StatusCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onStatus = std::move(cb);
}

void OfflineSpeechRecognizer::UpdateStatus(RecognizerStatus status, const std::wstring& diagnostic) {
    StatusCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.store(status);
        m_diagnosticMessage = diagnostic;
        cb = m_onStatus;
    }
    if (cb) {
        cb(status, diagnostic);
    }
}

bool OfflineSpeechRecognizer::Start(const std::vector<std::string>& scriptVocabulary) {
    Stop();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_vocabulary = scriptVocabulary;
        m_audioQueue.clear();
        m_stopRequested = false;
        m_rebuildRecognizerRequested = false;
        m_resetUtteranceRequested = false;
    }

    if (!EnsureVoskDllLoaded()) {
        UpdateStatus(RecognizerStatus::MissingRuntimeDll,
                     L"Vosk runtime (libvosk.dll) not found. Place libvosk.dll in the application folder.");
        return false;
    }

    std::wstring configured;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        configured = m_configuredModelPath;
    }

    std::wstring modelDir = LocateVoskModel(configured);
    if (modelDir.empty()) {
        std::wstring defaultDir = GetDefaultUserModelsDirectory();
        UpdateStatus(RecognizerStatus::MissingModel,
                     L"No offline Vosk speech model found in " + defaultDir +
                     L". Open Settings -> Presenter to install or select a model.");
        return false;
    }

    if (!IsValidVoskModelDirectory(modelDir)) {
        UpdateStatus(RecognizerStatus::InvalidModel,
                     L"Incompatible or incomplete Vosk model directory: " + modelDir);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_activeModelPath = modelDir;
    }

    m_workerThread = std::thread(&OfflineSpeechRecognizer::WorkerThreadMain, this);
    return true;
}

void OfflineSpeechRecognizer::UpdateVocabulary(const std::vector<std::string>& scriptVocabulary) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_vocabulary = scriptVocabulary;
        m_rebuildRecognizerRequested = true;
    }
    m_cv.notify_one();
}

void OfflineSpeechRecognizer::ResetUtterance() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_audioQueue.clear();
        m_resetUtteranceRequested = true;
    }
    m_cv.notify_one();
}

void OfflineSpeechRecognizer::FeedAudio(const int16_t* samples, size_t count) {
    if (!samples || count == 0 || m_status.load() != RecognizerStatus::Ready) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopRequested) return;
        // Cap queue to ~1.5 seconds of 16 kHz audio to prevent latency buildup
        while (m_audioQueue.size() > 75) {
            m_audioQueue.pop_front();
        }
        m_audioQueue.emplace_back(samples, samples + count);
    }
    m_cv.notify_one();
}

void OfflineSpeechRecognizer::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopRequested = true;
        m_audioQueue.clear();
    }
    m_cv.notify_all();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    m_usingGrammar.store(false);
    if (m_status.load() != RecognizerStatus::Uninitialized) {
        UpdateStatus(RecognizerStatus::Uninitialized, L"Recognizer stopped and resources released");
    }
}

VoskRecognizer* OfflineSpeechRecognizer::CreateRecognizerLocked(
    VoskModel* model,
    const std::vector<std::string>& vocab
) {
    if (!model || !m_api.IsLoaded()) {
        return nullptr;
    }

    VoskRecognizer* rec = nullptr;
    std::string grammar = BuildGrammarJson(vocab);
    if (!grammar.empty() && m_api.recognizer_new_grm) {
        rec = m_api.recognizer_new_grm(model, static_cast<float>(kSampleRate), grammar.c_str());
        if (rec) {
            m_usingGrammar.store(true);
            LOG_INFO("Created Vosk recognizer with script grammar lock (%zu distinct words)", vocab.size());
        }
    }

    if (!rec) {
        rec = m_api.recognizer_new(model, static_cast<float>(kSampleRate));
        m_usingGrammar.store(false);
        if (rec) {
            LOG_INFO("Created Vosk recognizer with open vocabulary");
        }
    }

    if (rec) {
        // Keep word-level lattice alignment off (0) so PartialResult() uses fast
        // best-path traceback instead of expensive CompactLattice determinization.
        if (m_api.recognizer_set_words) {
            m_api.recognizer_set_words(rec, 0);
        }
        if (m_api.recognizer_set_partial_words) {
            m_api.recognizer_set_partial_words(rec, 0);
        }
    }

    return rec;
}

void OfflineSpeechRecognizer::WorkerThreadMain() {
    std::wstring modelPathWide;
    std::vector<std::string> initialVocab;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        modelPathWide = m_activeModelPath;
        initialVocab = m_vocabulary;
    }

    UpdateStatus(RecognizerStatus::LoadingModel, L"Loading Vosk model...");

    std::string modelPathUtf8 = TextNormalizer::WideToUtf8(modelPathWide);
    VoskModel* model = m_api.model_new(modelPathUtf8.c_str());
    if (!model) {
        UpdateStatus(RecognizerStatus::InvalidModel, L"Failed to load Vosk model from: " + modelPathWide);
        return;
    }

    VoskRecognizer* recognizer = CreateRecognizerLocked(model, initialVocab);
    if (!recognizer) {
        m_api.model_free(model);
        UpdateStatus(RecognizerStatus::Error, L"Failed to initialize Vosk recognizer");
        return;
    }

    fs::path modelFolder(modelPathWide);
    UpdateStatus(RecognizerStatus::Ready, L"Ready (" + modelFolder.filename().wstring() + L")");

    std::string lastPartial;

    while (true) {
        std::vector<int16_t> chunk;
        bool doRebuild = false;
        bool doReset = false;
        std::vector<std::string> nextVocab;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&]() {
                return m_stopRequested || m_rebuildRecognizerRequested ||
                       m_resetUtteranceRequested || !m_audioQueue.empty();
            });

            if (m_stopRequested) {
                break;
            }

            if (m_rebuildRecognizerRequested) {
                m_rebuildRecognizerRequested = false;
                doRebuild = true;
                nextVocab = m_vocabulary;
            }

            if (m_resetUtteranceRequested) {
                m_resetUtteranceRequested = false;
                doReset = true;
            }

            // Drain all queued audio chunks at once so Vosk processes all available
            // samples in a single call and never builds up a decoding backlog.
            if (!m_audioQueue.empty()) {
                chunk = std::move(m_audioQueue.front());
                m_audioQueue.pop_front();
                while (!m_audioQueue.empty()) {
                    const auto& next = m_audioQueue.front();
                    chunk.insert(chunk.end(), next.begin(), next.end());
                    m_audioQueue.pop_front();
                }
            }
        }

        if (doRebuild) {
            if (recognizer) {
                m_api.recognizer_free(recognizer);
            }
            recognizer = CreateRecognizerLocked(model, nextVocab);
            lastPartial.clear();
            if (!recognizer) {
                UpdateStatus(RecognizerStatus::Error, L"Failed to rebuild Vosk recognizer");
                break;
            }
        }

        if (doReset && recognizer && m_api.recognizer_reset) {
            m_api.recognizer_reset(recognizer);
            lastPartial.clear();
        }

        if (!chunk.empty() && recognizer) {
            int isFinal = m_api.recognizer_accept_waveform_s(
                recognizer, chunk.data(), static_cast<int>(chunk.size()));

            HypothesisCallback cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                cb = m_onHypothesis;
            }

            if (isFinal != 0) {
                const char* resJson = m_api.recognizer_result ? m_api.recognizer_result(recognizer) : nullptr;
                std::string text = resJson ? ExtractFieldFromVoskJson(resJson, "text") : std::string{};
                lastPartial.clear();
                if (!text.empty() && cb) {
                    cb(text, true);
                }
            } else {
                const char* partJson = m_api.recognizer_partial_result ? m_api.recognizer_partial_result(recognizer) : nullptr;
                std::string partial = partJson ? ExtractFieldFromVoskJson(partJson, "partial") : std::string{};
                if (!partial.empty() && partial != lastPartial) {
                    lastPartial = partial;
                    if (cb) {
                        cb(partial, false);
                    }
                }
            }
        }
    }

    if (recognizer && m_api.recognizer_free) {
        m_api.recognizer_free(recognizer);
    }
    if (model && m_api.model_free) {
        m_api.model_free(model);
    }
    LOG_INFO("OfflineSpeechRecognizer: freed Vosk recognizer and model");
}

}  // namespace VirtualOverlay

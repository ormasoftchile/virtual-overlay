// Deterministic Unit & Integration Tests for Virtual Overlay Presenter
// Covers:
// 1. Speech-to-script alignment engine (ScriptAlignmentEngine, ScriptModel, TextNormalizer, NumberExpander, Levenshtein, SmoothDamp)
// 2. Audio subsystem (AudioResampler format/channel/sample-rate conversion, AudioInputManager enumeration & unavailable device handling, OfflineSpeechRecognizer grammar & model validation)
// 3. Presenter integration (PresenterWindow layout/line mapping, fixed reading baseline invariance, per-monitor anchor persistence, Pause/Resume/Close/Reopen lifecycle)

#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/config/Config.h"
#include "../src/presenter/AudioInputManager.h"
#include "../src/presenter/OfflineSpeechRecognizer.h"
#include "../src/presenter/PresenterController.h"
#include "../src/presenter/PresenterWindow.h"
#include "../src/presenter/ScriptAlignmentEngine.h"
#include "../src/utils/D2DRenderer.h"
#include "../src/utils/Monitor.h"
#include "../src/settings/SettingsWindow.h"
#include "../src/settings/SettingsPages.h"
#include "../src/overlay/OverlayWindow.h"
#include "../lib/json.hpp"

namespace fs = std::filesystem;
using namespace VirtualOverlay;

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(cond, msg)                                                        \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg));           \
            g_failed++;                                                               \
            return;                                                                   \
        }                                                                             \
    } while (0)

#define EXPECT_EQ(actual, expected, msg)                                              \
    do {                                                                              \
        auto _a = (actual);                                                           \
        auto _e = (expected);                                                         \
        if (!(_a == _e)) {                                                            \
            std::printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg));           \
            g_failed++;                                                               \
            return;                                                                   \
        }                                                                             \
    } while (0)

static void RunTest(const char* name, void (*fn)()) {
    int beforeFail = g_failed;
    fn();
    if (g_failed == beforeFail) {
        g_passed++;
        std::printf("  [PASS] %s\n", name);
    }
}

static std::string WordAt(const ScriptModel& model, int tokenIndex) {
    if (tokenIndex >= 0 && tokenIndex < model.TokenCount()) {
        return model.Tokens()[static_cast<size_t>(tokenIndex)].normalized;
    }
    return "<none>";
}

static MatchUpdate FeedGrowing(ScriptAlignmentEngine& matcher, const std::string& utterance) {
    std::istringstream iss(utterance);
    std::vector<std::string> words;
    std::string w;
    while (iss >> w) {
        words.push_back(w);
    }

    MatchUpdate update{};
    std::string prefix;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) prefix += " ";
        prefix += words[i];
        update = matcher.Process(prefix);
    }
    return update;
}

// =============================================================================
// 1. Speech Alignment Unit Tests
// =============================================================================

static const char* kFox = "The quick brown fox jumps over the lazy dog.";

static void TestNumberExpander() {
    auto joinWords = [](const std::vector<std::string>& v) {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i > 0) s += " ";
            s += v[i];
        }
        return s;
    };

    EXPECT_EQ(joinWords(NumberExpander::ToWords("0")), std::string("zero"), "0 -> zero");
    EXPECT_EQ(joinWords(NumberExpander::ToWords("5")), std::string("five"), "5 -> five");
    EXPECT_EQ(joinWords(NumberExpander::ToWords("13")), std::string("thirteen"), "13 -> thirteen");
    EXPECT_EQ(joinWords(NumberExpander::ToWords("25")), std::string("twenty five"), "25 -> twenty five");
    EXPECT_EQ(joinWords(NumberExpander::ToWords("100")), std::string("one hundred"), "100 -> one hundred");
    EXPECT_EQ(joinWords(NumberExpander::ToWords("135")), std::string("one hundred thirty five"), "135 -> one hundred thirty five");
    EXPECT_EQ(joinWords(NumberExpander::ToWords("2025")), std::string("two thousand twenty five"), "2025 -> two thousand twenty five");
    EXPECT_EQ(joinWords(NumberExpander::ToWords("007")), std::string("seven"), "007 -> seven");
    EXPECT_EQ(joinWords(NumberExpander::ToWords("abc")), std::string("abc"), "non-numeric unchanged");
    EXPECT_TRUE(NumberExpander::IsAllDigits("123"), "123 is all digits");
    EXPECT_TRUE(!NumberExpander::IsAllDigits(""), "empty is not all digits");
    EXPECT_TRUE(!NumberExpander::IsAllDigits("12a"), "12a is not all digits");
}

static void TestTextNormalizer() {
    EXPECT_EQ(TextNormalizer::NormalizeWordUtf8("Hello,"), std::string("hello"), "strip comma and lowercase");
    EXPECT_EQ(TextNormalizer::NormalizeWordUtf8("WORLD!"), std::string("world"), "strip bang and lowercase");
    EXPECT_EQ(TextNormalizer::NormalizeWordUtf8("don't"), std::string("dont"), "strip apostrophe");
    EXPECT_EQ(TextNormalizer::NormalizeWordUtf8("..."), std::string(""), "punctuation only -> empty");

    auto matchWords = TextNormalizer::ToMatchWordsUtf8("I have 25 cats");
    EXPECT_EQ(matchWords.size(), size_t(5), "25 expands to 2 words");
    EXPECT_EQ(matchWords[2], std::string("twenty"), "twenty");
    EXPECT_EQ(matchWords[3], std::string("five"), "five");

    std::wstring text = L"The quick, brown fox.";
    auto tokens = TextNormalizer::Tokenize(text);
    EXPECT_EQ(tokens.size(), size_t(4), "4 tokens");
    EXPECT_EQ(tokens[0].normalized, std::string("the"), "token 0");
    EXPECT_EQ(text.substr(tokens[1].start, tokens[1].length), std::wstring(L"quick"), "token 1 span");
    EXPECT_EQ(text.substr(tokens[3].start, tokens[3].length), std::wstring(L"fox"), "token 3 span");
}

static void TestFirstWordFastPathAndSingleWordGuard() {
    auto model = ScriptModel::BuildUtf8(kFox);
    ScriptAlignmentEngine matcher(model);

    // Non-expected single word must NOT advance or jump
    auto u0 = matcher.Process("banana");
    EXPECT_EQ(u0.tokenIndex, -1, "off-script single word does not advance");

    auto uLazy = matcher.Process("lazy");
    EXPECT_TRUE(!uLazy.advanced, "single word later in script does not jump");
    EXPECT_EQ(matcher.CurrentTokenIndex(), -1, "still at -1");

    // Expected first word advances immediately
    auto u1 = matcher.Process("the");
    EXPECT_EQ(u1.state, TrackingState::Tracking, "expected first word tracks immediately");
    EXPECT_EQ(WordAt(model, u1.tokenIndex), std::string("the"), "advanced to 'the'");
}

static void TestCleanSequentialReadAndPartials() {
    auto model = ScriptModel::BuildUtf8(kFox);
    ScriptAlignmentEngine matcher(model);

    int last = -1;
    const char* prefixes[] = {
        "the quick brown",
        "the quick brown fox jumps",
        "the quick brown fox jumps over the lazy dog"
    };

    for (const char* p : prefixes) {
        auto upd = matcher.Process(p);
        EXPECT_TRUE(upd.tokenIndex >= last, "monotonic progression on partial updates");
        last = upd.tokenIndex;
    }

    EXPECT_EQ(matcher.State(), TrackingState::Tracking, "tracking at end");
    EXPECT_EQ(WordAt(model, matcher.CurrentTokenIndex()), std::string("dog"), "reached end of script");
}

static void TestPauseHoldsAndFirstWordAfterPauseAdvances() {
    auto model = ScriptModel::BuildUtf8(kFox);
    ScriptAlignmentEngine matcher(model);

    FeedGrowing(matcher, "the quick brown fox");
    int held = matcher.CurrentTokenIndex();
    EXPECT_EQ(WordAt(model, held), std::string("fox"), "at fox");

    // Pause with filler words
    matcher.Process("um");
    auto pausedUpd = matcher.Process("um well");
    EXPECT_TRUE(!pausedUpd.advanced, "pause does not advance");
    EXPECT_EQ(matcher.CurrentTokenIndex(), held, "position held during pause");
    EXPECT_EQ(matcher.State(), TrackingState::Paused, "state is Paused");

    // First expected word after pause advances immediately
    auto resumedUpd = matcher.Process("jumps");
    EXPECT_EQ(resumedUpd.state, TrackingState::Tracking, "resumed tracking");
    EXPECT_EQ(resumedUpd.tokenIndex, held + 1, "advanced to jumps");
}

static void TestOffScriptAdlibAndRepeatedWords() {
    auto model = ScriptModel::BuildUtf8(kFox);
    ScriptAlignmentEngine matcher(model);

    matcher.Process("the quick brown");
    int beforeAdlib = matcher.CurrentTokenIndex();

    matcher.Process("so anyway let me think about this");
    EXPECT_EQ(matcher.CurrentTokenIndex(), beforeAdlib, "adlib does not move anchor");

    auto resumed = matcher.Process("brown fox jumps over");
    EXPECT_EQ(resumed.state, TrackingState::Tracking, "resumed after adlib");
    EXPECT_TRUE(matcher.CurrentTokenIndex() > beforeAdlib, "advanced after resuming script");

    // Repeated words and ambiguous phrases prefer local continuity
    auto repModel = ScriptModel::BuildUtf8("the cat and the dog and the bird");
    ScriptAlignmentEngine repMatcher(repModel);
    FeedGrowing(repMatcher, "the cat and the dog");
    EXPECT_EQ(WordAt(repModel, repMatcher.CurrentTokenIndex()), std::string("dog"), "at dog");
    repMatcher.Process("and the bird");
    EXPECT_EQ(repMatcher.CurrentTokenIndex(), 7, "matched third 'the' and 'bird'");

    // Repeated partials at the current anchor and 2-word matches further ahead must NOT skip lines
    auto multiLine = ScriptModel::BuildUtf8(
        "welcome to virtual overlay for windows. "
        "this presenter follows your speaking pace smoothly. "
        "you can also scroll or jump to any line."
    );
    ScriptAlignmentEngine antiSkip(multiLine);
    antiSkip.Process("welcome to virtual");
    EXPECT_EQ(antiSkip.CurrentTokenIndex(), 2, "at 'virtual' (token 2)");
    // Repeated partial at the same position must hold at token 2 and not ratchet forward
    for (int i = 0; i < 6; ++i) {
        antiSkip.Process("welcome to virtual");
    }
    EXPECT_EQ(antiSkip.CurrentTokenIndex(), 2, "repeated identical partial holds at token 2 without skipping");
    // A 2-word partial matching words on line 3 ("to any") must not jump ahead from token 2
    antiSkip.Process("to any");
    EXPECT_EQ(antiSkip.CurrentTokenIndex(), 2, "2-word match far ahead does not jump lines");
}

static void TestSkipAheadAndBackwardRecovery() {
    static const char* kDistinctWords[40] = {
        "apple", "banana", "cherry", "dragon", "emerald",
        "falcon", "glacier", "harbor", "island", "jungle",
        "lantern", "meadow", "nebula", "orchard", "pioneer",
        "quartz", "river", "summit", "tempest", "umbrella",
        "valley", "whisper", "xenon", "yonder", "zephyr",
        "anchor", "beacon", "compass", "dolphin", "eclipse",
        "fortress", "granite", "horizon", "ivory", "jasper",
        "kingdom", "legend", "monarch", "navigator", "odyssey"
    };
    std::string script40;
    for (int i = 0; i < 40; ++i) {
        if (i > 0) script40 += " ";
        script40 += kDistinctWords[i];
    }
    auto model40 = ScriptModel::BuildUtf8(script40);
    ScriptAlignmentEngine matcher40(model40);

    matcher40.Process("apple banana cherry dragon emerald");
    EXPECT_EQ(matcher40.CurrentTokenIndex(), 4, "at emerald (token 4)");

    MatchUpdate skipUpd{};
    for (int i = 0; i < 8; ++i) {
        skipUpd = matcher40.Process("anchor beacon compass");
        if (skipUpd.state == TrackingState::Tracking && matcher40.CurrentTokenIndex() >= 25) {
            break;
        }
    }
    EXPECT_EQ(skipUpd.state, TrackingState::Tracking, "reacquired after skipping ahead");
    EXPECT_TRUE(matcher40.CurrentTokenIndex() >= 25 && matcher40.CurrentTokenIndex() <= 27, "jumped to anchor..compass (25..27)");

    // Backward re-read recovery vs short echo guard
    const char* threeSentences =
        "alpha bravo charlie delta echo. "
        "foxtrot golf hotel india juliet. "
        "kilo lima mike november oscar.";
    auto model3 = ScriptModel::BuildUtf8(threeSentences);
    ScriptAlignmentEngine matcher3(model3);

    FeedGrowing(matcher3, "alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima mike november oscar");
    int atEnd = matcher3.CurrentTokenIndex();
    EXPECT_EQ(WordAt(model3, atEnd), std::string("oscar"), "at oscar");

    // Short 2-word backward echo must NOT rewind
    for (int i = 0; i < 12; ++i) {
        matcher3.Process("alpha bravo");
    }
    EXPECT_EQ(matcher3.CurrentTokenIndex(), atEnd, "2-word echo did not rewind");

    // Full sentence retake re-acquires backward
    MatchUpdate backUpd{};
    for (int r = 0; r < 3; ++r) {
        backUpd = FeedGrowing(matcher3, "alpha bravo charlie delta echo");
    }
    EXPECT_EQ(backUpd.state, TrackingState::Tracking, "backward retake reacquired");
    EXPECT_EQ(WordAt(model3, backUpd.tokenIndex), std::string("echo"), "aligned to echo");
}

static void TestNumbersSeekResetAndSmoothDamp() {
    auto numModel = ScriptModel::BuildUtf8("we raised 25 million dollars last year");
    ScriptAlignmentEngine numMatcher(numModel);
    numMatcher.Process("we raised twenty five million");
    EXPECT_EQ(WordAt(numModel, numMatcher.CurrentTokenIndex()), std::string("million"), "matched expanded 25");

    // SeekToToken & Reset
    auto foxModel = ScriptModel::BuildUtf8(kFox);
    ScriptAlignmentEngine foxMatcher(foxModel);
    foxMatcher.SeekToToken(4);
    EXPECT_EQ(WordAt(foxModel, foxMatcher.CurrentTokenIndex()), std::string("jumps"), "seek to token 4 (jumps)");
    foxMatcher.Reset();
    EXPECT_EQ(foxMatcher.CurrentTokenIndex(), -1, "reset token index to -1");
    EXPECT_EQ(foxMatcher.State(), TrackingState::Idle, "reset state to Idle");

    // SmoothDamp convergence without overshoot
    double pos = 0.0;
    double vel = 0.0;
    const double target = 120.0;
    for (int step = 0; step < 120; ++step) {
        double next = SmoothDamp::Step(pos, target, vel, 0.25, 1.0 / 60.0);
        EXPECT_TRUE(next >= pos && next <= target + 1e-6, "SmoothDamp monotonic without overshoot");
        pos = next;
    }
    EXPECT_TRUE(std::fabs(pos - target) < 0.5, "SmoothDamp converged to target");
}

// =============================================================================
// 2. Audio & Offline Speech Recognizer Tests
// =============================================================================

static void TestAudioResamplerAndChannelSelection() {
    // Generate 480 frames (10 ms at 48 kHz) of stereo Float32:
    // Channel 1 (Left) = +0.5f, Channel 2 (Right) = -0.25f
    const size_t frames = 480;
    std::vector<float> stereo(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        stereo[2 * i] = 0.50f;
        stereo[2 * i + 1] = -0.25f;
    }

    std::vector<float> mono;
    float peak = 0.0f;

    // Channel 0 = Mono mixdown: (0.50 - 0.25) / 2 = 0.125
    AudioResampler::ConvertToMonoFloat(
        reinterpret_cast<const uint8_t*>(stereo.data()),
        frames, 2, AudioSampleFormat::Float32, 0, mono, peak);
    EXPECT_EQ(mono.size(), frames, "mono mixdown frame count");
    EXPECT_TRUE(std::fabs(mono[0] - 0.125f) < 1e-4f, "mono mixdown value");
    EXPECT_TRUE(std::fabs(peak - 0.125f) < 1e-4f, "mono mixdown peak");

    // Channel 1 = Left only (0.50)
    AudioResampler::ConvertToMonoFloat(
        reinterpret_cast<const uint8_t*>(stereo.data()),
        frames, 2, AudioSampleFormat::Float32, 1, mono, peak);
    EXPECT_TRUE(std::fabs(mono[0] - 0.50f) < 1e-4f, "channel 1 value");
    EXPECT_TRUE(std::fabs(peak - 0.50f) < 1e-4f, "channel 1 peak");

    // Channel 2 = Right only (-0.25)
    AudioResampler::ConvertToMonoFloat(
        reinterpret_cast<const uint8_t*>(stereo.data()),
        frames, 2, AudioSampleFormat::Float32, 2, mono, peak);
    EXPECT_TRUE(std::fabs(mono[0] - (-0.25f)) < 1e-4f, "channel 2 value");
    EXPECT_TRUE(std::fabs(peak - 0.25f) < 1e-4f, "channel 2 peak");

    // Resample 48,000 Hz -> 16,000 Hz (3:1 ratio: 480 samples -> 160 samples)
    AudioResampler resampler;
    std::vector<int16_t> pcm16;
    resampler.ResampleTo16kPcm16(mono.data(), mono.size(), 48000, pcm16);
    EXPECT_EQ(pcm16.size(), size_t(160), "48kHz -> 16kHz produces 160 samples from 480");
    EXPECT_TRUE(pcm16[10] < -8000 && pcm16[10] > -8300, "PCM16 amplitude matches -0.25 * 32767");
}

static void TestAudioDeviceEnumerationAndMissingDeviceHandling() {
    AudioInputManager::Instance().Init();
    auto devices = AudioInputManager::Instance().EnumerateCaptureDevices();
    EXPECT_TRUE(!devices.empty(), "at least 'default' capture endpoint returned");
    EXPECT_EQ(devices[0].id, std::wstring(L"default"), "first entry is 'default'");

    // Test missing/unavailable device handling without silently switching devices
    AudioInputManager::Instance().SetSelectedDevice(L"{0.0.1.00000000}.{deadbeef-0000-0000-0000-000000000000}", L"Unplugged Test Mic", 1);
    AudioInputManager::Instance().StartCapture();
    Sleep(150);
    EXPECT_EQ(AudioInputManager::Instance().GetState(), AudioCaptureState::DeviceUnavailable,
              "non-existent explicit device reports DeviceUnavailable without switching");
    EXPECT_EQ(AudioInputManager::Instance().GetSelectedDeviceId(),
              std::wstring(L"{0.0.1.00000000}.{deadbeef-0000-0000-0000-000000000000}"),
              "selected device ID preserved while unavailable");
    AudioInputManager::Instance().StopCapture();
    AudioInputManager::Instance().SetSelectedDevice(L"default", L"", 0);
}

static void TestBundledVoskRuntime() {
    wchar_t exePath[MAX_PATH] = {};
    EXPECT_TRUE(GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0, "executable path available");
    fs::path exeDir = fs::path(exePath).parent_path();

    for (const wchar_t* name : {
             L"libvosk.dll", L"libgcc_s_seh-1.dll", L"libstdc++-6.dll", L"libwinpthread-1.dll" }) {
        EXPECT_TRUE(fs::is_regular_file(exeDir / name), "Vosk runtime DLL deployed beside executable");
    }

    HMODULE module = LoadLibraryExW((exeDir / L"libvosk.dll").c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    EXPECT_TRUE(module != nullptr, "bundled Vosk runtime loads with its dependencies");

    for (const char* symbol : {
             "vosk_set_log_level", "vosk_model_new", "vosk_model_free",
             "vosk_recognizer_new", "vosk_recognizer_new_grm", "vosk_recognizer_free",
             "vosk_recognizer_set_words", "vosk_recognizer_set_partial_words",
             "vosk_recognizer_accept_waveform_s", "vosk_recognizer_result",
             "vosk_recognizer_partial_result", "vosk_recognizer_final_result",
             "vosk_recognizer_reset" }) {
        if (!GetProcAddress(module, symbol)) {
            FreeLibrary(module);
            EXPECT_TRUE(false, symbol);
        }
    }
    FreeLibrary(module);
}

static void TestVoskGrammarAndModelValidation() {
    std::string grm = OfflineSpeechRecognizer::BuildGrammarJson({ "hello", "world", "hello", "größe" });
    EXPECT_TRUE(!grm.empty(), "grammar built for small vocabulary");
    EXPECT_TRUE(grm.find("\\u") == std::string::npos, "no \\u escapes in Vosk grammar");
    EXPECT_TRUE(grm.find("\"größe\"") != std::string::npos, "contains literal UTF-8 word");
    EXPECT_TRUE(grm.find("\"[unk]\"") != std::string::npos, "contains [unk]");

    // Exceeding 1200 distinct words returns empty string (open vocabulary fallback)
    std::vector<std::string> hugeVocab;
    for (int i = 0; i <= 1205; ++i) {
        hugeVocab.push_back("word" + std::to_string(i));
    }
    EXPECT_TRUE(OfflineSpeechRecognizer::BuildGrammarJson(hugeVocab).empty(),
                "vocab > 1200 returns empty grammar for open-vocab fallback");

    // ExtractFieldFromVoskJson strips [unk]
    std::string partial = OfflineSpeechRecognizer::ExtractFieldFromVoskJson(
        "{\"partial\" : \"the quick [unk] brown\"}", "partial");
    EXPECT_EQ(partial, std::string("the quick brown"), "strips [unk] from partial JSON");

    // Model folder validation
    fs::path tempModel = fs::temp_directory_path() / L"vo_test_vosk_model";
    fs::remove_all(tempModel);
    fs::create_directories(tempModel / "am");
    fs::create_directories(tempModel / "conf");
    fs::create_directories(tempModel / "graph");
    EXPECT_TRUE(!OfflineSpeechRecognizer::IsValidVoskModelDirectory(tempModel.wstring()),
                "incomplete model folder rejected");

    std::ofstream(tempModel / "am" / "final.mdl").put('x');
    std::ofstream(tempModel / "conf" / "mfcc.conf").put('x');
    std::ofstream(tempModel / "graph" / "HCLG.fst").put('x');
    EXPECT_TRUE(OfflineSpeechRecognizer::IsValidVoskModelDirectory(tempModel.wstring()),
                "complete Vosk model folder accepted");
    fs::remove_all(tempModel);
}

// =============================================================================
// 3. Presenter Integration & Persistence Tests
// =============================================================================

static void TestConfigPersistenceRoundTrip() {
    fs::path tmpCfg = fs::temp_directory_path() / L"vo_presenter_test_config.json";
    fs::remove(tmpCfg);

    auto& cfg = Config::Instance();
    cfg.Reset();
    auto& mut = cfg.GetMutable().presenter;
    mut.enabled = true;
    mut.fontSize = 40;
    mut.captionWidth = 960;
    mut.audioDeviceId = L"{0.0.1.00000000}.{test-mic-guid}";
    mut.audioDeviceName = L"Maono PD200X";
    mut.audioChannel = 2;
    mut.scriptText = L"Line one of script.\nLine two of script.";
    mut.savedTokenIndex = 3;
    mut.monitorAnchors[L"MNN"] = PresenterMonitorAnchor{ 0.48f, 0.12f };
    mut.monitorAnchors[L"PRIMARY"] = PresenterMonitorAnchor{ 0.52f, 0.18f };

    EXPECT_TRUE(cfg.Save(tmpCfg.wstring()), "save presenter config");

    cfg.Reset();
    EXPECT_TRUE(cfg.Load(tmpCfg.wstring()), "load presenter config");

    const auto& loaded = cfg.Get().presenter;
    EXPECT_EQ(loaded.enabled, true, "enabled persisted");
    EXPECT_EQ(loaded.fontSize, 40, "fontSize persisted");
    EXPECT_EQ(loaded.captionWidth, 960, "captionWidth persisted");
    EXPECT_EQ(loaded.audioDeviceId, std::wstring(L"{0.0.1.00000000}.{test-mic-guid}"), "audioDeviceId persisted");
    EXPECT_EQ(loaded.audioDeviceName, std::wstring(L"Maono PD200X"), "audioDeviceName persisted");
    EXPECT_EQ(loaded.audioChannel, 2, "audioChannel persisted");
    EXPECT_EQ(loaded.savedTokenIndex, 3, "savedTokenIndex persisted");
    EXPECT_EQ(loaded.monitorAnchors.size(), size_t(2), "2 monitor anchors persisted");
    EXPECT_TRUE(std::fabs(loaded.monitorAnchors.at(L"MNN").normY - 0.12f) < 1e-4f, "MNN normY persisted");
    EXPECT_TRUE(std::fabs(loaded.monitorAnchors.at(L"PRIMARY").normY - 0.18f) < 1e-4f, "PRIMARY normY persisted");

    fs::remove(tmpCfg);
    fs::remove(fs::path(tmpCfg.wstring() + L".bak"));
}

static void TestMonitorRegistryAndLegacyMigration() {
    // --- Registry helpers ---
    MonitorSettingsMap registry;

    MonitorInfo primary = {};
    primary.friendlyName = L"MNN";
    primary.devicePath = L"\\\\?\\DISPLAY#MNN#1";
    primary.name = L"\\\\.\\DISPLAY1";
    primary.isPrimary = true;

    MonitorInfo teleprompter = {};
    teleprompter.friendlyName = L"TPM";
    teleprompter.devicePath = L"\\\\?\\DISPLAY#TPM#2";
    teleprompter.name = L"\\\\.\\DISPLAY2";
    teleprompter.isPrimary = false;

    EXPECT_TRUE(FindMonitorSettings(registry, primary) == nullptr, "no entry on empty registry");
    EXPECT_EQ(GetMonitorSettings(registry, primary).showOverlay, true, "default shows overlay");
    EXPECT_EQ(GetMonitorSettings(registry, primary).flip, false, "default not flipped");

    PerMonitorSettings tp;
    tp.flip = true;
    tp.showOverlay = false;
    SetMonitorSettings(registry, teleprompter.name, tp);  // store under legacy GDI key
    EXPECT_EQ(registry.size(), size_t(1), "entry stored once");
    EXPECT_TRUE(FindMonitorSettings(registry, teleprompter) != nullptr, "find by MonitorInfo resolves legacy key");
    SetMonitorSettings(registry, teleprompter, tp);       // canonical write collapses the key
    EXPECT_EQ(registry.size(), size_t(1), "no duplicate entries for one monitor");
    EXPECT_TRUE(FindMonitorSettings(registry, std::wstring(L"TPM")) != nullptr, "entry keyed by stable id");
    SetMonitorSettings(registry, teleprompter, PerMonitorSettings{});
    EXPECT_TRUE(registry.empty(), "default settings drop the entry");

    // --- Overlay policy ---
    PerMonitorSettings flipOnly;
    flipOnly.flip = true;
    SetMonitorSettings(registry, teleprompter, flipOnly);
    EXPECT_EQ(IsOverlayAllowedOn(registry, primary), true, "watermark allowed on primary");
    EXPECT_EQ(IsOverlayAllowedOn(registry, teleprompter), false, "watermark hidden on flipped screen");

    PerMonitorSettings primaryFlip;
    primaryFlip.flip = true;
    SetMonitorSettings(registry, primary, primaryFlip);
    EXPECT_EQ(IsOverlayAllowedOn(registry, primary), true, "flipped primary keeps watermark");

    registry.clear();
    PerMonitorSettings optedOut;
    optedOut.showOverlay = false;
    SetMonitorSettings(registry, teleprompter, optedOut);
    EXPECT_EQ(IsOverlayAllowedOn(registry, teleprompter), false, "explicit opt-out hides watermark");

    // --- Legacy flip.monitors array migration (config v1) ---
    fs::path tmpCfg = fs::temp_directory_path() / L"vo_test_monitors_config.json";
    fs::remove(tmpCfg);
    {
        std::ofstream out(tmpCfg);
        out << R"({
  "$schema": "virtual-overlay-config-v1",
  "flip": { "enabled": true, "monitors": ["MNN", "\\\\.\\DISPLAY2"] }
})";
    }

    auto& cfg = Config::Instance();
    cfg.Reset();
    EXPECT_TRUE(cfg.Load(tmpCfg.wstring()), "load legacy config");
    EXPECT_EQ(cfg.Get().monitors.size(), size_t(2), "legacy flip list migrated to registry");
    const PerMonitorSettings* migrated = FindMonitorSettings(cfg.Get().monitors, std::wstring(L"MNN"));
    EXPECT_TRUE(migrated != nullptr, "migrated entry present");
    EXPECT_EQ(migrated ? migrated->flip : false, true, "migrated entry keeps flip");
    EXPECT_EQ(migrated ? migrated->showOverlay : false, true, "migrated entry keeps overlay default");

    // --- Registry round-trip ---
    PerMonitorSettings optOut;
    optOut.flip = false;
    optOut.showOverlay = false;
    cfg.GetMutable().monitors[L"TPM"] = optOut;
    EXPECT_TRUE(cfg.Save(tmpCfg.wstring()), "save registry config");
    cfg.Reset();
    EXPECT_TRUE(cfg.Load(tmpCfg.wstring()), "reload registry config");
    const PerMonitorSettings* reloaded = FindMonitorSettings(cfg.Get().monitors, std::wstring(L"TPM"));
    EXPECT_EQ(reloaded ? reloaded->showOverlay : true, false, "opt-out persisted");
    EXPECT_EQ(reloaded ? reloaded->flip : true, false, "flip flag persisted");

    fs::remove(tmpCfg);
    fs::remove(fs::path(tmpCfg.wstring() + L".bak"));
}

static void TestWatermarkOnlyConfiguration() {
    using nlohmann::json;
    auto& config = Config::Instance();
    fs::path file = fs::temp_directory_path() /
        (L"vo_watermark_only_" + std::to_wstring(GetCurrentProcessId()) + L".json");
    struct Cleanup {
        fs::path file;
        AppConfig original;
        ~Cleanup() {
            Config::Instance().GetMutable() = original;
            for (const auto& path : { file, fs::path(file.wstring() + L".bak") }) {
                std::error_code error;
                fs::remove(path, error);
                if (error) {
                    std::printf("  Failed to clean watermark test config: %s\n", error.message().c_str());
                    ++g_failed;
                }
            }
        }
    } cleanup{ file, config.Get() };

    for (const char* mode : { "notification", "watermark" }) {
        json legacy = {
            { "overlay", {
                { "mode", mode }, { "enabled", true }, { "autoHide", true }, { "autoHideDelayMs", 500 },
                { "position", "bottom-left" }, { "monitor", "primary" }, { "format", "{name}" },
                { "watermarkFontSize", 96 }, { "watermarkOpacity", 0.4f }, { "watermarkColor", "#AABBCC" },
                { "dodgeOnHover", true }, { "dodgeProximity", 80 }, { "dodgeSticky", true },
                { "style", { { "blur", "acrylic" }, { "tintOpacity", 5.0f }, { "cornerRadius", 100 }, { "padding", 18 } } },
                { "text", { { "fontFamily", "Segoe UI" }, { "fontWeight", 500 }, { "fontSize", 30 }, { "color", "#FFFFFF" } } },
                { "animation", { { "fadeInDurationMs", 150 }, { "fadeOutDurationMs", 200 }, { "slideIn", true } } }
            } },
            { "presenter", { { "fontSize", 40 }, { "captionWidth", 900 } } },
            { "monitors", { { "TPM", { { "flip", true }, { "overlay", false } } } } }
        };
        {
            std::ofstream out(file);
            out << legacy.dump(2);
            EXPECT_TRUE(out.good(), "write legacy overlay config");
        }
        config.Reset();
        EXPECT_TRUE(config.Load(file.wstring()), "load either legacy overlay mode");
        EXPECT_TRUE(config.Validate(), "retired popup options cannot invalidate watermark settings");
        const auto& overlay = config.Get().overlay;
        EXPECT_EQ(overlay.enabled, true, "legacy watermark remains enabled");
        EXPECT_EQ(overlay.position, OverlayPosition::BottomLeft, "position preserved");
        EXPECT_EQ(overlay.monitor, MonitorSelection::Primary, "monitor choice preserved");
        EXPECT_EQ(overlay.watermarkFontSize, 96, "watermark font size preserved");
        EXPECT_TRUE(std::fabs(overlay.watermarkOpacity - 0.4f) < 1e-4f, "watermark opacity preserved");
        EXPECT_EQ(overlay.watermarkColor, uint32_t(0xAABBCC), "watermark color preserved");
        EXPECT_EQ(overlay.dodgeOnHover, true, "dodge preference preserved");
        EXPECT_EQ(overlay.dodgeSticky, true, "sticky dodge preference preserved");
        EXPECT_EQ(overlay.style.padding, 18, "watermark padding preserved");
        EXPECT_EQ(overlay.text.fontFamily, std::wstring(L"Segoe UI"), "watermark font family preserved");
        EXPECT_EQ(overlay.text.fontWeight, 500, "watermark weight preserved");
        EXPECT_EQ(config.Get().presenter.fontSize, 40, "Presenter configuration preserved");
        EXPECT_EQ(config.Get().monitors.at(L"TPM").showOverlay, false, "screen opt-out preserved");
        EXPECT_EQ(config.Get().monitors.at(L"TPM").flip, true, "screen flip preserved");
        EXPECT_TRUE(config.Save(file.wstring()), "save watermark-only configuration");
        json saved;
        {
            std::ifstream input(file);
            input >> saved;
        }
        const auto& savedOverlay = saved.at("overlay");
        for (const char* key : { "mode", "autoHide", "autoHideDelayMs", "animation" }) {
            EXPECT_TRUE(!savedOverlay.contains(key), "saved configuration omits retired popup options");
        }
        EXPECT_EQ(savedOverlay.at("style"), json({ { "padding", 18 } }), "only watermark style is saved");
        EXPECT_TRUE(!savedOverlay.at("text").contains("fontSize") && !savedOverlay.at("text").contains("color"),
                    "unused popup text options are omitted");
        config.Reset();
        EXPECT_TRUE(config.Load(file.wstring()), "reload watermark-only config without a mode field");
        EXPECT_EQ(config.Get().overlay.format, std::wstring(L"{name}"), "watermark format round-trips");
    }

    config.GetMutable().overlay.enabled = false;
    EXPECT_TRUE(config.Save(file.wstring()), "save disabled watermark");
    config.Reset();
    EXPECT_TRUE(config.Load(file.wstring()), "reload disabled watermark");
    EXPECT_EQ(config.Get().overlay.enabled, false, "disabled watermark is not force-enabled");
}

static void TestPersistentWatermarkLifecycle() {
    auto& config = Config::Instance();
    auto& overlay = OverlayWindow::Instance();
    struct Cleanup {
        AppConfig original;
        ~Cleanup() {
            OverlayWindow::Instance().Hide();
            OverlayWindow::Instance().Shutdown();
            Config::Instance().GetMutable() = original;
        }
    } cleanup{ config.Get() };
    config.Reset();
    Monitor::Instance().Refresh();
    EXPECT_TRUE(overlay.Init(GetModuleHandleW(nullptr)), "watermark window initializes");
    OverlaySettings settings;
    settings.monitor = MonitorSelection::Primary;
    settings.watermarkFontSize = 24;
    settings.format = L"{name}";
    overlay.ApplySettings(settings);
    overlay.Show(1, L"Watermark test");
    EXPECT_EQ(overlay.GetState(), OverlayState::Visible, "watermark appears immediately without fading");
    EXPECT_TRUE(IsWindowVisible(overlay.GetHwnd()), "watermark is physically visible");

    ULONGLONG deadline = GetTickCount64() + 1250;
    while (GetTickCount64() < deadline) {
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    EXPECT_TRUE(IsWindowVisible(overlay.GetHwnd()), "watermark remains visible beyond the old 500ms auto-hide delay");
    EXPECT_EQ(overlay.GetState(), OverlayState::Visible, "watermark does not transition into a fade-out");

    RECT before = {}, after = {};
    GetWindowRect(overlay.GetHwnd(), &before);
    overlay.Show(2, L"A longer renamed desktop watermark");
    GetWindowRect(overlay.GetHwnd(), &after);
    EXPECT_TRUE(after.right - after.left > before.right - before.left, "desktop changes update watermark text dimensions");

    const MonitorInfo* primary = Monitor::Instance().GetPrimary();
    EXPECT_TRUE(primary != nullptr, "primary monitor available");
    PerMonitorSettings optOut;
    optOut.showOverlay = false;
    SetMonitorSettings(config.GetMutable().monitors, *primary, optOut);
    overlay.CheckPosition();
    EXPECT_TRUE(!IsWindowVisible(overlay.GetHwnd()) && overlay.IsVisible(),
                "screen opt-out hides the watermark while retaining its logical state");
    SetMonitorSettings(config.GetMutable().monitors, *primary, PerMonitorSettings{});
    overlay.CheckPosition();
    EXPECT_TRUE(IsWindowVisible(overlay.GetHwnd()), "watermark returns when screen opt-out is removed");

    settings.enabled = false;
    overlay.ApplySettings(settings);
    EXPECT_EQ(overlay.GetState(), OverlayState::Hidden, "disabling watermark hides immediately");
    settings.enabled = true;
    overlay.ApplySettings(settings);
    EXPECT_TRUE(IsWindowVisible(overlay.GetHwnd()), "reenabling watermark shows it immediately");
    overlay.Hide();
    EXPECT_TRUE(!IsWindowVisible(overlay.GetHwnd()), "visibility toggle hides watermark without animation");
}

static void TestPresenterWindowAndControllerIntegration() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    D2DRenderer::Instance().Init();
    Monitor::Instance().Refresh();

    PresenterWindow win;
    EXPECT_TRUE(win.Init(hInst), "PresenterWindow Init");

    PresenterConfig cfg;
    cfg.fontSize = 28;
    cfg.captionWidth = 700;
    cfg.previousLines = 1;
    cfg.upcomingLines = 2;

    std::wstring multiLineScript =
        L"First line of the teleprompter presentation script.\n"
        L"Second line advances automatically as you speak.\n"
        L"Third line stays ready below the fixed reading line.\n"
        L"Fourth line concludes the demonstration.";

    ScriptModel model = ScriptModel::Build(multiLineScript, true);
    win.ApplyConfig(cfg);
    win.SetScriptModel(model, -1);

    EXPECT_TRUE(win.GetLineCount() >= 4, "script wrapped into >= 4 lines");
    EXPECT_EQ(win.GetActiveLineIndex(), 0, "starts on line 0");

    // Simulate speech recognition advancing into the second line
    ScriptAlignmentEngine engine(model);
    auto upd1 = engine.Process("first line of the teleprompter presentation script");
    win.SetActiveToken(upd1.tokenIndex, upd1.state, true);
    EXPECT_EQ(win.GetActiveLineIndex(), 0, "still on line 0 at end of first line");

    auto upd2 = engine.Process("second line advances automatically");
    win.SetActiveToken(upd2.tokenIndex, upd2.state, true);
    EXPECT_TRUE(win.GetActiveLineIndex() >= 1, "advanced to line 1+");

    // Manual line selection updates alignment anchor
    int line2FirstTok = win.GetFirstTokenForLine(2);
    EXPECT_TRUE(line2FirstTok > 0, "line 2 has valid first token");
    engine.SeekToToken(line2FirstTok);
    win.SetActiveToken(engine.CurrentTokenIndex(), engine.State(), true);
    EXPECT_EQ(win.GetActiveLineIndex(), 2, "manual seek to line 2 updated active line");

    // Verify bullet formatting, line breaks, and paragraph breaks
    ScriptModel bulletModel = ScriptModel::Build(
        L"Introductory sentence before bullets:\n"
        L"- First bullet item.\n"
        L"- Second bullet item.\n\n"
        L"Closing paragraph after a blank line.",
        true
    );
    win.SetScriptModel(bulletModel, -1);
    auto previews = win.GetLinePreviews();
    EXPECT_EQ(previews.size(), size_t(4), "4 non-blank visual lines for intro + 2 bullets + closing paragraph");
    EXPECT_TRUE(previews[1].find(L"\u2022 First bullet item.") == 0, "bullet 1 prefixed with bullet symbol");
    EXPECT_TRUE(previews[2].find(L"\u2022 Second bullet item.") == 0, "bullet 2 prefixed with bullet symbol");

    // Verify fixed reading-baseline screen Y invariance across fontSize, captionWidth, and script changes
    win.Show();
    RECT rcBefore{};
    GetWindowRect(win.GetHwnd(), &rcBefore);
    int baselineScreenYBefore = rcBefore.top + static_cast<int>(std::round(win.GetReadingBaselineLocalY()));

    cfg.fontSize = 44;
    cfg.captionWidth = 960;
    win.ApplyConfig(cfg);
    RECT rcAfterFont{};
    GetWindowRect(win.GetHwnd(), &rcAfterFont);
    int baselineScreenYAfterFont = rcAfterFont.top + static_cast<int>(std::round(win.GetReadingBaselineLocalY()));
    EXPECT_EQ(baselineScreenYAfterFont, baselineScreenYBefore,
              "reading baseline screen Y invariant when fontSize and captionWidth change");

    ScriptModel anotherModel = ScriptModel::Build(L"A completely different short script.", true);
    win.SetScriptModel(anotherModel, -1);
    RECT rcAfterScript{};
    GetWindowRect(win.GetHwnd(), &rcAfterScript);
    int baselineScreenYAfterScript = rcAfterScript.top + static_cast<int>(std::round(win.GetReadingBaselineLocalY()));
    EXPECT_EQ(baselineScreenYAfterScript, baselineScreenYBefore,
              "reading baseline screen Y invariant when script content changes");

    // Verify passive click-through and no-activate styles on HWND
    LONG_PTR exStyle = GetWindowLongPtrW(win.GetHwnd(), GWL_EXSTYLE);
    EXPECT_TRUE((exStyle & WS_EX_LAYERED) != 0, "WS_EX_LAYERED set");
    EXPECT_TRUE((exStyle & WS_EX_NOACTIVATE) != 0, "WS_EX_NOACTIVATE set");
    EXPECT_TRUE((exStyle & WS_EX_TRANSPARENT) != 0, "WS_EX_TRANSPARENT set in passive mode");

    win.Shutdown();
}

static HWND SelectSettingsPage(HWND window, int index) {
    HWND tab = GetDlgItem(window, IDC_TAB_CONTROL);
    TabCtrl_SetCurSel(tab, index);
    NMHDR notification = { tab, IDC_TAB_CONTROL, TCN_SELCHANGE };
    SendMessageW(window, WM_NOTIFY, IDC_TAB_CONTROL, reinterpret_cast<LPARAM>(&notification));
    for (HWND page = FindWindowExW(window, nullptr, L"VirtualOverlaySettingsPage", nullptr);
         page; page = FindWindowExW(window, page, L"VirtualOverlaySettingsPage", nullptr)) {
        if (IsWindowVisible(page)) return page;
    }
    return nullptr;
}

static LRESULT CALLBACK SettingsDpiTestProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR subclassId, DWORD_PTR data) {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, SettingsDpiTestProc, subclassId);
    }
    LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_GETMINMAXINFO) {
        // Simulating DPI does not enlarge the CI runner's 1024x768 desktop.
        // Model the corresponding viewport without clipping it to that desktop.
        const auto* viewport = reinterpret_cast<const SIZE*>(data);
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMaxTrackSize = { viewport->cx, viewport->cy };
    }
    return result;
}

static void TestSettingsTypographyAndDpiLayout() {
    auto& settings = SettingsWindow::Instance();
    struct Cleanup {
        SIZE viewport = { 700, 500 };
        ~Cleanup() { SettingsWindow::Instance().Shutdown(); }
    } cleanup;
    std::wstring originalFormat = Config::Instance().Get().overlay.format;
    EXPECT_TRUE(settings.Init(GetModuleHandleW(nullptr), nullptr), "settings initialize");
    settings.Open();
    HWND window = settings.GetHwnd();
    EXPECT_TRUE(window != nullptr && settings.IsOpen(), "settings open");
    EXPECT_TRUE(SetWindowSubclass(window, SettingsDpiTestProc, 1,
                                  reinterpret_cast<DWORD_PTR>(&cleanup.viewport)),
                "synthetic DPI viewport is independent of the physical test desktop");

    HWND overlay = SelectSettingsPage(window, 1);
    EXPECT_TRUE(overlay != nullptr, "overlay page visible");
    for (int retiredControl : { 301, 309, 310, 320 }) {
        EXPECT_TRUE(GetDlgItem(overlay, retiredControl) == nullptr, "mode and auto-hide controls are removed");
    }
    SetDlgItemTextW(overlay, IDC_OVL_FORMAT, L"Unsaved DPI test");
    HWND presenter = SelectSettingsPage(window, 3);
    EXPECT_TRUE(presenter != nullptr, "presenter page visible");
    SendDlgItemMessageW(presenter, IDC_PRES_FONT_SIZE, TBM_SETPOS, TRUE, 44);
    SendDlgItemMessageW(presenter, IDC_PRES_LINES_COMBO, CB_SETCURSEL, 3, 0);

    bool captionsFit = true;
    for (UINT dpi : { 96u, 120u, 144u, 192u, 96u, 144u, 96u }) {
        int selectedTab = TabCtrl_GetCurSel(GetDlgItem(window, IDC_TAB_CONTROL));
        RECT suggested = {};
        GetWindowRect(window, &suggested);
        suggested.right = suggested.left + MulDiv(700, dpi, USER_DEFAULT_SCREEN_DPI);
        suggested.bottom = suggested.top + MulDiv(500, dpi, USER_DEFAULT_SCREEN_DPI);
        cleanup.viewport = { suggested.right - suggested.left, suggested.bottom - suggested.top };
        SendMessageW(window, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
        EXPECT_EQ(TabCtrl_GetCurSel(GetDlgItem(window, IDC_TAB_CONTROL)), selectedTab,
                  "selected tab survives DPI changes");

        HFONT font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        LOGFONTW actual = {};
        EXPECT_TRUE(GetObjectW(font, sizeof(actual), &actual) != 0, "settings font is valid");
        NONCLIENTMETRICSW metrics = { sizeof(metrics) };
        EXPECT_TRUE(SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi),
                    "native UI metrics available");
        EXPECT_EQ(actual.lfHeight, metrics.lfMessageFont.lfHeight, "font follows native monitor DPI");
        EXPECT_TRUE(wcscmp(actual.lfFaceName, metrics.lfMessageFont.lfFaceName) == 0,
                    "settings use the native UI font face");
        for (int id : { IDC_TAB_CONTROL, IDC_BTN_OK, IDC_BTN_CANCEL, IDC_BTN_APPLY }) {
            EXPECT_EQ(reinterpret_cast<HFONT>(SendDlgItemMessageW(window, id, WM_GETFONT, 0, 0)), font,
                      "tabs and footer buttons share the same font");
        }

        for (int tabIndex = 0; tabIndex < 5; ++tabIndex) {
            HWND page = SelectSettingsPage(window, tabIndex);
            EXPECT_TRUE(page != nullptr, "each settings tab is accessible");
            constexpr int pageControls[] = {
                IDC_GEN_START_WINDOWS, IDC_OVL_ENABLE, IDC_FLIP_ENABLE, IDC_PRES_ENABLE, IDC_ABOUT_VERSION
            };
            EXPECT_TRUE(GetDlgItem(page, pageControls[tabIndex]) != nullptr, "selected tab shows the correct page");
            RECT pageClient = {};
            GetClientRect(page, &pageClient);
            for (HWND control = GetWindow(page, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT)) {
                RECT bounds = {};
                GetWindowRect(control, &bounds);
                MapWindowPoints(HWND_DESKTOP, page, reinterpret_cast<LPPOINT>(&bounds), 2);
                if (bounds.left < 0 || bounds.top < 0 ||
                    bounds.right > pageClient.right || bounds.bottom > pageClient.bottom) {
                    RECT windowBounds = {};
                    GetWindowRect(window, &windowBounds);
                    std::printf("  Settings overflow: target DPI %u, actual DPI %u, tab %d, control %d, "
                                "bounds (%ld,%ld)-(%ld,%ld), page %ldx%ld, window %ldx%ld\n",
                                dpi, GetDpiForWindow(window), tabIndex, GetDlgCtrlID(control),
                                bounds.left, bounds.top, bounds.right, bounds.bottom,
                                pageClient.right, pageClient.bottom,
                                windowBounds.right - windowBounds.left, windowBounds.bottom - windowBounds.top);
                }
                EXPECT_TRUE(bounds.left >= 0 && bounds.top >= 0 &&
                            bounds.right <= pageClient.right && bounds.bottom <= pageClient.bottom,
                            "settings control stays inside its page");
                wchar_t className[32] = {};
                GetClassNameW(control, className, 32);
                bool isLabel = wcscmp(className, L"Static") == 0;
                bool isButton = wcscmp(className, L"Button") == 0;
                bool isCombo = wcscmp(className, WC_COMBOBOXW) == 0;
                if (isLabel || isButton || isCombo || wcscmp(className, L"Edit") == 0 ||
                    wcscmp(className, WC_LINK) == 0) {
                    EXPECT_EQ(reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)), font,
                              "all settings controls share the DPI-correct font");
                }
                if ((!isLabel && !isButton) || GetWindowTextLengthW(control) == 0) continue;

                wchar_t text[1024] = {};
                GetWindowTextW(control, text, 1024);
                HDC dc = GetDC(control);
                HGDIOBJ previous = SelectObject(dc, font);
                RECT available = {};
                GetClientRect(control, &available);
                UINT format = DT_CALCRECT | DT_NOPREFIX;
                if (isButton) {
                    LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
                    bool checkbox = (style & BS_TYPEMASK) == BS_AUTOCHECKBOX;
                    available.right -= checkbox ? GetSystemMetricsForDpi(SM_CXMENUCHECK, dpi) + MulDiv(6, dpi, 96)
                                                : MulDiv(12, dpi, 96);
                    format |= DT_SINGLELINE;
                } else {
                    format |= DT_WORDBREAK;
                }
                RECT measured = available;
                DrawTextW(dc, text, -1, &measured, format);
                SelectObject(dc, previous);
                ReleaseDC(control, dc);
                if (measured.right > available.right || measured.bottom > available.bottom) {
                    std::printf("  Clipped settings text at %u DPI: %ls (needs %ldx%ld; available %ldx%ld)\n",
                                dpi, text, measured.right, measured.bottom, available.right, available.bottom);
                    captionsFit = false;
                }
            }
        }

        RECT slider = {};
        HWND fontSize = GetDlgItem(presenter, IDC_PRES_FONT_SIZE);
        GetWindowRect(fontSize, &slider);
        MapWindowPoints(HWND_DESKTOP, presenter, reinterpret_cast<LPPOINT>(&slider), 2);
        EXPECT_EQ(slider.left, MulDiv(123, dpi, 96), "slider alignment scales without accumulated rounding");
        EXPECT_EQ(slider.right - slider.left, MulDiv(230, dpi, 96), "slider width scales without drift");
        EXPECT_EQ(SendMessageW(fontSize, TBM_GETPOS, 0, 0), LRESULT(44), "unsaved slider value survives DPI changes");
        EXPECT_EQ(SendDlgItemMessageW(presenter, IDC_PRES_LINES_COMBO, CB_GETCURSEL, 0, 0), LRESULT(3),
                  "unsaved combo selection survives DPI changes");
        wchar_t format[64] = {};
        GetDlgItemTextW(overlay, IDC_OVL_FORMAT, format, 64);
        EXPECT_TRUE(wcscmp(format, L"Unsaved DPI test") == 0, "unsaved edit survives DPI changes");

        for (int id : { IDC_PRES_CHANNEL_COMBO, IDC_PRES_LINES_COMBO }) {
            HWND combo = GetDlgItem(presenter, id);
            COMBOBOXINFO info = { sizeof(info) };
            EXPECT_TRUE(GetComboBoxInfo(combo, &info), "combo text area available");
            wchar_t text[256] = {};
            GetWindowTextW(combo, text, 256);
            HDC dc = GetDC(combo);
            HGDIOBJ previous = SelectObject(dc, font);
            SIZE textSize = {};
            GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &textSize);
            SelectObject(dc, previous);
            ReleaseDC(combo, dc);
            EXPECT_TRUE(textSize.cx <= info.rcItem.right - info.rcItem.left,
                        "Presenter dropdown selection fits beside its arrow");
        }
    }
    EXPECT_TRUE(captionsFit, "settings captions fit without truncation");

    SendMessageW(window, WM_COMMAND, IDC_BTN_CANCEL, 0);
    EXPECT_TRUE(!settings.IsOpen(), "settings close cleanly");
    settings.Open();
    EXPECT_TRUE(settings.IsOpen(), "settings reopen with a fresh font and layout");
    overlay = SelectSettingsPage(settings.GetHwnd(), 1);
    wchar_t restored[256] = {};
    GetDlgItemTextW(overlay, IDC_OVL_FORMAT, restored, 256);
    EXPECT_EQ(std::wstring(restored), originalFormat, "Cancel still discards unsaved settings");
}

static void TestVoskDllAndPresenterControllerLifecycle() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    auto& ctrl = PresenterController::Instance();
    EXPECT_TRUE(ctrl.Init(hInst, nullptr), "PresenterController Init");

    // Load UTF-8 markdown script file
    fs::path tmpMd = fs::temp_directory_path() / L"vo_test_script.md";
    {
        std::ofstream out(tmpMd, std::ios::binary);
        out << "Welcome to Virtual Overlay native presenter.\n"
            << "This second line tests file loading and close recovery.\n"
            << "This third line tests position preservation.\n";
    }
    EXPECT_TRUE(ctrl.LoadScriptFromFile(tmpMd.wstring()), "LoadScriptFromFile UTF-8 .md");
    fs::remove(tmpMd);

    // Seek to token 7 and open presenter
    ctrl.SeekToToken(7);
    EXPECT_EQ(ctrl.GetCurrentTokenIndex(), 7, "SeekToToken(7)");
    ctrl.Open();
    EXPECT_EQ(ctrl.IsOpen(), true, "presenter visible after Open()");

    // Pause / Resume toggle
    EXPECT_EQ(ctrl.IsPaused(), false, "initially not paused");
    ctrl.TogglePause();
    EXPECT_EQ(ctrl.IsPaused(), true, "paused after TogglePause");
    // While paused, hypotheses do not advance the script
    ctrl.ProcessHypothesisForTest("this third line tests position preservation", false);
    EXPECT_EQ(ctrl.GetCurrentTokenIndex(), 7, "paused presenter ignores speech hypotheses");

    ctrl.TogglePause();
    EXPECT_EQ(ctrl.IsPaused(), false, "resumed after second TogglePause");
    // Once resumed, hypotheses advance the script
    ctrl.ProcessHypothesisForTest("second line tests file loading and close recovery", false);
    EXPECT_TRUE(ctrl.GetCurrentTokenIndex() > 7, "resumed presenter advances on speech hypothesis");
    int savedTok = ctrl.GetCurrentTokenIndex();

    // Close presenter: stops audio capture, releases recognizer, preserves script & token position
    ctrl.Close();
    EXPECT_EQ(ctrl.IsOpen(), false, "presenter hidden after Close()");
    EXPECT_EQ(AudioInputManager::Instance().GetState(), AudioCaptureState::Stopped,
              "audio capture stopped on Close()");
    EXPECT_EQ(ctrl.GetRecognizerStatus(), RecognizerStatus::Uninitialized,
              "speech recognizer released on Close()");
    EXPECT_EQ(ctrl.GetCurrentTokenIndex(), savedTok, "reading position preserved on Close()");

    // Reopen presenter: restores script and preserved reading position
    ctrl.Open();
    EXPECT_EQ(ctrl.IsOpen(), true, "presenter reopened via Open()");
    EXPECT_EQ(ctrl.GetCurrentTokenIndex(), savedTok, "reading position restored on reopen");
    ctrl.Close();

    ctrl.Shutdown();
}

int main(int argc, char* argv[]) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::printf("=== Virtual Overlay Presenter Test Suite ===\n");

    if (argc == 2 && std::string(argv[1]) == "--watermark-only") {
        RunTest("Watermark-only legacy configuration migration", TestWatermarkOnlyConfiguration);
        RunTest("Persistent watermark lifecycle and monitor policy", TestPersistentWatermarkLifecycle);
        RunTest("Per-monitor registry and legacy migration", TestMonitorRegistryAndLegacyMigration);
        std::printf("Passed: %d | Failed: %d\n", g_passed, g_failed);
        CoUninitialize();
        return (g_failed == 0) ? 0 : 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--settings-only") {
        RunTest("Settings native typography, control fit, DPI changes, and unsaved state",
                TestSettingsTypographyAndDpiLayout);
        CoUninitialize();
        return (g_failed == 0) ? 0 : 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--settings-preview") {
        auto& settings = SettingsWindow::Instance();
        if (!settings.Init(GetModuleHandleW(nullptr), nullptr)) return 1;
        settings.Open();
        if (!settings.IsOpen()) return 1;
        SelectSettingsPage(settings.GetHwnd(), 3);
        MSG message = {};
        while (settings.IsOpen() && GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(settings.GetHwnd(), &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        settings.Shutdown();
        CoUninitialize();
        return 0;
    }

    RunTest("Bundled Vosk runtime and required exports", TestBundledVoskRuntime);
    if (argc == 2 && std::string(argv[1]) == "--vosk-runtime-only") {
        std::printf("Passed: %d | Failed: %d\n", g_passed, g_failed);
        CoUninitialize();
        return (g_failed == 0) ? 0 : 1;
    }

    RunTest("NumberExpander cardinal expansion", TestNumberExpander);
    RunTest("TextNormalizer punctuation, numbers, and spans", TestTextNormalizer);
    RunTest("ScriptAlignmentEngine first-word fast path & single-word guard", TestFirstWordFastPathAndSingleWordGuard);
    RunTest("ScriptAlignmentEngine sequential reading & partial hypotheses", TestCleanSequentialReadAndPartials);
    RunTest("ScriptAlignmentEngine pause holds & resume advances", TestPauseHoldsAndFirstWordAfterPauseAdvances);
    RunTest("ScriptAlignmentEngine off-script adlib & repeated words", TestOffScriptAdlibAndRepeatedWords);
    RunTest("ScriptAlignmentEngine skip-ahead & backward retake recovery", TestSkipAheadAndBackwardRecovery);
    RunTest("ScriptAlignmentEngine numbers, SeekToToken, Reset & SmoothDamp", TestNumbersSeekResetAndSmoothDamp);
    RunTest("AudioResampler format, channel selection & 48k->16k resampling", TestAudioResamplerAndChannelSelection);
    RunTest("AudioInputManager enumeration & unavailable device handling", TestAudioDeviceEnumerationAndMissingDeviceHandling);
    RunTest("OfflineSpeechRecognizer grammar builder & model validation", TestVoskGrammarAndModelValidation);
    RunTest("Config presenter & per-monitor anchor persistence round-trip", TestConfigPersistenceRoundTrip);
    RunTest("Per-monitor registry, overlay opt-out & legacy flip migration", TestMonitorRegistryAndLegacyMigration);
    RunTest("Watermark-only legacy configuration migration", TestWatermarkOnlyConfiguration);
    RunTest("Persistent watermark lifecycle and monitor policy", TestPersistentWatermarkLifecycle);
    RunTest("Settings native typography, control fit, DPI changes, and unsaved state",
            TestSettingsTypographyAndDpiLayout);
    RunTest("PresenterWindow layout, speech advancement & manual line seek", TestPresenterWindowAndControllerIntegration);
    RunTest("Vosk DLL loading & PresenterController close/reopen lifecycle", TestVoskDllAndPresenterControllerLifecycle);

    std::printf("============================================\n");
    std::printf("Passed: %d | Failed: %d\n", g_passed, g_failed);

    CoUninitialize();
    return (g_failed == 0) ? 0 : 1;
}

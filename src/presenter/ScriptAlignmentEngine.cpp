// Portions adapted from TalkPrompter (https://github.com/Sven-Bo/talkprompter)
// MIT License — Copyright (c) 2026 Sven Bosau (Bosau Digital LLC)

#include "ScriptAlignmentEngine.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <unordered_set>

namespace VirtualOverlay {

// =============================================================================
// NumberExpander
// =============================================================================

static const char* const kOnes[] = {
    "zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
    "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
    "sixteen", "seventeen", "eighteen", "nineteen"
};

static const char* const kTens[] = {
    "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"
};

struct ScaleEntry {
    int64_t value;
    const char* name;
};

static const ScaleEntry kScales[] = {
    { 1000000000LL, "billion" },
    { 1000000LL,    "million" },
    { 1000LL,       "thousand" }
};

bool NumberExpander::IsAllDigits(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

static void AppendThreeDigits(int64_t n, std::vector<std::string>& output) {
    if (n >= 100) {
        output.emplace_back(kOnes[n / 100]);
        output.emplace_back("hundred");
        n %= 100;
    }

    if (n >= 20) {
        output.emplace_back(kTens[n / 10]);
        n %= 10;
        if (n > 0) {
            output.emplace_back(kOnes[n]);
        }
    } else if (n > 0) {
        output.emplace_back(kOnes[n]);
    }
}

static void AppendNumber(int64_t n, std::vector<std::string>& output) {
    if (n == 0) {
        output.emplace_back("zero");
        return;
    }

    if (n < 0) {
        n = -n;
    }

    for (const auto& scale : kScales) {
        if (n >= scale.value) {
            AppendThreeDigits(n / scale.value, output);
            output.emplace_back(scale.name);
            n %= scale.value;
        }
    }

    if (n > 0) {
        AppendThreeDigits(n, output);
    }
}

std::vector<std::string> NumberExpander::ToWords(const std::string& digits) {
    if (!IsAllDigits(digits) || digits.size() > 15) {
        return { digits };
    }

    int64_t n = 0;
    try {
        size_t idx = 0;
        n = std::stoll(digits, &idx, 10);
        if (idx != digits.size()) {
            return { digits };
        }
    } catch (...) {
        return { digits };
    }

    std::vector<std::string> words;
    AppendNumber(n, words);
    if (words.empty()) {
        words.emplace_back("zero");
    }
    return words;
}

// =============================================================================
// TextNormalizer
// =============================================================================

std::string TextNormalizer::WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring TextNormalizer::Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int size = MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
        nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
        result.data(), size);
    return result;
}

bool TextNormalizer::IsWordChar(wchar_t c) {
    if (c == L'\'' || c == L'\u2019') {
        return true;
    }
    if (iswalnum(c)) {
        return true;
    }
    WORD charType = 0;
    if (GetStringTypeW(CT_CTYPE1, &c, 1, &charType)) {
        if ((charType & (C1_ALPHA | C1_DIGIT)) != 0) {
            return true;
        }
    }
    // Combining marks range (Unicode General Category Mn/Mc)
    if (c >= 0x0300 && c <= 0x036F) {
        return true;
    }
    return false;
}

std::string TextNormalizer::NormalizeWord(const std::wstring& raw) {
    if (raw.empty()) return {};

    std::wstring buf;
    buf.reserve(raw.size());

    for (wchar_t c : raw) {
        if (c == L'\'' || c == L'\u2019') {
            continue;
        }
        if (IsWordChar(c)) {
            wchar_t lower = c;
            CharLowerBuffW(&lower, 1);
            buf.push_back(lower);
        }
    }

    return WideToUtf8(buf);
}

std::string TextNormalizer::NormalizeWordUtf8(const std::string& rawUtf8) {
    return NormalizeWord(Utf8ToWide(rawUtf8));
}

std::vector<ScriptToken> TextNormalizer::Tokenize(const std::wstring& scriptText) {
    std::vector<ScriptToken> tokens;
    if (scriptText.empty()) {
        return tokens;
    }

    const int n = static_cast<int>(scriptText.size());
    int i = 0;
    while (i < n) {
        while (i < n && !IsWordChar(scriptText[static_cast<size_t>(i)])) {
            i++;
        }

        int start = i;
        while (i < n && IsWordChar(scriptText[static_cast<size_t>(i)])) {
            i++;
        }

        int length = i - start;
        if (length > 0) {
            std::wstring slice = scriptText.substr(static_cast<size_t>(start), static_cast<size_t>(length));
            std::string normalized = NormalizeWord(slice);
            if (!normalized.empty()) {
                ScriptToken tok;
                tok.index = static_cast<int>(tokens.size());
                tok.normalized = std::move(normalized);
                tok.start = start;
                tok.length = length;
                tokens.push_back(std::move(tok));
            }
        }
    }

    return tokens;
}

std::vector<ScriptToken> TextNormalizer::TokenizeUtf8(const std::string& scriptTextUtf8) {
    return Tokenize(Utf8ToWide(scriptTextUtf8));
}

std::vector<std::string> TextNormalizer::ToMatchWords(const std::wstring& utterance, bool expandNumbers) {
    std::vector<std::string> words;
    if (utterance.empty()) {
        return words;
    }

    const int n = static_cast<int>(utterance.size());
    int i = 0;
    while (i < n) {
        while (i < n && !IsWordChar(utterance[static_cast<size_t>(i)])) {
            i++;
        }

        int start = i;
        while (i < n && IsWordChar(utterance[static_cast<size_t>(i)])) {
            i++;
        }

        if (i > start) {
            std::wstring slice = utterance.substr(static_cast<size_t>(start), static_cast<size_t>(i - start));
            std::string w = NormalizeWord(slice);
            if (w.empty()) {
                continue;
            }

            if (expandNumbers && NumberExpander::IsAllDigits(w)) {
                auto expanded = NumberExpander::ToWords(w);
                for (auto& sub : expanded) {
                    words.push_back(std::move(sub));
                }
            } else {
                words.push_back(std::move(w));
            }
        }
    }

    return words;
}

std::vector<std::string> TextNormalizer::ToMatchWordsUtf8(const std::string& utteranceUtf8, bool expandNumbers) {
    return ToMatchWords(Utf8ToWide(utteranceUtf8), expandNumbers);
}

// =============================================================================
// ScriptModel
// =============================================================================

static std::vector<int> FindParagraphStarts(
    const std::wstring& text,
    const std::vector<ScriptToken>& tokens
) {
    std::vector<int> starts;
    if (tokens.empty()) {
        return starts;
    }

    starts.push_back(0);
    for (size_t t = 1; t < tokens.size(); ++t) {
        int from = tokens[t - 1].End();
        int to = tokens[t].start;
        int newlines = 0;
        for (int i = from; i < to && i < static_cast<int>(text.size()); ++i) {
            if (text[static_cast<size_t>(i)] == L'\n') {
                newlines++;
                if (newlines >= 2) {
                    starts.push_back(static_cast<int>(t));
                    break;
                }
            }
        }
    }

    return starts;
}

ScriptModel ScriptModel::Build(const std::wstring& text, bool expandNumbers) {
    ScriptModel model;
    model.m_text = text;
    model.m_tokens = TextNormalizer::Tokenize(text);
    model.m_tokenToFirstMatchWord.resize(model.m_tokens.size(), 0);

    for (size_t t = 0; t < model.m_tokens.size(); ++t) {
        model.m_tokenToFirstMatchWord[t] = static_cast<int>(model.m_matchWords.size());
        const std::string& norm = model.m_tokens[t].normalized;
        if (expandNumbers && NumberExpander::IsAllDigits(norm)) {
            auto expanded = NumberExpander::ToWords(norm);
            for (auto& sub : expanded) {
                model.m_matchWords.push_back(std::move(sub));
                model.m_matchWordToToken.push_back(static_cast<int>(t));
            }
        } else {
            model.m_matchWords.push_back(norm);
            model.m_matchWordToToken.push_back(static_cast<int>(t));
        }
    }

    model.m_paragraphStartTokens = FindParagraphStarts(text, model.m_tokens);
    return model;
}

ScriptModel ScriptModel::BuildUtf8(const std::string& textUtf8, bool expandNumbers) {
    return Build(TextNormalizer::Utf8ToWide(textUtf8), expandNumbers);
}

int ScriptModel::TokenIndexForMatchWord(int matchWordIndex) const {
    if (m_matchWordToToken.empty()) {
        return -1;
    }
    int clamped = std::clamp(matchWordIndex, 0, static_cast<int>(m_matchWordToToken.size()) - 1);
    return m_matchWordToToken[static_cast<size_t>(clamped)];
}

int ScriptModel::FirstMatchWordForToken(int tokenIndex) const {
    if (m_tokenToFirstMatchWord.empty()) {
        return 0;
    }
    int clamped = std::clamp(tokenIndex, 0, static_cast<int>(m_tokenToFirstMatchWord.size()) - 1);
    return m_tokenToFirstMatchWord[static_cast<size_t>(clamped)];
}

std::vector<std::string> ScriptModel::DistinctVocabulary() const {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    result.reserve(m_matchWords.size());
    for (const auto& w : m_matchWords) {
        if (!w.empty() && seen.insert(w).second) {
            result.push_back(w);
        }
    }
    return result;
}

// =============================================================================
// Levenshtein
// =============================================================================

int Levenshtein::Distance(const std::string& a, const std::string& b) {
    if (a == b) return 0;
    if (a.empty()) return static_cast<int>(b.size());
    if (b.empty()) return static_cast<int>(a.size());

    constexpr size_t kMaxStackCols = 128;
    int stackRow[kMaxStackCols];
    std::vector<int> heapRow;

    const size_t bLen = b.size();
    int* prev = (bLen + 1 <= kMaxStackCols) ? stackRow : nullptr;
    if (!prev) {
        heapRow.resize(bLen + 1);
        prev = heapRow.data();
    }

    for (size_t j = 0; j <= bLen; ++j) {
        prev[j] = static_cast<int>(j);
    }

    for (size_t i = 0; i < a.size(); ++i) {
        int upperLeft = prev[0];
        prev[0] = static_cast<int>(i) + 1;
        char ca = a[i];

        for (size_t j = 0; j < bLen; ++j) {
            int upper = prev[j + 1];
            int cost = (ca == b[j]) ? 0 : 1;
            int del = upper + 1;
            int ins = prev[j] + 1;
            int sub = upperLeft + cost;
            prev[j + 1] = std::min({ del, ins, sub });
            upperLeft = upper;
        }
    }

    return prev[bLen];
}

double Levenshtein::Similarity(const std::string& a, const std::string& b) {
    size_t maxLen = std::max(a.size(), b.size());
    if (maxLen == 0) {
        return 1.0;
    }
    int dist = Distance(a, b);
    return 1.0 - static_cast<double>(dist) / static_cast<double>(maxLen);
}

bool Levenshtein::WordsMatch(const std::string& a, const std::string& b) {
    if (a == b) {
        return true;
    }
    // Allow at most 1 character edit only on longer words (>= 5 chars) to
    // tolerate minor morphological variants without false-matching short words.
    if (a.size() >= 5 && b.size() >= 5 &&
        std::abs(static_cast<int>(a.size()) - static_cast<int>(b.size())) <= 1) {
        return Distance(a, b) <= 1;
    }
    return false;
}

int Levenshtein::WordDistance(
    const std::vector<std::string>& spoken,
    const std::vector<std::string>& scriptWords,
    int scriptStart,
    int scriptLength
) {
    int aCount = static_cast<int>(spoken.size());
    int bCount = std::max(0, scriptLength);
    if (aCount == 0) return bCount;
    if (bCount == 0) return aCount;

    constexpr int kMaxStackWords = 32;
    int stackPrev[kMaxStackWords];
    int stackCurr[kMaxStackWords];
    std::vector<int> heapPrev;
    std::vector<int> heapCurr;

    int* prev = (bCount + 1 <= kMaxStackWords) ? stackPrev : nullptr;
    int* curr = (bCount + 1 <= kMaxStackWords) ? stackCurr : nullptr;
    if (!prev) {
        heapPrev.resize(static_cast<size_t>(bCount + 1));
        heapCurr.resize(static_cast<size_t>(bCount + 1));
        prev = heapPrev.data();
        curr = heapCurr.data();
    }

    for (int j = 0; j <= bCount; ++j) {
        prev[j] = j;
    }

    for (int i = 1; i <= aCount; ++i) {
        curr[0] = i;
        const std::string& sw = spoken[static_cast<size_t>(i - 1)];
        for (int j = 1; j <= bCount; ++j) {
            const std::string& kw = scriptWords[static_cast<size_t>(scriptStart + j - 1)];
            int cost = WordsMatch(sw, kw) ? 0 : 1;
            curr[j] = std::min({ curr[j - 1] + 1, prev[j] + 1, prev[j - 1] + cost });
        }
        std::swap(prev, curr);
    }

    return prev[bCount];
}

double Levenshtein::WordSimilarity(
    const std::vector<std::string>& spoken,
    const std::vector<std::string>& scriptWords,
    int scriptStart,
    int scriptLength
) {
    int maxCount = std::max(static_cast<int>(spoken.size()), std::max(0, scriptLength));
    if (maxCount == 0) {
        return 1.0;
    }
    int dist = WordDistance(spoken, scriptWords, scriptStart, scriptLength);
    return 1.0 - static_cast<double>(dist) / static_cast<double>(maxCount);
}

// =============================================================================
// ScriptAlignmentEngine (adapted from TalkPrompter ScriptMatcher)
// =============================================================================

ScriptAlignmentEngine::ScriptAlignmentEngine(
    ScriptModel model,
    MatcherOptions options,
    bool expandNumbers
)
    : m_model(std::move(model))
    , m_options(options)
    , m_expandNumbers(expandNumbers)
{
}

void ScriptAlignmentEngine::SetModel(ScriptModel model) {
    m_model = std::move(model);
    Reset();
}

int ScriptAlignmentEngine::CurrentTokenIndex() const {
    if (m_anchor <= 0 || m_model.TokenCount() == 0) {
        return -1;
    }
    return m_model.TokenIndexForMatchWord(m_anchor - 1);
}

void ScriptAlignmentEngine::Reset() {
    m_anchor = 0;
    m_noMatchCount = 0;
    m_state = TrackingState::Idle;
}

void ScriptAlignmentEngine::SeekToToken(int tokenIndex) {
    if (m_model.TokenCount() == 0) {
        return;
    }
    int clamped = std::clamp(tokenIndex, 0, m_model.TokenCount() - 1);
    m_anchor = m_model.FirstMatchWordForToken(clamped) + 1;
    m_noMatchCount = 0;
    m_state = TrackingState::Tracking;
}

MatchUpdate ScriptAlignmentEngine::Process(const std::string& hypothesisUtf8) {
    return ProcessWide(TextNormalizer::Utf8ToWide(hypothesisUtf8));
}

MatchUpdate ScriptAlignmentEngine::ProcessWide(const std::wstring& hypothesis) {
    if (m_model.MatchWordCount() == 0) {
        return MatchUpdate{ -1, 0.0, TrackingState::Idle, false };
    }

    auto spoken = TextNormalizer::ToMatchWords(hypothesis, m_expandNumbers);
    if (spoken.empty()) {
        return MatchUpdate{ CurrentTokenIndex(), 0.0, m_state, false };
    }

    if (static_cast<int>(spoken.size()) > m_options.tailWords) {
        spoken.erase(spoken.begin(), spoken.end() - m_options.tailWords);
    }

    // Fast path 1: single spoken word that is the exact next expected word
    const auto& scriptWords = m_model.MatchWords();
    if (spoken.size() == 1
        && m_anchor < m_model.MatchWordCount()
        && spoken[0] == scriptWords[static_cast<size_t>(m_anchor)]) {
        return ApplyHit(WindowHit{ m_anchor + 1, 1.0 }, false);
    }

    // Fast path 2: anchored tail continuation during continuous speech.
    // Only advances +1 or +2 words when the end of the spoken partial is
    // strictly anchored at m_anchor - 1 (the word just read), preventing any
    // possibility of skipping ahead across lines.
    if (spoken.size() >= 2 && m_anchor > 0 && m_anchor < m_model.MatchWordCount()) {
        const std::string& lastSpoken = spoken.back();
        const std::string& prevSpoken = spoken[spoken.size() - 2];

        if (Levenshtein::WordsMatch(prevSpoken, scriptWords[static_cast<size_t>(m_anchor - 1)]) &&
            Levenshtein::WordsMatch(lastSpoken, scriptWords[static_cast<size_t>(m_anchor)])) {
            return ApplyHit(WindowHit{ m_anchor + 1, 1.0 }, false);
        }

        if (spoken.size() >= 3 && m_anchor + 1 < m_model.MatchWordCount()) {
            const std::string& prev2Spoken = spoken[spoken.size() - 3];
            if (Levenshtein::WordsMatch(prev2Spoken, scriptWords[static_cast<size_t>(m_anchor - 1)]) &&
                Levenshtein::WordsMatch(prevSpoken, scriptWords[static_cast<size_t>(m_anchor)]) &&
                Levenshtein::WordsMatch(lastSpoken, scriptWords[static_cast<size_t>(m_anchor + 1)])) {
                return ApplyHit(WindowHit{ m_anchor + 2, 1.0 }, false);
            }
        }
    }

    // Hold rather than risk a spurious jump when fewer than minMatchedWords are present
    if (static_cast<int>(spoken.size()) < m_options.minMatchedWords) {
        return MatchUpdate{ CurrentTokenIndex(), 0.0, m_state, false };
    }

    // 1. Local search around the current anchor
    int winStart = std::max(0, m_anchor - static_cast<int>(spoken.size()) - m_options.backTolerance);
    int winEnd = std::min(m_model.MatchWordCount(), m_anchor + static_cast<int>(spoken.size()) + m_options.forwardWindow);

    WindowHit localHit;
    if (FindBestInWindow(spoken, winStart, winEnd, m_options.acceptThreshold, false, localHit)
        && localHit.newAnchor <= m_anchor + static_cast<int>(spoken.size()) + m_options.maxJump) {
        return ApplyHit(localHit, false);
    }

    m_noMatchCount++;

    // 2. Re-acquisition after persistent non-matching hypotheses
    if (m_noMatchCount >= m_options.lostThreshold) {
        WindowHit forwardHit;
        bool hasForward = false;
        if (static_cast<int>(spoken.size()) >= 3) {
            int forwardEnd = std::min(m_model.MatchWordCount(), m_anchor + m_options.reacquireWindow);
            hasForward = FindBestInWindow(
                spoken, m_anchor, forwardEnd,
                m_options.reacquireThreshold, true, forwardHit);
        }

        WindowHit backHit;
        bool hasBack = false;
        if (static_cast<int>(spoken.size()) >= m_options.reacquireBackMinWords && m_anchor > 0) {
            int backStart = std::max(0, m_anchor - m_options.reacquireBackWindow);
            hasBack = FindBestInWindow(
                spoken, backStart, m_anchor,
                m_options.reacquireThreshold, true, backHit);
        }

        if (hasForward || hasBack) {
            bool pickBack = (hasBack && (!hasForward || backHit.confidence > forwardHit.confidence));
            const WindowHit& best = pickBack ? backHit : forwardHit;
            return ApplyHit(best, true);
        }

        m_state = TrackingState::Lost;
        return MatchUpdate{ CurrentTokenIndex(), 0.0, TrackingState::Lost, false };
    }

    if (m_state == TrackingState::Tracking) {
        m_state = TrackingState::Paused;
    }

    return MatchUpdate{ CurrentTokenIndex(), 0.0, m_state, false };
}

MatchUpdate ScriptAlignmentEngine::ApplyHit(const WindowHit& hit, bool allowBehind) {
    int prevToken = CurrentTokenIndex();
    int targetAnchor = allowBehind ? hit.newAnchor : std::max(m_anchor, hit.newAnchor);
    m_anchor = std::clamp(targetAnchor, 0, m_model.MatchWordCount());
    m_noMatchCount = 0;
    m_state = TrackingState::Tracking;
    int newToken = CurrentTokenIndex();
    return MatchUpdate{
        newToken,
        hit.confidence,
        TrackingState::Tracking,
        newToken > prevToken
    };
}

bool ScriptAlignmentEngine::FindBestInWindow(
    const std::vector<std::string>& spoken,
    int windowStart,
    int windowEndExclusive,
    double threshold,
    bool allowBehind,
    WindowHit& outHit
) const {
    outHit = WindowHit{};
    if (static_cast<int>(spoken.size()) < m_options.minMatchedWords) {
        return false;
    }

    const auto& scriptWords = m_model.MatchWords();
    int scriptCount = m_model.MatchWordCount();
    int spokenCount = static_cast<int>(spoken.size());

    double bestScore = -1.0;
    double bestRawSimilarity = 0.0;
    int bestEnd = m_anchor;

    for (int start = windowStart; start < windowEndExclusive; ++start) {
        int length = std::min(spokenCount, scriptCount - start);
        if (length <= 0) {
            break;
        }

        int candidateEnd = start + length;
        if (!allowBehind && candidateEnd < m_anchor - m_options.backTolerance) {
            continue;
        }

        double sim = Levenshtein::WordSimilarity(spoken, scriptWords, start, length);

        // During normal local tracking (!allowBehind), prevent noisy 2-word
        // partials from jumping more than 3 words ahead of m_anchor. Jumping
        // 4+ words ahead requires at least 3 spoken words and strong similarity.
        if (!allowBehind && (candidateEnd - m_anchor) > 3) {
            if (spokenCount < 3 || sim < 0.72) {
                continue;
            }
        }

        double distancePenalty = allowBehind
            ? (0.0001 * std::abs(candidateEnd - m_anchor))
            : (0.005 * std::abs(candidateEnd - m_anchor));
        double adjusted = sim - distancePenalty;

        if (sim >= threshold && adjusted > bestScore) {
            bestScore = adjusted;
            bestRawSimilarity = sim;
            bestEnd = candidateEnd;
        }
    }

    if (bestScore < 0.0) {
        return false;
    }

    outHit = WindowHit{ bestEnd, bestRawSimilarity };
    return true;
}

double ScriptAlignmentEngine::ScoreCandidate(
    const std::vector<std::string>& spoken,
    int scriptStart,
    int& outConsumed
) const {
    int available = m_model.MatchWordCount() - scriptStart;
    int length = std::min(static_cast<int>(spoken.size()), available);
    if (length <= 0) {
        outConsumed = 0;
        return 0.0;
    }
    outConsumed = length;
    return Levenshtein::WordSimilarity(spoken, m_model.MatchWords(), scriptStart, length);
}

// =============================================================================
// SmoothDamp
// =============================================================================

double SmoothDamp::Step(
    double current,
    double target,
    double& currentVelocity,
    double smoothTime,
    double deltaTime,
    double maxSpeed
) {
    smoothTime = std::max(0.0001, smoothTime);
    double omega = 2.0 / smoothTime;

    double x = omega * deltaTime;
    double exp = 1.0 / (1.0 + x + 0.48 * x * x + 0.235 * x * x * x);

    double change = current - target;
    double originalTo = target;

    double maxChange = maxSpeed * smoothTime;
    change = std::clamp(change, -maxChange, maxChange);
    target = current - change;

    double temp = (currentVelocity + omega * change) * deltaTime;
    currentVelocity = (currentVelocity - omega * temp) * exp;
    double output = target + (change + temp) * exp;

    if ((originalTo - current > 0.0) == (output > originalTo)) {
        output = originalTo;
        currentVelocity = (deltaTime > 0.0) ? (output - originalTo) / deltaTime : 0.0;
    }

    return output;
}

}  // namespace VirtualOverlay

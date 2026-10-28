// Portions adapted from TalkPrompter (https://github.com/Sven-Bo/talkprompter)
// MIT License — Copyright (c) 2026 Sven Bosau (Bosau Digital LLC)

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace VirtualOverlay {

struct ScriptToken {
    int index = 0;                // Zero-based token index in the script
    std::string normalized;       // UTF-8 lower-cased, punctuation-stripped form
    int start = 0;                // Character offset in the original display text (wstring)
    int length = 0;               // Character length in the original display text (wstring)

    int End() const { return start + length; }
};

class NumberExpander {
public:
    static bool IsAllDigits(const std::string& s);
    static std::vector<std::string> ToWords(const std::string& digits);
};

class TextNormalizer {
public:
    static std::string WideToUtf8(const std::wstring& wide);
    static std::wstring Utf8ToWide(const std::string& utf8);

    static bool IsWordChar(wchar_t c);
    static std::string NormalizeWord(const std::wstring& raw);
    static std::string NormalizeWordUtf8(const std::string& rawUtf8);

    static std::vector<ScriptToken> Tokenize(const std::wstring& scriptText);
    static std::vector<ScriptToken> TokenizeUtf8(const std::string& scriptTextUtf8);

    static std::vector<std::string> ToMatchWords(const std::wstring& utterance, bool expandNumbers = true);
    static std::vector<std::string> ToMatchWordsUtf8(const std::string& utteranceUtf8, bool expandNumbers = true);
};

class ScriptModel {
public:
    ScriptModel() = default;

    static ScriptModel Build(const std::wstring& text, bool expandNumbers = true);
    static ScriptModel BuildUtf8(const std::string& textUtf8, bool expandNumbers = true);

    const std::wstring& Text() const { return m_text; }
    const std::vector<ScriptToken>& Tokens() const { return m_tokens; }
    const std::vector<std::string>& MatchWords() const { return m_matchWords; }
    const std::vector<int>& ParagraphStartTokens() const { return m_paragraphStartTokens; }

    int TokenCount() const { return static_cast<int>(m_tokens.size()); }
    int MatchWordCount() const { return static_cast<int>(m_matchWords.size()); }

    int TokenIndexForMatchWord(int matchWordIndex) const;
    int FirstMatchWordForToken(int tokenIndex) const;

    // Distinct normalized words in the script (including expanded number words)
    std::vector<std::string> DistinctVocabulary() const;

private:
    std::wstring m_text;
    std::vector<ScriptToken> m_tokens;
    std::vector<std::string> m_matchWords;
    std::vector<int> m_matchWordToToken;
    std::vector<int> m_tokenToFirstMatchWord;
    std::vector<int> m_paragraphStartTokens;
};

class Levenshtein {
public:
    static int Distance(const std::string& a, const std::string& b);
    static double Similarity(const std::string& a, const std::string& b);

    static bool WordsMatch(const std::string& a, const std::string& b);
    static int WordDistance(
        const std::vector<std::string>& spoken,
        const std::vector<std::string>& scriptWords,
        int scriptStart,
        int scriptLength
    );
    static double WordSimilarity(
        const std::vector<std::string>& spoken,
        const std::vector<std::string>& scriptWords,
        int scriptStart,
        int scriptLength
    );
};

struct MatcherOptions {
    int backTolerance = 2;
    int forwardWindow = 8;
    int maxJump = 8;
    double acceptThreshold = 0.50;
    int minMatchedWords = 2;
    int tailWords = 7;
    int lostThreshold = 8;
    int reacquireWindow = 400;
    double reacquireThreshold = 0.72;
    int reacquireBackWindow = 80;
    int reacquireBackMinWords = 4;
};

enum class TrackingState {
    Idle,
    Tracking,
    Paused,
    Lost
};

struct MatchUpdate {
    int tokenIndex = -1;
    double confidence = 0.0;
    TrackingState state = TrackingState::Idle;
    bool advanced = false;
};

class ScriptAlignmentEngine {
public:
    explicit ScriptAlignmentEngine(
        ScriptModel model = ScriptModel{},
        MatcherOptions options = MatcherOptions{},
        bool expandNumbers = true
    );

    void SetModel(ScriptModel model);
    const ScriptModel& Model() const { return m_model; }

    int CurrentTokenIndex() const;
    TrackingState State() const { return m_state; }
    int Anchor() const { return m_anchor; }

    void Reset();
    void SeekToToken(int tokenIndex);

    MatchUpdate Process(const std::string& hypothesisUtf8);
    MatchUpdate ProcessWide(const std::wstring& hypothesis);

private:
    struct WindowHit {
        int newAnchor = 0;
        double confidence = 0.0;
    };

    bool FindBestInWindow(
        const std::vector<std::string>& spoken,
        int windowStart,
        int windowEndExclusive,
        double threshold,
        bool allowBehind,
        WindowHit& outHit
    ) const;

    double ScoreCandidate(
        const std::vector<std::string>& spoken,
        int scriptStart,
        int& outConsumed
    ) const;

    MatchUpdate ApplyHit(const WindowHit& hit, bool allowBehind = false);

    ScriptModel m_model;
    MatcherOptions m_options;
    bool m_expandNumbers = true;

    int m_anchor = 0;
    int m_noMatchCount = 0;
    TrackingState m_state = TrackingState::Idle;
};

class SmoothDamp {
public:
    static double Step(
        double current,
        double target,
        double& currentVelocity,
        double smoothTime,
        double deltaTime,
        double maxSpeed = 1e9
    );
};

}  // namespace VirtualOverlay

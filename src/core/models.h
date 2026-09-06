#pragma once

#include <string>
#include <vector>

namespace thai_overlay {

struct AppConfig {
    std::wstring sourceLanguage = L"zh";
    std::wstring targetLanguage = L"th";
    std::wstring translationEngine = L"ai";
    std::wstring apiBase = L"https://api.openai.com";
    std::wstring apiKey;
    std::wstring model = L"gpt-4.1-mini";
    unsigned int translateKey = 'T';
    unsigned int ocrKey = 'O';
    unsigned int quitKey = 'Q';
    std::wstring ocrLanguage = L"auto";
    bool showOriginal = true;
    bool showKaraoke = true;
    bool showExplanation = true;
    bool showWordBreakdown = true;
    int overlayOpacity = 96;
    std::wstring overlayPosition = L"bottom";
    int autoHideSeconds = 0;
};

struct Translation {
    std::wstring originalPinyin;
    std::wstring translatedPinyin;
    std::wstring sourceLanguage = L"auto";
    std::wstring targetLanguage = L"th";
    std::wstring engine = L"ai";
    std::wstring translated;
    std::wstring original;
    std::wstring karaoke;
    std::wstring thai;
    std::wstring explanation;
    struct WordExplanation {
        std::wstring word;
        std::wstring pinyin;
        std::wstring karaoke;
        std::wstring meaning;
        std::wstring note;
    };
    std::vector<WordExplanation> words;
    struct Reply {
        std::wstring tone;
        std::wstring text;
        std::wstring thai;
        std::wstring pinyin;
    };
    std::vector<Reply> replies;
    std::wstring error;
};

struct OcrCandidate {
    std::wstring language;
    std::wstring text;
};

}  // namespace thai_overlay

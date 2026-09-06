#include "services/history_service.h"
#include "services/translation_service.h"
#include "core/text.h"
#include <winrt/base.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace thai_overlay;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 3 || argc == 4, "Usage: tests <temporary history path> <mock API base> [argos]");
        winrt::init_apartment();
        const std::filesystem::path path(argv[1]);
        Require(!std::filesystem::exists(path), "Test path must not exist");
        AppConfig config;
        config.apiBase = argv[2]; config.model = L"test";
        if (argc == 4) {
            const wchar_t* codes[] = {L"zh",L"en",L"th"};
            const wchar_t* samples[] = {L"你好，今天有空吗？",L"Hello, are you free today?",L"สวัสดี วันนี้ว่างไหม"};
            for (int from=0;from<3;++from) for(int to=0;to<3;++to) {
                if(from==to) continue;
                auto pair=config; pair.translationEngine=L"argos";
                pair.sourceLanguage=codes[from]; pair.targetLanguage=codes[to];
                const auto result=Translate(pair,samples[from]);
                if(!result.error.empty()) std::cerr << WideToUtf8(result.error) << '\n';
                Require(result.error.empty() && !result.translated.empty(), "Live Argos translation");
                Require(result.replies.size()==3, "Argos with AI suggestions");
                std::cout << WideToUtf8(pair.sourceLanguage+L" -> "+pair.targetLanguage+L": "+result.translated) << '\n';
            }
        }
        const auto translation = Translate(config, L"你好，今天有空吗？");
        Require(translation.error.empty(), "Translation failed");
        Require(translation.thai == L"สวัสดี วันนี้ว่างไหม?", "Thai parsing");
        Require(translation.replies.size() == 3, "Three replies required");
        Require(translation.replies[0].text == L"您好，请问有什么事？", "Original-language reply");
        Require(translation.words.size() == 1, "Word parsing");
        Require(translation.originalPinyin==L"nǐ hǎo", "Original pinyin parsing");
        Require(translation.replies[0].pinyin==L"nǐ hǎo", "Reply pinyin parsing");
        for (const auto* source : {L"zh",L"en",L"th"}) {
            for (const auto* target : {L"zh",L"en",L"th"}) {
                AppConfig pair=config; pair.sourceLanguage=source; pair.targetLanguage=target;
                const auto result=Translate(pair,L"sample");
                Require(result.error.empty(), "Language pair translation");
                Require(result.sourceLanguage==source && result.targetLanguage==target, "Language metadata");
                Require(!result.translated.empty(), "Target translation required");
                if (pair.sourceLanguage==pair.targetLanguage) Require(result.translated==L"sample", "Identity translation");
                else if (pair.targetLanguage==L"en") Require(result.translated==L"Hello, are you free today?", "English target");
            }
        }
        std::wstring error;
        Require(LoadHistory(error,path).empty() && error.empty(), "Missing history is normal");
        std::vector<HistoryEntry> entries{{L"2026-09-06 14:00",translation}};
        entries.front().translation.explanation = L"ไทย 中文 😀 \"quote\"\nline\\path";
        Require(SaveHistory(entries,error,path), "Save history");
        const auto restored = LoadHistory(error,path);
        Require(error.empty() && restored.size()==1, "Reload history");
        const auto& saved=restored.front().translation;
        Require(saved.original==translation.original && saved.thai==translation.thai, "Translation round trip");
        Require(saved.originalPinyin==translation.originalPinyin && saved.replies[0].pinyin==translation.replies[0].pinyin, "Pinyin history round trip");
        Require(saved.translated==translation.translated && saved.targetLanguage==translation.targetLanguage && saved.engine==translation.engine, "Language and engine persistence");
        Require(saved.explanation==entries.front().translation.explanation, "Unicode and escaped characters");
        Require(saved.replies.size()==3 && saved.replies[2].thai==translation.replies[2].thai, "Replies round trip");
        Require(saved.words.size()==1 && saved.words[0].pinyin==translation.words[0].pinyin, "Words round trip");
        Require(SaveHistory({},error,path), "Clear history");
        Require(LoadHistory(error,path).empty() && error.empty(), "Clear persists");
        { std::ofstream corrupt(path); corrupt << "{broken"; }
        Require(LoadHistory(error,path).empty() && !error.empty(), "Corrupt history detected");
        Require(std::filesystem::file_size(path)==7, "Corrupt file preserved");
        { std::ofstream legacy(path); legacy << R"([{"original":"Hello","thai":"legacy Thai","words":[],"replies":[]}])"; }
        const auto legacy=LoadHistory(error,path);
        Require(error.empty() && legacy.size()==1 && legacy[0].translation.targetLanguage==L"th" &&
                legacy[0].translation.translated==L"legacy Thai", "Legacy history migration");
        const auto failed=Translate(config,L"HTTP_ERROR");
        Require(failed.error.find(L"503")!=std::wstring::npos, "HTTP error handled");
        const auto missing=Translate(config,L"NO_REPLIES");
        Require(missing.error.empty() && missing.replies.empty(), "Older provider response remains readable");
        std::cout << "PASS: translation, three replies, Unicode persistence, clear, corruption, HTTP failure, missing replies\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

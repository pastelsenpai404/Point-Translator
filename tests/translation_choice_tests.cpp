#include "services/translation_service.h"
#include "core/text.h"

#include <winrt/base.h>

#include <iostream>

int wmain() {
    winrt::init_apartment();
    thai_overlay::AppConfig config;
    config.translationEngine = L"argos";
    config.sourceLanguage = L"zh";
    config.targetLanguage = L"en";
    config.apiBase = L"http://127.0.0.1:11434";
    config.model = L"qwen3:4b-instruct";
    const auto result = thai_overlay::Translate(config, L"你好，今天有空吗？");
    if (!result.error.empty() || result.translated.empty() || result.replies.empty()) {
        std::cerr << "Translation failed: " << thai_overlay::WideToUtf8(result.error) << '\n';
        return 1;
    }
    if (!result.alternativeTranslated.empty() && result.alternativeEngine != L"AI") {
        std::cerr << "Alternative engine mislabeled\n";
        return 1;
    }
    if (!result.alternativeTranslated.empty() &&
        result.layaTranslationChoice != L"" &&
        result.layaTranslationChoice != L"AI" &&
        result.layaTranslationChoice != L"Argos") {
        std::cerr << "Unexpected Laya choice\n";
        return 1;
    }
    std::cout << "PASS: Argos primary, "
              << (result.alternativeTranslated.empty() ? "same/missing AI candidate" : "AI alternative")
              << ", Laya choice " << thai_overlay::WideToUtf8(result.layaTranslationChoice) << '\n';
    return 0;
}

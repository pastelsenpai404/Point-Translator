#include "services/translation_service.h"

#include <chrono>
#include <iostream>

int wmain() {
    thai_overlay::AppConfig config;
    config.translationEngine = L"argos";
    config.sourceLanguage = L"zh";
    config.targetLanguage = L"en";
    const auto start = std::chrono::steady_clock::now();
    const auto preview = thai_overlay::Translate(config, L"你好，今天有空吗？", false, false);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (!preview.error.empty() || preview.translated.empty() ||
        !preview.words.empty() || !preview.replies.empty()) {
        std::cerr << "Argos preview failed\n";
        return 1;
    }
    std::cout << "PASS: Argos preview in " << elapsed << " ms, before AI enrichment\n";
    return 0;
}

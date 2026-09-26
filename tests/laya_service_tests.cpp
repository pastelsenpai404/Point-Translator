#include "services/laya_service.h"
#include "core/text.h"

#include <winrt/base.h>

#include <iostream>

int wmain() {
    winrt::init_apartment();
    thai_overlay::AppConfig config;
    config.layaEnabled = false;
    thai_overlay::Translation result;
    result.original = L"Can you help me with this today?";
    thai_overlay::AnalyzeWithLaya(config, result);
    if (!result.layaIntent.empty() || !result.layaStatus.empty()) return 1;
    config.layaEnabled = true;
    config.sourceLanguage = L"en";
    thai_overlay::AnalyzeWithLaya(config, result);
    if (result.layaIntent.empty() || result.layaUrgency.empty() || !result.layaStatus.empty()) {
        std::cerr << "Laya result: " << thai_overlay::WideToUtf8(result.layaStatus) << '\n';
        return 1;
    }
    std::cout << "PASS: local Laya HTTP response and disabled mode\n";
    return 0;
}

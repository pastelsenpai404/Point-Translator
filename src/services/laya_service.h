#pragma once

#include "core/models.h"

namespace thai_overlay {

void StartLayaService();
void AnalyzeWithLaya(const AppConfig& config, Translation& translation);
enum class LayaTranslationChoice { Unavailable, Argos, Ai };
LayaTranslationChoice SelectTranslationWithLaya(const AppConfig& config,
    const std::wstring& original, const std::wstring& argos, const std::wstring& ai);

}  // namespace thai_overlay

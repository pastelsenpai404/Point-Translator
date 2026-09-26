#pragma once

#include "core/models.h"

namespace thai_overlay {

void StartLayaService();
void AnalyzeWithLaya(const AppConfig& config, Translation& translation);

}  // namespace thai_overlay

#pragma once

#include "core/models.h"

#include <string>

namespace thai_overlay {
void StartArgosService();

Translation Translate(const AppConfig& config, const std::wstring& original,
                      bool chooseOcrCandidate = false);

}  // namespace thai_overlay

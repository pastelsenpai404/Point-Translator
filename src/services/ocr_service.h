#pragma once

#include "core/models.h"

#include <windows.h>

#include <string>
#include <vector>

namespace thai_overlay {

std::vector<OcrCandidate> RecognizeBitmapText(HBITMAP bitmap,
                                               const AppConfig& config,
                                               std::wstring& error);

}  // namespace thai_overlay

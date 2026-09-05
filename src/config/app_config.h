#pragma once

#include "core/models.h"

#include <string>

namespace thai_overlay {

AppConfig LoadAppConfig();
bool SaveAppConfig(const AppConfig& config, std::wstring& error);

}  // namespace thai_overlay

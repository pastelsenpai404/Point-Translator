#pragma once

#include "core/models.h"

#include <windows.h>

namespace thai_overlay {

bool ShowSettingsWindow(HINSTANCE instance, HWND owner,
                        const AppConfig& current, AppConfig& updated);

}  // namespace thai_overlay

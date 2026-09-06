#pragma once
#include "core/models.h"
#include <windows.h>
#include <functional>
namespace thai_overlay {
void InitializeWorkspace(HINSTANCE instance, AppConfig& config, std::function<void(std::wstring)> translate,
                         std::function<void()> capture, std::function<void()> settings);
void ShowWorkspace();
void WorkspaceBusy(bool busy);
void WorkspaceCompleted(const Translation& translation);
bool WorkspaceVisible();
bool WorkspaceMessage(MSG& message);
}

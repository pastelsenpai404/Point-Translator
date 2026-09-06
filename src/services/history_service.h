#pragma once
#include "core/models.h"
#include <filesystem>

namespace thai_overlay {
struct HistoryEntry {
    std::wstring time;
    Translation translation;
};
std::filesystem::path HistoryPath();
std::vector<HistoryEntry> LoadHistory(std::wstring& error, std::filesystem::path path = {});
bool SaveHistory(const std::vector<HistoryEntry>& entries, std::wstring& error, std::filesystem::path path = {});
}

#include "config/app_config.h"

#include "core/text.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <vector>

namespace thai_overlay {
namespace {

std::wstring ExecutableDirectory() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path().wstring();
}

unsigned int ParseHotkey(const std::wstring& value, unsigned int fallback) {
    if (value.size() == 1) {
        const wchar_t key = static_cast<wchar_t>(std::towupper(value.front()));
        if ((key >= L'A' && key <= L'Z') || (key >= L'0' && key <= L'9')) return key;
    }
    return fallback;
}

bool ParseBoolean(const std::wstring& value, bool fallback) {
    if (value == L"true" || value == L"1" || value == L"yes") return true;
    if (value == L"false" || value == L"0" || value == L"no") return false;
    return fallback;
}

int ParseInteger(const std::wstring& value, int fallback, int minimum, int maximum) {
    try {
        const int parsed = std::stoi(value);
        return (std::max)(minimum, (std::min)(maximum, parsed));
    } catch (...) {
        return fallback;
    }
}

}  // namespace

AppConfig LoadAppConfig() {
    AppConfig config;
    const auto path = std::filesystem::path(ExecutableDirectory()) / L"config.ini";
    std::wifstream stream(path);
    stream.imbue(std::locale(""));
    std::wstring line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos) continue;
        const auto key = Trim(line.substr(0, equals));
        const auto value = Trim(line.substr(equals + 1));
        if (key == L"api_base") config.apiBase = value;
        else if (key == L"api_key") config.apiKey = value;
        else if (key == L"model") config.model = value;
        else if (key == L"translate_hotkey") config.translateKey = ParseHotkey(value, 'T');
        else if (key == L"ocr_hotkey") config.ocrKey = ParseHotkey(value, 'O');
        else if (key == L"ocr_language") config.ocrLanguage = value;
        else if (key == L"quit_hotkey") config.quitKey = ParseHotkey(value, 'Q');
        else if (key == L"show_original") config.showOriginal = ParseBoolean(value, true);
        else if (key == L"show_karaoke") config.showKaraoke = ParseBoolean(value, true);
        else if (key == L"show_explanation") config.showExplanation = ParseBoolean(value, true);
        else if (key == L"show_word_breakdown") config.showWordBreakdown = ParseBoolean(value, true);
        else if (key == L"overlay_opacity") config.overlayOpacity = ParseInteger(value, 96, 65, 100);
        else if (key == L"overlay_position") config.overlayPosition = value;
        else if (key == L"auto_hide_seconds") config.autoHideSeconds = ParseInteger(value, 0, 0, 300);
    }

    wchar_t environmentKey[4096]{};
    const DWORD count = GetEnvironmentVariableW(
        L"THAI_OVERLAY_API_KEY", environmentKey,
        static_cast<DWORD>(std::size(environmentKey)));
    if (count > 0 && count < std::size(environmentKey)) {
        config.apiKey.assign(environmentKey, count);
    }
    while (!config.apiBase.empty() && config.apiBase.back() == L'/') config.apiBase.pop_back();
    return config;
}

bool SaveAppConfig(const AppConfig& config, std::wstring& error) {
    const auto configPath = std::filesystem::path(ExecutableDirectory()) / L"config.ini";
    const auto temporaryPath = configPath.wstring() + L".tmp";

    std::wostringstream content;
    content << L"# Thai Karaoke Overlay configuration\n"
            << L"api_base=" << config.apiBase << L"\n"
            << L"model=" << config.model << L"\n"
            << L"translate_hotkey=" << static_cast<wchar_t>(config.translateKey) << L"\n"
            << L"ocr_hotkey=" << static_cast<wchar_t>(config.ocrKey) << L"\n"
            << L"quit_hotkey=" << static_cast<wchar_t>(config.quitKey) << L"\n"
            << L"ocr_language=" << config.ocrLanguage << L"\n"
            << L"show_original=" << (config.showOriginal ? L"true" : L"false") << L"\n"
            << L"show_karaoke=" << (config.showKaraoke ? L"true" : L"false") << L"\n"
            << L"show_explanation=" << (config.showExplanation ? L"true" : L"false") << L"\n"
            << L"show_word_breakdown=" << (config.showWordBreakdown ? L"true" : L"false") << L"\n"
            << L"overlay_opacity=" << config.overlayOpacity << L"\n"
            << L"overlay_position=" << config.overlayPosition << L"\n"
            << L"auto_hide_seconds=" << config.autoHideSeconds << L"\n";

    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = L"Could not create the temporary configuration file.";
            return false;
        }
        const std::string utf8 = WideToUtf8(content.str());
        stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        if (!stream) {
            error = L"Could not write the configuration file.";
            return false;
        }
    }

    HKEY environmentKey = nullptr;
    const LSTATUS openStatus = RegCreateKeyExW(
        HKEY_CURRENT_USER, L"Environment", 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &environmentKey, nullptr);
    if (openStatus != ERROR_SUCCESS) {
        DeleteFileW(temporaryPath.c_str());
        error = L"Could not open the user environment settings for the API key.";
        return false;
    }

    LSTATUS keyStatus = ERROR_SUCCESS;
    if (config.apiKey.empty()) {
        keyStatus = RegDeleteValueW(environmentKey, L"THAI_OVERLAY_API_KEY");
        if (keyStatus == ERROR_FILE_NOT_FOUND) keyStatus = ERROR_SUCCESS;
    } else {
        const DWORD bytes = static_cast<DWORD>((config.apiKey.size() + 1) * sizeof(wchar_t));
        keyStatus = RegSetValueExW(
            environmentKey, L"THAI_OVERLAY_API_KEY", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(config.apiKey.c_str()), bytes);
    }
    RegCloseKey(environmentKey);
    if (keyStatus != ERROR_SUCCESS) {
        DeleteFileW(temporaryPath.c_str());
        error = L"Could not save the API key to the user environment.";
        return false;
    }

    if (!MoveFileExW(temporaryPath.c_str(), configPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporaryPath.c_str());
        error = L"Could not replace config.ini.";
        return false;
    }

    SetEnvironmentVariableW(L"THAI_OVERLAY_API_KEY",
                            config.apiKey.empty() ? nullptr : config.apiKey.c_str());
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG, 1000, &ignored);
    return true;
}

}  // namespace thai_overlay

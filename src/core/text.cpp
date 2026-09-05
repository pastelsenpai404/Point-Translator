#include "core/text.h"

#include <windows.h>

namespace thai_overlay {

std::wstring Trim(std::wstring value) {
    constexpr wchar_t spaces[] = L" \t\r\n";
    const auto first = value.find_first_not_of(spaces);
    if (first == std::wstring::npos) return {};
    value.erase(0, first);
    const auto last = value.find_last_not_of(spaces);
    value.erase(last + 1);
    return value;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

}  // namespace thai_overlay

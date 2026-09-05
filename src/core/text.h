#pragma once

#include <string>

namespace thai_overlay {

std::wstring Trim(std::wstring value);
std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);

}  // namespace thai_overlay

#include "services/ocr_service.h"

#include "core/text.h"

#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <utility>
#include <vector>

namespace thai_overlay {
namespace {

struct __declspec(uuid("5B0D3235-4DBA-4D44-865E-8F1D0E4FD04D"))
    IMemoryBufferByteAccess : IUnknown {
    virtual HRESULT __stdcall GetBuffer(std::uint8_t** value, std::uint32_t* capacity) = 0;
};

bool StartsWithIgnoringCase(const std::wstring& value, const std::wstring& prefix) {
    if (prefix.size() > value.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), value.begin(),
                      [](wchar_t left, wchar_t right) {
                          return std::towlower(left) == std::towlower(right);
                      });
}

std::vector<std::pair<std::wstring, winrt::Windows::Media::Ocr::OcrEngine>>
CreateOcrEngines(const AppConfig& config) {
    using namespace winrt::Windows::Globalization;
    using namespace winrt::Windows::Media::Ocr;
    std::vector<std::pair<std::wstring, OcrEngine>> engines;

    if (config.ocrLanguage.empty() || config.ocrLanguage == L"auto") {
        for (const auto& language : OcrEngine::AvailableRecognizerLanguages()) {
            if (auto engine = OcrEngine::TryCreateFromLanguage(language)) {
                engines.emplace_back(language.LanguageTag().c_str(), engine);
            }
        }
        return engines;
    }

    try {
        if (auto engine = OcrEngine::TryCreateFromLanguage(Language(config.ocrLanguage))) {
            engines.emplace_back(config.ocrLanguage, engine);
            return engines;
        }
    } catch (const winrt::hresult_error&) {
        // Try a compatible installed language tag below (for example zh-Hans-CN).
    }

    for (const auto& language : OcrEngine::AvailableRecognizerLanguages()) {
        const std::wstring tag = language.LanguageTag().c_str();
        if (StartsWithIgnoringCase(tag, config.ocrLanguage) ||
            StartsWithIgnoringCase(config.ocrLanguage, tag)) {
            if (auto engine = OcrEngine::TryCreateFromLanguage(language)) {
                engines.emplace_back(tag, engine);
                return engines;
            }
        }
    }
    return engines;
}

}  // namespace

std::vector<OcrCandidate> RecognizeBitmapText(HBITMAP bitmap,
                                               const AppConfig& config,
                                               std::wstring& error) {
    using namespace winrt::Windows::Graphics::Imaging;

    BITMAP details{};
    if (!GetObjectW(bitmap, sizeof(details), &details) ||
        details.bmWidth <= 0 || details.bmHeight <= 0) {
        error = L"Windows could not read the captured image.";
        return {};
    }

    const int width = details.bmWidth;
    const int height = details.bmHeight;
    std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const HDC screen = GetDC(nullptr);
    const int copied = GetDIBits(screen, bitmap, 0, static_cast<UINT>(height),
                                 pixels.data(), &info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (copied != height) {
        error = L"Windows could not convert the captured image for OCR.";
        return {};
    }

    try {
        SoftwareBitmap softwareBitmap(BitmapPixelFormat::Bgra8, width, height,
                                      BitmapAlphaMode::Premultiplied);
        {
            auto buffer = softwareBitmap.LockBuffer(BitmapBufferAccessMode::Write);
            const auto plane = buffer.GetPlaneDescription(0);
            auto reference = buffer.CreateReference();
            std::uint8_t* destination = nullptr;
            std::uint32_t capacity = 0;
            winrt::check_hresult(reference.as<IMemoryBufferByteAccess>()->GetBuffer(
                &destination, &capacity));
            const size_t rowBytes = static_cast<size_t>(width) * 4;
            for (int row = 0; row < height; ++row) {
                const size_t target = static_cast<size_t>(plane.StartIndex) +
                                      static_cast<size_t>(row) * plane.Stride;
                if (target + rowBytes > capacity) {
                    error = L"The OCR image buffer is too small.";
                    return {};
                }
                std::memcpy(destination + target, pixels.data() + row * rowBytes, rowBytes);
            }
        }

        const auto engines = CreateOcrEngines(config);
        if (engines.empty()) {
            error = L"The Windows OCR language pack is not installed for " +
                    config.ocrLanguage + L".";
            return {};
        }

        std::vector<OcrCandidate> candidates;
        for (const auto& [language, engine] : engines) {
            const std::wstring text = Trim(engine.RecognizeAsync(softwareBitmap).get().Text().c_str());
            if (text.empty()) continue;
            const bool duplicate = std::any_of(
                candidates.begin(), candidates.end(),
                [&text](const OcrCandidate& candidate) { return candidate.text == text; });
            if (!duplicate) candidates.push_back({language, text});
        }
        return candidates;
    } catch (const winrt::hresult_error& exception) {
        error = L"Windows OCR failed: ";
        error += exception.message().c_str();
        return {};
    }
}


}  // namespace thai_overlay

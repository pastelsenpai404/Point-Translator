#include "services/laya_service.h"

#include "core/text.h"

#include <windows.h>
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <filesystem>
#include <string>

namespace thai_overlay {
namespace {

struct HttpHandle {
    HINTERNET value = nullptr;
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};

std::wstring IntentName(const std::wstring& code) {
    if (code == L"question") return L"ถามข้อมูล";
    if (code == L"request") return L"ขอให้ช่วยหรือดำเนินการ";
    if (code == L"complaint") return L"แจ้งปัญหาหรือร้องเรียน";
    if (code == L"statement") return L"บอกเล่าหรือทักทาย";
    return {};
}

std::wstring UrgencyName(const std::wstring& code) {
    if (code == L"normal") return L"ปกติ";
    if (code == L"soon") return L"ควรตอบเร็ว";
    if (code == L"urgent") return L"เร่งด่วน";
    return {};
}

bool PostLaya(const wchar_t* path,
              const winrt::Windows::Data::Json::JsonObject& payload,
              std::string& response) {
    const std::string body = WideToUtf8(std::wstring(payload.Stringify()));
    HttpHandle session{WinHttpOpen(L"PointTranslator-Laya/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return false;
    WinHttpSetTimeouts(session.value, 2000, 2000, 3000, 120000);
    HttpHandle connection{WinHttpConnect(session.value, L"127.0.0.1", 18766, 0)};
    if (!connection.value) return false;
    HttpHandle request{WinHttpOpenRequest(connection.value, L"POST", path, nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)};
    if (!request.value) return false;
    constexpr wchar_t headers[] = L"Content-Type: application/json; charset=utf-8\r\n";
    if (!WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1),
                            const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) return false;
    DWORD status = 0, statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) || status != 200) return false;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) return false;
        if (available == 0) break;
        if (response.size() + available > 65536) return false;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.value, chunk.data(), available, &read)) return false;
        response.append(chunk.data(), read);
    }
    return true;
}

}  // namespace

void StartLayaService() {
    HANDLE existing = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\PointTranslatorLayaBridge");
    if (existing) {
        CloseHandle(existing);
        return;
    }
    wchar_t executable[32768]{};
    if (!GetModuleFileNameW(nullptr, executable, 32768)) return;
    auto directory = std::filesystem::path(executable).parent_path();
    for (int level = 0; level < 4; ++level, directory = directory.parent_path()) {
        const auto python = directory / L".laya-venv" / L"Scripts" / L"python.exe";
        const auto bridge = directory / L"laya" / L"bridge.py";
        if (!std::filesystem::exists(python) || !std::filesystem::exists(bridge)) continue;
        std::wstring command = L"\"" + python.wstring() + L"\" \"" + bridge.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(python.c_str(), command.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
        return;
    }
}

void AnalyzeWithLaya(const AppConfig& config, Translation& translation) {
    if (!config.layaEnabled || !translation.error.empty() || translation.original.empty()) return;
    translation.layaStatus = L"Laya ยังไม่พร้อม · ติดตั้งด้วย Start-Laya.ps1 -Install แล้วลองใหม่";
    if (translation.original.size() > 4000) {
        translation.layaStatus = L"ข้อความยาวเกิน 4,000 ตัวอักษรสำหรับ Laya";
        return;
    }

    using namespace winrt::Windows::Data::Json;
    JsonObject payload;
    payload.Insert(L"text", JsonValue::CreateStringValue(translation.original));
    payload.Insert(L"lang", JsonValue::CreateStringValue(config.sourceLanguage));
    std::string response;
    if (!PostLaya(L"/analyze", payload, response)) {
        translation.layaStatus = L"Laya วิเคราะห์ไม่สำเร็จ · ตรวจสอบบริการและโมเดลในเครื่อง";
        return;
    }
    try {
        const auto result = JsonObject::Parse(Utf8ToWide(response));
        const auto intent = IntentName(std::wstring(result.GetNamedString(L"intent", L"")));
        const auto urgency = UrgencyName(std::wstring(result.GetNamedString(L"urgency", L"")));
        if (intent.empty() || urgency.empty()) return;
        translation.layaIntent = intent;
        translation.layaUrgency = urgency;
        translation.layaStatus.clear();
    } catch (...) {
        translation.layaStatus = L"Laya ส่งผลวิเคราะห์ไม่ถูกต้อง";
    }
}

LayaTranslationChoice SelectTranslationWithLaya(const AppConfig& config,
    const std::wstring& original, const std::wstring& argos, const std::wstring& ai) {
    if (!config.layaEnabled || original.empty() || argos.empty() || ai.empty() ||
        argos == ai || original.size() > 800 || argos.size() > 800 || ai.size() > 800)
        return LayaTranslationChoice::Unavailable;
    try {
        using namespace winrt::Windows::Data::Json;
        JsonObject payload;
        payload.Insert(L"source", JsonValue::CreateStringValue(original));
        payload.Insert(L"argos", JsonValue::CreateStringValue(argos));
        payload.Insert(L"ai", JsonValue::CreateStringValue(ai));
        payload.Insert(L"lang", JsonValue::CreateStringValue(config.sourceLanguage));
        payload.Insert(L"target", JsonValue::CreateStringValue(config.targetLanguage));
        std::string response;
        if (!PostLaya(L"/select-translation", payload, response))
            return LayaTranslationChoice::Unavailable;
        const auto result = JsonObject::Parse(Utf8ToWide(response));
        const std::wstring choice(result.GetNamedString(L"choice", L""));
        if (choice == L"argos") return LayaTranslationChoice::Argos;
        if (choice == L"ai") return LayaTranslationChoice::Ai;
    } catch (...) { }
    return LayaTranslationChoice::Unavailable;
}

}  // namespace thai_overlay

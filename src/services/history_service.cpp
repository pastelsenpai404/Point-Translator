#include "services/history_service.h"
#include "core/text.h"
#include <windows.h>
#include <shlobj.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <fstream>
#include <stdexcept>

namespace thai_overlay {
using namespace winrt::Windows::Data::Json;
namespace {
void Put(JsonObject& object, const wchar_t* key, const std::wstring& value) {
    object.Insert(key, JsonValue::CreateStringValue(value));
}
std::wstring Get(const JsonObject& object, const wchar_t* key) {
    return std::wstring(object.GetNamedString(key, L""));
}
}
std::filesystem::path HistoryPath() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)))
        throw std::runtime_error("Local application data unavailable");
    std::filesystem::path path(folder);
    CoTaskMemFree(folder);
    return path / L"PointTranslator" / L"history.json";
}
std::vector<HistoryEntry> LoadHistory(std::wstring& error, std::filesystem::path path) {
    error.clear();
    std::vector<HistoryEntry> entries;
    try {
        if (path.empty()) path = HistoryPath();
        if (!std::filesystem::exists(path)) return entries;
        if (std::filesystem::file_size(path) > 32 * 1024 * 1024) throw std::runtime_error("History too large");
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("Read failed");
        std::string data((std::istreambuf_iterator<char>(stream)), {});
        const auto array = JsonArray::Parse(Utf8ToWide(data));
        for (const auto& value : array) {
            const auto object = value.GetObject();
            HistoryEntry entry;
            entry.time = Get(object, L"time");
            auto& t = entry.translation;
            t.originalPinyin=Get(object,L"original_pinyin");
            t.translatedPinyin=Get(object,L"translated_pinyin");
            t.sourceLanguage = object.GetNamedString(L"source_language", L"auto");
            t.targetLanguage = object.GetNamedString(L"target_language", L"th");
            t.engine = object.GetNamedString(L"engine", L"ai");
            t.translated = object.GetNamedString(L"translated", object.GetNamedString(L"thai", L""));
            t.original = Get(object, L"original"); t.thai = Get(object, L"thai");
            t.karaoke = Get(object, L"karaoke"); t.explanation = Get(object, L"explanation");
            for (const auto& item : object.GetNamedArray(L"words", JsonArray())) {
                const auto w = item.GetObject();
                t.words.push_back({Get(w,L"word"),Get(w,L"pinyin"),Get(w,L"karaoke"),Get(w,L"meaning"),Get(w,L"note")});
            }
            for (const auto& item : object.GetNamedArray(L"replies", JsonArray())) {
                const auto r = item.GetObject();
                t.replies.push_back({Get(r,L"tone"),Get(r,L"text"),Get(r,L"thai"),Get(r,L"pinyin")});
                if (t.replies.size() == 3) break;
            }
            entries.push_back(std::move(entry));
            if (entries.size() == 500) break;
        }
    } catch (...) {
        entries.clear();
        error = L"อ่านประวัติไม่สำเร็จ ไฟล์เดิมยังอยู่ ระบบหยุดบันทึกเพื่อป้องกันข้อมูลเดิม";
    }
    return entries;
}
bool SaveHistory(const std::vector<HistoryEntry>& entries, std::wstring& error, std::filesystem::path path) {
    error.clear();
    try {
        JsonArray array;
        for (const auto& entry : entries) {
            JsonObject object;
            const auto& t = entry.translation;
            Put(object,L"original_pinyin",t.originalPinyin); Put(object,L"translated_pinyin",t.translatedPinyin);
            Put(object,L"source_language",t.sourceLanguage); Put(object,L"target_language",t.targetLanguage);
            Put(object,L"engine",t.engine); Put(object,L"translated",t.translated);
            Put(object,L"time",entry.time); Put(object,L"original",t.original);
            Put(object,L"thai",t.thai); Put(object,L"karaoke",t.karaoke); Put(object,L"explanation",t.explanation);
            JsonArray words, replies;
            for (const auto& w : t.words) {
                JsonObject item;
                Put(item,L"word",w.word); Put(item,L"pinyin",w.pinyin); Put(item,L"karaoke",w.karaoke);
                Put(item,L"meaning",w.meaning); Put(item,L"note",w.note); words.Append(item);
            }
            for (const auto& r : t.replies) {
                JsonObject item;
                Put(item,L"tone",r.tone); Put(item,L"text",r.text); Put(item,L"thai",r.thai); Put(item,L"pinyin",r.pinyin); replies.Append(item);
            }
            object.Insert(L"words",words); object.Insert(L"replies",replies); array.Append(object);
        }
        if (path.empty()) path = HistoryPath();
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.wstring() + L".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            const auto data = WideToUtf8(std::wstring(array.Stringify()));
            stream.write(data.data(), static_cast<std::streamsize>(data.size()));
            stream.close();
            if (!stream) throw std::runtime_error("Write failed");
        }
        if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Replace failed");
        return true;
    } catch (...) {
        error = L"บันทึกประวัติไม่สำเร็จ กรุณาตรวจสอบพื้นที่ว่างและสิทธิ์เข้าถึงไฟล์";
        return false;
    }
}
}

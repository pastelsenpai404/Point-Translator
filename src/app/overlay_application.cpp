#include "app/overlay_application.h"
#include "app/settings_window.h"
#include "app/workspace_window.h"

#include "config/app_config.h"
#include "core/models.h"
#include "core/text.h"
#include "services/ocr_service.h"
#include "services/translation_service.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace thai_overlay {
namespace {

constexpr wchar_t kWindowClass[] = L"ThaiKaraokeOverlayWindow";
constexpr wchar_t kCaptureWindowClass[] = L"ThaiKaraokeCaptureWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTranslationReady = WM_APP + 2;
constexpr UINT kHotkeyTranslate = 1;
constexpr UINT kHotkeyQuit = 2;
constexpr UINT kHotkeyOcr = 3;
constexpr UINT kTrayId = 1;
constexpr UINT kMenuTranslate = 1001;
constexpr UINT kMenuExit = 1002;
constexpr UINT kMenuOcr = 1003;
constexpr UINT kMenuSettings = 1004;
constexpr UINT kMenuWorkspace = 1005;
constexpr UINT_PTR kAutoHideTimer = 1;
constexpr int kOverlayWidth = 980;
constexpr int kOverlayHeight = 680;

AppConfig g_config;
HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_captureWindow = nullptr;
NOTIFYICONDATAW g_tray{};
Translation g_translation;
std::atomic_bool g_busy = false;
ULONGLONG g_closeDeadline = 0;
HFONT g_titleFont = nullptr;
HFONT g_bodyFont = nullptr;
HFONT g_smallFont = nullptr;
size_t g_wordScroll = 0;
POINT g_captureStart{};
POINT g_captureEnd{};
bool g_selectingCapture = false;

bool ReadClipboardText(std::wstring& text) {
    if (!OpenClipboard(nullptr)) return false;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data) { CloseClipboard(); return false; }
    const auto* characters = static_cast<const wchar_t*>(GlobalLock(data));
    if (!characters) { CloseClipboard(); return false; }
    text = characters;
    GlobalUnlock(data);
    CloseClipboard();
    return true;
}

void SendCopyShortcut() {
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'C';
    inputs[2] = inputs[1];
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3] = inputs[0];
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(static_cast<UINT>(std::size(inputs)), inputs, sizeof(INPUT));
}

std::wstring CaptureSelectedText() {
    // WM_HOTKEY fires while the physical Ctrl/Alt keys may still be down. Wait
    // briefly so the generated keystroke is Ctrl+C rather than Ctrl+Alt+C.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0 &&
            (GetAsyncKeyState(VK_MENU) & 0x8000) == 0) break;
        Sleep(15);
    }
    const DWORD previousSequence = GetClipboardSequenceNumber();
    SendCopyShortcut();
    for (int attempt = 0; attempt < 20; ++attempt) {
        Sleep(35);
        if (GetClipboardSequenceNumber() != previousSequence) {
            std::wstring text;
            if (ReadClipboardText(text)) return Trim(text);
        }
    }
    return {};
}

HBITMAP CaptureScreenRectangle(const RECT& rectangle) {
    const int width = rectangle.right - rectangle.left;
    const int height = rectangle.bottom - rectangle.top;
    if (width <= 0 || height <= 0) return nullptr;

    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    const HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    const HGDIOBJ previous = SelectObject(memory, bitmap);
    const BOOL copied = BitBlt(memory, 0, 0, width, height, screen,
                               rectangle.left, rectangle.top, SRCCOPY | CAPTUREBLT);
    SelectObject(memory, previous);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!copied) {
        DeleteObject(bitmap);
        return nullptr;
    }
    return bitmap;
}

void PositionAndShowOverlay() {
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const int availableWidth = static_cast<int>(info.rcWork.right - info.rcWork.left) - 24;
    const int availableHeight = static_cast<int>(info.rcWork.bottom - info.rcWork.top) - 24;
    const int width = (std::min)(kOverlayWidth, availableWidth);
    const int height = (std::min)(kOverlayHeight, availableHeight);
    const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
    int y = info.rcWork.bottom - height - 28;
    if (g_config.overlayPosition == L"top") {
        y = info.rcWork.top + 28;
    } else if (g_config.overlayPosition == L"center") {
        y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
    }
    y = (std::max)(static_cast<int>(info.rcWork.top + 12), y);
    SetWindowRgn(g_window, CreateRoundRectRgn(0, 0, width, height, 24, 24), TRUE);
    SetWindowPos(g_window, HWND_TOPMOST, x, y, width, height,
                 SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    SetForegroundWindow(g_window);
    InvalidateRect(g_window, nullptr, TRUE);
}

void StartTranslation(bool copySelection, std::wstring typed = {}) {
    if (g_busy.exchange(true)) return;
    KillTimer(g_window, kAutoHideTimer);
    std::wstring selected;
    WorkspaceBusy(true);
    if (!typed.empty()) selected = std::move(typed);
    else if (copySelection) selected = CaptureSelectedText();
    else ReadClipboardText(selected);
    selected = Trim(selected);
    g_translation = {};
    g_translation.original = selected.empty() ? L"No selected text" : selected;
    if (!WorkspaceVisible()) PositionAndShowOverlay();
    const AppConfig config = g_config;

    std::thread([selected, copySelection, config] {
        auto* completed = new Translation;
        if (selected.empty()) {
            completed->error = copySelection
                ? L"No text was copied. Select text in an app, then press Ctrl+Alt+T."
                : L"The clipboard does not contain text.";
        } else {
            *completed = Translate(config, selected);
        }
        if (!PostMessageW(g_window, kTranslationReady, 0,
                          reinterpret_cast<LPARAM>(completed))) {
            delete completed;
        }
    }).detach();
}

void StartOcrTranslation(const RECT& rectangle) {
    AppConfig config = g_config;
    if (config.ocrLanguage == L"auto") {
        if (config.sourceLanguage == L"zh") config.ocrLanguage = L"zh-Hans-CN";
        else if (config.sourceLanguage == L"en") config.ocrLanguage = L"en-US";
        else if (config.sourceLanguage == L"th") config.ocrLanguage = L"th-TH";
    }
    std::thread([rectangle, config] {
        // The capture window must be fully hidden before copying screen pixels.
        Sleep(120);
        auto* completed = new Translation;
        HBITMAP bitmap = CaptureScreenRectangle(rectangle);
        if (!bitmap) {
            completed->error =
                L"Windows could not capture the selected screen area.";
        } else {
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                std::wstring ocrError;
                const auto candidates = RecognizeBitmapText(bitmap, config, ocrError);
                DeleteObject(bitmap);
                bitmap = nullptr;
                if (!ocrError.empty()) {
                    completed->error = std::move(ocrError);
                } else if (candidates.empty()) {
                    completed->error =
                        L"Windows OCR did not find text in the selected screen area.";
                } else if (candidates.size() == 1) {
                    *completed = Translate(config, candidates.front().text);
                } else {
                    std::wostringstream choices;
                    for (const auto& candidate : candidates) {
                        choices << L"\n[language=" << candidate.language << L"] "
                                << candidate.text;
                    }
                    *completed = Translate(config, choices.str(), true);
                }
                winrt::uninit_apartment();
            } catch (const winrt::hresult_error& exception) {
                completed->error = L"Could not start Windows OCR: ";
                completed->error += exception.message().c_str();
            }
        }
        if (bitmap) DeleteObject(bitmap);
        if (!PostMessageW(g_window, kTranslationReady, 0,
                          reinterpret_cast<LPARAM>(completed))) {
            delete completed;
        }
    }).detach();
}

void BeginOcrCapture() {
    if (g_busy.exchange(true)) return;
    WorkspaceBusy(true);
    KillTimer(g_window, kAutoHideTimer);
    ShowWindow(g_window, SW_HIDE);
    g_selectingCapture = false;
    g_captureStart = {};
    g_captureEnd = {};

    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    SetWindowPos(g_captureWindow, HWND_TOPMOST, left, top, width, height,
                 SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    SetForegroundWindow(g_captureWindow);
    SetFocus(g_captureWindow);
    InvalidateRect(g_captureWindow, nullptr, TRUE);
}

void DrawTextBlock(HDC dc, const wchar_t* label, const std::wstring& value,
                   RECT& area, COLORREF labelColor, COLORREF textColor,
                   int valueHeight = 42) {
    SelectObject(dc, g_titleFont);
    SetTextColor(dc, labelColor);
    RECT labelRect = area;
    labelRect.bottom = labelRect.top + 25;
    DrawTextW(dc, label, -1, &labelRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    area.top += 28;

    SelectObject(dc, g_bodyFont);
    SetTextColor(dc, textColor);
    RECT textRect = area;
    DrawTextW(dc, value.c_str(), -1, &textRect,
              DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    area.top += valueHeight;
}

void DrawWordCell(HDC dc, const std::wstring& value, RECT cell, COLORREF color) {
    SetTextColor(dc, color);
    cell.left += 8;
    cell.right -= 8;
    DrawTextW(dc, value.c_str(), -1, &cell,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void DrawWordBreakdown(HDC dc, RECT area) {
    SelectObject(dc, g_titleFont);
    SetTextColor(dc, RGB(238, 191, 92));
    RECT title = area;
    title.bottom = title.top + 26;
    DrawTextW(dc, L"WORD-BY-WORD", -1, &title, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    area.top += 30;

    constexpr int headerHeight = 28;
    constexpr int rowHeight = 38;
    const std::array<int, 6> columns{
        area.left, area.left + 130, area.left + 280, area.left + 430,
        area.left + 630, area.right};
    const std::array<const wchar_t*, 5> headings{
        L"คำ/วลี", L"Pinyin", L"คำอ่านไทย", L"ความหมาย", L"หน้าที่/บริบท"};

    HBRUSH headerBrush = CreateSolidBrush(RGB(34, 39, 49));
    RECT header{area.left, area.top, area.right, area.top + headerHeight};
    FillRect(dc, &header, headerBrush);
    DeleteObject(headerBrush);
    SelectObject(dc, g_titleFont);
    for (size_t column = 0; column < headings.size(); ++column) {
        DrawWordCell(dc, headings[column],
                     RECT{columns[column], header.top, columns[column + 1], header.bottom},
                     RGB(168, 178, 194));
    }
    area.top += headerHeight;

    const int availableRowsHeight = static_cast<int>(area.bottom - area.top);
    const size_t visibleRows = static_cast<size_t>(
        (std::max)(0, availableRowsHeight) / rowHeight);
    const size_t maximumStart = g_translation.words.size() > visibleRows
        ? g_translation.words.size() - visibleRows : 0;
    g_wordScroll = (std::min)(g_wordScroll, maximumStart);
    const size_t end = (std::min)(g_translation.words.size(), g_wordScroll + visibleRows);
    SelectObject(dc, g_smallFont);
    for (size_t index = g_wordScroll; index < end; ++index) {
        const int rowTop = area.top + static_cast<int>(index - g_wordScroll) * rowHeight;
        RECT row{area.left, rowTop, area.right, rowTop + rowHeight};
        if ((index - g_wordScroll) % 2 == 1) {
            HBRUSH rowBrush = CreateSolidBrush(RGB(25, 29, 37));
            FillRect(dc, &row, rowBrush);
            DeleteObject(rowBrush);
        }
        const auto& word = g_translation.words[index];
        const std::array<std::wstring, 5> values{
            word.word, word.pinyin, word.karaoke, word.meaning, word.note};
        const std::array<COLORREF, 5> colors{
            RGB(255, 224, 132), RGB(153, 199, 255), RGB(111, 231, 190),
            RGB(245, 245, 248), RGB(190, 195, 205)};
        for (size_t column = 0; column < values.size(); ++column) {
            DrawWordCell(dc, values[column],
                         RECT{columns[column], row.top, columns[column + 1], row.bottom},
                         colors[column]);
        }
    }
}

void PaintOverlay(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    HBRUSH background = CreateSolidBrush(RGB(20, 23, 30));
    FillRect(dc, &client, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);

    RECT content{28, 18, client.right - 28, client.bottom - 48};
    if (!g_translation.error.empty()) {
        DrawTextBlock(dc, L"TRANSLATION ERROR", g_translation.error, content,
                      RGB(255, 125, 125), RGB(245, 245, 248));
    } else {
        if (g_config.showOriginal) {
            DrawTextBlock(dc, L"ORIGINAL", g_translation.original +
                          (g_translation.originalPinyin.empty()?L"":L"\nPinyin: "+g_translation.originalPinyin), content,
                          RGB(143, 156, 176), RGB(238, 241, 246), g_translation.originalPinyin.empty()?42:72);
        }
        if (g_config.showKaraoke) {
            DrawTextBlock(dc, L"คำอ่านคาราโอเกะภาษาไทย",
                          g_translation.karaoke.empty() ? L"กำลังแปล…" : g_translation.karaoke,
                          content, RGB(92, 220, 175), RGB(255, 255, 255));
        }
        DrawTextBlock(dc, (L"คำแปล · " + g_translation.targetLanguage).c_str(), g_translation.translated +
                      (g_translation.translatedPinyin.empty()?L"":L"\nPinyin: "+g_translation.translatedPinyin), content,
                      RGB(111, 180, 255), RGB(255, 255, 255), g_translation.translatedPinyin.empty()?42:72);
        if (g_config.showExplanation && !g_translation.explanation.empty()) {
            DrawTextBlock(dc, L"อธิบายประโยค", g_translation.explanation, content,
                          RGB(238, 191, 92), RGB(235, 237, 242), 48);
        }
        if (g_config.showWordBreakdown && !g_translation.words.empty()) {
            DrawWordBreakdown(dc, content);
        }
    }

    SelectObject(dc, g_titleFont);
    SetTextColor(dc, RGB(126, 134, 148));
    RECT hint{28, client.bottom - 38, client.right - 28, client.bottom - 12};
    DrawTextW(dc, L"Mouse wheel: more words    |    Esc: hide",
              -1, &hint, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    EndPaint(window, &paint);
}

void UnregisterApplicationHotkeys() {
    UnregisterHotKey(g_window, kHotkeyTranslate);
    UnregisterHotKey(g_window, kHotkeyOcr);
    UnregisterHotKey(g_window, kHotkeyQuit);
}

bool RegisterApplicationHotkeys(const AppConfig& config) {
    const bool translateRegistered = RegisterHotKey(
        g_window, kHotkeyTranslate, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
        config.translateKey) != FALSE;
    const bool ocrRegistered = RegisterHotKey(
        g_window, kHotkeyOcr, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
        config.ocrKey) != FALSE;
    const bool quitRegistered = RegisterHotKey(
        g_window, kHotkeyQuit, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
        config.quitKey) != FALSE;
    return translateRegistered && ocrRegistered && quitRegistered;
}

void OpenSettings() {
    AppConfig candidate;
    if (!ShowSettingsWindow(g_instance, g_window, g_config, candidate)) return;

    const AppConfig previous = g_config;
    UnregisterApplicationHotkeys();
    if (!RegisterApplicationHotkeys(candidate)) {
        UnregisterApplicationHotkeys();
        RegisterApplicationHotkeys(previous);
        MessageBoxW(g_window,
                    L"One of the requested Ctrl+Alt hotkeys is already used by another app. "
                    L"The previous settings are still active.",
                    L"Hotkey conflict", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring error;
    if (!SaveAppConfig(candidate, error)) {
        UnregisterApplicationHotkeys();
        RegisterApplicationHotkeys(previous);
        MessageBoxW(g_window, error.c_str(), L"Could not save settings",
                    MB_OK | MB_ICONERROR);
        return;
    }
    g_config = std::move(candidate);
    SetLayeredWindowAttributes(
        g_window, 0, static_cast<BYTE>(g_config.overlayOpacity * 255 / 100), LWA_ALPHA);
    if (IsWindowVisible(g_window)) PositionAndShowOverlay();
    MessageBoxW(g_window, L"Settings saved and applied.",
                L"Thai Karaoke Overlay", MB_OK | MB_ICONINFORMATION);
}

void ShowTrayMenu(HWND window) {
    POINT point{};
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuWorkspace, L"Point Translator / History / Replies");
    AppendMenuW(menu, MF_STRING, kMenuTranslate, L"Translate clipboard text");
    AppendMenuW(menu, MF_STRING, kMenuOcr, L"Capture text from screen\tCtrl+Alt+O");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit\tCtrl+Alt+Q");
    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK CaptureWindowProcedure(HWND window, UINT message,
                                        WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_LBUTTONDOWN:
            g_captureStart = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            g_captureEnd = g_captureStart;
            g_selectingCapture = true;
            SetCapture(window);
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case WM_MOUSEMOVE:
            if (g_selectingCapture) {
                g_captureEnd = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                InvalidateRect(window, nullptr, TRUE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (g_selectingCapture) {
                g_captureEnd = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                g_selectingCapture = false;
                ReleaseCapture();

                POINT first = g_captureStart;
                POINT last = g_captureEnd;
                ClientToScreen(window, &first);
                ClientToScreen(window, &last);
                RECT rectangle{
                    (std::min)(first.x, last.x),
                    (std::min)(first.y, last.y),
                    (std::max)(first.x, last.x),
                    (std::max)(first.y, last.y)};
                ShowWindow(window, SW_HIDE);
                if (rectangle.right - rectangle.left >= 8 &&
                    rectangle.bottom - rectangle.top >= 8) {
                    StartOcrTranslation(rectangle);
                } else {
                    g_busy = false;
                    WorkspaceBusy(false);
                    ShowWorkspace();
                }
            }
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_selectingCapture = false;
                if (GetCapture() == window) ReleaseCapture();
                ShowWindow(window, SW_HIDE);
                g_busy = false;
                WorkspaceBusy(false);
                ShowWorkspace();
            }
            return 0;
        case WM_RBUTTONDOWN:
            g_selectingCapture = false;
            if (GetCapture() == window) ReleaseCapture();
            ShowWindow(window, SW_HIDE);
            g_busy = false;
            WorkspaceBusy(false);
            ShowWorkspace();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            HBRUSH shade = CreateSolidBrush(RGB(5, 8, 14));
            FillRect(dc, &client, shade);
            DeleteObject(shade);
            SetBkMode(dc, TRANSPARENT);
            SelectObject(dc, g_bodyFont);
            SetTextColor(dc, RGB(255, 255, 255));
            RECT instruction{24, 20, client.right - 24, 65};
            DrawTextW(dc, L"Drag over the game text to OCR  |  Esc/right-click: cancel",
                      -1, &instruction, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (g_selectingCapture) {
                RECT selected{
                    (std::min)(g_captureStart.x, g_captureEnd.x),
                    (std::min)(g_captureStart.y, g_captureEnd.y),
                    (std::max)(g_captureStart.x, g_captureEnd.x),
                    (std::max)(g_captureStart.y, g_captureEnd.y)};
                HPEN pen = CreatePen(PS_SOLID, 4, RGB(52, 230, 174));
                const HGDIOBJ oldPen = SelectObject(dc, pen);
                const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(dc, selected.left, selected.top, selected.right, selected.bottom);
                SelectObject(dc, oldBrush);
                SelectObject(dc, oldPen);
                DeleteObject(pen);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CLOSE:
            // Let an in-flight translation finish and preserve any settings draft.
            // The launcher reports a timeout instead of force-killing the app.
            if (g_busy || !IsWindowEnabled(window)) {
                g_closeDeadline = GetTickCount64() + 14000;
                SetTimer(window, 2, 250, nullptr);
                return 0;
            }
            DestroyWindow(window);
            return 0;
        case WM_SIZE:
            MoveWindow(GetDlgItem(window, kMenuWorkspace), 28,
                       (std::max)(0, static_cast<int>(HIWORD(lParam)) - 42), 160, 28, TRUE);
            return 0;
        case WM_PAINT:
            PaintOverlay(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_HOTKEY:
            if (wParam == kHotkeyTranslate) StartTranslation(true);
            else if (wParam == kHotkeyOcr) BeginOcrCapture();
            else if (wParam == kHotkeyQuit) DestroyWindow(window);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                KillTimer(window, kAutoHideTimer);
                ShowWindow(window, SW_HIDE);
            }
            return 0;
        case WM_TIMER:
            if (wParam == 2) {
                if (GetTickCount64() >= g_closeDeadline) {
                    KillTimer(window, 2);
                    return 0;
                }
                if (!g_busy && IsWindowEnabled(window)) {
                    KillTimer(window, 2);
                    DestroyWindow(window);
                }
                return 0;
            }
            if (wParam == kAutoHideTimer) {
                KillTimer(window, kAutoHideTimer);
                ShowWindow(window, SW_HIDE);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (!g_translation.words.empty()) {
                if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) {
                    g_wordScroll = g_wordScroll > 2 ? g_wordScroll - 3 : 0;
                } else {
                    g_wordScroll = (std::min)(g_wordScroll + 3,
                                               g_translation.words.size() - 1);
                }
                InvalidateRect(window, nullptr, TRUE);
            }
            return 0;
        case kTranslationReady: {
            std::unique_ptr<Translation> completed(reinterpret_cast<Translation*>(lParam));
            g_translation = std::move(*completed);
            g_wordScroll = 0;
            g_busy = false;
            WorkspaceCompleted(g_translation);
            if (!WorkspaceVisible()) PositionAndShowOverlay();
            if (g_config.autoHideSeconds > 0) {
                SetTimer(window, kAutoHideTimer,
                         static_cast<UINT>(g_config.autoHideSeconds * 1000), nullptr);
            }
            return 0;
        }
        case kTrayMessage:
            if (lParam == WM_LBUTTONDBLCLK) ShowWorkspace();
            else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu(window);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kMenuTranslate) StartTranslation(false);
            else if (LOWORD(wParam) == kMenuOcr) BeginOcrCapture();
            else if (LOWORD(wParam) == kMenuSettings) OpenSettings();
            else if (LOWORD(wParam) == kMenuWorkspace) { ShowWindow(window, SW_HIDE); ShowWorkspace(); }
            else if (LOWORD(wParam) == kMenuExit) DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            UnregisterApplicationHotkeys();
            Shell_NotifyIconW(NIM_DELETE, &g_tray);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool AddTrayIcon(HWND window) {
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = window;
    g_tray.uID = kTrayId;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = kTrayMessage;
    g_tray.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
    wcscpy_s(g_tray.szTip, L"Thai Karaoke Overlay");
    return Shell_NotifyIconW(NIM_ADD, &g_tray) != FALSE;
}

}  // namespace

int RunOverlayApplication(HINSTANCE instance) {
    HANDLE singleInstance = CreateMutexW(nullptr, FALSE, L"Local\\PointTranslatorApplication");
    if (!singleInstance) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"PointTranslatorWorkspace", nullptr);
        if (existing) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
        CloseHandle(singleInstance);
        return 0;
    }
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_instance = instance;
    g_config = LoadAppConfig();
    StartArgosService();
    g_titleFont = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_bodyFont = CreateFontW(-22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Leelawadee UI");
    g_smallFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Leelawadee UI");

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
    if (!RegisterClassExW(&windowClass)) return 1;

    WNDCLASSEXW captureClass{};
    captureClass.cbSize = sizeof(captureClass);
    captureClass.hInstance = instance;
    captureClass.lpfnWndProc = CaptureWindowProcedure;
    captureClass.lpszClassName = kCaptureWindowClass;
    captureClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    if (!RegisterClassExW(&captureClass)) return 1;

    g_captureWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                                      kCaptureWindowClass, L"Select text for OCR", WS_POPUP,
                                      0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!g_captureWindow) return 1;
    SetLayeredWindowAttributes(g_captureWindow, 0, 105, LWA_ALPHA);

    g_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                               kWindowClass, L"Thai Karaoke Overlay", WS_POPUP,
                               CW_USEDEFAULT, CW_USEDEFAULT, kOverlayWidth, kOverlayHeight,
                               nullptr, nullptr,
                               instance, nullptr);
    if (!g_window) return 1;
    SetLayeredWindowAttributes(
        g_window, 0, static_cast<BYTE>(g_config.overlayOpacity * 255 / 100), LWA_ALPHA);
    SetWindowRgn(g_window, CreateRoundRectRgn(0, 0, kOverlayWidth, kOverlayHeight,
                                              24, 24), TRUE);
    AddTrayIcon(g_window);
    InitializeWorkspace(instance, g_config,
        [](std::wstring text) { StartTranslation(false, std::move(text)); },
        [] { BeginOcrCapture(); }, [] { OpenSettings(); });
    CreateWindowW(L"BUTTON", L"History / Replies", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  28, kOverlayHeight - 42, 160, 28, g_window,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMenuWorkspace)), instance, nullptr);
    ShowWorkspace();

    if (!RegisterApplicationHotkeys(g_config)) {
        MessageBoxW(nullptr,
                    L"One or more configured Ctrl+Alt hotkeys are already used by another app. "
                    L"Open Settings from the tray icon to change them.",
                    L"Thai Karaoke Overlay", MB_OK | MB_ICONWARNING);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (WorkspaceMessage(message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_titleFont) DeleteObject(g_titleFont);
    if (g_bodyFont) DeleteObject(g_bodyFont);
    if (g_smallFont) DeleteObject(g_smallFont);
    CloseHandle(singleInstance);
    return static_cast<int>(message.wParam);
}



}  // namespace thai_overlay

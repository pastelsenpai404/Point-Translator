#include "app/settings_window.h"

#include "core/text.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thai_overlay {
namespace {

constexpr wchar_t kSettingsClass[] = L"ThaiKaraokeSettingsWindow";
constexpr int kTab = 2000;
constexpr int kApiBase = 2001;
constexpr int kModel = 2002;
constexpr int kApiKey = 2003;
constexpr int kOcrLanguage = 2004;
constexpr int kTranslateKey = 2005;
constexpr int kOcrKey = 2006;
constexpr int kQuitKey = 2007;
constexpr int kShowOriginal = 2020;
constexpr int kShowKaraoke = 2021;
constexpr int kShowExplanation = 2022;
constexpr int kShowWords = 2023;
constexpr int kOpacity = 2030;
constexpr int kOpacityValue = 2031;
constexpr int kPosition = 2032;
constexpr int kAutoHide = 2033;
constexpr int kSave = 2090;
constexpr int kCancel = 2091;
constexpr size_t kPageCount = 5;

struct SettingsState {
    AppConfig draft;
    bool accepted = false;
    HFONT font = nullptr;
    HFONT headingFont = nullptr;
    HWND tab = nullptr;
    std::array<std::vector<HWND>, kPageCount> pageControls;
};

std::wstring ReadControlText(HWND window, int id) {
    const HWND control = GetDlgItem(window, id);
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return Trim(value);
}

void ApplyFont(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void AddToPage(SettingsState& state, size_t page, HWND control) {
    state.pageControls[page].push_back(control);
}

HWND AddLabel(HWND window, SettingsState& state, size_t page,
              const wchar_t* text, int x, int y, int width,
              int height = 24, bool heading = false) {
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                 x, y, width, height, window, nullptr, nullptr, nullptr);
    ApplyFont(label, heading ? state.headingFont : state.font);
    AddToPage(state, page, label);
    return label;
}

HWND AddEdit(HWND window, SettingsState& state, size_t page, int id,
             const std::wstring& value, int x, int y, int width,
             DWORD extraStyle = 0) {
    HWND edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extraStyle,
        x, y, width, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr, nullptr);
    ApplyFont(edit, state.font);
    AddToPage(state, page, edit);
    return edit;
}

HWND AddCheckbox(HWND window, SettingsState& state, size_t page, int id,
                 const wchar_t* text, bool checked, int x, int y, int width) {
    HWND checkbox = CreateWindowExW(
        0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        x, y, width, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr, nullptr);
    ApplyFont(checkbox, state.font);
    Button_SetCheck(checkbox, checked ? BST_CHECKED : BST_UNCHECKED);
    AddToPage(state, page, checkbox);
    return checkbox;
}

void ShowPage(SettingsState& state, size_t selectedPage) {
    for (size_t page = 0; page < state.pageControls.size(); ++page) {
        for (HWND control : state.pageControls[page]) {
            ShowWindow(control, page == selectedPage ? SW_SHOW : SW_HIDE);
        }
    }
}

unsigned int ParseKey(HWND window, int id, const wchar_t* label, bool& valid) {
    const std::wstring value = ReadControlText(window, id);
    if (value.size() != 1) {
        MessageBoxW(window, (std::wstring(label) + L" must contain one letter or number.").c_str(),
                    L"Invalid settings", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(window, id));
        valid = false;
        return 0;
    }
    const wchar_t key = static_cast<wchar_t>(std::towupper(value.front()));
    if (!((key >= L'A' && key <= L'Z') || (key >= L'0' && key <= L'9'))) {
        MessageBoxW(window, (std::wstring(label) + L" must be A-Z or 0-9.").c_str(),
                    L"Invalid settings", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(window, id));
        valid = false;
        return 0;
    }
    return static_cast<unsigned int>(key);
}

bool ParseRange(HWND window, int id, const wchar_t* label,
                int minimum, int maximum, int& result) {
    try {
        const std::wstring value = ReadControlText(window, id);
        size_t parsedCharacters = 0;
        const int parsed = std::stoi(value, &parsedCharacters);
        if (parsedCharacters != value.size() || parsed < minimum || parsed > maximum) {
            throw std::out_of_range("range");
        }
        result = parsed;
        return true;
    } catch (...) {
        const std::wstring message = std::wstring(label) + L" must be between " +
            std::to_wstring(minimum) + L" and " + std::to_wstring(maximum) + L".";
        MessageBoxW(window, message.c_str(), L"Invalid settings", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(window, id));
        return false;
    }
}

bool ReadSettings(HWND window, SettingsState& state) {
    AppConfig candidate = state.draft;
    candidate.apiBase = ReadControlText(window, kApiBase);
    candidate.model = ReadControlText(window, kModel);
    candidate.apiKey = ReadControlText(window, kApiKey);
    candidate.ocrLanguage = ReadControlText(window, kOcrLanguage);
    while (!candidate.apiBase.empty() && candidate.apiBase.back() == L'/') {
        candidate.apiBase.pop_back();
    }
    if (candidate.apiBase.empty() || candidate.model.empty() || candidate.ocrLanguage.empty()) {
        MessageBoxW(window, L"API Base, Model, and OCR Language are required.",
                    L"Invalid settings", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (candidate.apiBase.rfind(L"http://", 0) != 0 &&
        candidate.apiBase.rfind(L"https://", 0) != 0) {
        MessageBoxW(window, L"API Base must start with http:// or https://.",
                    L"Invalid settings", MB_OK | MB_ICONWARNING);
        return false;
    }

    candidate.showOriginal = Button_GetCheck(GetDlgItem(window, kShowOriginal)) == BST_CHECKED;
    candidate.showKaraoke = Button_GetCheck(GetDlgItem(window, kShowKaraoke)) == BST_CHECKED;
    candidate.showExplanation = Button_GetCheck(GetDlgItem(window, kShowExplanation)) == BST_CHECKED;
    candidate.showWordBreakdown = Button_GetCheck(GetDlgItem(window, kShowWords)) == BST_CHECKED;
    candidate.overlayOpacity = static_cast<int>(
        SendMessageW(GetDlgItem(window, kOpacity), TBM_GETPOS, 0, 0));
    candidate.overlayPosition = ReadControlText(window, kPosition);
    if (!ParseRange(window, kAutoHide, L"Auto-hide seconds", 0, 300,
                    candidate.autoHideSeconds)) return false;

    bool valid = true;
    candidate.translateKey = ParseKey(window, kTranslateKey, L"Translate hotkey", valid);
    if (!valid) return false;
    candidate.ocrKey = ParseKey(window, kOcrKey, L"OCR hotkey", valid);
    if (!valid) return false;
    candidate.quitKey = ParseKey(window, kQuitKey, L"Quit hotkey", valid);
    if (!valid) return false;
    if (candidate.translateKey == candidate.ocrKey ||
        candidate.translateKey == candidate.quitKey ||
        candidate.ocrKey == candidate.quitKey) {
        MessageBoxW(window, L"Each hotkey must use a different key.",
                    L"Invalid settings", MB_OK | MB_ICONWARNING);
        return false;
    }

    state.draft = std::move(candidate);
    return true;
}

void BuildProviderPage(HWND window, SettingsState& state) {
    constexpr size_t page = 0;
    AddLabel(window, state, page, L"Translation Provider", 48, 75, 360, 28, true);
    const bool local = state.draft.apiBase.find(L"localhost") != std::wstring::npos ||
                       state.draft.apiBase.find(L"127.0.0.1") != std::wstring::npos;
    AddLabel(window, state, page,
             local ? L"● Local AI — text stays on this computer"
                   : L"● Cloud provider — selected text leaves this computer",
             48, 110, 600);
    AddLabel(window, state, page, L"API Base", 48, 157, 130);
    AddEdit(window, state, page, kApiBase, state.draft.apiBase, 180, 150, 450);
    AddLabel(window, state, page, L"Model", 48, 207, 130);
    AddEdit(window, state, page, kModel, state.draft.model, 180, 200, 450);
    AddLabel(window, state, page, L"API Key", 48, 257, 130);
    AddEdit(window, state, page, kApiKey, state.draft.apiKey, 180, 250, 450, ES_PASSWORD);
    AddLabel(window, state, page,
             L"API key is stored in your Windows user environment, never in config.ini.",
             180, 286, 460, 40);
}

void BuildReadingPage(HWND window, SettingsState& state) {
    constexpr size_t page = 1;
    AddLabel(window, state, page, L"Reading & Learning", 48, 75, 360, 28, true);
    AddLabel(window, state, page,
             L"Choose the learning details displayed after every translation.",
             48, 110, 600);
    AddCheckbox(window, state, page, kShowOriginal, L"Show original recognized text",
                state.draft.showOriginal, 48, 155, 420);
    AddCheckbox(window, state, page, kShowKaraoke, L"Show Thai karaoke pronunciation",
                state.draft.showKaraoke, 48, 195, 420);
    AddCheckbox(window, state, page, kShowExplanation, L"Show grammar, tone, and slang explanation",
                state.draft.showExplanation, 48, 235, 500);
    AddCheckbox(window, state, page, kShowWords, L"Show word-by-word learning table",
                state.draft.showWordBreakdown, 48, 275, 500);
    AddLabel(window, state, page,
             L"Tip: use the mouse wheel over the Overlay when a sentence has many words.",
             48, 330, 600, 45);
}

void BuildOcrPage(HWND window, SettingsState& state) {
    constexpr size_t page = 2;
    AddLabel(window, state, page, L"Screen OCR", 48, 75, 360, 28, true);
    AddLabel(window, state, page,
             L"Capture text from games, images, subtitles, and apps that block copying.",
             48, 110, 610, 40);
    AddLabel(window, state, page, L"OCR Language", 48, 175, 130);
    AddEdit(window, state, page, kOcrLanguage, state.draft.ocrLanguage, 180, 168, 260);
    AddLabel(window, state, page,
             L"auto = run every installed Windows OCR language and let AI select the best result.",
             48, 215, 600, 50);
    AddLabel(window, state, page,
             L"Examples: zh-Hans-CN (Simplified Chinese), en-US (English), ja-JP (Japanese)",
             48, 280, 610, 45);
}

void BuildAppearancePage(HWND window, SettingsState& state) {
    constexpr size_t page = 3;
    AddLabel(window, state, page, L"Overlay Appearance", 48, 75, 360, 28, true);
    AddLabel(window, state, page, L"Opacity", 48, 145, 130);
    HWND slider = CreateWindowExW(
        0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
        180, 132, 330, 45, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpacity)), nullptr, nullptr);
    SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(65, 100));
    SendMessageW(slider, TBM_SETPOS, TRUE, state.draft.overlayOpacity);
    AddToPage(state, page, slider);
    AddLabel(window, state, page,
             (std::to_wstring(state.draft.overlayOpacity) + L"%").c_str(),
             525, 142, 70);
    SetWindowLongPtrW(state.pageControls[page].back(), GWLP_ID, kOpacityValue);

    AddLabel(window, state, page, L"Position", 48, 215, 130);
    HWND position = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_COMBOBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        180, 207, 220, 150, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPosition)), nullptr, nullptr);
    ApplyFont(position, state.font);
    for (const wchar_t* value : {L"bottom", L"center", L"top"}) {
        SendMessageW(position, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    const int selected = state.draft.overlayPosition == L"top" ? 2 :
                         state.draft.overlayPosition == L"center" ? 1 : 0;
    SendMessageW(position, CB_SETCURSEL, selected, 0);
    AddToPage(state, page, position);

    AddLabel(window, state, page, L"Auto-hide", 48, 285, 130);
    AddEdit(window, state, page, kAutoHide,
            std::to_wstring(state.draft.autoHideSeconds), 180, 278, 90);
    AddLabel(window, state, page, L"seconds (0 = keep open until Esc)", 282, 285, 300);
}

void BuildShortcutsPage(HWND window, SettingsState& state) {
    constexpr size_t page = 4;
    AddLabel(window, state, page, L"Global Shortcuts", 48, 75, 360, 28, true);
    AddLabel(window, state, page,
             L"Every shortcut uses Ctrl + Alt plus one letter or number.",
             48, 110, 600);
    AddLabel(window, state, page, L"Translate selected text", 48, 170, 240);
    HWND translate = AddEdit(window, state, page, kTranslateKey,
        std::wstring(1, static_cast<wchar_t>(state.draft.translateKey)), 320, 163, 60);
    AddLabel(window, state, page, L"Capture screen OCR", 48, 220, 240);
    HWND ocr = AddEdit(window, state, page, kOcrKey,
        std::wstring(1, static_cast<wchar_t>(state.draft.ocrKey)), 320, 213, 60);
    AddLabel(window, state, page, L"Exit application", 48, 270, 240);
    HWND quit = AddEdit(window, state, page, kQuitKey,
        std::wstring(1, static_cast<wchar_t>(state.draft.quitKey)), 320, 263, 60);
    SendMessageW(translate, EM_SETLIMITTEXT, 1, 0);
    SendMessageW(ocr, EM_SETLIMITTEXT, 1, 0);
    SendMessageW(quit, EM_SETLIMITTEXT, 1, 0);
}

LRESULT CALLBACK SettingsProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<SettingsState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
        case WM_CREATE: {
            state->font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            state->headingFont = CreateFontW(-23, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                             DEFAULT_PITCH, L"Segoe UI");
            state->tab = CreateWindowExW(
                0, WC_TABCONTROLW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                18, 18, 674, 465, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTab)), nullptr, nullptr);
            ApplyFont(state->tab, state->font);
            const std::array<const wchar_t*, kPageCount> tabs{
                L"Provider", L"Reading", L"OCR", L"Appearance", L"Shortcuts"};
            for (size_t index = 0; index < tabs.size(); ++index) {
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<wchar_t*>(tabs[index]);
                TabCtrl_InsertItem(state->tab, static_cast<int>(index), &item);
            }
            BuildProviderPage(window, *state);
            BuildReadingPage(window, *state);
            BuildOcrPage(window, *state);
            BuildAppearancePage(window, *state);
            BuildShortcutsPage(window, *state);
            ShowPage(*state, 0);

            HWND save = CreateWindowExW(
                0, L"BUTTON", L"Save & Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                BS_DEFPUSHBUTTON, 490, 502, 105, 34, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSave)), nullptr, nullptr);
            HWND cancel = CreateWindowExW(
                0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                605, 502, 86, 34, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancel)), nullptr, nullptr);
            ApplyFont(save, state->font);
            ApplyFont(cancel, state->font);
            return 0;
        }
        case WM_NOTIFY:
            if (state && reinterpret_cast<NMHDR*>(lParam)->idFrom == kTab &&
                reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE) {
                ShowPage(*state, static_cast<size_t>(TabCtrl_GetCurSel(state->tab)));
            }
            return 0;
        case WM_HSCROLL:
            if (state && reinterpret_cast<HWND>(lParam) == GetDlgItem(window, kOpacity)) {
                const int value = static_cast<int>(
                    SendMessageW(GetDlgItem(window, kOpacity), TBM_GETPOS, 0, 0));
                SetWindowTextW(GetDlgItem(window, kOpacityValue),
                               (std::to_wstring(value) + L"%").c_str());
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kSave && ReadSettings(window, *state)) {
                state->accepted = true;
                DestroyWindow(window);
            } else if (LOWORD(wParam) == kCancel) {
                DestroyWindow(window);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool EnsureSettingsClass(HINSTANCE instance) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(instance, kSettingsClass, &existing)) return true;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = SettingsProcedure;
    windowClass.lpszClassName = kSettingsClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
    return RegisterClassExW(&windowClass) != 0;
}

}  // namespace

bool ShowSettingsWindow(HINSTANCE instance, HWND owner,
                        const AppConfig& current, AppConfig& updated) {
    if (!EnsureSettingsClass(instance)) return false;
    SettingsState state;
    state.draft = current;
    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kSettingsClass, L"Thai Karaoke Overlay — Control Center",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 728, 590,
        owner, nullptr, instance, &state);
    if (!window) return false;

    RECT bounds{};
    GetWindowRect(window, &bounds);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    SetWindowPos(window, HWND_TOP, work.left + (work.right - work.left - width) / 2,
                 work.top + (work.bottom - work.top - height) / 2,
                 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    EnableWindow(owner, FALSE);

    MSG message{};
    bool receivedQuit = false;
    int quitCode = 0;
    while (IsWindow(window)) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            receivedQuit = result == 0;
            quitCode = static_cast<int>(message.wParam);
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (state.font) DeleteObject(state.font);
    if (state.headingFont) DeleteObject(state.headingFont);
    if (receivedQuit) PostQuitMessage(quitCode);
    if (state.accepted) updated = std::move(state.draft);
    return state.accepted;
}

}  // namespace thai_overlay

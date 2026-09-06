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
constexpr int kLocalPreset = 2100, kCloudPreset = 2101, kRevealKey = 2102;
constexpr int kStatus = 2103, kResetAppearance = 2104, kResetKeys = 2105;
constexpr int kProviderStatus = 2106;
const wchar_t* kPageNames[] = {L"AI และการแปล",L"การอ่านและเรียนรู้",L"อ่านข้อความจากภาพ",L"หน้าต่างแปล",L"ปุ่มลัด"};

struct SettingsState {
    AppConfig draft;
    bool accepted = false;
    HFONT font = nullptr;
    HFONT headingFont = nullptr;
    HWND tab = nullptr;
    HBRUSH background = nullptr;
    HBRUSH white = nullptr;
    bool building = true;
    size_t selectedPage = 0;
    struct Placement { HWND control; RECT rectangle; };
    std::vector<Placement> placements;
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
    if (heading) height = (std::max)(height, 36);
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
    state.selectedPage = selectedPage;
    SendMessageW(state.tab, LB_SETCURSEL, selectedPage, 0);
    for (size_t page = 0; page < state.pageControls.size(); ++page) {
        for (HWND control : state.pageControls[page]) {
            ShowWindow(control, page == selectedPage ? SW_SHOW : SW_HIDE);
        }
    }
}

unsigned int ParseKey(HWND window, int id, const wchar_t* label, bool& valid) {
    const std::wstring value = ReadControlText(window, id);
    if (value.size() != 1) {
        MessageBoxW(window, (std::wstring(label) + L" ต้องเป็นตัวอักษรหรือตัวเลขหนึ่งตัว").c_str(),
                    L"ตรวจสอบการตั้งค่า", MB_OK | MB_ICONWARNING);
        auto* state=reinterpret_cast<SettingsState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(state) ShowPage(*state,id==kAutoHide?3:4);
        SetFocus(GetDlgItem(window, id));
        valid = false;
        return 0;
    }
    const wchar_t key = static_cast<wchar_t>(std::towupper(value.front()));
    if (!((key >= L'A' && key <= L'Z') || (key >= L'0' && key <= L'9'))) {
        MessageBoxW(window, (std::wstring(label) + L" ต้องเป็น A–Z หรือ 0–9").c_str(),
                    L"ตรวจสอบการตั้งค่า", MB_OK | MB_ICONWARNING);
        auto* state=reinterpret_cast<SettingsState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(state) ShowPage(*state,id==kAutoHide?3:4);
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
        const std::wstring message = std::wstring(label) + L" ต้องอยู่ระหว่าง " +
            std::to_wstring(minimum) + L" ถึง " + std::to_wstring(maximum) + L".";
        MessageBoxW(window, message.c_str(), L"ตรวจสอบการตั้งค่า", MB_OK | MB_ICONWARNING);
        auto* state=reinterpret_cast<SettingsState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(state) ShowPage(*state,id==kAutoHide?3:4);
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
        MessageBoxW(window, L"กรุณากรอกที่อยู่บริการ ชื่อโมเดล และภาษา OCR",
                    L"ตรวจสอบการตั้งค่า", MB_OK | MB_ICONWARNING);
        ShowPage(state, candidate.ocrLanguage.empty()?2:0);
        return false;
    }
    if (candidate.apiBase.rfind(L"http://", 0) != 0 &&
        candidate.apiBase.rfind(L"https://", 0) != 0) {
        MessageBoxW(window, L"ที่อยู่บริการต้องเริ่มด้วย http:// หรือ https://",
                    L"ตรวจสอบการตั้งค่า", MB_OK | MB_ICONWARNING);
        ShowPage(state,0);
        return false;
    }

    candidate.showOriginal = Button_GetCheck(GetDlgItem(window, kShowOriginal)) == BST_CHECKED;
    candidate.showKaraoke = Button_GetCheck(GetDlgItem(window, kShowKaraoke)) == BST_CHECKED;
    candidate.showExplanation = Button_GetCheck(GetDlgItem(window, kShowExplanation)) == BST_CHECKED;
    candidate.showWordBreakdown = Button_GetCheck(GetDlgItem(window, kShowWords)) == BST_CHECKED;
    candidate.overlayOpacity = static_cast<int>(
        SendMessageW(GetDlgItem(window, kOpacity), TBM_GETPOS, 0, 0));
    const int position=static_cast<int>(SendMessageW(GetDlgItem(window,kPosition),CB_GETCURSEL,0,0));
    candidate.overlayPosition=position==2?L"top":position==1?L"center":L"bottom";
    if (!ParseRange(window, kAutoHide, L"เวลาซ่อนอัตโนมัติ", 0, 300,
                    candidate.autoHideSeconds)) return false;

    bool valid = true;
    candidate.translateKey = ParseKey(window, kTranslateKey, L"ปุ่มแปลข้อความ", valid);
    if (!valid) return false;
    candidate.ocrKey = ParseKey(window, kOcrKey, L"ปุ่มจับภาพ", valid);
    if (!valid) return false;
    candidate.quitKey = ParseKey(window, kQuitKey, L"ปุ่มออกจากโปรแกรม", valid);
    if (!valid) return false;
    if (candidate.translateKey == candidate.ocrKey ||
        candidate.translateKey == candidate.quitKey ||
        candidate.ocrKey == candidate.quitKey) {
        MessageBoxW(window, L"ปุ่มลัดแต่ละคำสั่งต้องใช้คนละปุ่ม",
                    L"ตรวจสอบการตั้งค่า", MB_OK | MB_ICONWARNING);
        ShowPage(state,4);
        return false;
    }

    state.draft = std::move(candidate);
    return true;
}

HWND AddButton(HWND window, SettingsState& state, size_t page, int id,
               const wchar_t* text, int x, int y, int width) {
    HWND button = CreateWindowW(L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP,
        x,y,width,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
    ApplyFont(button,state.font); AddToPage(state,page,button); return button;
}

void ProviderStatus(HWND window) {
    const auto url=ReadControlText(window,kApiBase);
    const auto end=url.find(L'/',url.find(L"://")==std::wstring::npos?0:url.find(L"://")+3);
    auto host=url.substr(0,end);
    const bool local=host==L"http://localhost" || host.rfind(L"http://localhost:",0)==0 ||
        host==L"http://127.0.0.1" || host.rfind(L"http://127.0.0.1:",0)==0;
    SetWindowTextW(GetDlgItem(window,kProviderStatus),local
        ?L"บนเครื่องนี้  ·  ใช้ AI ผ่านบริการภายในเครื่อง"
        :L"บริการที่กำหนดเอง  ·  ข้อความจะถูกส่งไปยังที่อยู่นี้");
}

void BuildProviderPage(HWND window, SettingsState& state) {
    constexpr size_t page=0;
    AddLabel(window,state,page,L"AI และการแปล",48,75,580,32,true);
    AddLabel(window,state,page,L"ตั้งค่าโมเดลที่ใช้แปล อธิบายบริบท และช่วยร่างคำตอบ",48,116,600);
    AddButton(window,state,page,kLocalPreset,L"ใช้ Ollama บนเครื่อง",48,155,230);
    AddButton(window,state,page,kCloudPreset,L"ใช้ OpenAI",290,155,180);
    auto status=AddLabel(window,state,page,L"",48,202,600,28);
    SetWindowLongPtrW(status,GWLP_ID,kProviderStatus);
    AddLabel(window,state,page,L"ที่อยู่บริการ AI",48,246,135);
    AddEdit(window,state,page,kApiBase,state.draft.apiBase,190,240,430);
    AddLabel(window,state,page,L"ชื่อโมเดล",48,292,135);
    AddEdit(window,state,page,kModel,state.draft.model,190,286,430);
    AddLabel(window,state,page,L"API key",48,338,135);
    AddEdit(window,state,page,kApiKey,state.draft.apiKey,190,332,324,ES_PASSWORD);
    AddButton(window,state,page,kRevealKey,L"แสดง",526,330,94);
    AddLabel(window,state,page,L"Ollama ไม่ต้องใส่ key · key เก็บในบัญชี Windows ของคุณ",190,374,435,42);
    AddLabel(window,state,page,L"เลือกคู่ภาษาและ AI / Argos + AI ได้จากหน้าหลัก\n"
        L"Argos แปลบนเครื่อง ส่วนคำอ่านและคำตอบแนะนำใช้ AI ด้านบน",48,432,590,64);
    ProviderStatus(window);
}

void BuildReadingPage(HWND window, SettingsState& state) {
    constexpr size_t page = 1;
    AddLabel(window, state, page, L"การอ่านและเรียนรู้", 48, 75, 360, 28, true);
    AddLabel(window, state, page,
             L"เลือกรายละเอียดที่แสดงบนหน้าต่างแปลแบบลอย (Overlay)",
             48, 110, 600);
    AddCheckbox(window, state, page, kShowOriginal, L"แสดงข้อความต้นฉบับ",
                state.draft.showOriginal, 48, 155, 420);
    AddCheckbox(window, state, page, kShowKaraoke, L"แสดงคำอ่านด้วยตัวอักษรไทย",
                state.draft.showKaraoke, 48, 195, 420);
    AddCheckbox(window, state, page, kShowExplanation, L"แสดงคำอธิบายไวยากรณ์ น้ำเสียง และสแลง",
                state.draft.showExplanation, 48, 235, 500);
    AddCheckbox(window, state, page, kShowWords, L"แสดงคำศัพท์และคำอธิบายรายคำ",
                state.draft.showWordBreakdown, 48, 275, 500);
    AddLabel(window, state, page,
             L"หน้าหลักยังเก็บรายละเอียดครบทุกแท็บ\nใช้ล้อเมาส์เลื่อนดูคำศัพท์ในหน้าต่างแปลได้",
             48, 330, 600, 45);
}

void BuildOcrPage(HWND window, SettingsState& state) {
    constexpr size_t page = 2;
    AddLabel(window, state, page, L"อ่านข้อความจากภาพ", 48, 75, 360, 28, true);
    AddLabel(window, state, page,
             L"จับข้อความจากเกม รูปภาพ หรือแอปที่เลือกคัดลอกข้อความไม่ได้",
             48, 110, 610, 40);
    AddLabel(window, state, page, L"ภาษา OCR", 48, 175, 130);
    HWND language=CreateWindowW(WC_COMBOBOXW,state.draft.ocrLanguage.c_str(),
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL,
        180,168,300,180,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOcrLanguage)),nullptr,nullptr);
    ApplyFont(language,state.font); AddToPage(state,page,language);
    for(const wchar_t* code:{L"auto",L"zh-Hans-CN",L"en-US",L"th-TH"})
        SendMessageW(language,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(code));
    SetWindowTextW(language,state.draft.ocrLanguage.c_str());
    AddLabel(window, state, page,
             L"auto = ใช้ภาษาต้นทางที่เลือกไว้ในหน้าหลัก",
             48, 215, 600, 50);
    AddLabel(window, state, page,
             L"เลือกจากรายการหรือพิมพ์รหัสภาษาเองได้\nภาษานั้นต้องมีชุด OCR ติดตั้งอยู่ใน Windows",
             48, 280, 610, 45);
}

void BuildAppearancePage(HWND window, SettingsState& state) {
    constexpr size_t page = 3;
    AddLabel(window, state, page, L"หน้าต่างแปลแบบลอย", 48, 75, 360, 28, true);
    AddLabel(window, state, page, L"ความทึบ", 48, 145, 130);
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

    AddLabel(window, state, page, L"ตำแหน่ง", 48, 215, 130);
    HWND position = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_COMBOBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        180, 207, 220, 150, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPosition)), nullptr, nullptr);
    ApplyFont(position, state.font);
    for (const wchar_t* value : {L"ด้านล่าง", L"กึ่งกลาง", L"ด้านบน"}) {
        SendMessageW(position, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    const int selected = state.draft.overlayPosition == L"top" ? 2 :
                         state.draft.overlayPosition == L"center" ? 1 : 0;
    SendMessageW(position, CB_SETCURSEL, selected, 0);
    AddToPage(state, page, position);

    AddLabel(window, state, page, L"ซ่อนอัตโนมัติ", 48, 285, 130);
    AddEdit(window, state, page, kAutoHide,
            std::to_wstring(state.draft.autoHideSeconds), 180, 278, 90);
    AddLabel(window, state, page, L"วินาที · 0 = แสดงค้างจนกด Esc", 282, 285, 340);
    AddButton(window,state,page,kResetAppearance,L"คืนค่าหน้าต่างแปล",48,350,240);
    AddLabel(window,state,page,L"ความทึบสูงทำให้อ่านชัด · ความทึบต่ำช่วยให้มองเห็นพื้นหลัง\nการเปลี่ยนแปลงจะมีผลเมื่อกดบันทึก",48,405,590,60);
}

void BuildShortcutsPage(HWND window, SettingsState& state) {
    constexpr size_t page = 4;
    AddLabel(window, state, page, L"ปุ่มลัด", 48, 75, 360, 28, true);
    AddLabel(window, state, page,
             L"ใช้ได้จากทุกแอป · กด Ctrl + Alt ร่วมกับปุ่มที่เลือก",
             48, 110, 600);
    AddLabel(window, state, page, L"แปลข้อความที่เลือก", 48, 170, 150);
    HWND translate = AddEdit(window, state, page, kTranslateKey,
        std::wstring(1, static_cast<wchar_t>(state.draft.translateKey)), 320, 163, 60);
    AddLabel(window, state, page, L"อ่านข้อความจากภาพ", 48, 220, 150);
    HWND ocr = AddEdit(window, state, page, kOcrKey,
        std::wstring(1, static_cast<wchar_t>(state.draft.ocrKey)), 320, 213, 60);
    AddLabel(window, state, page, L"ออกจากโปรแกรม", 48, 270, 150);
    HWND quit = AddEdit(window, state, page, kQuitKey,
        std::wstring(1, static_cast<wchar_t>(state.draft.quitKey)), 320, 263, 60);
    SendMessageW(translate, EM_SETLIMITTEXT, 1, 0);
    SendMessageW(ocr, EM_SETLIMITTEXT, 1, 0);
    SendMessageW(quit, EM_SETLIMITTEXT, 1, 0);
    for(int y:{170,220,270}) AddLabel(window,state,page,L"Ctrl + Alt +",200,y,110);
    AddButton(window,state,page,kResetKeys,L"คืนค่าปุ่มลัด T / O / Q",48,350,260);
    AddLabel(window,state,page,L"หากปุ่มถูกใช้โดยแอปอื่น โปรแกรมจะแจ้งเมื่อบันทึก",48,402,580,48);
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
            state->background=CreateSolidBrush(RGB(242,246,251));
            state->white=CreateSolidBrush(RGB(255,255,255));
            state->font = CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Leelawadee UI");
            state->headingFont = CreateFontW(-26,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Leelawadee UI");
            auto control=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id) {
                HWND child=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
                ApplyFont(child,state->font); return child;
            };
            auto title=control(L"STATIC",L"ตั้งค่า Point Translator",0,28,22,700,38,2200);
            ApplyFont(title,state->headingFont);
            control(L"STATIC",L"ปรับการแปลและการอ่านให้เข้ากับวิธีใช้งานของคุณ",0,28,65,830,28,2201);
            state->tab=control(L"LISTBOX",L"",WS_TABSTOP|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,
                24,125,180,330,kTab);
            SendMessageW(state->tab,LB_SETITEMHEIGHT,0,52);
            for(const auto* name:kPageNames) SendMessageW(state->tab,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
            control(L"STATIC",L"POINT TRANSLATOR\n\nการตั้งค่าจะมีผล\nหลังจากกดบันทึก",0,34,484,160,105,2202);
            BuildProviderPage(window,*state); BuildReadingPage(window,*state);
            BuildOcrPage(window,*state); BuildAppearancePage(window,*state); BuildShortcutsPage(window,*state);
            for(const auto& page:state->pageControls) for(HWND child:page) {
                RECT r{}; GetWindowRect(child,&r); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
                SetWindowPos(child,nullptr,r.left+180,r.top+48,0,0,SWP_NOSIZE|SWP_NOZORDER);
            }
            ShowPage(*state,0);
            control(L"STATIC",L"พร้อมปรับแต่ง · กดยกเลิกเพื่อกลับโดยไม่บันทึก",0,28,642,560,38,kStatus);
            control(L"BUTTON",L"ยกเลิก",WS_TABSTOP,628,638,118,38,kCancel);
            control(L"BUTTON",L"บันทึกการตั้งค่า",WS_TABSTOP|BS_DEFPUSHBUTTON,760,638,156,38,kSave);
            for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
                RECT r{}; GetWindowRect(child,&r); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
                wchar_t type[64]{}; GetClassNameW(child,type,64);
                if(lstrcmpiW(type,WC_COMBOBOXW)==0) r.bottom=r.top+180;
                state->placements.push_back({child,r});
            }
            state->building=false;
            return 0;
        }
        case WM_SIZE:
            if(state && !state->building) {
                const double scale=(std::min)(LOWORD(lParam)/940.0,HIWORD(lParam)/700.0);
                if(scale<=0) return 0;
                for(const auto& item:state->placements) {
                    const auto& r=item.rectangle;
                    MoveWindow(item.control,static_cast<int>(r.left*scale),static_cast<int>(r.top*scale),
                        static_cast<int>((r.right-r.left)*scale),static_cast<int>((r.bottom-r.top)*scale),TRUE);
                }
                HFONT old=state->font, oldHeading=state->headingFont;
                state->font=CreateFontW(-static_cast<int>(17*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Leelawadee UI");
                state->headingFont=CreateFontW(-static_cast<int>(26*scale),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Leelawadee UI");
                for(const auto& item:state->placements) {
                    const bool heading=reinterpret_cast<HFONT>(SendMessageW(item.control,WM_GETFONT,0,0))==oldHeading;
                    ApplyFont(item.control,heading?state->headingFont:state->font);
                }
                SendMessageW(state->tab,LB_SETITEMHEIGHT,0,static_cast<LPARAM>(52*scale));
                DeleteObject(old); DeleteObject(oldHeading); InvalidateRect(window,nullptr,TRUE);
            }
            return 0;
        case WM_DPICHANGED: {
            const auto* r=reinterpret_cast<RECT*>(lParam);
            SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);
            return 0;
        }
        case WM_ERASEBKGND:
            if(state && state->background) {
                RECT r{}; GetClientRect(window,&r); FillRect(reinterpret_cast<HDC>(wParam),&r,state->background);
                const double scale=(std::min)(r.right/940.0,r.bottom/700.0);
                RECT card{static_cast<LONG>(216*scale),static_cast<LONG>(108*scale),static_cast<LONG>(916*scale),static_cast<LONG>(616*scale)};
                FillRect(reinterpret_cast<HDC>(wParam),&card,state->white);
                return 1;
            } break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            if(state && state->white) {
                const HWND child=reinterpret_cast<HWND>(lParam);
                bool content=false;
                for(const auto& page:state->pageControls)
                    if(std::find(page.begin(),page.end(),child)!=page.end()) content=true;
                const HDC dc=reinterpret_cast<HDC>(wParam);
                SetTextColor(dc,RGB(35,51,72)); SetBkColor(dc,content?RGB(255,255,255):RGB(242,246,251));
                return reinterpret_cast<LRESULT>(content?state->white:state->background);
            } break;
        case WM_DRAWITEM:
            if(state && wParam==kTab) {
                const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                if(item->itemID>=kPageCount) return TRUE;
                const bool selected=(item->itemState&ODS_SELECTED)!=0;
                HBRUSH brush=CreateSolidBrush(selected?RGB(219,234,254):RGB(242,246,251));
                FillRect(item->hDC,&item->rcItem,brush); DeleteObject(brush);
                SetBkMode(item->hDC,TRANSPARENT); SetTextColor(item->hDC,selected?RGB(29,78,160):RGB(76,91,111));
                SelectObject(item->hDC,state->font); RECT r=item->rcItem; r.left+=10;
                DrawTextW(item->hDC,kPageNames[item->itemID],-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
                if(item->itemState&ODS_FOCUS) DrawFocusRect(item->hDC,&item->rcItem);
                return TRUE;
            } break;
        case WM_HSCROLL:
            if (state && reinterpret_cast<HWND>(lParam) == GetDlgItem(window, kOpacity)) {
                const int value = static_cast<int>(
                    SendMessageW(GetDlgItem(window, kOpacity), TBM_GETPOS, 0, 0));
                SetWindowTextW(GetDlgItem(window, kOpacityValue),
                               (std::to_wstring(value) + L"%").c_str());
                SetWindowTextW(GetDlgItem(window,kStatus),L"แก้ไขแล้ว · กดบันทึกเพื่อนำการตั้งค่าไปใช้");
            }
            return 0;
        case WM_COMMAND:
            if(!state) return 0;
            if(LOWORD(wParam)==kTab && HIWORD(wParam)==LBN_SELCHANGE) {
                const auto page=SendMessageW(state->tab,LB_GETCURSEL,0,0);
                if(page>=0 && page<static_cast<LRESULT>(kPageCount)) ShowPage(*state,static_cast<size_t>(page));
                InvalidateRect(window,nullptr,TRUE); return 0;
            }
            if(LOWORD(wParam)==kLocalPreset || LOWORD(wParam)==kCloudPreset) {
                const bool local=LOWORD(wParam)==kLocalPreset;
                const std::wstring nextBase=local?L"http://127.0.0.1:11434":L"https://api.openai.com";
                if(ReadControlText(window,kApiBase)!=nextBase)
                    SetWindowTextW(GetDlgItem(window,kApiKey),L"");
                SetWindowTextW(GetDlgItem(window,kApiBase),local?L"http://127.0.0.1:11434":L"https://api.openai.com");
                SetWindowTextW(GetDlgItem(window,kModel),local?L"qwen3:4b-instruct":L"gpt-4.1-mini");
                ProviderStatus(window);
                SetWindowTextW(GetDlgItem(window,kStatus),L"เลือกค่าตัวอย่างแล้ว · ตรวจชื่อโมเดลและ key ก่อนบันทึก"); return 0;
            }
            if(LOWORD(wParam)==kRevealKey) {
                HWND key=GetDlgItem(window,kApiKey);
                const bool hidden=SendMessageW(key,EM_GETPASSWORDCHAR,0,0)!=0;
                SendMessageW(key,EM_SETPASSWORDCHAR,hidden?0:L'●',0);
                SetWindowTextW(GetDlgItem(window,kRevealKey),hidden?L"ซ่อน":L"แสดง");
                InvalidateRect(key,nullptr,TRUE); return 0;
            }
            if(LOWORD(wParam)==kResetAppearance) {
                SendMessageW(GetDlgItem(window,kOpacity),TBM_SETPOS,TRUE,96);
                SetWindowTextW(GetDlgItem(window,kOpacityValue),L"96%");
                SendMessageW(GetDlgItem(window,kPosition),CB_SETCURSEL,0,0);
                SetWindowTextW(GetDlgItem(window,kAutoHide),L"0");
            }
            if(LOWORD(wParam)==kResetKeys) {
                SetWindowTextW(GetDlgItem(window,kTranslateKey),L"T");
                SetWindowTextW(GetDlgItem(window,kOcrKey),L"O");
                SetWindowTextW(GetDlgItem(window,kQuitKey),L"Q");
            }
            if(LOWORD(wParam)==kApiBase && HIWORD(wParam)==EN_CHANGE) ProviderStatus(window);
            if(!state->building && (HIWORD(wParam)==EN_CHANGE || HIWORD(wParam)==CBN_SELCHANGE || HIWORD(wParam)==BN_CLICKED))
                SetWindowTextW(GetDlgItem(window,kStatus),L"แก้ไขแล้ว · กดบันทึกเพื่อนำการตั้งค่าไปใช้");
            if (LOWORD(wParam) == kSave && ReadSettings(window, *state)) {
                state->accepted = true;
                DestroyWindow(window);
            } else if ((LOWORD(wParam) == kCancel || LOWORD(wParam) == IDCANCEL)) {
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
        WS_EX_DLGMODALFRAME, kSettingsClass, L"Point Translator · ตั้งค่า",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 956, 739,
        owner, nullptr, instance, &state);
    if (!window) return false;

    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    const RECT work=monitor.rcWork;
    UINT dpi=GetDpiForWindow(owner); if(!dpi) dpi=96;
    RECT desired{0,0,MulDiv(940,dpi,96),MulDiv(700,dpi,96)};
    AdjustWindowRectExForDpi(&desired,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME,dpi);
    const int width=(std::min)(static_cast<int>(desired.right-desired.left),static_cast<int>(work.right-work.left)-24);
    const int height=(std::min)(static_cast<int>(desired.bottom-desired.top),static_cast<int>(work.bottom-work.top)-24);
    SetWindowPos(window, HWND_TOP, work.left + (work.right - work.left - width) / 2,
                 work.top + (work.bottom - work.top - height) / 2,
                 width, height, SWP_SHOWWINDOW);
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
    if (state.background) DeleteObject(state.background);
    if (state.white) DeleteObject(state.white);
    if (receivedQuit) PostQuitMessage(quitCode);
    if (state.accepted) updated = std::move(state.draft);
    return state.accepted;
}

}  // namespace thai_overlay

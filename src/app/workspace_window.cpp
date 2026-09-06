#include "app/workspace_window.h"
#include "services/history_service.h"
#include "core/text.h"
#include "config/app_config.h"
#include <commctrl.h>
#include <algorithm>
#include <cwctype>

namespace thai_overlay {
namespace {
AppConfig* activeConfig;
HWND sourceCombo, targetCombo, engineCombo;
HWND window, input, search, history, output, status, tabs, translateButton;
HWND replyLabels[3]{}, replyEdits[3]{}, replyButtons[3]{};
HFONT font, headingFont;
HBRUSH background, surface;
std::vector<HistoryEntry> entries;
std::vector<size_t> filtered;
Translation current;
bool busy = false, historyReadable = true;
std::wstring storageError;
std::function<void(std::wstring)> translateAction;
std::function<void()> captureAction, settingsAction;
constexpr int TranslateId=101, CaptureId=102, SettingsId=103, SearchId=104,
    HistoryId=105, TabsId=106, CopyId=107, DeleteId=108, ClearId=109;
int Scale(int value) { return MulDiv(value, static_cast<int>(GetDpiForWindow(window)), 96); }
const wchar_t* languageCodes[] = {L"zh",L"en",L"th"};
const wchar_t* languageNames[] = {L"จีน",L"อังกฤษ",L"ไทย"};
std::wstring PinyinLine(const std::wstring& value) { return value.empty()?L"":L"\r\nPinyin: "+value; }
std::wstring LanguageName(const std::wstring& code) {
    for(int i=0;i<3;++i) if(code==languageCodes[i]) return languageNames[i];
    return L"อัตโนมัติ";
}
void SyncLanguages() {
    if(!activeConfig) return;
    for(int i=0;i<3;++i) {
        if(activeConfig->sourceLanguage==languageCodes[i]) SendMessageW(sourceCombo,CB_SETCURSEL,i,0);
        if(activeConfig->targetLanguage==languageCodes[i]) SendMessageW(targetCombo,CB_SETCURSEL,i,0);
    }
    SendMessageW(engineCombo,CB_SETCURSEL,activeConfig->translationEngine==L"argos"?1:0,0);
}
std::wstring Text(HWND control) {
    std::wstring value(GetWindowTextLengthW(control) + 1, L'\0');
    value.resize(GetWindowTextW(control, value.data(), static_cast<int>(value.size())));
    return value;
}
void Status(const std::wstring& text) { SetWindowTextW(status,text.c_str()); }
std::wstring Lower(std::wstring value) {
    std::transform(value.begin(),value.end(),value.begin(), [](wchar_t c){ return static_cast<wchar_t>(towlower(c)); });
    return value;
}
HWND Control(const wchar_t* type, const wchar_t* text, DWORD style, int id) {
    HWND control = CreateWindowExW(0,type,text,WS_CHILD | WS_VISIBLE | style,
        0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    return control;
}
void Place(HWND control,int x,int y,int w,int h) {
    MoveWindow(control,Scale(x),Scale(y),Scale(w),Scale(h),TRUE);
}
void RefreshHistory() {
    SendMessageW(history,LB_RESETCONTENT,0,0);
    filtered.clear();
    const auto query=Lower(Text(search));
    for(size_t i=0;i<entries.size();++i) {
        const auto& entry=entries[i];
        if(!query.empty() && Lower(entry.translation.original+L" "+entry.translation.translated+L" "+entry.translation.thai).find(query)==std::wstring::npos) continue;
        auto preview=entry.translation.original.substr(0,70);
        std::replace(preview.begin(),preview.end(),L'\n',L' ');
        std::replace(preview.begin(),preview.end(),L'\r',L' ');
        const auto label=entry.time+L"  |  "+LanguageName(entry.translation.sourceLanguage)+L" → "+LanguageName(entry.translation.targetLanguage)+L" | "+preview;
        SendMessageW(history,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        filtered.push_back(i);
    }
    SetWindowTextW(GetDlgItem(window,205),(L"ประวัติ  ·  "+std::to_wstring(filtered.size())+L" รายการ").c_str());
}
void Display() {
    const int tab=TabCtrl_GetCurSel(tabs);
    ShowWindow(output,tab==1?SW_HIDE:SW_SHOW);
    ShowWindow(GetDlgItem(window,CopyId),tab==1?SW_HIDE:SW_SHOW);
    for(int i=0;i<3;++i) {
        ShowWindow(replyLabels[i],tab==1?SW_SHOW:SW_HIDE);
        ShowWindow(replyEdits[i],tab==1?SW_SHOW:SW_HIDE);
        ShowWindow(replyButtons[i],tab==1?SW_SHOW:SW_HIDE);
        const bool exists=static_cast<size_t>(i)<current.replies.size();
        const auto label=std::to_wstring(i+1)+L"  ·  "+(exists?current.replies[i].tone:L"คำตอบแนะนำ");
        SetWindowTextW(replyLabels[i],label.c_str());
        SetWindowTextW(replyEdits[i],exists?(current.replies[i].text+PinyinLine(current.replies[i].pinyin)+L"\r\n\r\nคำแปล: "+current.replies[i].thai).c_str():L"ยังไม่มีคำแนะนำจากโมเดล ลองแปลใหม่ได้ครับ");
        EnableWindow(replyButtons[i],exists);
    }
    std::wstring value;
    if(!current.error.empty()) value=L"แปลไม่สำเร็จ\r\n\r\n"+current.error+L"\r\n\r\nตรวจสอบการตั้งค่า แล้วกดแปลอีกครั้ง";
    else if(current.original.empty()) value=L"เริ่มต้นด้วยข้อความที่อยากเข้าใจ\r\n\r\nพิมพ์หรือวางข้อความด้านบน แล้วกด แปลข้อความ\r\nหรือเลือก จับภาพ OCR เพื่ออ่านข้อความจากหน้าจอ\r\n\r\nผลแปล คำอ่าน และคำตอบแนะนำจะปรากฏที่นี่\r\nเลือกประวัติด้านซ้ายเพื่อกลับมาอ่านได้โดยไม่ต้องแปลใหม่";
    else if(tab==2) {
        for(const auto& w:current.words) value+=w.word+L"  ·  "+w.pinyin+L"\r\nคำอ่าน: "+w.karaoke+L"\r\nความหมาย: "+w.meaning+L"\r\n"+w.note+L"\r\n\r\n";
        if(value.empty()) value=L"โมเดลไม่ได้ส่งคำอธิบายรายคำสำหรับข้อความนี้";
    } else value=L"ต้นฉบับ ("+LanguageName(current.sourceLanguage)+L")\r\n"+current.original+PinyinLine(current.originalPinyin)+L"\r\n\r\nคำแปล ("+LanguageName(current.targetLanguage)+L") · "+current.engine+L"\r\n"+current.translated+PinyinLine(current.translatedPinyin)+L"\r\n\r\nคำอ่านภาษาไทย\r\n"+current.karaoke+L"\r\n\r\nบริบทและความหมาย\r\n"+current.explanation;
    SetWindowTextW(output,value.c_str());
    EnableWindow(GetDlgItem(window,CopyId),!current.translated.empty());
}
void Layout() {
    if (!status) return;
    RECT r{}; GetClientRect(window,&r);
    int w=MulDiv(r.right,96,static_cast<int>(GetDpiForWindow(window)));
    int h=MulDiv(r.bottom,96,static_cast<int>(GetDpiForWindow(window)));
    int x=308, right=w-x-24;
    Place(GetDlgItem(window,201),24,18,w-220,34);
    Place(GetDlgItem(window,202),24,56,w-48,26);
    Place(GetDlgItem(window,SettingsId),w-140,22,116,34);
    Place(GetDlgItem(window,205),24,105,260,28);
    Place(search,24,140,260,34);
    Place(history,24,186,260,h-302);
    Place(GetDlgItem(window,DeleteId),24,h-104,122,34);
    Place(GetDlgItem(window,ClearId),156,h-104,128,34);
    Place(GetDlgItem(window,203),x,105,right,26);
    Place(GetDlgItem(window,404),x,135,40,24);
    Place(sourceCombo,x+42,131,110,200);
    Place(GetDlgItem(window,403),x+160,131,46,30);
    Place(targetCombo,x+214,131,110,200);
    Place(engineCombo,x+336,131,right-336,200);
    Place(input,x,174,right,54);
    Place(translateButton,x,240,150,36);
    Place(GetDlgItem(window,CaptureId),x+162,240,146,36);
    Place(tabs,x,294,right,32);
    Place(output,x,338,right,h-438);
    Place(GetDlgItem(window,CopyId),x,h-90,170,32);
    int cardHeight=(h-408)/3;
    for(int i=0;i<3;++i) {
        int y=338+i*cardHeight;
        Place(replyLabels[i],x,y,right-132,26);
        Place(replyButtons[i],w-148,y,124,28);
        Place(replyEdits[i],x,y+32,right,cardHeight-42);
    }
    Place(status,24,h-44,w-48,30);
}
void Copy(const std::wstring& value) {
    if(value.empty()) return;
    HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,(value.size()+1)*sizeof(wchar_t));
    if(!data) { Status(L"คัดลอกไม่สำเร็จ"); return; }
    void* pointer=GlobalLock(data);
    if(!pointer) { GlobalFree(data); return; }
    memcpy(pointer,value.c_str(),(value.size()+1)*sizeof(wchar_t)); GlobalUnlock(data);
    if(!OpenClipboard(window)) { GlobalFree(data); Status(L"คลิปบอร์ดไม่ว่าง ลองอีกครั้ง"); return; }
    EmptyClipboard();
    if(SetClipboardData(CF_UNICODETEXT,data)) Status(L"คัดลอกแล้ว พร้อมนำไปวาง");
    else { GlobalFree(data); Status(L"คัดลอกไม่สำเร็จ"); }
    CloseClipboard();
}
LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_SIZE: Layout(); return 0;
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lp);
        const UINT dpi=GetDpiForWindow(hwnd);
        info->ptMinTrackSize={MulDiv(900,dpi?dpi:96,96),MulDiv(760,dpi?dpi:96,96)}; return 0;
    }
    case WM_DPICHANGED: {
        auto* r=reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);
        Layout(); return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc=reinterpret_cast<HDC>(wp);
        SetTextColor(dc,RGB(32,48,68));
        bool plain=message==WM_CTLCOLORSTATIC && reinterpret_cast<HWND>(lp)!=output &&
            reinterpret_cast<HWND>(lp)!=replyEdits[0] && reinterpret_cast<HWND>(lp)!=replyEdits[1] && reinterpret_cast<HWND>(lp)!=replyEdits[2];
        SetBkColor(dc,plain?RGB(241,245,250):RGB(255,255,255));
        return reinterpret_cast<LRESULT>(plain?background:surface);
    }
    case WM_NOTIFY:
        if(reinterpret_cast<NMHDR*>(lp)->idFrom==TabsId && reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE) Display();
        return 0;
    case WM_COMMAND: {
        int id=LOWORD(wp);
        if(id==TranslateId && !busy) {
            auto value=Trim(Text(input));
            if(value.empty()) { Status(L"พิมพ์หรือวางข้อความก่อนแปล"); SetFocus(input); }
            else translateAction(value);
        } else if(id==CaptureId && !busy) { ShowWindow(window,SW_HIDE); captureAction(); }
        else if(id==SettingsId) {
            EnableWindow(window,FALSE); settingsAction(); EnableWindow(window,TRUE); SetForegroundWindow(window);
        }
        else if (((id==400 || id==401 || id==402) && HIWORD(wp)==CBN_SELCHANGE) || id==403) {
            AppConfig candidate=*activeConfig;
            if(id==403) std::swap(candidate.sourceLanguage,candidate.targetLanguage);
            else {
                candidate.sourceLanguage=languageCodes[SendMessageW(sourceCombo,CB_GETCURSEL,0,0)];
                candidate.targetLanguage=languageCodes[SendMessageW(targetCombo,CB_GETCURSEL,0,0)];
                candidate.translationEngine=SendMessageW(engineCombo,CB_GETCURSEL,0,0)==1?L"argos":L"ai";
            }
            std::wstring error;
            if(SaveAppConfig(candidate,error)) { *activeConfig=candidate; Status(L"บันทึกคู่ภาษาแล้ว ใช้กับข้อความและ OCR ครั้งถัดไป"); }
            else Status(error);
            SyncLanguages();
        }
        else if(id==SearchId && HIWORD(wp)==EN_CHANGE) RefreshHistory();
        else if(id==HistoryId && HIWORD(wp)==LBN_SELCHANGE) {
            const auto selection=SendMessageW(history,LB_GETCURSEL,0,0);
            if(selection!=LB_ERR && static_cast<size_t>(selection)<filtered.size()) {
                const auto& entry=entries[filtered[selection]];
                current=entry.translation; SetWindowTextW(input,current.original.c_str()); Display();
                Status(L"ประวัติ · "+entry.time+L" · อ่านได้โดยไม่เรียก AI ใหม่");
            }
        } else if(id==CopyId) Copy(current.translated);
        else if(id>=300 && id<303 && static_cast<size_t>(id-300)<current.replies.size()) Copy(current.replies[id-300].text);
        else if((id==DeleteId || id==ClearId) && historyReadable) {
            const auto selection=SendMessageW(history,LB_GETCURSEL,0,0);
            if(id==DeleteId && selection==LB_ERR) { Status(L"เลือกรายการประวัติก่อนลบ"); return 0; }
            if(entries.empty()) return 0;
            if(MessageBoxW(window,id==ClearId?L"ลบประวัติทั้งหมดบนเครื่องนี้?":L"ลบรายการที่เลือก?",L"ยืนยันการลบประวัติ",MB_YESNO|MB_ICONQUESTION)!=IDYES) return 0;
            auto candidate=entries;
            if(id==ClearId) candidate.clear(); else candidate.erase(candidate.begin()+filtered[selection]);
            std::wstring error;
            if(SaveHistory(candidate,error)) { entries=std::move(candidate); RefreshHistory(); Status(L"ลบประวัติแล้ว"); }
            else Status(error);
        }
        return 0;
    }
    case WM_CLOSE: ShowWindow(hwnd,SW_HIDE); return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
void InitializeWorkspace(HINSTANCE instance,AppConfig& config,std::function<void(std::wstring)> translate,
                         std::function<void()> capture,std::function<void()> settings) {
    activeConfig=&config;
    translateAction=std::move(translate); captureAction=std::move(capture); settingsAction=std::move(settings);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES}; InitCommonControlsEx(&controls);
    entries=LoadHistory(storageError); historyReadable=storageError.empty();
    background=CreateSolidBrush(RGB(241,245,250)); surface=CreateSolidBrush(RGB(255,255,255));
    font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Leelawadee UI");
    headingFont=CreateFontW(-28,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=Procedure;
    wc.lpszClassName=L"PointTranslatorWorkspace"; wc.hbrBackground=background; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION); RegisterClassW(&wc);
    window=CreateWindowExW(WS_EX_APPWINDOW,wc.lpszClassName,L"Point Translator · แปล เข้าใจ ตอบกลับ",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,1120,860,nullptr,nullptr,instance,nullptr);
    auto title=Control(L"STATIC",L"Point Translator",0,201);
    SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(headingFont),TRUE);
    Control(L"STATIC",L"แปลให้เข้าใจ  ·  อ่านออกเสียง  ·  เลือกคำตอบที่เป็นคุณ",0,202);
    Control(L"BUTTON",L"ตั้งค่า",WS_TABSTOP,SettingsId);
    Control(L"STATIC",L"ประวัติ",0,205);
    search=Control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,SearchId);
    SendMessageW(search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"ค้นหาต้นฉบับ / คำแปล"));
    history=Control(L"LISTBOX",L"",WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,HistoryId);
    SendMessageW(history,LB_SETHORIZONTALEXTENT,900,0);
    Control(L"BUTTON",L"ลบรายการ",WS_TABSTOP,DeleteId); Control(L"BUTTON",L"ล้างประวัติ",WS_TABSTOP,ClearId);
    Control(L"STATIC",L"ข้อความที่ต้องการแปล",0,203);
    Control(L"STATIC",L"จาก",0,404);
    sourceCombo=Control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,400);
    targetCombo=Control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,401);
    Control(L"BUTTON",L"⇄",WS_TABSTOP,403);
    engineCombo=Control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,402);
    for(const auto* name:languageNames) {
        SendMessageW(sourceCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(targetCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
    }
    SendMessageW(engineCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"AI"));
    SendMessageW(engineCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Argos + AI"));
    SyncLanguages();
    input=Control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,204);
    SendMessageW(input,EM_SETLIMITTEXT,12000,0);
    translateButton=Control(L"BUTTON",L"แปลข้อความ",WS_TABSTOP|BS_DEFPUSHBUTTON,TranslateId);
    Control(L"BUTTON",L"จับภาพ OCR",WS_TABSTOP,CaptureId);
    tabs=Control(WC_TABCONTROLW,L"",WS_TABSTOP,TabsId);
    const wchar_t* names[]={L"คำแปลและคำอ่าน",L"คำตอบแนะนำ 3 แบบ",L"เรียนรู้รายคำ"};
    for(int i=0;i<3;++i) { TCITEMW item{}; item.mask=TCIF_TEXT; item.pszText=const_cast<wchar_t*>(names[i]); TabCtrl_InsertItem(tabs,i,&item); }
    output=Control(L"EDIT",L"",WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,210);
    SendMessageW(output,EM_SETLIMITTEXT,1024*1024,0);
    Control(L"BUTTON",L"คัดลอกคำแปล",WS_TABSTOP,CopyId);
    for(int i=0;i<3;++i) {
        replyLabels[i]=Control(L"STATIC",L"",0,310+i);
        replyEdits[i]=Control(L"EDIT",L"",WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,320+i);
        replyButtons[i]=Control(L"BUTTON",L"คัดลอกคำตอบ",WS_TABSTOP,300+i);
    }
    status=Control(L"STATIC",L"พร้อมใช้งาน · เก็บประวัติ 500 รายการล่าสุดบนเครื่องนี้",SS_LEFT,220);
    RefreshHistory(); Display(); Layout();
    if(!storageError.empty()) Status(storageError);
}
void ShowWorkspace() { ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); }
bool WorkspaceVisible() { return IsWindowVisible(window)!=FALSE; }
bool WorkspaceMessage(MSG& message) { return window && IsDialogMessageW(window,&message); }
void WorkspaceBusy(bool value) {
    busy=value;
    for(int id: {400,401,402,403}) EnableWindow(GetDlgItem(window,id),!busy);
    EnableWindow(translateButton,!busy); EnableWindow(GetDlgItem(window,CaptureId),!busy);
    SetWindowTextW(translateButton,busy?L"กำลังแปล…":L"แปลข้อความ");
    if(busy) Status(L"กำลังประมวลผล · เตรียมคำแปลและคำตอบแนะนำ…");
    else Status(L"พร้อมใช้งาน · เก็บประวัติ 500 รายการล่าสุดบนเครื่องนี้");
}
void WorkspaceCompleted(const Translation& translation) {
    WorkspaceBusy(false); current=translation; Display();
    if (!translation.original.empty()) SetWindowTextW(input,translation.original.c_str());
    if(!translation.error.empty()) { Status(L"แปลไม่สำเร็จ · ดูรายละเอียดแล้วลองใหม่"); return; }
    if(!historyReadable) { Status(storageError); return; }
    SYSTEMTIME time{}; GetLocalTime(&time); wchar_t stamp[32];
    swprintf_s(stamp,L"%04u-%02u-%02u %02u:%02u",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute);
    entries.insert(entries.begin(),{stamp,translation});
    if(entries.size()>500) entries.resize(500);
    std::wstring error;
    bool saved=SaveHistory(entries,error); RefreshHistory();
    Status(saved?(translation.replies.size()==3?L"แปลเสร็จแล้ว · บันทึกประวัติแล้ว · เลือกดูคำตอบแนะนำได้":L"บันทึกแล้ว · โมเดลส่งคำตอบแนะนำไม่ครบ 3 แบบ ลองแปลใหม่ได้"):error);
}
}

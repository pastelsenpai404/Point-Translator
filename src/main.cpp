#include <windows.h>
#include <shellapi.h>
#include <string>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "app/overlay_application.h"

namespace {
int CloseExisting(const wchar_t* expectedPath) {
    // Match the application window AND the exact executable path. Never close
    // another program merely because its process name happens to match.
    const HWND window=FindWindowW(L"ThaiKaraokeOverlayWindow",nullptr);
    if(!window) return 0;
    DWORD processId=0; GetWindowThreadProcessId(window,&processId);
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,processId);
    if(!process) return static_cast<int>(GetLastError());
    wchar_t actual[32768]{}, expected[32768]{};
    DWORD length=32768;
    const DWORD expectedLength=GetFullPathNameW(expectedPath,32768,expected,nullptr);
    if(!expectedLength || expectedLength>=32768 ||
       !QueryFullProcessImageNameW(process,0,actual,&length) || lstrcmpiW(actual,expected)!=0) {
        CloseHandle(process); return ERROR_INVALID_PARAMETER;
    }
    if(!PostMessageW(window,WM_CLOSE,0,0)) {
        const DWORD error=GetLastError(); CloseHandle(process); return static_cast<int>(error);
    }
    const DWORD result=WaitForSingleObject(process,15000);
    CloseHandle(process);
    return result==WAIT_OBJECT_0?0:static_cast<int>(result);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int count=0;
    LPWSTR* arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!arguments) return ERROR_INVALID_PARAMETER;
    if(count>1) {
        const bool close=count==3 && lstrcmpW(arguments[1],L"--close-existing")==0;
        const int result=close?CloseExisting(arguments[2]):ERROR_INVALID_PARAMETER;
        LocalFree(arguments); return result;
    }
    LocalFree(arguments);
    return thai_overlay::RunOverlayApplication(instance);
}

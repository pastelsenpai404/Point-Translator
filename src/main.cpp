#include <windows.h>

#include "app/overlay_application.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    return thai_overlay::RunOverlayApplication(instance);
}

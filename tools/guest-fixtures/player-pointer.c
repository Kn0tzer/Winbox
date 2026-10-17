#include <windows.h>
#include <stdio.h>

static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM word, LPARAM coordinates) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC context = BeginPaint(window, &paint);
        RECT bounds;
        GetClientRect(window, &bounds);
        HBRUSH background = CreateSolidBrush(RGB(230, 235, 240));
        FillRect(context, &bounds, background);
        DeleteObject(background);
        HBRUSH marker = CreateSolidBrush(RGB(220, 30, 40));
        RECT corner = {0, 0, bounds.right / 2, bounds.bottom / 2};
        FillRect(context, &corner, marker);
        DeleteObject(marker);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_LBUTTONDOWN) {
        FILE *report = fopen("Z:\\home\\username\\pointer-result.txt", "a");
        if (report) { fprintf(report, "WINBOX_POINTER x=%d y=%d\n", (short)LOWORD(coordinates), (short)HIWORD(coordinates)); fclose(report); }
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, word, coordinates);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    WNDCLASSA definition = {0};
    definition.lpfnWndProc = windowProcedure;
    definition.hInstance = GetModuleHandleA(NULL);
    definition.lpszClassName = "WinboxPointer";
    RegisterClassA(&definition);
    HWND window = CreateWindowA(definition.lpszClassName, "Pointer test", WS_POPUP | WS_VISIBLE,
        0, 0, 640, 480, NULL, NULL, definition.hInstance, NULL);
    if (!window) return 1;
    SetForegroundWindow(window);
    FILE *report = fopen("Z:\\home\\username\\pointer-result.txt", "w");
    if (report) { fputs("ready\n", report); fclose(report); }
    MSG message;
    while (GetMessageA(&message, NULL, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageA(&message); }
    return 0;
}

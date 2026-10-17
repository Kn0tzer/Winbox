/* Real launcher acceptance fixture: visible window and a persistent counter. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR arguments, int show) {
    (void)previous; (void)arguments; (void)show;
    const char *path = "Z:\\home\\username\\launcher-counter.txt";
    unsigned count = 0;
    FILE *file = fopen(path, "r");
    if (file) { fscanf(file, "%u", &count); fclose(file); }
    file = fopen(path, "w");
    if (!file) return 1;
    fprintf(file, "%u", ++count); fclose(file);
    WNDCLASSA cls = {0};
    cls.lpfnWndProc = DefWindowProcA; cls.hInstance = instance;
    cls.lpszClassName = "LauncherReady"; cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassA(&cls)) return 2;
    HWND window = CreateWindowA(cls.lpszClassName, "Launcher upload works", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        0, 0, 640, 480, NULL, NULL, instance, NULL);
    if (!window) return 3;
    UpdateWindow(window);
    printf("launcher-ready: READY count=%u\n", count); fflush(stdout);
    MSG message;
    while (GetMessageA(&message, NULL, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageA(&message); }
    return 0;
}

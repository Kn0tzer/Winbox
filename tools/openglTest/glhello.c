#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR c, int s) {
    fprintf(stderr, "glhello: WinMain start\n");
    fflush(stderr);
    fprintf(stderr, "glhello: RESULT 0\n");
    return 0;
}

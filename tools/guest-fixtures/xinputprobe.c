#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <stdio.h>

typedef struct InputState {
    DWORD rc;
    WORD buttons;
    SHORT lx;
    SHORT ly;
    BYTE lt;
    BYTE rt;
} InputState;

static int same_state(const InputState* a, const InputState* b) {
    return a->rc == b->rc && a->buttons == b->buttons && a->lx == b->lx &&
        a->ly == b->ly && a->lt == b->lt && a->rt == b->rt;
}

int main(void) {
    InputState previous[4] = {{0}};
    BOOL have_previous[4] = {FALSE, FALSE, FALSE, FALSE};
    DWORD started = GetTickCount();

    while (GetTickCount() - started < 60000) {
        DWORD elapsed = GetTickCount() - started;
        for (DWORD index = 0; index < 4; ++index) {
            XINPUT_STATE xinput = {0};
            InputState current = {0};
            current.rc = XInputGetState(index, &xinput);
            if (current.rc == ERROR_SUCCESS) {
                current.buttons = xinput.Gamepad.wButtons;
                current.lx = xinput.Gamepad.sThumbLX;
                current.ly = xinput.Gamepad.sThumbLY;
                current.lt = xinput.Gamepad.bLeftTrigger;
                current.rt = xinput.Gamepad.bRightTrigger;
            }
            if (!have_previous[index] || !same_state(&current, &previous[index])) {
                char line[160];
                snprintf(line, sizeof(line), "t=%lu idx=%lu rc=%lu buttons=%04x lx=%d ly=%d lt=%u rt=%u\n",
                    (unsigned long)elapsed, (unsigned long)index,
                    (unsigned long)current.rc, current.buttons, current.lx, current.ly,
                    current.lt, current.rt);
                fputs(line, stdout);
                fflush(stdout);
                OutputDebugStringA(line);
                previous[index] = current;
                have_previous[index] = TRUE;
            }
        }
        Sleep(100);
    }
    return 0;
}

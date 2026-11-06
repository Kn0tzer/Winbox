/* Win32 gamepad witness: reports what the standard Windows controller
 * interfaces see — XInput (slots, capabilities, live state) and
 * DirectInput (device enumeration, live state) — via OutputDebugString,
 * so the browser suite can watch a simulated controller's values arrive.
 * Prints only on change, plus a first full report per interface. */
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <xinput.h>
#include <dinput.h>

static void emit(const char* format, ...) {
    char message[512]; va_list arguments;
    va_start(arguments, format); vsnprintf(message, sizeof(message), format, arguments); va_end(arguments);
    OutputDebugStringA(message);
}

typedef DWORD (WINAPI *XInputGetStateFn)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI *XInputGetCapabilitiesFn)(DWORD, DWORD, XINPUT_CAPABILITIES*);
static XInputGetStateFn pXInputGetState;
static XInputGetCapabilitiesFn pXInputGetCapabilities;

static int load_xinput(void) {
    static const char* names[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", "xinput1_2.dll", "xinput1_1.dll" };
    for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        HMODULE module = LoadLibraryA(names[i]);
        if (!module) continue;
        pXInputGetState = (XInputGetStateFn)(void*)GetProcAddress(module, "XInputGetState");
        pXInputGetCapabilities = (XInputGetCapabilitiesFn)(void*)GetProcAddress(module, "XInputGetCapabilities");
        if (pXInputGetState) { emit("gamepad-witness: XINPUT dll=%s\n", names[i]); return 1; }
    }
    emit("gamepad-witness: XINPUT missing\n");
    return 0;
}

/* ---- DirectInput side ---- */
static IDirectInput8A* di;
static IDirectInputDevice8A* di_device;
static unsigned di_device_count;
static char di_first_name[256];

static BOOL CALLBACK di_enum_callback(const DIDEVICEINSTANCEA* instance, void* context) {
    (void)context;
    if (di_device_count == 0 && instance) {
        strncpy(di_first_name, instance->tszProductName, sizeof(di_first_name) - 1);
        di_first_name[sizeof(di_first_name) - 1] = 0;
    }
    di_device_count++;
    return DIENUM_CONTINUE;
}


/* Second enumeration pass that creates + acquires the first game controller. */
static IDirectInput8A* di_bridge_owner;
static HWND di_bridge_window;
static BOOL CALLBACK di_create_callback(const DIDEVICEINSTANCEA* instance, void* context) {
    (void)context;
    IDirectInputDevice8A* device = NULL;
    HRESULT hr = di_bridge_owner->lpVtbl->CreateDevice(di_bridge_owner, &instance->guidInstance, &device, NULL);
    if (FAILED(hr) || !device) return DIENUM_CONTINUE;
    device->lpVtbl->SetDataFormat(device, &c_dfDIJoystick2);
    device->lpVtbl->SetCooperativeLevel(device, di_bridge_window, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    hr = device->lpVtbl->Acquire(device);
    if (FAILED(hr)) { emit("gamepad-witness: DINPUT acquire hr=0x%08lx\n", (unsigned long)hr); }
    *(IDirectInputDevice8A**)context = device;
    return DIENUM_STOP;
}
static IDirectInputDevice8A* di_created_from_enum(IDirectInput8A* owner, HWND window) {
    IDirectInputDevice8A* device = NULL;
    di_bridge_owner = owner; di_bridge_window = window;
    owner->lpVtbl->EnumDevices(owner, DI8DEVCLASS_GAMECTRL, di_create_callback, &device, DIEDFL_ATTACHEDONLY);
    return device;
}

static int init_dinput(HWND window) {
    HRESULT hr = DirectInput8Create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION,
                                    &IID_IDirectInput8A, (void**)&di, NULL);
    if (FAILED(hr) || !di) { emit("gamepad-witness: DINPUT create failed hr=0x%08lx\n", (unsigned long)hr); return 0; }
    di_device_count = 0; di_first_name[0] = 0;
    hr = di->lpVtbl->EnumDevices(di, DI8DEVCLASS_GAMECTRL, di_enum_callback, NULL, DIEDFL_ATTACHEDONLY);
    if (FAILED(hr)) { emit("gamepad-witness: DINPUT enum failed hr=0x%08lx\n", (unsigned long)hr); return 0; }
    emit("gamepad-witness: DINPUT devices=%u name0=%s\n", di_device_count, di_first_name);
    if (!di_device_count) return 0;
    /* Re-enumerate, this time creating + acquiring the first device. */
    di_device = di_created_from_enum(di, window);
    if (!di_device) { emit("gamepad-witness: DINPUT device create failed\n"); return 0; }
    return 1;
}

static LRESULT CALLBACK wndproc(HWND window, UINT message, WPARAM w, LPARAM l) {
    (void)w; (void)l;
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, w, l);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR arguments, int show) {
    (void)previous; (void)arguments; (void)show;
    WNDCLASSA cls = {0}; cls.lpfnWndProc = wndproc; cls.hInstance = instance; cls.lpszClassName = "GamepadWitness";
    if (!RegisterClassA(&cls)) return 1;
    HWND window = CreateWindowA(cls.lpszClassName, "Gamepad witness", WS_OVERLAPPEDWINDOW,
        0, 0, 320, 200, NULL, NULL, instance, NULL);
    if (!window) return 2;

    int have_xinput = load_xinput();
    int have_dinput = init_dinput(window);
    emit("gamepad-witness: READY xinput=%d dinput=%d\n", have_xinput, have_dinput);

    int xi_connected[4] = {0, 0, 0, 0};
    XINPUT_STATE xi_last[4]; memset(xi_last, 0, sizeof(xi_last));
    DIJOYSTATE2 di_last; memset(&di_last, 0, sizeof(di_last)); int di_have_last = 0;

    DWORD started = GetTickCount();
    while (GetTickCount() - started < 300000) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        if (have_xinput) {
            for (DWORD slot = 0; slot < 4; slot++) {
                XINPUT_STATE state; memset(&state, 0, sizeof(state));
                DWORD result = pXInputGetState(slot, &state);
                if (result == ERROR_SUCCESS) {
                    if (!xi_connected[slot]) {
                        xi_connected[slot] = 1;
                        emit("gamepad-witness: XINPUT slot=%u CONNECTED\n", (unsigned)slot);
                        if (pXInputGetCapabilities) {
                            XINPUT_CAPABILITIES caps; memset(&caps, 0, sizeof(caps));
                            if (pXInputGetCapabilities(slot, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS) {
                                emit("gamepad-witness: XINPUT slot=%u CAPS type=%u subtype=%u buttons=0x%04x\n",
                                    (unsigned)slot, caps.Type, caps.SubType, caps.Gamepad.wButtons);
                            }
                        }
                    }
                    if (state.dwPacketNumber != xi_last[slot].dwPacketNumber) {
                        xi_last[slot] = state;
                        emit("gamepad-witness: XINPUT slot=%u STATE buttons=0x%04x lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d\n",
                            (unsigned)slot, state.Gamepad.wButtons,
                            state.Gamepad.bLeftTrigger, state.Gamepad.bRightTrigger,
                            state.Gamepad.sThumbLX, state.Gamepad.sThumbLY,
                            state.Gamepad.sThumbRX, state.Gamepad.sThumbRY);
                    }
                } else if (xi_connected[slot]) {
                    xi_connected[slot] = 0;
                    emit("gamepad-witness: XINPUT slot=%u DISCONNECTED\n", (unsigned)slot);
                }
            }
        }
        if (have_dinput && di_device) {
            di_device->lpVtbl->Poll(di_device);
            DIJOYSTATE2 state; memset(&state, 0, sizeof(state));
            HRESULT hr = di_device->lpVtbl->GetDeviceState(di_device, sizeof(state), &state);
            if (hr == DIERR_INPUTLOST) { di_device->lpVtbl->Acquire(di_device); hr = di_device->lpVtbl->GetDeviceState(di_device, sizeof(state), &state); }
            if (SUCCEEDED(hr)) {
                unsigned long buttons = 0;
                for (int i = 0; i < 32; i++) if (state.rgbButtons[i] & 0x80) buttons |= (1ul << i);
                int changed = !di_have_last || memcmp(&state, &di_last, sizeof(state)) != 0;
                if (changed) {
                    di_last = state; di_have_last = 1;
                    emit("gamepad-witness: DINPUT STATE buttons=0x%08lx x=%ld y=%ld z=%ld rx=%ld ry=%ld rz=%ld pov=%ld\n",
                        buttons, state.lX, state.lY, state.lZ, state.lRx, state.lRy, state.lRz,
                        state.rgdwPOV[0]);
                }
            }
        }
        Sleep(50);
    }
    return 0;
}

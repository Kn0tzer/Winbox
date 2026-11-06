/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"
#include <SDL.h>
#include "../../source/x11/x11.h"
#include "sdlcallback.h"
#include "devinput.h"
#include "knativeinputSDL.h"
#include "knativesystem.h"
#include "kdspaudio.h"
#ifdef BOXEDWINE_GUEST_X64
#include "../../source/x11wire/xwirepresent.h"
#endif

U32 sdlCustomEvent;

KNativeInputSDL::KNativeInputSDL(U32 cx, U32 cy, int scaleX, int scaleY) {
    if (!sdlCustomEvent) {
        sdlCustomEvent = SDL_RegisterEvents(1);
    }
    
    this->width = cx;
    this->height = cy;
    this->scaleX = scaleX;
    this->scaleY = scaleY;
    this->scaleXOffset = 0;
    this->scaleYOffset = 0;    
}

void KNativeInputSDL::runOnUiThread(std::function<void()> callback) {
    DISPATCH_MAIN_THREAD_BLOCK_THIS_BEGIN
        callback();
    DISPATCH_MAIN_THREAD_BLOCK_END
}


void KNativeInputSDL::setScreenSize(U32 cx, U32 cy) {
    width = cx;
    height = cy;
}

U32 KNativeInputSDL::screenWidth() {
    return width;
}

U32 KNativeInputSDL::screenHeight() {
    return height;
}

bool KNativeInputSDL::mouseMove(int x, int y, bool relative) {
    XServer* server = XServer::getServer(true);

    if (relative) {
        x = x * 100 / scaleX;
        y = y * 100 / scaleY;
    } else {
        x = xFromScreen(x);
        y = yFromScreen(y);
    }

#ifdef BOXEDWINE_RECORDER
    if (Player::instance && !relative) {
        lastX = x;
        lastY = y;
    }
#endif

    if (server) {
        server->mouseMove(x, y, relative);
        return true;
    }
    return false;
}

bool KNativeInputSDL::mouseWheel(int amount, int x, int y) {
    x = xFromScreen(x);
    y = yFromScreen(y);

    checkMousePos(x, y, true);

    XServer* server = XServer::getServer(true);
    if (server) {
        U32 btn = amount > 0 ? 4 : 5;
        server->mouseButton(btn, x, y, true);
        server->mouseButton(btn, x, y, false);
        return true;
    }
    return false;
}

bool KNativeInputSDL::mouseButton(U32 down, U32 button, int x, int y) {
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_GUEST_X64)
    xwireRecordInputPhase(10);
#endif
    if (KSystem::enableSoundAfterMouseClick) {
        KSystem::enableSoundAfterMouseClick = false;
        KSystem::soundEnabled = true;
        KDspAudio::iterateOpenAudio([](KDspAudioPtr& audio) {
            audio->soundEnabled();
            });
    }
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_GUEST_X64)
    xwireRecordInputPhase(11);
#endif
    x = xFromScreen(x);
    y = yFromScreen(y);

    checkMousePos(x, y, true);
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_GUEST_X64)
    xwireRecordInputPhase(12);
#endif

    XServer* server = XServer::getServer(true);
    if (server) {
        U32 btn = button + 1;
        if (btn == 2) {
            btn = 3;
        } else if (btn == 3) {
            btn = 2;
        } else if (btn == 4) {
            btn = 8;
        } else if (btn == 5) {
            btn = 9;
        }
        server->mouseButton(btn, x, y, down ? true : false);
        return true;
    }
    return false;
}

bool KNativeInputSDL::getMousePos(int* x, int* y, bool allowWarp) {
#ifdef BOXEDWINE_RECORDER
    if (Player::instance) {
        *x = lastX;
        *y = lastY;
        return checkMousePos(*x, *y, false);
    }
#endif
    SDL_GetMouseState(x, y);

    *x = xFromScreen(*x);
    *y = yFromScreen(*y);

    if (XServer::getServer(true) && XServer::getServer()->fakeFullScreenWnd) {
        XServer::getServer()->fakeFullScreenWnd->screenToWindow(*x, *y);
    }
    return checkMousePos(*x, *y, false);
}

void KNativeInputSDL::setMousePos(int x, int y) {
#ifdef BOXEDWINE_RECORDER
    if (Player::instance) {
        lastX = x;
        lastY = y;
        return;
    }
#endif

    if (XServer::getServer(true) && XServer::getServer()->fakeFullScreenWnd) {
        XServer::getServer()->fakeFullScreenWnd->screenToWindow(x, y);
    }
    x = xToScreen(x);
    y = yToScreen(y);

    KNativeSystem::warpMouse(x, y);
}

int KNativeInputSDL::xToScreen(int x) {
    return x * (int)scaleX / 100 + (int)scaleXOffset;
}

int KNativeInputSDL::xFromScreen(int x) {
    return (x - (int)scaleXOffset) * 100 / (int)scaleX;
}

int KNativeInputSDL::yToScreen(int y) {
    return y * (int)scaleY / 100 + (int)scaleYOffset;
}

int KNativeInputSDL::yFromScreen(int y) {
    return (y - (int)scaleYOffset) * 100 / (int)scaleY;
}

bool KNativeInputSDL::checkMousePos(int& x, int& y, bool allowWarp) {
    bool warp = false;
    if (x < 0) {
        x = 0;
        warp = true;
    }
    if (x >= (int)width) {
        x = (int)width - 1;
        warp = true;
    }
    if (y < 0) {
        y = 0;
        warp = true;
    }
    if (y >= (int)height) {
        y = (int)height;
        warp = true;
    }
    if (allowWarp && warp) {
        int scaledX = xToScreen(x);
        int scaledY = yToScreen(y);
        KNativeSystem::getScreen()->warpMouse(scaledX, scaledY);
    }
    return allowWarp || !warp;
}

#ifndef KMOD_SCROLL
#define KMOD_SCROLL KMOD_RESERVED
#endif

U32 KNativeInputSDL::getInputModifiers() {
#ifdef BOXEDWINE_RECORDER
    if (Player::instance) {
        return Player::instance->currentInputModifiers;
    }
#endif
    int x, y;
    unsigned int result = SDL_GetMouseState(&x, &y);
    U32 modifiers = 0;
    if (result & SDL_BUTTON_LMASK) {
        modifiers |= NATIVE_LEFT_BUTTON_MASK;
    }
    if (result & SDL_BUTTON_RMASK) {
        modifiers |= NATIVE_RIGHT_BUTTON_MASK;
    }
    if (result & SDL_BUTTON_MMASK) {
        modifiers |= NATIVE_MIDDLE_BUTTON_MASK;
    }
    if (result & SDL_BUTTON_X1MASK) {
        modifiers |= NATIVE_BUTTON_4_MASK;
    }
    if (result & SDL_BUTTON_X2MASK) {
        modifiers |= NATIVE_BUTTON_5_MASK;
    }
    SDL_Keymod mods = SDL_GetModState();
    if (mods & KMOD_SHIFT) {
        modifiers |= NATIVE_SHIFT_MASK;
    }
    if (mods & KMOD_CAPS) {
        modifiers |= NATIVE_CAPS_MASK;
    }
    if (mods & KMOD_CTRL) {
        modifiers |= NATIVE_CONTROL_MASK;
    }
    if (mods & KMOD_ALT) {
        modifiers |= NATIVE_CONTROL_MASK;
    }
    if (mods & KMOD_NUM) {
        modifiers |= NATIVE_NUM_MASK;
    }
    if (mods & KMOD_SCROLL) {
        modifiers |= NATIVE_SCROLL_MASK;
    }
    return modifiers;
}

bool KNativeInputSDL::key(U32 sdlScanCode, U32 key, U32 down) {
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_GUEST_X64)
    xwireRecordInputPhase(13);
#endif
    XServer* server = XServer::getServer(true);
    if (server) {
        U32 x11Key = XKeyboard::sdl2x11(sdlScanCode);
        if (x11Key) {
            server->key(x11Key, down ? true : false);
        }
        return true;
    }
    return false;
}

bool KNativeInputSDL::waitForEvent(U32 ms) {
    SDL_Event e = { 0 };
    if (SDL_WaitEventTimeout(&e, ms) == 1) {
#ifdef BOXEDWINE_MULTI_THREADED
        if (e.type == sdlCustomEvent) {
            SdlCallback* callback = (SdlCallback*)e.user.data1;
            if (callback->pfn) {
                callback->result = (U32)callback->pfn();
            }
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(callback->cond);
            BOXEDWINE_CONDITION_SIGNAL(callback->cond);
            return true;
        }
#endif
        handlSdlEvent(&e);
        return true;
    }
    return false;
}

bool KNativeInputSDL::processEvents() {
    SDL_Event e = {};

    while (SDL_PollEvent(&e) == 1) {
#ifdef BOXEDWINE_MULTI_THREADED
        if (e.type == sdlCustomEvent) {
            SdlCallback* callback = (SdlCallback*)e.user.data1;
            callback->result = (U32)callback->pfn();
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(callback->cond);
            BOXEDWINE_CONDITION_SIGNAL(callback->cond);
        } else
#endif
            if (!handlSdlEvent(&e)) {
                return false;
            }
    }
    return true;
}

#ifndef SDLK_NUMLOCK
#define SDLK_NUMLOCK SDL_SCANCODE_NUMLOCKCLEAR
#endif
#ifndef SDLK_SCROLLOCK
#define SDLK_SCROLLOCK SDLK_SCROLLLOCK
#endif

static U32 translate(U32 key) {
    switch (key) {
    case SDLK_ESCAPE:
        return K_KEY_ESC;
    case SDLK_1:
        return K_KEY_1;
    case SDLK_2:
        return K_KEY_2;
    case SDLK_3:
        return K_KEY_3;
    case SDLK_4:
        return K_KEY_4;
    case SDLK_5:
        return K_KEY_5;
    case SDLK_6:
        return K_KEY_6;
    case SDLK_7:
        return K_KEY_7;
    case SDLK_8:
        return K_KEY_8;
    case SDLK_9:
        return K_KEY_9;
    case SDLK_0:
        return K_KEY_0;
    case SDLK_MINUS:
        return K_KEY_MINUS;
    case SDLK_EQUALS:
        return K_KEY_EQUAL;
    case SDLK_BACKSPACE:
        return K_KEY_BACKSPACE;
    case SDLK_TAB:
        return K_KEY_TAB;
    case SDLK_q:
        return K_KEY_Q;
    case SDLK_w:
        return K_KEY_W;
    case SDLK_e:
        return K_KEY_E;
    case SDLK_r:
        return K_KEY_R;
    case SDLK_t:
        return K_KEY_T;
    case SDLK_y:
        return K_KEY_Y;
    case SDLK_u:
        return K_KEY_U;
    case SDLK_i:
        return K_KEY_I;
    case SDLK_o:
        return K_KEY_O;
    case SDLK_p:
        return K_KEY_P;
    case SDLK_LEFTBRACKET:
        return K_KEY_LEFTBRACE;
    case SDLK_RIGHTBRACKET:
        return K_KEY_RIGHTBRACE;
    case SDLK_RETURN:
        return K_KEY_ENTER;
    case SDLK_LCTRL:
        return K_KEY_LEFTCTRL;
    case SDLK_RCTRL:
        return K_KEY_RIGHTCTRL;
    case SDLK_a:
        return K_KEY_A;
    case SDLK_s:
        return K_KEY_S;
    case SDLK_d:
        return K_KEY_D;
    case SDLK_f:
        return K_KEY_F;
    case SDLK_g:
        return K_KEY_G;
    case SDLK_h:
        return K_KEY_H;
    case SDLK_j:
        return K_KEY_J;
    case SDLK_k:
        return K_KEY_K;
    case SDLK_l:
        return K_KEY_L;
    case SDLK_SEMICOLON:
        return K_KEY_SEMICOLON;
    case SDLK_QUOTE:
        return K_KEY_APOSTROPHE;
    case SDLK_BACKQUOTE:
        return K_KEY_GRAVE;
    case SDLK_LSHIFT:
        return K_KEY_LEFTSHIFT;
    case SDLK_RSHIFT:
        return K_KEY_RIGHTSHIFT;
    case SDLK_BACKSLASH:
        return K_KEY_BACKSLASH;
    case SDLK_z:
        return K_KEY_Z;
    case SDLK_x:
        return K_KEY_X;
    case SDLK_c:
        return K_KEY_C;
    case SDLK_v:
        return K_KEY_V;
    case SDLK_b:
        return K_KEY_B;
    case SDLK_n:
        return K_KEY_N;
    case SDLK_m:
        return K_KEY_M;
    case SDLK_COMMA:
        return K_KEY_COMMA;
    case SDLK_PERIOD:
        return K_KEY_DOT;
    case SDLK_SLASH:
        return K_KEY_SLASH;
    case SDLK_LALT:
        return K_KEY_LEFTALT;
    case SDLK_RALT:
        return K_KEY_RIGHTALT;
    case SDLK_SPACE:
        return K_KEY_SPACE;
    case SDLK_CAPSLOCK:
        return K_KEY_CAPSLOCK;
    case SDLK_F1:
        return K_KEY_F1;
    case SDLK_F2:
        return K_KEY_F2;
    case SDLK_F3:
        return K_KEY_F3;
    case SDLK_F4:
        return K_KEY_F4;
    case SDLK_F5:
        return K_KEY_F5;
    case SDLK_F6:
        return K_KEY_F6;
    case SDLK_F7:
        return K_KEY_F7;
    case SDLK_F8:
        return K_KEY_F8;
    case SDLK_F9:
        return K_KEY_F9;
    case SDLK_F10:
        return K_KEY_F10;
    case SDLK_NUMLOCK:
        return K_KEY_NUMLOCK;
    case SDLK_SCROLLOCK:
        return K_KEY_SCROLLLOCK;
    case SDLK_F11:
        return K_KEY_F11;
    case SDLK_F12:
        return K_KEY_F12;
    case SDLK_HOME:
        return K_KEY_HOME;
    case SDLK_UP:
        return K_KEY_UP;
    case SDLK_PAGEUP:
        return K_KEY_PAGEUP;
    case SDLK_LEFT:
        return K_KEY_LEFT;
    case SDLK_RIGHT:
        return K_KEY_RIGHT;
    case SDLK_END:
        return K_KEY_END;
    case SDLK_DOWN:
        return K_KEY_DOWN;
    case SDLK_PAGEDOWN:
        return K_KEY_PAGEDOWN;
    case SDLK_INSERT:
        return K_KEY_INSERT;
    case SDLK_DELETE:
        return K_KEY_DELETE;
    case SDLK_PAUSE:
        return K_KEY_PAUSE;
    default:
        kdebug("Unhandled key: %d", key);
        return 0;
    }
}

static int getMouseButtonFromEvent(SDL_Event* e) {
    if (e->button.button == SDL_BUTTON_LEFT) {
        return 0;
    } else if (e->button.button == SDL_BUTTON_MIDDLE) {
        return 2;
    } else if (e->button.button == SDL_BUTTON_RIGHT) {
        return 1;
    }
    return 0;
}

// return true to continue processing for custom handlers
//
// should only call on main thread
void KNativeInputSDL::processCustomEvents(std::function<bool(bool isKeyDown, int key, bool isF11)> onKey, std::function<bool(bool isButtonDown, int button, int x, int y)> onMouseButton, std::function<bool(int x, int y)> onMouseMove) {
    SDL_Event e = {};

#ifdef _DEBUG
    if (!isMainthread()) {
        kpanic("KNativeInputSDL::processCustomEvents should only be called on main thread");
    }
#endif
    while (SDL_WaitEvent(&e)) {
        if (e.type == SDL_KEYUP) {
            if (!onKey(false, e.key.keysym.sym, e.key.keysym.sym == SDLK_F11)) {
                return;
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            if (!onMouseButton(true, getMouseButtonFromEvent(&e), e.motion.x, e.motion.y)) {
                return;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP) {
            if (!onMouseButton(false, getMouseButtonFromEvent(&e), e.motion.x, e.motion.y)) {
                return;
            }
        } else if (e.type == SDL_MOUSEMOTION) {
            if (!onMouseMove(e.motion.x, e.motion.y)) {
                return;
            }
        }
#ifdef BOXEDWINE_MULTI_THREADED
        else if (e.type == sdlCustomEvent) {
            SdlCallback* callback = (SdlCallback*)e.user.data1;
            callback->result = (U32)callback->pfn();
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(callback->cond);
            BOXEDWINE_CONDITION_SIGNAL(callback->cond);
        }
#endif
    }
}


// ---- Game controller feed -------------------------------------------------
// The kernel exposes exactly one evdev gamepad (/dev/input/event5,
// DevInputJoystick in source/kernel/devs/devinput.cpp); onGamepadButton /
// onGamepadAxis are its host-feed entry points and nothing fed them, so the
// device sat at rest forever and no guest ever saw a live controller. This
// pump owns host controllers the way it owns the keyboard and mouse: SDL
// events are translated to the evdev codes the kernel device speaks.
// SDL_GameController events (normalized through SDL's standard mapping;
// under Emscripten they originate from the browser Gamepad API) are the
// primary path. A device SDL cannot classify as a game controller falls
// back to raw joystick events read in the W3C standard-gamepad layout,
// with its triggers degraded to digital 0/255 (the raw joystick API
// exposes no analog trigger value). Only the FIRST controller is fed:
// the kernel models a single pad. On removal the guest is fed a neutral
// state first, so a disconnected pad never leaves a button stuck down.

namespace {

SDL_GameController* bwPadController = nullptr;
SDL_Joystick* bwPadJoystick = nullptr;
SDL_JoystickID bwPadInstance = -1;
bool bwHatUp = false, bwHatDown = false, bwHatLeft = false, bwHatRight = false;

void bwFeedHat() {
    onGamepadAxis(K_ABS_HAT0X, bwHatRight ? 1 : (bwHatLeft ? -1 : 0));
    onGamepadAxis(K_ABS_HAT0Y, bwHatDown ? 1 : (bwHatUp ? -1 : 0));
}

void bwFeedHatDirection(U32 direction, bool down) {
    // direction: 0=up 1=down 2=left 3=right
    if (direction == 0) bwHatUp = down;
    else if (direction == 1) bwHatDown = down;
    else if (direction == 2) bwHatLeft = down;
    else bwHatRight = down;
    bwFeedHat();
}

void bwFeedTrigger(U32 axis, S32 value) {
    // SDL controller triggers rest at 0 and reach 32767; the evdev
    // triggers are bytes 0..255.
    if (value < 0) value = 0;
    onGamepadAxis(axis, value * 255 / 32767);
}

void bwFeedDigitalTrigger(U32 axis, bool down) {
    onGamepadAxis(axis, down ? 255 : 0);
}

U32 bwEvdevButton(SDL_GameControllerButton button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A: return K_BTN_SOUTH;
        case SDL_CONTROLLER_BUTTON_B: return K_BTN_EAST;
        case SDL_CONTROLLER_BUTTON_X: return K_BTN_WEST;
        case SDL_CONTROLLER_BUTTON_Y: return K_BTN_NORTH;
        case SDL_CONTROLLER_BUTTON_BACK: return K_BTN_SELECT;
        case SDL_CONTROLLER_BUTTON_GUIDE: return K_BTN_MODE;
        case SDL_CONTROLLER_BUTTON_START: return K_BTN_START;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return K_BTN_THUMBL;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return K_BTN_THUMBR;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return K_BTN_TL;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return K_BTN_TR;
        default: return 0;
    }
}

void bwFeedNeutralState() {
    for (U32 code = K_BTN_SOUTH; code <= K_BTN_THUMBR; code++) {
        onGamepadButton(code, false);
    }
    bwHatUp = bwHatDown = bwHatLeft = bwHatRight = false;
    bwFeedHat();
    onGamepadAxis(K_ABS_X, 0);
    onGamepadAxis(K_ABS_Y, 0);
    onGamepadAxis(K_ABS_Z, 0);
    onGamepadAxis(K_ABS_RX, 0);
    onGamepadAxis(K_ABS_RY, 0);
    onGamepadAxis(K_ABS_RZ, 0);
}

void bwSyncControllerState() {
    // A button or stick may already be held when the device opens;
    // EVIOCGKEY/EVIOCGABS readers in the guest must see the true state,
    // not wait for the next change.
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
        SDL_GameControllerButton button = (SDL_GameControllerButton)b;
        bool down = SDL_GameControllerGetButton(bwPadController, button) != 0;
        if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) bwHatUp = down;
        else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) bwHatDown = down;
        else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) bwHatLeft = down;
        else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) bwHatRight = down;
        else {
            U32 code = bwEvdevButton(button);
            if (code) onGamepadButton(code, down);
        }
    }
    bwFeedHat();
    for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) {
        SDL_GameControllerAxis axis = (SDL_GameControllerAxis)a;
        S32 value = SDL_GameControllerGetAxis(bwPadController, axis);
        switch (axis) {
            case SDL_CONTROLLER_AXIS_LEFTX: onGamepadAxis(K_ABS_X, value); break;
            case SDL_CONTROLLER_AXIS_LEFTY: onGamepadAxis(K_ABS_Y, value); break;
            case SDL_CONTROLLER_AXIS_RIGHTX: onGamepadAxis(K_ABS_RX, value); break;
            case SDL_CONTROLLER_AXIS_RIGHTY: onGamepadAxis(K_ABS_RY, value); break;
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT: bwFeedTrigger(K_ABS_Z, value); break;
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: bwFeedTrigger(K_ABS_RZ, value); break;
            default: break;
        }
    }
}

void bwSyncJoystickState() {
    // W3C standard layout indices for the fallback path.
    static const U32 kButtons[] = {K_BTN_SOUTH, K_BTN_EAST, K_BTN_WEST, K_BTN_NORTH,
        K_BTN_TL, K_BTN_TR, 0, 0, K_BTN_SELECT, K_BTN_START, K_BTN_THUMBL, K_BTN_THUMBR};
    int count = SDL_JoystickNumButtons(bwPadJoystick);
    for (int i = 0; i < count && i < 17; i++) {
        bool down = SDL_JoystickGetButton(bwPadJoystick, i) != 0;
        if (i == 6) bwFeedDigitalTrigger(K_ABS_Z, down);
        else if (i == 7) bwFeedDigitalTrigger(K_ABS_RZ, down);
        else if (i == 12) bwHatUp = down;
        else if (i == 13) bwHatDown = down;
        else if (i == 14) bwHatLeft = down;
        else if (i == 15) bwHatRight = down;
        else if (i == 16) onGamepadButton(K_BTN_MODE, down);
        else if (i < (int)(sizeof(kButtons) / sizeof(kButtons[0])) && kButtons[i]) onGamepadButton(kButtons[i], down);
    }
    bwFeedHat();
    if (SDL_JoystickNumAxes(bwPadJoystick) > 0) onGamepadAxis(K_ABS_X, SDL_JoystickGetAxis(bwPadJoystick, 0));
    if (SDL_JoystickNumAxes(bwPadJoystick) > 1) onGamepadAxis(K_ABS_Y, SDL_JoystickGetAxis(bwPadJoystick, 1));
    if (SDL_JoystickNumAxes(bwPadJoystick) > 2) onGamepadAxis(K_ABS_RX, SDL_JoystickGetAxis(bwPadJoystick, 2));
    if (SDL_JoystickNumAxes(bwPadJoystick) > 3) onGamepadAxis(K_ABS_RY, SDL_JoystickGetAxis(bwPadJoystick, 3));
}

void bwOpenPad(int index) {
    if (bwPadInstance >= 0) {
        return; // one pad is already fed; the kernel models exactly one
    }
    if (SDL_IsGameController(index)) {
        bwPadController = SDL_GameControllerOpen(index);
        if (bwPadController) {
            SDL_Joystick* stick = SDL_GameControllerGetJoystick(bwPadController);
            bwPadInstance = stick ? SDL_JoystickInstanceID(stick) : -1;
            klog_fmt("Gamepad feed: controller '%s' opened (instance %d)", SDL_GameControllerName(bwPadController), (int)bwPadInstance);
            bwSyncControllerState();
            return;
        }
    }
    bwPadJoystick = SDL_JoystickOpen(index);
    if (bwPadJoystick) {
        bwPadInstance = SDL_JoystickInstanceID(bwPadJoystick);
        klog_fmt("Gamepad feed: joystick '%s' opened as fallback (instance %d)", SDL_JoystickName(bwPadJoystick), (int)bwPadInstance);
        bwSyncJoystickState();
    }
}

void bwClosePad(SDL_JoystickID instance) {
    if (instance != bwPadInstance) {
        return;
    }
    bwFeedNeutralState();
    if (bwPadController) {
        SDL_GameControllerClose(bwPadController);
        bwPadController = nullptr;
    }
    if (bwPadJoystick) {
        SDL_JoystickClose(bwPadJoystick);
        bwPadJoystick = nullptr;
    }
    bwPadInstance = -1;
    klog_fmt("Gamepad feed: controller removed (instance %d)", (int)instance);
}

// Returns true when the event belonged to the controller feed.
bool bwHandlePadEvent(SDL_Event* e) {
    switch (e->type) {
        case SDL_JOYDEVICEADDED:
            bwOpenPad(e->jdevice.which);
            return true;
        case SDL_JOYDEVICEREMOVED:
            bwClosePad(e->jdevice.which);
            return true;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            if (!bwPadController || e->cbutton.which != bwPadInstance) return true;
            bool down = e->type == SDL_CONTROLLERBUTTONDOWN;
            SDL_GameControllerButton button = (SDL_GameControllerButton)e->cbutton.button;
            if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) bwFeedHatDirection(0, down);
            else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) bwFeedHatDirection(1, down);
            else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) bwFeedHatDirection(2, down);
            else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) bwFeedHatDirection(3, down);
            else {
                U32 code = bwEvdevButton(button);
                if (code) onGamepadButton(code, down);
            }
            return true;
        }
        case SDL_CONTROLLERAXISMOTION: {
            if (!bwPadController || e->caxis.which != bwPadInstance) return true;
            switch ((SDL_GameControllerAxis)e->caxis.axis) {
                case SDL_CONTROLLER_AXIS_LEFTX: onGamepadAxis(K_ABS_X, e->caxis.value); break;
                case SDL_CONTROLLER_AXIS_LEFTY: onGamepadAxis(K_ABS_Y, e->caxis.value); break;
                case SDL_CONTROLLER_AXIS_RIGHTX: onGamepadAxis(K_ABS_RX, e->caxis.value); break;
                case SDL_CONTROLLER_AXIS_RIGHTY: onGamepadAxis(K_ABS_RY, e->caxis.value); break;
                case SDL_CONTROLLER_AXIS_TRIGGERLEFT: bwFeedTrigger(K_ABS_Z, e->caxis.value); break;
                case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: bwFeedTrigger(K_ABS_RZ, e->caxis.value); break;
                default: break;
            }
            return true;
        }
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP: {
            // With a GameController open, SDL also emits raw events for
            // the same device; the controller events carry it, so the raw
            // copies are consumed and ignored to avoid double-feeding.
            if (bwPadController) return true;
            if (!bwPadJoystick || e->jbutton.which != bwPadInstance) return false;
            bool down = e->type == SDL_JOYBUTTONDOWN;
            switch (e->jbutton.button) {
                case 0: onGamepadButton(K_BTN_SOUTH, down); break;
                case 1: onGamepadButton(K_BTN_EAST, down); break;
                case 2: onGamepadButton(K_BTN_WEST, down); break;
                case 3: onGamepadButton(K_BTN_NORTH, down); break;
                case 4: onGamepadButton(K_BTN_TL, down); break;
                case 5: onGamepadButton(K_BTN_TR, down); break;
                case 6: bwFeedDigitalTrigger(K_ABS_Z, down); break;
                case 7: bwFeedDigitalTrigger(K_ABS_RZ, down); break;
                case 8: onGamepadButton(K_BTN_SELECT, down); break;
                case 9: onGamepadButton(K_BTN_START, down); break;
                case 10: onGamepadButton(K_BTN_THUMBL, down); break;
                case 11: onGamepadButton(K_BTN_THUMBR, down); break;
                case 12: bwFeedHatDirection(0, down); break;
                case 13: bwFeedHatDirection(1, down); break;
                case 14: bwFeedHatDirection(2, down); break;
                case 15: bwFeedHatDirection(3, down); break;
                case 16: onGamepadButton(K_BTN_MODE, down); break;
                default: break;
            }
            return true;
        }
        case SDL_JOYAXISMOTION: {
            if (bwPadController) return true;
            if (!bwPadJoystick || e->jaxis.which != bwPadInstance) return false;
            switch (e->jaxis.axis) {
                case 0: onGamepadAxis(K_ABS_X, e->jaxis.value); break;
                case 1: onGamepadAxis(K_ABS_Y, e->jaxis.value); break;
                case 2: onGamepadAxis(K_ABS_RX, e->jaxis.value); break;
                case 3: onGamepadAxis(K_ABS_RY, e->jaxis.value); break;
                default: break;
            }
            return true;
        }
        case SDL_JOYHATMOTION: {
            if (bwPadController) return true;
            if (!bwPadJoystick || e->jhat.which != bwPadInstance) return false;
            if (e->jhat.hat == 0) {
                bwHatUp = (e->jhat.value & SDL_HAT_UP) != 0;
                bwHatDown = (e->jhat.value & SDL_HAT_DOWN) != 0;
                bwHatLeft = (e->jhat.value & SDL_HAT_LEFT) != 0;
                bwHatRight = (e->jhat.value & SDL_HAT_RIGHT) != 0;
                bwFeedHat();
            }
            return true;
        }
        default:
            return false;
    }
}

} // namespace

bool KNativeInputSDL::handlSdlEvent(SDL_Event* e) {
#ifdef BOXEDWINE_RECORDER
    if (Player::instance) {
        if (e->type == SDL_QUIT) {
            return false;
        }
        return true;
    }
#endif    
    if (e->type == SDL_QUIT) {
        KThread::setCurrentThread(nullptr);
        KProcessPtr p = KSystem::getProcess(10);
        if (p && !KSystem::shutingDown) {
            p->killAllThreads();
            KSystem::eraseProcess(p->id);
            return true;
        }
        return false;
    }
    if (bwHandlePadEvent(e)) {
        return true;
    }
#ifdef BOXEDWINE_GUEST_X64
    // When the in-process X11 wire server owns the host window (64-bit wine GUI),
    // route key/mouse input to it. This is the SINGLE SDL pump in the process,
    // so forwarding here (rather than a competing SDL_PollEvent in the XWire
    // sink) is what actually delivers input to wine. No-op when no sink is
    // active. We still fall through to BoxedWine's own handling below — harmless
    // for the 64-bit path (its 32-bit X server is dead code) and keeps QUIT etc.
    xwireForwardSdlEvent(*e);
#endif
    if (e->type == SDL_MOUSEMOTION) {
        BOXEDWINE_RECORDER_HANDLE_MOUSE_MOVE(e->motion.x, e->motion.y);
        const bool relative = SDL_GetRelativeMouseMode() == SDL_TRUE;
        if (!mouseMove(relative ? e->motion.xrel : e->motion.x,
                       relative ? e->motion.yrel : e->motion.y, relative)) {
            onMouseMove(e->motion.x, e->motion.y, false);
        }
    } else if (e->type == SDL_MOUSEBUTTONDOWN) {    
        U32 button = getMouseButtonFromEvent(e);
        BOXEDWINE_RECORDER_HANDLE_MOUSE_BUTTON_DOWN(button, e->motion.x, e->motion.y);
        if (!mouseButton(1, button, e->motion.x, e->motion.y)) {
            onMouseButtonDown(button);
        }
    } else if (e->type == SDL_MOUSEBUTTONUP) {      
        U32 button = getMouseButtonFromEvent(e);
        BOXEDWINE_RECORDER_HANDLE_MOUSE_BUTTON_UP(button, e->motion.x, e->motion.y);
        if (!mouseButton(0, button, e->motion.x, e->motion.y)) {
            onMouseButtonUp(button);
        }
    } else if (e->type == SDL_MOUSEWHEEL) {
        // Handle up/down mouse wheel movements
        int x, y;
        SDL_GetMouseState(&x, &y);
        if (!mouseWheel(e->wheel.y * 80, x, y)) {
            onMouseWheel(e->wheel.y);
        }
    } else if (e->type == SDL_KEYDOWN) {       
        if (!BOXEDWINE_RECORDER_HANDLE_KEY_DOWN(e->key.keysym.scancode, e->key.keysym.sym == SDLK_F11)) {
            if (e->key.keysym.sym == SDLK_SCROLLOCK) {
                KSystem::printStacks();
            } else if (!key(e->key.keysym.scancode, e->key.keysym.sym, 1)) {
                onKeyDown(translate(e->key.keysym.sym));
            }
        }
    } else if (e->type == SDL_KEYUP) {
        if (!BOXEDWINE_RECORDER_HANDLE_KEY_UP(e->key.keysym.scancode, e->key.keysym.sym == SDLK_F11)) {
            if (!key(e->key.keysym.scancode, e->key.keysym.sym, 0)) {
                onKeyUp(translate(e->key.keysym.sym));
            }
        }
    } else if (e->type == SDL_WINDOWEVENT) {
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            for (auto& callback : onFocusGained) {
                callback();
            }
        } else if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            for (auto& callback : onFocusLost) {
                callback();
            }
        }
    }
    return true;
}

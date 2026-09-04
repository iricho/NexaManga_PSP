#include "Input.hpp"

#include "BuildConfig.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

#ifdef __PSP__
#include <pspctrl.h>
#endif

namespace {

uint32_t bit(Button b) {
    return static_cast<uint32_t>(b);
}

#ifndef __PSP__
uint32_t readDesktop(float& analogX, float& analogY) {
    SDL_PumpEvents();
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    uint32_t value = 0;

    if (keys[SDL_SCANCODE_UP]) value |= bit(Button::Up);
    if (keys[SDL_SCANCODE_DOWN]) value |= bit(Button::Down);
    if (keys[SDL_SCANCODE_LEFT]) value |= bit(Button::Left);
    if (keys[SDL_SCANCODE_RIGHT]) value |= bit(Button::Right);

    if (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_RETURN]) value |= bit(Button::Confirm);
    if (keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_ESCAPE]) value |= bit(Button::Back);

    if (keys[SDL_SCANCODE_Q]) value |= bit(Button::PrevPage);
    if (keys[SDL_SCANCODE_E]) value |= bit(Button::NextPage);

    if (keys[SDL_SCANCODE_T]) value |= bit(Button::ZoomOut);
    if (keys[SDL_SCANCODE_F]) value |= bit(Button::ToggleMode);
    if (keys[SDL_SCANCODE_P]) value |= bit(Button::Start);
    if (keys[SDL_SCANCODE_TAB]) value |= bit(Button::Select);
    if (keys[SDL_SCANCODE_F3]) value |= bit(Button::Debug);

    if (keys[SDL_SCANCODE_A]) analogX -= 1.0f;
    if (keys[SDL_SCANCODE_D]) analogX += 1.0f;
    if (keys[SDL_SCANCODE_W]) analogY -= 1.0f;
    if (keys[SDL_SCANCODE_S]) analogY += 1.0f;

    return value;
}
#endif

#ifdef __PSP__
float normalizeAxis(unsigned char raw, float deadZone) {
    constexpr float center = 128.0f;
    const float delta = static_cast<float>(raw) - center;
    const float magnitude = std::abs(delta);
    if (magnitude <= deadZone) return 0.0f;
    const float normalized = (magnitude - deadZone) / (127.0f - deadZone);
    return std::max(-1.0f, std::min(1.0f, delta < 0.0f ? -normalized : normalized));
}
#endif

} // namespace

Input::Input() {
#if !(MANGAPSP_DEVELOPMENT && MANGAPSP_DEFER_PSP_INPUT_SETUP)
    initializePlatform();
#endif
}

void Input::initializePlatform() {
#ifdef __PSP__
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
#endif
}

void Input::setAnalogDeadZone(float rawUnits) {
    analogDeadZone_ = std::max(4.0f, std::min(48.0f, rawUnits));
}

InputFrame Input::poll() {
    uint32_t current = 0;

#ifdef __PSP__
    SceCtrlData pad {};
    sceCtrlPeekBufferPositive(&pad, 1);

    if (pad.Buttons & PSP_CTRL_UP) current |= bit(Button::Up);
    if (pad.Buttons & PSP_CTRL_DOWN) current |= bit(Button::Down);
    if (pad.Buttons & PSP_CTRL_LEFT) current |= bit(Button::Left);
    if (pad.Buttons & PSP_CTRL_RIGHT) current |= bit(Button::Right);

    if (pad.Buttons & PSP_CTRL_CROSS) current |= bit(Button::Confirm);
    if (pad.Buttons & PSP_CTRL_CIRCLE) current |= bit(Button::Back);

    if (pad.Buttons & PSP_CTRL_LTRIGGER) current |= bit(Button::PrevPage);
    if (pad.Buttons & PSP_CTRL_RTRIGGER) current |= bit(Button::NextPage);

    if (pad.Buttons & PSP_CTRL_TRIANGLE) current |= bit(Button::ZoomOut);
    if (pad.Buttons & PSP_CTRL_SQUARE) current |= bit(Button::ToggleMode);
    if (pad.Buttons & PSP_CTRL_START) current |= bit(Button::Start);
    if (pad.Buttons & PSP_CTRL_SELECT) current |= bit(Button::Select);
#else
    float analogX = 0.0f;
    float analogY = 0.0f;
    current = readDesktop(analogX, analogY);
#endif

    InputFrame frame;
    frame.held = current;
    frame.pressed = current & ~previous_;
#ifdef __PSP__
    frame.analogX = normalizeAxis(pad.Lx, analogDeadZone_);
    frame.analogY = normalizeAxis(pad.Ly, analogDeadZone_);
#else
    frame.analogX = analogX;
    frame.analogY = analogY;
#endif

    previous_ = current;
    return frame;
}

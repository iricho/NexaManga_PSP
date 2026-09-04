#pragma once

#include <cstdint>

enum class Button : uint32_t {
    Up         = 1u << 0,
    Down       = 1u << 1,
    Left       = 1u << 2,
    Right      = 1u << 3,
    Confirm    = 1u << 4,
    Back       = 1u << 5,
    PrevPage   = 1u << 6,
    NextPage   = 1u << 7,
    ZoomOut    = 1u << 8,
    ToggleMode = 1u << 9,
    Start      = 1u << 10,
    Select     = 1u << 11,
    Debug      = 1u << 12,
};

struct InputFrame {
    uint32_t held = 0;
    uint32_t pressed = 0;
    float analogX = 0.0f;
    float analogY = 0.0f;

    bool isHeld(Button b) const {
        return (held & static_cast<uint32_t>(b)) != 0;
    }

    bool isPressed(Button b) const {
        return (pressed & static_cast<uint32_t>(b)) != 0;
    }
};

class Input {
public:
    Input();
    void initializePlatform();
    InputFrame poll();
    void setAnalogDeadZone(float rawUnits);
    float analogDeadZone() const { return analogDeadZone_; }

private:
    uint32_t previous_ = 0;
    float analogDeadZone_ = 18.0f;
};

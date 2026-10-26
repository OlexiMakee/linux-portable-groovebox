#pragma once

#include "input_manager.hpp"
#include "ui/ui_context.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace groovebox {

enum class Button : uint8_t {
    kDpadUp = 0,
    kDpadDown,
    kDpadLeft,
    kDpadRight,
    kA,
    kB,
    kX,
    kY,
    kL1,
    kL2,
    kR1,
    kR2,
    kL3,
    kR3,
    kStart,
    kSelect,
    kFn,
    kVolUp,
    kVolDown,
    kCount
};

inline constexpr int kButtonCount = static_cast<int>(Button::kCount);

inline constexpr const char* getButtonName(Button b) {
    switch (b) {
        case Button::kDpadUp:    return "UP";
        case Button::kDpadDown:  return "DOWN";
        case Button::kDpadLeft:  return "LEFT";
        case Button::kDpadRight: return "RIGHT";
        case Button::kA:         return "A";
        case Button::kB:         return "B";
        case Button::kX:         return "X";
        case Button::kY:         return "Y";
        case Button::kL1:        return "L1";
        case Button::kL2:        return "L2";
        case Button::kR1:        return "R1";
        case Button::kR2:        return "R2";
        case Button::kL3:        return "L3";
        case Button::kR3:        return "R3";
        case Button::kStart:     return "START";
        case Button::kSelect:    return "SELECT";
        case Button::kFn:        return "FN";
        case Button::kVolUp:     return "VOL+";
        case Button::kVolDown:   return "VOL-";
        default:                 return "???";
    }
}

enum class Layer : uint8_t {
    kBase = 0,   // Звичайний режим
    kSelect,     // Затиснуто SELECT
    kHoldL2,     // Затиснуто L2 у секвенсорі (Elektron Quick Mute: A/B/X/Y глушать треки 1-4)
    kCount
};

enum class Gesture : uint8_t {
    kPress = 0,  // Спрацьовує одразу натисканням (клік або повтор D-Pad)
    kTap,        // Короткий клік (< 200 мс, спрацьовує при відпусканні)
    kHold        // Утримання (>= 200 мс)
};

struct KeyBinding {
    ViewId view = ViewId::kCount; // ViewId::kCount = Глобальна прив'язка (для всіх вікон)
    Layer layer = Layer::kBase;
    Button button = Button::kA;
    Gesture gesture = Gesture::kPress;
    std::string action_id;
};

class Keymap {
public:
    Keymap();

    void bind(KeyBinding b);
    void bind(ViewId v, Layer l, Button b, Gesture g, std::string action_id);

    const std::string* lookup(ViewId view, Layer layer, Button button, Gesture gesture) const;
    std::string actionLabel(ViewId view, Layer layer, Button button) const;

private:
    std::vector<KeyBinding> bindings_;
};

class InputRouter {
public:
    explicit InputRouter(Keymap keymap);

    void processInput(UiContext& ctx);
    void consume(Button b); // запобігає спрацьовуванню Tap після стік-твіку

    Layer activeLayer(const UiContext& ctx) const;
    const Keymap& keymap() const { return keymap_; }

private:
    void dispatch(UiContext& ctx, ViewId view, Layer layer, Button btn, Gesture gest);

    Keymap keymap_;
    std::array<uint32_t, kButtonCount> press_time_ms_{};
    std::array<bool, kButtonCount> is_down_{};
    std::array<bool, kButtonCount> hold_fired_{};
    std::array<bool, kButtonCount> consumed_{};
};

} // namespace groovebox

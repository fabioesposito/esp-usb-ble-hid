#pragma once

// Input mapping: the user settings applied to each controller report, and the
// Switch's Change Grip/Order registration window.
//
// Standard-library only (plus GamepadInputs) so the mapping math is
// host-testable (see test/input_mapping_host_test.cpp); the app wires it to
// the persisted settings and the USB personalities in main/main.cpp.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "gamepad_inputs.hpp"

namespace input_mapping {

/// The settings that affect the input mapping: a POD subset of
/// device_config::Settings, with the same defaults.
struct Settings {
  bool invert_left_y{true};
  bool invert_right_y{true};
  bool swap_ab{false};
  bool swap_xy{false};
  uint8_t deadzone_percent{0};
};

/// Radial deadzone: inside it the stick reads (0,0); outside it the remaining
/// travel is rescaled so full deflection still reaches 1.0. @p deadzone is a
/// fraction of full travel, [0, 1) (the settings validate 0..50 %).
inline void apply_deadzone(GamepadInputs::Joystick &j, float deadzone) {
  if (deadzone <= 0.0f)
    return;
  const float mag = std::sqrt(j.x * j.x + j.y * j.y);
  if (mag < deadzone) {
    j.x = j.y = 0.0f;
    return;
  }
  const float scale = std::min(1.0f, (mag - deadzone) / (1.0f - deadzone)) / mag;
  j.x *= scale;
  j.y *= scale;
}

/// Apply the settings to one report, in place: Y inversion, radial deadzone,
/// then the A/B and X/Y swaps.
inline void apply(GamepadInputs &inputs, const Settings &s) {
  if (s.invert_left_y)
    inputs.left_joystick.y = -inputs.left_joystick.y;
  if (s.invert_right_y)
    inputs.right_joystick.y = -inputs.right_joystick.y;
  const float deadzone = s.deadzone_percent / 100.0f;
  apply_deadzone(inputs.left_joystick, deadzone);
  apply_deadzone(inputs.right_joystick, deadzone);
  if (s.swap_ab) {
    const bool a = inputs.buttons.a, b = inputs.buttons.b;
    inputs.buttons.a = b;
    inputs.buttons.b = a;
  }
  if (s.swap_xy) {
    const bool x = inputs.buttons.x, y = inputs.buttons.y;
    inputs.buttons.x = y;
    inputs.buttons.y = x;
  }
}

/// X-Input axis scaling: a [-1, 1] float to the full 16-bit range.
inline int16_t axis(float v) { return static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }

/// X-Input trigger scaling: a [0, 1] float to the full 8-bit range.
inline uint8_t trigger(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); }

/// The Switch's Change Grip/Order screen registers a controller when L+R are
/// pressed: hold them for kWindowUs after the host starts taking reports, then
/// release them. Feed it the HID-ready state from a periodic tick; it says
/// when the latest inputs must be re-pushed so the press / release reaches the
/// host even without a new controller report.
class RegistrationWindow {
public:
  static constexpr int64_t kWindowUs = 1000 * 1000; // 1 s

  /// Advance the window. Returns true when the window just opened or closed
  /// and the caller should re-push the latest inputs.
  bool tick(int64_t now_us, bool hid_ready) {
    bool repush = false;
    if (hid_ready && !was_ready_) {
      until_us_ = now_us + kWindowUs;
      repush = true;
    } else if (!hid_ready) {
      until_us_ = 0;
    } else if (until_us_ && now_us >= until_us_) {
      until_us_ = 0;
      repush = true;
    }
    was_ready_ = hid_ready;
    return repush;
  }

  /// Whether L+R should be held at @p now_us.
  bool holds_lr(int64_t now_us) const { return now_us < until_us_; }

private:
  bool was_ready_{false};
  int64_t until_us_{0};
};

} // namespace input_mapping

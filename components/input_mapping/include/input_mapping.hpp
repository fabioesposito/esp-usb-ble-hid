#pragma once

// Output-stage helpers: the X-Input axis / trigger scaling and the Switch's
// Change Grip/Order registration window.
//
// The per-report input mapping (swaps, inversions, dead zones) is the button
// map's job since v4 (espp::GamepadMap, applied in main/main.cpp before the
// inputs get here).
//
// Standard-library only (plus GamepadInputs) so the math is host-testable
// (see test/input_mapping_host_test.cpp); the app wires it to the USB
// personalities in main/main.cpp.

#include <algorithm>
#include <cstdint>

#include "gamepad_inputs.hpp"

namespace input_mapping {

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

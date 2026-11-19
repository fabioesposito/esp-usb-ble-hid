// Host-side golden-vector test for the Xbox BLE report parser.
//
// The vectors encode the Xbox Wireless Controller's BLE HID reports byte for
// byte (the layout its own HID report descriptor declares, which
// espp::XboxGamepadInputReport reproduces):
//
//   input report id 1, 16-byte payload:
//     [0..1]  left stick X   u16 LE, 0..65535, center 32767
//     [2..3]  left stick Y   u16 LE
//     [4..5]  right stick X  u16 LE
//     [6..7]  right stick Y  u16 LE
//     [8..9]  left trigger   u16 LE, 0..1023
//     [10..11] right trigger u16 LE, 0..1023
//     [12]    hat switch, low nibble (0x0F centered, 1 up, 2 up-right, 3 right,
//             4 down-right, 5 down, 6 down-left, 7 left, 8 up-left)
//     [13..14] buttons, LE bitset, button N = bit N-1:
//             A=1 B=2 X=4 Y=5 L1=7 R1=8 L2=9 R2=10 Options=11 Menu=12
//             Home=13 L3=14 R3=15
//     [15]    bit0 = capture / share (consumer record)
//   battery report id 4: one status byte
//   rumble output report id 3: [enable][lt][rt][strong][weak][sustain][release][loop]
//
// Needs the espp headers (hid-rp, logger, format, math, base_component), which
// the IDF component manager fetches into managed_components/ on the first
// build; after `idf.py build` (or reconfigure) this builds on a host (C++23,
// like ESP-IDF itself -- the espp button concepts use deducing auto):
/*
  c++ -std=c++23 -Wall -Wextra -Werror -DFMT_HEADER_ONLY \
      -I components/xbox/include -I components/gamepad_device/include \
      -I components/gamepad_inputs/include \
      -isystem managed_components/espp__hid-rp/include \
      -isystem managed_components/espp__hid-rp/detail/hid-rp/hid-rp \
      -isystem managed_components/espp__math/include \
      -isystem managed_components/espp__base_component/include \
      -isystem managed_components/espp__logger/include \
      -isystem managed_components/espp__format/include \
      -isystem managed_components/espp__format/detail/fmt/include \
      components/xbox/test/xbox_host_test.cpp components/xbox/src/xbox.cpp \
      managed_components/espp__logger/src/logger.cpp -lm -o xbox_test && ./xbox_test
*/
// CI runs it in the Build workflow, right after the firmware build (the
// managed components are already there, pinned by dependencies.lock).

#include <cmath>
#include <cstdio>
#include <vector>

#include "xbox.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);                                      \
      ++g_failures;                                                                                \
    }                                                                                              \
  } while (0)

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// A neutral report: sticks centered, triggers released, hat centered, nothing
// pressed. Every other vector starts from this.
static std::vector<uint8_t> neutral() {
  return {0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, // sticks = 32767
          0x00, 0x00, 0x00, 0x00,                         // triggers = 0
          0x0F,                                           // hat centered
          0x00, 0x00,                                     // no buttons
          0x00};                                          // no capture
}

int main() {
  Xbox xbox;

  // ---- neutral report ----
  {
    xbox.set_report_data(1, neutral().data(), 16);
    const auto in = xbox.get_gamepad_inputs();
    CHECK(near(in.left_joystick.x, 0.0f) && near(in.left_joystick.y, 0.0f));
    CHECK(near(in.right_joystick.x, 0.0f) && near(in.right_joystick.y, 0.0f));
    CHECK(near(in.l2.value, 0.0f) && near(in.r2.value, 0.0f));
    CHECK(in.buttons.raw == 0);
  }

  // ---- face buttons + home / menu / options + capture ----
  {
    auto v = neutral();
    // A(bit0) B(bit1) X(bit3) Y(bit4) -> 0x1B; Options(bit10) Menu(bit11)
    // Home(bit12) -> high byte 0x1C
    v[13] = 0x1B;
    v[14] = 0x1C;
    v[15] = 0x01; // capture / share
    xbox.set_report_data(1, v.data(), v.size());
    const auto in = xbox.get_gamepad_inputs();
    CHECK(in.buttons.a && in.buttons.b && in.buttons.x && in.buttons.y);
    CHECK(in.buttons.home && in.buttons.menu && in.buttons.options);
    CHECK(in.buttons.start && in.buttons.select); // overlapped aliases
    CHECK(in.buttons.capture);
    CHECK(!in.buttons.l1 && !in.buttons.r1 && !in.buttons.l3 && !in.buttons.r3);
    CHECK(!in.buttons.up && !in.buttons.down && !in.buttons.left && !in.buttons.right);
  }

  // ---- shoulders, trigger buttons, stick clicks ----
  {
    auto v = neutral();
    // L1(bit6) R1(bit7) -> 0xC0; L2(bit8) R2(bit9) L3(bit13) R3(bit14) -> 0x63
    v[13] = 0xC0;
    v[14] = 0x63;
    xbox.set_report_data(1, v.data(), v.size());
    const auto in = xbox.get_gamepad_inputs();
    CHECK(in.buttons.l1 && in.buttons.r1 && in.buttons.l2 && in.buttons.r2);
    CHECK(in.buttons.l3 && in.buttons.r3);
    CHECK(!in.buttons.a && !in.buttons.b && !in.buttons.x && !in.buttons.y);
  }

  // ---- sticks and analog triggers ----
  {
    auto v = neutral();
    v[0] = 0x00;
    v[1] = 0x00; // LX = 0 -> -1
    v[2] = 0xFF;
    v[3] = 0xFF; // LY = 65535 -> ~+1
    v[4] = 0x00;
    v[5] = 0x80; // RX = 32768 -> ~0
    v[6] = 0xFF;
    v[7] = 0x7F; // RY = 32767 -> 0
    v[8] = 0xFF;
    v[9] = 0x03; // LT = 1023 -> 1
    v[10] = 0x00;
    v[11] = 0x02; // RT = 512 -> ~0.5
    xbox.set_report_data(1, v.data(), v.size());
    const auto in = xbox.get_gamepad_inputs();
    CHECK(near(in.left_joystick.x, -1.0f));
    CHECK(near(in.left_joystick.y, 1.0f));
    CHECK(near(in.right_joystick.x, 0.0f));
    CHECK(near(in.right_joystick.y, 0.0f));
    CHECK(near(in.l2.value, 1.0f));
    CHECK(near(in.r2.value, 512.0f / 1023.0f));
  }

  // ---- hat switch -> d-pad buttons ----
  {
    struct HatCase {
      uint8_t hat;
      bool up, down, left, right;
    };
    const HatCase cases[] = {
        {0x0F, false, false, false, false}, // centered
        {1, true, false, false, false},     // up
        {2, true, false, false, true},      // up-right
        {3, false, false, false, true},     // right
        {4, false, true, false, true},      // down-right
        {5, false, true, false, false},     // down
        {6, false, true, true, false},      // down-left
        {7, false, false, true, false},     // left
        {8, true, false, true, false},      // up-left
    };
    for (const auto &c : cases) {
      auto v = neutral();
      v[12] = c.hat;
      xbox.set_report_data(1, v.data(), v.size());
      const auto in = xbox.get_gamepad_inputs();
      CHECK(in.buttons.up == c.up);
      CHECK(in.buttons.down == c.down);
      CHECK(in.buttons.left == c.left);
      CHECK(in.buttons.right == c.right);
    }
  }

  // ---- short report: bounded copy, the rest zero-filled (no over-read) ----
  {
    auto v = neutral();
    v.resize(8); // only the sticks arrive
    xbox.set_report_data(1, v.data(), v.size());
    const auto in = xbox.get_gamepad_inputs();
    CHECK(in.buttons.raw == 0);
    CHECK(near(in.l2.value, 0.0f) && near(in.r2.value, 0.0f));
    CHECK(near(in.left_joystick.x, 0.0f));
  }

  // ---- unknown report id is ignored, the last inputs survive ----
  {
    auto v = neutral();
    v[13] = 0x01; // A pressed
    xbox.set_report_data(1, v.data(), v.size());
    const uint8_t junk[] = {0xAA, 0xBB};
    xbox.set_report_data(0x7F, junk, sizeof(junk));
    CHECK(xbox.get_gamepad_inputs().buttons.a);
    CHECK(xbox.get_report_data(0x7F).empty());
  }

  // ---- battery report round trip (id 4) ----
  {
    const uint8_t status[] = {0x2B}; // cable + charging + level bits
    xbox.set_report_data(4, status, 1);
    const auto back = xbox.get_report_data(4);
    CHECK(back.size() == 1 && back[0] == 0x2B);
  }

  // ---- rumble report builder (id 3) ----
  {
    const auto full = xbox.make_rumble_report(255, 128);
    CHECK(full.has_value());
    if (full) {
      CHECK(full->first == 3);
      // enable = strong|weak, trigger motors off, strong = 100, weak = 50,
      // long pulse (sustain 0xFF, loop 0xEB) as the Windows driver / xpadneo do
      const std::vector<uint8_t> expected{0x03, 0, 0, 100, 50, 0xFF, 0, 0xEB};
      CHECK(full->second == expected);
    }
    const auto off = xbox.make_rumble_report(0, 0);
    CHECK(off && off->second[3] == 0 && off->second[4] == 0);
  }

  // ---- inputs -> report -> inputs round trip ----
  {
    GamepadInputs in{};
    in.buttons.a = 1;
    in.buttons.home = 1;
    in.buttons.up = 1;
    in.buttons.right = 1; // hat up-right
    in.left_joystick = {0.5f, -0.25f};
    in.right_joystick = {-0.75f, 0.1f};
    in.l2.value = 0.25f;
    in.r2.value = 1.0f;
    xbox.set_gamepad_inputs(in);
    const auto bytes = xbox.get_report_data(1);
    CHECK(bytes.size() == 16);
    Xbox other;
    other.set_report_data(1, bytes.data(), bytes.size());
    const auto out = other.get_gamepad_inputs();
    CHECK(out.buttons.a && out.buttons.home);
    CHECK(out.buttons.up && out.buttons.right && !out.buttons.down && !out.buttons.left);
    CHECK(near(out.left_joystick.x, 0.5f) && near(out.left_joystick.y, -0.25f));
    CHECK(near(out.right_joystick.x, -0.75f) && near(out.right_joystick.y, 0.1f));
    CHECK(near(out.l2.value, 0.25f) && near(out.r2.value, 1.0f));
  }

  if (g_failures == 0) {
    std::printf("ALL XBOX PARSER TESTS PASSED\n");
    return 0;
  }
  std::printf("%d FAILURE(S)\n", g_failures);
  return 1;
}

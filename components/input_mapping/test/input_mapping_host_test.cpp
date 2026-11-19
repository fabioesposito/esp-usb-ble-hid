// Host-side unit test for the input mapping (settings -> report, X-Input
// scaling, and the L+R registration window). The header is standard-library
// only (plus GamepadInputs), so this builds on a host:
/*
  c++ -std=c++20 -Wall -Wextra -Werror -I components/input_mapping/include \
      -I components/gamepad_inputs/include \
      components/input_mapping/test/input_mapping_host_test.cpp -lm -o im_test && ./im_test
*/

#include <cmath>
#include <cstdio>

#include "input_mapping.hpp"

namespace im = input_mapping;

static int g_failures = 0;
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);                                      \
      ++g_failures;                                                                                \
    }                                                                                              \
  } while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

int main() {
  // ---- radial deadzone ----
  {
    GamepadInputs::Joystick j{0.1f, -0.2f};
    im::apply_deadzone(j, 0.0f); // disabled: untouched
    CHECK(near(j.x, 0.1f) && near(j.y, -0.2f));
    j = {0.1f, 0.1f}; // |(0.1, 0.1)| ~ 0.141 < 0.2: inside, zeroed
    im::apply_deadzone(j, 0.2f);
    CHECK(j.x == 0.0f && j.y == 0.0f);
    j = {0.75f, 0.0f}; // outside: remaining travel rescaled, (0.75-0.5)/0.5 = 0.5
    im::apply_deadzone(j, 0.5f);
    CHECK(near(j.x, 0.5f) && near(j.y, 0.0f));
    j = {1.0f, 0.0f}; // full deflection still reaches 1.0
    im::apply_deadzone(j, 0.5f);
    CHECK(near(j.x, 1.0f));
    j = {0.6f, 0.8f}; // |(0.6, 0.8)| = 1: unchanged, direction preserved
    im::apply_deadzone(j, 0.25f);
    CHECK(near(j.x, 0.6f) && near(j.y, 0.8f));
    j = {0.3f, 0.4f}; // mag 0.5, dz 0.25: new mag (0.5-0.25)/0.75 = 1/3
    im::apply_deadzone(j, 0.25f);
    CHECK(near(j.x, 0.2f) && near(j.y, 0.4f / 1.5f));
    j = {0.0f, 0.0f}; // center stays center (no division by zero)
    im::apply_deadzone(j, 0.5f);
    CHECK(j.x == 0.0f && j.y == 0.0f);
  }

  // ---- settings applied to a report ----
  {
    GamepadInputs in{};
    in.left_joystick = {0.5f, 0.25f};
    in.right_joystick = {-0.5f, -0.25f};
    in.buttons.a = 1;
    in.buttons.x = 1;
    im::apply(in, im::Settings{}); // defaults: both Y inverted, no swaps
    CHECK(near(in.left_joystick.x, 0.5f) && near(in.left_joystick.y, -0.25f));
    CHECK(near(in.right_joystick.x, -0.5f) && near(in.right_joystick.y, 0.25f));
    CHECK(in.buttons.a == 1 && in.buttons.b == 0 && in.buttons.x == 1 && in.buttons.y == 0);

    im::Settings s;
    s.invert_left_y = false;
    s.invert_right_y = false;
    s.swap_ab = true;
    s.swap_xy = true;
    in = GamepadInputs{};
    in.left_joystick = {0.0f, 0.25f};
    in.buttons.a = 1;
    in.buttons.x = 1;
    im::apply(in, s);
    CHECK(near(in.left_joystick.y, 0.25f)); // inversion off
    CHECK(in.buttons.a == 0 && in.buttons.b == 1);
    CHECK(in.buttons.x == 0 && in.buttons.y == 1);

    s = im::Settings{};
    s.deadzone_percent = 25;
    in = GamepadInputs{};
    in.left_joystick = {0.1f, 0.1f};  // inside the 25 % deadzone
    in.right_joystick = {1.0f, 0.0f}; // full deflection survives
    im::apply(in, s);
    CHECK(in.left_joystick.x == 0.0f && in.left_joystick.y == 0.0f);
    CHECK(near(in.right_joystick.x, 1.0f));
  }

  // ---- X-Input scaling ----
  {
    CHECK(im::axis(0.0f) == 0);
    CHECK(im::axis(1.0f) == 32767);
    CHECK(im::axis(-1.0f) == -32767);
    CHECK(im::axis(2.0f) == 32767); // clamped
    CHECK(im::axis(-2.0f) == -32767);
    CHECK(im::axis(0.5f) == 16383); // 0.5 * 32767 truncates
    CHECK(im::trigger(0.0f) == 0);
    CHECK(im::trigger(1.0f) == 255);
    CHECK(im::trigger(-0.5f) == 0); // clamped
    CHECK(im::trigger(2.0f) == 255);
    CHECK(im::trigger(0.5f) == 127); // 0.5 * 255 truncates
  }

  // ---- GamepadInputs button layout (the union the mapping writes through) ----
  {
    GamepadInputs in{};
    in.set_button(0, true); // a
    CHECK(in.buttons.a == 1 && in.buttons.raw == 1u);
    in.set_button(14, true); // home
    CHECK(in.buttons.home == 1);
    in.set_button(16, true); // start
    CHECK(in.buttons.start == 1 && in.buttons.menu == 1);
    in.set_button(0, false);
    CHECK(in.buttons.a == 0 && in.buttons.home == 1 && in.buttons.start == 1);
  }

  // ---- registration window ----
  {
    im::RegistrationWindow w;
    constexpr int64_t W = im::RegistrationWindow::kWindowUs;
    CHECK(!w.holds_lr(0));
    CHECK(!w.tick(0, false)); // not ready: nothing
    CHECK(!w.holds_lr(0));
    CHECK(w.tick(1000, true)); // ready rising edge: open, re-push with L+R
    CHECK(w.holds_lr(1000));
    CHECK(w.holds_lr(1000 + W - 1));
    CHECK(!w.tick(1000 + W / 2, true)); // still open: no re-push
    CHECK(w.holds_lr(1000 + W / 2));
    CHECK(w.tick(1000 + W, true)); // expired: re-push to release L+R
    CHECK(!w.holds_lr(1000 + W));
    CHECK(!w.tick(1000 + W + 1000, true)); // steady state: nothing
    CHECK(!w.tick(2000 + W, false));       // link dropped: window cleared
    CHECK(!w.holds_lr(2000 + W));
    CHECK(w.tick(3000, true)); // a new enumeration opens a new window
    CHECK(w.holds_lr(3000));
    // a drop inside the window cancels it without a re-push
    CHECK(!w.tick(3000 + 10, false));
    CHECK(!w.holds_lr(3000 + 10));
    CHECK(w.tick(3000 + 20, true));
    CHECK(w.holds_lr(3000 + 20));
  }

  if (g_failures == 0) {
    std::printf("ALL INPUT MAPPING TESTS PASSED\n");
    return 0;
  }
  std::printf("%d FAILURE(S)\n", g_failures);
  return 1;
}

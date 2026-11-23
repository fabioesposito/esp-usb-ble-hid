// Host-side unit test for the output-stage helpers (X-Input scaling and the
// L+R registration window). The header is standard-library only (plus
// GamepadInputs), so this builds on a host:
/*
  c++ -std=c++20 -Wall -Wextra -Werror -I components/input_mapping/include \
      -I components/gamepad_inputs/include \
      components/input_mapping/test/input_mapping_host_test.cpp -lm -o im_test && ./im_test
*/

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

int main() {
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

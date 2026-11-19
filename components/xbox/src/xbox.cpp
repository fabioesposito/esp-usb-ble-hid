#include "xbox.hpp"

const DeviceInfo Xbox::device_info = {.vid = Xbox::vid,
                                      .pid = Xbox::pid,
                                      .bcd = Xbox::bcd,
                                      .usb_bcd = Xbox::usb_bcd,
                                      .manufacturer_name = Xbox::manufacturer,
                                      .product_name = Xbox::product,
                                      .serial_number = Xbox::serial};

void Xbox::set_report_data(uint8_t report_id, const uint8_t *data, size_t len) {
  std::vector<uint8_t> data_vec(data, data + len);
  switch (report_id) {
  case input_report.ID:
    input_report.set_data(data_vec);
    break;
  case rumble_report.ID:
    rumble_report.set_data(data_vec);
    break;
  case battery_report.ID:
    battery_report.set_data(data_vec);
    break;
  default:
    logger_.warn("Unknown report id: {}", report_id);
    break;
  }
}

std::vector<uint8_t> Xbox::get_report_data(uint8_t report_id) const {
  switch (report_id) {
  case input_report.ID:
    return input_report.get_report();
  case rumble_report.ID:
    return rumble_report.get_report();
  case battery_report.ID:
    return battery_report.get_report();
  default:
    return {};
  }
}

// Gamepad inputs
GamepadInputs Xbox::get_gamepad_inputs() const {
  GamepadInputs inputs{};
  input_report.get_buttons(inputs.buttons);

  bool up, down, left, right;
  input_report.get_hat(up, down, left, right);
  inputs.buttons.up = up;
  inputs.buttons.down = down;
  inputs.buttons.left = left;
  inputs.buttons.right = right;

  input_report.get_left_joystick(inputs.left_joystick.x, inputs.left_joystick.y);
  input_report.get_right_joystick(inputs.right_joystick.x, inputs.right_joystick.y);
  input_report.get_brake(inputs.l2.value);
  input_report.get_accelerator(inputs.r2.value);

  return inputs;
}

void Xbox::set_gamepad_inputs(const GamepadInputs &inputs) {
  input_report.reset();

  input_report.set_buttons(inputs.buttons);

  input_report.set_hat(inputs.buttons.up, inputs.buttons.down, inputs.buttons.left,
                       inputs.buttons.right);

  input_report.set_left_joystick(inputs.left_joystick.x, inputs.left_joystick.y);
  input_report.set_right_joystick(inputs.right_joystick.x, inputs.right_joystick.y);
  input_report.set_brake(inputs.l2.value);
  input_report.set_accelerator(inputs.r2.value);
}

// Battery: the status byte's bits 0-1 carry the capacity in quarters (0 low
// .. 3 full -- the mapping Linux's xpadneo driver reads); the other bits are
// cable / charging / error flags. 0 until a battery report has arrived.
uint8_t Xbox::get_battery_level() const {
  const auto data = battery_report.get_report();
  if (data.empty())
    return 0;
  static constexpr uint8_t kQuarterPercent[] = {0, 33, 66, 100};
  return kQuarterPercent[data[0] & 0x03];
}

// HID handlers
std::optional<GamepadDevice::ReportData> Xbox::on_attach() { return {}; }

std::optional<GamepadDevice::ReportData> Xbox::on_hid_report(uint8_t, const uint8_t *, size_t) {
  return {};
}

// Rumble over BLE: output report id 3, payload
//   [enable nibble][left trigger][right trigger][strong][weak]
//   [sustain 10ms][release 10ms][loop count]
// (the layout Linux's xpadneo driver uses). Enable bits: 0x01 weak (right main
// motor), 0x02 strong (left main motor), 0x04 / 0x08 the trigger motors. The
// effect is a long pulse reprogrammed by every host update, like the Windows
// driver does; a zero-level update stops it.
std::optional<GamepadDevice::ReportData> Xbox::make_rumble_report(uint8_t left, uint8_t right) {
  RumbleReport report;
  report.set_enabled(0x03);                                         // strong + weak main motors
  report.set_magnitude(2, static_cast<uint8_t>(left * 100 / 255));  // strong = left
  report.set_magnitude(3, static_cast<uint8_t>(right * 100 / 255)); // weak = right
  report.set_duration(0xFF); // 2.55 s sustain, reprogrammed by each update
  report.set_start_delay(0);
  report.set_loop_count(0xEB);
  auto bytes = report.get_report();
  bytes[0] &= 0x0F; // the enable byte's high nibble is reserved (and uninitialized)
  return ReportData{RumbleReport::ID, std::move(bytes)};
}

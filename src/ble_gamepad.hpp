#pragma once
#include <cstdint>
namespace gamepad {struct Receiver;}

// HID profile adapter. No device lists, selection policy or persistent settings.
namespace ble_gamepad {
enum class Event {Ready,Failed,RemoteClose,Activity};
using Notify=void (*)(Event,uint8_t);
void init(Notify notify);
uint8_t connect(uint16_t handle);
void reset();
void trace(bool enabled);
uint8_t service();
uint8_t report();
gamepad::Receiver &input();
}

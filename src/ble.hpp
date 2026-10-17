#pragma once
#include <cstdint>

// All functions and BTstack callbacks run on core0, using a polling async context.
namespace ble {
void init();
void poll();
void shutdown();
bool command(const char *line);
uint8_t buttons();
bool home();
}

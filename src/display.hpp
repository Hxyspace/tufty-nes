#pragma once
#include <cstdint>
namespace display {
constexpr int width=320, height=240;
extern uint16_t *menu_buffers[2];
extern uint16_t palette[64];
void early_init();
void start();
void present_menu(unsigned index);
void present_game(const uint8_t *screen);
void pause(bool enabled);
bool paused();
void brightness(uint8_t percent);
void prepare_reboot();
}

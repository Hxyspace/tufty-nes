#pragma once
#include "protocol.hpp"
#include <cstddef>
#include <cstdint>

namespace hidpad {
// Physical XTARK input report, without the GATT Report Reference's Report ID.
constexpr size_t ReportBytes=10;
// Application control message carried by report 4, never a gamepad state.
constexpr uint8_t DisconnectReport[ReportBytes]={0x54,0x47,0x50,0x01,0x42,0x59,0x45,0,0,0xa5};
bool disconnect_report(const uint8_t *data,size_t bytes);
bool decode(const uint8_t *data,size_t bytes,uint8_t &buttons);
bool address(const char *text,uint8_t out[6]);
bool uuid16(const char *text,uint16_t &out);
struct Receiver {
    uint8_t buttons=0;
    bool home=false;
    uint32_t received=0,rejected=0;
    bool receive(const uint8_t *data,size_t bytes,uint32_t now=0);
    bool poll(uint32_t now); // Returns true once when the menu combo fires.
    void release();
private:
    int8_t stick_x=0,stick_y=0;
    bool combo_holding=false,combo_latched=false;
    uint32_t combo_at=0;
};
}

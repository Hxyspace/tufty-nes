#pragma once
#include "protocol.hpp"
#include <cstddef>
#include <cstdint>

namespace hidpad {
// XTARK/Android input report, without the GATT Report Reference's Report ID.
bool decode(const uint8_t *data,size_t bytes,uint8_t &buttons);
bool address(const char *text,uint8_t out[6]);
bool uuid16(const char *text,uint16_t &out);
struct Receiver {
    uint8_t buttons=0;
    bool home=false;
    uint32_t received=0,rejected=0;
    bool receive(const uint8_t *data,size_t bytes) {
        if(!decode(data,bytes,buttons)) {release(); ++rejected; return false;}
        home=(data[6]&0x10)!=0;
        ++received; return true;
    }
    void release() {buttons=0; home=false;}
};
}

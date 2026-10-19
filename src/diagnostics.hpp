#pragma once
#include <cstdint>
#include "frame_rate.hpp"
namespace diagnostics {
// Core0 only. Display-thread counters are exposed through display::stats().
inline bool playing=false,working=false;
inline std::uint32_t frames=0,work_last_us=0,work_max_us=0,service_gap_max_us=0;
inline std::uint32_t publish_last_us=0,publish_max_us=0;
inline std::uint32_t service_work_us=0,service_last_us=0,usb_max_us=0,ble_max_us=0;
inline FrameRate rate;
inline void reset() {
    frames=work_last_us=work_max_us=service_gap_max_us=0;
    publish_last_us=publish_max_us=0;
    service_work_us=service_last_us=usb_max_us=ble_max_us=0;
    working=false; rate={};
}
}

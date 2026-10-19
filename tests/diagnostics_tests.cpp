#include "diagnostics.hpp"
#include <cassert>

int main() {
    diagnostics::FrameRate rate;
    auto sample=rate.sample();
    assert(sample.frames==0 && sample.elapsed_us==0 && sample.fps_x100==0);
    std::uint32_t at=0xfffff000;
    rate.frame(at);
    assert(rate.sample().frames==0); // First frame establishes the time origin.
    for(unsigned n=0;n<600;++n) {at+=16667; rate.frame(at);}
    sample=rate.sample();
    assert(sample.frames==600 && sample.elapsed_us==600*16667 && sample.fps_x100==5999);
    sample=rate.sample();
    assert(sample.frames==0 && sample.elapsed_us==0 && sample.fps_x100==0);
    at+=33333; rate.frame(at);
    sample=rate.sample();
    assert(sample.frames==1 && sample.elapsed_us==33333 && sample.fps_x100==3000);
    // Accumulated windows may exceed a full 32-bit timer period.
    for(unsigned n=0;n<100000;++n) {at+=50000; rate.frame(at);}
    sample=rate.sample();
    assert(sample.frames==100000 && sample.elapsed_us==5000000000ull && sample.fps_x100==2000);

    diagnostics::frames=5; diagnostics::work_last_us=10; diagnostics::work_max_us=20;
    diagnostics::service_gap_max_us=30; diagnostics::publish_last_us=40;
    diagnostics::publish_max_us=50; diagnostics::service_work_us=60;
    diagnostics::service_last_us=70; diagnostics::usb_max_us=80; diagnostics::ble_max_us=90;
    diagnostics::working=true; diagnostics::rate.frame(100); diagnostics::rate.frame(200);
    diagnostics::reset();
    assert(!diagnostics::working && !diagnostics::frames && !diagnostics::work_last_us &&
        !diagnostics::work_max_us && !diagnostics::service_gap_max_us && !diagnostics::publish_last_us &&
        !diagnostics::publish_max_us && !diagnostics::service_work_us && !diagnostics::service_last_us &&
        !diagnostics::usb_max_us && !diagnostics::ble_max_us);
    diagnostics::rate.frame(300);
    assert(diagnostics::rate.sample().frames==0); // A new game has a new origin.
}

#pragma once
#include <cstdint>
namespace display {
// The LCD streams columns. Keep each column contiguous in PSRAM instead of
// fetching one byte from a different row for every transmitted pixel.
struct GameFrame {
    static constexpr unsigned width=256,height=240;
    std::uint8_t colors[width][height];
    void line(unsigned y,const std::uint16_t *pixels) {
        for(unsigned x=0;x<width;++x) colors[x][y]=pixels[x]&63;
    }
};
static_assert(sizeof(GameFrame)==256*240);
}

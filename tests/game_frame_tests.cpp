#include "game_frame.hpp"
#include <cassert>
#include <cstring>

int main() {
    display::GameFrame frame{},published;
    std::uint8_t original[240][256];
    std::uint16_t line[256];
    for(unsigned pass=0;pass<2;++pass) {
        for(unsigned y=0;y<240;++y) {
            for(unsigned x=0;x<256;++x) {
                line[x]=0x8000|((x*7+y*11+pass*13)&63);
                original[y][x]=line[x]&63;
            }
            frame.line(y,line);
        }
        std::memcpy(&published,&frame,sizeof frame);
        // The DMA conversion must see the same pixels as the old row-major
        // framebuffer, including first/last lines and transparency flag removal.
        for(unsigned x=0;x<256;++x) {
            const auto *column=published.colors[x];
            for(unsigned y=0;y<240;++y) assert(column[y]==original[y][x]);
        }
        // Building the next scanline must not change an already published frame.
        std::memset(line,0xff,sizeof line);
        frame.line(239,line);
        for(unsigned x=0;x<256;++x) assert(published.colors[x][239]==original[239][x]);
    }
}

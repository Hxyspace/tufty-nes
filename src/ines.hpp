#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ines {
struct Image {uint32_t prg=0,chr=0,offset=0; uint16_t mapper=0;};
inline bool inspect(const uint8_t *p,size_t bytes,Image &out) {
    out={};
    if(!p || bytes<16 || std::memcmp(p,"NES\x1a",4) || !p[4] ||
       (p[7]&0x0c)==0x08 || (p[7]&3)) return false; // iNES 1.0, standard NES only.
    out.offset=16+((p[6]&4)?512:0);
    out.prg=uint32_t(p[4])*16384; out.chr=uint32_t(p[5])*8192;
    out.mapper=(p[6]>>4)|(p[7]&0xf0);
    return bytes>=out.offset && out.prg<=bytes-out.offset && out.chr<=bytes-out.offset-out.prg;
}
}

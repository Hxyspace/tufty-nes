#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ines {
struct Image {
    uint32_t prg=0,chr=0,offset=0;
    uint16_t mapper=0;
    uint8_t header[16]={}; // Validated header normalized for the iNES-only core.
};
inline uint32_t ram_bytes(uint8_t shift) {return shift?64u<<shift:0;}
inline bool inspect(const uint8_t *p,size_t bytes,Image &out,const char **error=nullptr) {
    out={};
    if(error) *error=nullptr;
    auto reject=[&](const char *reason) {if(error) *error=reason; return false;};
    if(!p || bytes<16 || std::memcmp(p,"NES\x1a",4)) return reject("Invalid/truncated NES header");
    if(p[7]&3) return reject("Only standard NES console is supported");
    const bool nes2=(p[7]&0x0c)==0x08;
    if(nes2) {
        // Keep the existing core and mapper ABI: byte-sized ROM bank counts,
        // eight-bit mapper IDs, standard controller, NTSC and at most 8 KiB RAM.
        if(p[8]&0xf0) return reject("NES 2.0 submapper is unsupported");
        if(p[8]&0x0f) return reject("NES 2.0 extended mapper is unsupported");
        if(p[9]) return reject("NES 2.0 extended ROM sizes are unsupported");
        if(ram_bytes(p[10]&15)+ram_bytes(p[10]>>4)>8192 ||
           ram_bytes(p[11]&15)+ram_bytes(p[11]>>4)>8192 || (p[5] && p[11]))
            return reject("NES 2.0 RAM layout is unsupported");
        if(p[12]) return reject("NES 2.0 requires NTSC timing");
        if(p[13] || p[14] || p[15]>1) return reject("NES 2.0 extra hardware/data is unsupported");
    }
    if(!p[4]) return reject("NES ROM has no PRG data");
    out.offset=16+((p[6]&4)?512:0);
    out.prg=uint32_t(p[4])*16384; out.chr=uint32_t(p[5])*8192;
    out.mapper=(p[6]>>4)|(p[7]&0xf0);
    if(bytes<out.offset || out.prg>bytes-out.offset || out.chr>bytes-out.offset-out.prg)
        return reject("Truncated NES PRG/CHR data");
    std::memcpy(out.header,p,8);
    if(nes2) out.header[7]&=0xf0;
    return true;
}
}

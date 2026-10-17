#include "protocol.hpp"
#include "ines.hpp"
#include <cassert>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
static std::string output;
static bool send(const char *s,size_t n) {output.append(s,n); return true;}
static void line(tgp::Receiver &r,const std::string &s,uint32_t now) {for(unsigned char c:s) r.byte(c,now);}
int main() {
    tgp::Receiver r; r.send=send;
    line(r,"#TGP:5447500201000001\n",0); assert(!r.active && !r.buttons);
    line(r,"#TGP:HELLO:0002:0123456789abcdef\n",1); assert(!r.active);
    line(r,"#TGP:HELLO:0001:0123456789abcdef\r\n",2);
    assert(r.active && output=="#TGP:READY:0123456789abcdef\n");
    line(r,"#TGP:544750010100001f\n",10);
    assert(r.buttons==(tgp::Up|tgp::Down|tgp::Left|tgp::Right|tgp::A));
    line(r,"#TGP:54475002010001ff\n",20); assert(r.buttons==255);
    line(r,"#TGP:5447500201000100\n",21); assert(r.buttons==255); // Duplicate.
    r.poll(369); assert(r.buttons==255);
    r.poll(370); assert(!r.buttons && r.active); // Input release != session close.
    r.poll(520); assert(output.find("#TGP:ALIVE:")!=std::string::npos);
    r.poll(1220); assert(!r.active && output.find("#TGP:BYE:")!=std::string::npos);
    line(r,"#TGP:HELLO:0001:0123456789abcdef\n",1300);
    line(r,std::string(150,'x')+"#TGP:54475002010002ff\n",1310); assert(!r.buttons);
    line(r,"#TGP:54475002010002aa\n",1320); assert(r.buttons==0xaa);
    line(r,"#TGP:MENU:fedcba9876543210\n",1330); assert(!r.menu);
    line(r,"#TGP:MENU:0123456789abcdef\n",1340); assert(r.menu && !r.buttons);
    line(r,"#TGP:END:0123456789abcdef\n",1350); assert(!r.active && !r.menu);
    // Sequence wrap, malformed bits, wrong version, controller ID.
    uint8_t p[8]={'T','G','P',1,1,0,255,31},seq=0,keys=0;
    assert(tgp::decode(p,8,1,seq,keys) && seq==255);
    p[7]=32; assert(!tgp::decode(p,8,1,seq,keys));
    p[3]=2; assert(tgp::decode(p,8,1,seq,keys));
    assert(!tgp::decode(p,8,2,seq,keys));
    p[3]=3; assert(!tgp::decode(p,8,1,seq,keys));
    for(unsigned i=0;i<8;++i) assert(!tgp::decode(p,i,1,seq,keys));
    std::vector<uint8_t> rom(16+16384+8192); std::memcpy(rom.data(),"NES\x1a",4);
    rom[4]=1; rom[5]=1;
    ines::Image image;
    assert(ines::inspect(rom.data(),rom.size(),image) && image.offset==16 && image.chr==8192);
    for(size_t n=0;n<rom.size();++n) assert(!ines::inspect(rom.data(),n,image));
    rom[5]=0; assert(ines::inspect(rom.data(),rom.size(),image) && !image.chr);
    rom[6]=4; assert(!ines::inspect(rom.data(),16+16384,image));
    assert(ines::inspect(rom.data(),rom.size(),image) && image.offset==528);
    rom[7]=8; rom[15]=1;
    assert(ines::inspect(rom.data(),rom.size(),image) && image.offset==528);
    assert(image.header[7]==0 && image.header[15]==0 && rom[7]==8 && rom[15]==1);
    // NES2 metadata must never be silently reinterpreted as a legacy ROM.
    const std::pair<unsigned,uint8_t> unsupported[]={
        {8,0x20},{8,1},{9,1},{9,0x0f},{9,0xf0}, // submapper, mapper, sizes/exponent
        {10,8},{10,0x80},{10,0x77},{11,8},{11,0x80},{11,0x77},
        {12,1},{12,2},{12,3},{13,1},{14,1},{15,2},{15,0x40},{7,9},{7,10},{7,11}
    };
    for(auto field:unsupported) {
        auto invalid=rom; invalid[field.first]=field.second;
        const char *error=nullptr;
        assert(!ines::inspect(invalid.data(),invalid.size(),image,&error) && error);
    }
    const char *error=nullptr;
    assert(!ines::inspect(nullptr,0,image,&error) && error);
    rom[6]=0x57; rom[7]=0x48; rom[5]=1; // Mapper 69, trainer/battery/mirroring.
    rom.resize(528+16384+8192);
    assert(ines::inspect(rom.data(),rom.size(),image,&error) && !error);
    assert(image.mapper==69 && image.prg==16384 && image.chr==8192 && image.offset==528);
    assert(image.header[6]==0x57 && image.header[7]==0x40);
    for(unsigned i=8;i<16;++i) assert(image.header[i]==0);
    for(size_t n=0;n<rom.size();++n) assert(!ines::inspect(rom.data(),n,image));
    rom[11]=7; assert(!ines::inspect(rom.data(),rom.size(),image)); // Mixed CHR ROM/RAM.
    rom[5]=0;
    assert(ines::inspect(rom.data(),rom.size(),image)); // Standard 8 KiB CHR-RAM.
    rom[10]=7; assert(ines::inspect(rom.data(),rom.size(),image));
    rom[10]=0x70; assert(ines::inspect(rom.data(),rom.size(),image));
    rom[4]=0; assert(!ines::inspect(rom.data(),rom.size(),image));
}

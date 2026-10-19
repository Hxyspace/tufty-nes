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
static constexpr uint8_t neutral[]={128,128,128,128,255,0,0,0,0,0};
static const char *token="0123456789abcdef";
static std::string frame(const uint8_t *report,uint8_t sequence,uint16_t id=1,uint8_t version=3) {
    const uint8_t prefix[]={'T','G','P',version,uint8_t(id),uint8_t(id>>8),sequence};
    constexpr char hex[]="0123456789abcdef";
    std::string wire="#TGP:";
    for(uint8_t byte:prefix) {wire+=hex[byte>>4]; wire+=hex[byte&15];}
    for(size_t i=0;i<gamepad::ReportBytes;++i) {wire+=hex[report[i]>>4]; wire+=hex[report[i]&15];}
    return wire+'\n';
}
static void hello(tgp::Receiver &r,uint32_t now=0) {
    line(r,std::string("#TGP:HELLO:0001:")+token+":3\r\n",now);
    assert(r.active);
}
static void serial_tests() {
    tgp::Receiver r; r.send=send;
    line(r,frame(neutral,0),0); assert(!r.active && !r.input.nes());
    // Only v3 handshakes with the configured nonzero ID are accepted.
    for(const char *invalid:{"#TGP:HELLO:0002:0123456789abcdef:3\n",
        "#TGP:HELLO:0001:0123456789abcdef\n","#TGP:HELLO:0001:0123456789abcdef:2\n",
        "#TGP:HELLO:0001:0123456789abcdeF:3\n","#TGP:HELLO:0001:0123456789abcdeg:3\n"}) {
        line(r,invalid,1); assert(!r.active);
    }
    hello(r,2); assert(output=="#TGP:READY:0123456789abcdef:3\n");
    uint8_t report[10]; std::memcpy(report,neutral,10);
    report[4]=2; report[5]=1;
    assert(frame(report,0)=="#TGP:5447500301000080808080020100000000\n");
    line(r,frame(report,0),10); assert(r.input.nes()==(gamepad::Right|gamepad::A));
    line(r,frame(neutral,0),20); assert(r.input.nes()==(gamepad::Right|gamepad::A)); // Duplicate.
    line(r,frame(neutral,1,2),20); assert(r.received==1); // Wrong ID.
    for(uint8_t version:{1,2,4}) line(r,frame(neutral,1,1,version),20);
    line(r,"#TGP:54475002010001ff\n",20); // Old eight-byte format.
    line(r,"#TGP:MENU:0123456789abcdef\n",20);
    line(r,"#TGP:END:0123456789abcdef\n",20);
    assert(r.received==1 && r.active && !r.input.home);
    hello(r,100); // Repeated HELLO must not reset input age or sequence tracking.
    line(r,frame(neutral,0),110); assert(r.received==1);
    r.poll(359); assert(r.input.nes());
    r.poll(360); assert(!r.input.nes() && !r.input.home && r.active);
    r.poll(502); assert(output.find("#TGP:ALIVE:0123456789abcdef\n")!=std::string::npos);
    r.poll(1210); assert(!r.active && output.find("#TGP:BYE:0123456789abcdef\n")!=std::string::npos);

    hello(r,1300);
    line(r,std::string(150,'x')+frame(report,1),1310); assert(!r.input.nes());
    line(r,frame(report,255),1320); assert(r.input.nes());
    line(r,frame(neutral,0),1330); assert(!r.input.nes()); // Sequence wraps.
    report[6]=16; line(r,frame(report,1),1340); assert(r.input.home);
    line(r,frame(neutral,2),1350); assert(!r.input.home);
    report[6]=0; line(r,frame(report,3),1360);
    // Garbage, bad hex, and partial frames cannot refresh the watchdog.
    std::string invalid=frame(neutral,4); invalid[20]='z'; line(r,invalid,1370);
    invalid=frame(neutral,4); invalid.erase(20,2); line(r,invalid,1380);
    r.poll(1710); assert(!r.input.nes() && r.active);
    line(r,frame(report,4),1720); assert(r.input.nes());
    line(r,frame(gamepad::DisconnectReport,5),1730);
    assert(!r.active && !r.input.nes() && !r.input.home);
    line(r,frame(report,6),1740); assert(!r.active && !r.input.nes());

    // Shared decoding: axes, D-pad precedence, and Home agree across transports.
    hello(r,2000); gamepad::Receiver ble;
    uint8_t samples[][10]={
        {80,128,128,128,255,1,0,0,0,0}, {90,128,128,128,255,0,0,0,0,0},
        {96,128,128,128,255,0,0,0,0,0}, {0,0,128,128,255,0,0,0,0,0},
        {0,0,128,128,4,0,0,0,0,0}, {128,128,128,128,255,0,16,0,0,0}};
    uint8_t seq=0;
    for(auto &sample:samples) {
        assert(ble.receive(sample,10,2000)); line(r,frame(sample,seq++),2000);
        assert(r.input.nes()==ble.nes() && r.input.home==ble.home);
    }
    r.poll(2350); assert(!r.input.home && ble.home); // Serial timeout is isolated.
    r.reset(); assert(ble.home);

    // Repeated serial heartbeats must preserve the shared L3+R3 hold timer.
    hello(r,3000); std::memcpy(report,neutral,10); report[6]=0x60; seq=0;
    for(uint32_t at=3000;at<=4440;at+=120) {
        line(r,frame(report,seq++),at); assert(!r.poll(at) && !r.input.home);
    }
    assert(!r.poll(4499)); assert(r.poll(4500) && r.input.home);
    assert(!r.poll(4510)); // A latched gesture only fires once.
    line(r,frame(neutral,seq++),4520); assert(!r.input.home);
    line(r,frame(report,seq++),4600); r.poll(4950); assert(!r.input.home);
    line(r,frame(report,seq++),5000); r.poll(6000); assert(!r.input.home); // Stale reports cannot finish a gesture.
    r.reset(); assert(!r.input.home && !r.input.nes());

    // A new nonce replaces held input and all gesture/hysteresis state.
    hello(r,7000); report[6]=16; line(r,frame(report,0),7000); assert(r.input.home);
    line(r,"#TGP:HELLO:0001:fedcba9876543210:3\n",7010);
    assert(!r.input.home && !r.input.nes() && !r.seen);
    line(r,frame(neutral,0),7020); assert(r.received);
    // Unsigned time arithmetic also covers the millisecond counter wrapping.
    r.reset(); hello(r,0xffffff00u); report[6]=16; line(r,frame(report,0),0xffffff00u);
    r.poll(uint32_t(0xffffff00u+349)); assert(r.input.home);
    r.poll(uint32_t(0xffffff00u+350)); assert(!r.input.home && r.active);
    r.poll(uint32_t(0xffffff00u+1200)); assert(!r.active);
}
int main() {
    serial_tests();
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

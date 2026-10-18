#include "hid_gamepad.hpp"
#include <cstring>

namespace hidpad {
namespace {
int hex(char c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='A' && c<='F') return c-'A'+10;
    if(c>='a' && c<='f') return c-'a'+10;
    return -1;
}
int8_t axis(uint8_t value,int8_t previous) {
    // Engage 48 counts from center; release 32 counts from center. Hysteresis
    // prevents slight movement at the threshold from repeatedly toggling keys.
    if(value<=80) return -1;
    if(value>=176) return 1;
    if(previous<0 && value<96) return -1;
    if(previous>0 && value>160) return 1;
    return 0;
}
bool centered(uint8_t value) {return value>=96 && value<=160;}
}
bool disconnect_report(const uint8_t *data,size_t bytes) {
    return data && bytes==ReportBytes && !std::memcmp(data,DisconnectReport,ReportBytes);
}
bool decode(const uint8_t *data,size_t bytes,uint8_t &buttons) {
    if(!data || bytes!=ReportBytes || disconnect_report(data,bytes)) return false;
    // Hat is a four-bit absolute field with Null State; upper bits are padding.
    // Values outside its logical range 0..7 indicate no direction.
    uint8_t hat=data[4]&0x0f;
    constexpr uint8_t hats[]={tgp::Up,tgp::Up|tgp::Right,tgp::Right,tgp::Right|tgp::Down,
        tgp::Down,tgp::Down|tgp::Left,tgp::Left,tgp::Left|tgp::Up};
    buttons=(data[5]&3)|(data[6]&12)|(hat<8?hats[hat]:0);
    return true;
}
bool Receiver::receive(const uint8_t *data,size_t bytes,uint32_t now) {
    if(!decode(data,bytes,buttons)) {release(); ++rejected; return false;}
    stick_x=axis(data[0],stick_x); stick_y=axis(data[1],stick_y);
    if((data[4]&15)>=8) { // A pressed D-pad overrides the whole stick direction.
        if(stick_x<0) buttons|=tgp::Left;
        if(stick_x>0) buttons|=tgp::Right;
        if(stick_y<0) buttons|=tgp::Up;
        if(stick_y>0) buttons|=tgp::Down;
    }
    if(!(data[6]&0x60)) combo_latched=false; // Release both clicks to re-arm.
    bool candidate=data[5]==0 && data[6]==0x60 && (data[4]&15)>=8 &&
        centered(data[0]) && centered(data[1]) && centered(data[2]) && centered(data[3]) &&
        data[7]==0 && data[8]==0;
    if(candidate && !combo_holding) combo_at=now;
    combo_holding=candidate;
    home=(data[6]&0x10)!=0 || combo_latched;
    if(combo_latched) buttons=0;
    ++received; return true;
}
bool Receiver::poll(uint32_t now) {
    if(!combo_holding || combo_latched || uint32_t(now-combo_at)<1500) return false;
    combo_latched=true; home=true; buttons=0;
    return true;
}
void Receiver::release() {
    buttons=0; home=false;
    stick_x=stick_y=0;
    combo_holding=combo_latched=false; combo_at=0;
}
bool address(const char *text,uint8_t out[6]) {
    if(!text || std::strlen(text)!=17) return false;
    for(unsigned i=0;i<6;++i) {
        int a=hex(text[i*3]),b=hex(text[i*3+1]);
        if(a<0 || b<0 || (i<5 && text[i*3+2]!=':')) return false;
        out[i]=uint8_t(a*16+b);
    }
    return true;
}
bool uuid16(const char *text,uint16_t &out) {
    if(!text || std::strlen(text)!=6 || text[0]!='0' || (text[1]!='x' && text[1]!='X')) return false;
    unsigned value=0;
    for(unsigned i=2;i<6;++i) {
        int n=hex(text[i]);
        if(n<0) return false;
        value=(value<<4)|unsigned(n);
    }
    if(!value) return false;
    out=uint16_t(value); return true;
}
}

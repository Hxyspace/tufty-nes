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
}
bool decode(const uint8_t *data,size_t bytes,uint8_t &buttons) {
    if(!data || bytes!=9 || (data[4]>7 && data[4]!=0xff)) return false;
    constexpr uint8_t hats[]={tgp::Up,tgp::Up|tgp::Right,tgp::Right,tgp::Right|tgp::Down,
        tgp::Down,tgp::Down|tgp::Left,tgp::Left,tgp::Left|tgp::Up};
    buttons=(data[5]&3)|(data[6]&12)|(data[4]<8?hats[data[4]]:0);
    return true;
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

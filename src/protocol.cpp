#include "protocol.hpp"
#include <cstdio>
#include <cstring>
namespace tgp {
static int hex(char c) {
    if(c >= '0' && c <= '9') return c-'0';
    if(c >= 'a' && c <= 'f') return c-'a'+10;
    if(c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
}
bool decode(const uint8_t *p, size_t n, uint16_t id, uint8_t &seq, uint8_t &keys) {
    if(!p || n != 8 || !id || std::memcmp(p,"TGP",3) ||
       (p[3]!=1 && p[3]!=2) || (p[4] | uint16_t(p[5])<<8)!=id) return false;
    seq=p[6]; keys=p[7];
    if(p[3]==1) {
        if(keys & ~31) return false;
        keys=((keys&1)?Up:0)|((keys&2)?Down:0)|((keys&4)?Left:0)|
             ((keys&8)?Right:0)|((keys&16)?A:0);
    }
    return true;
}
bool Receiver::reply(const char *kind, const char *token) {
    char out[64]; int n=std::snprintf(out,sizeof(out),"#TGP:%s:%s\n",kind,token);
    return send && n>0 && send(out,static_cast<size_t>(n));
}
void Receiver::announce() { if(send) send("#TGP:APP\n",9); }
void Receiver::reset(bool notify) {
    if(notify && active) reply("BYE",nonce);
    active=seen=menu=false; buttons=0; nonce[0]=0; used=0; overflow=false;
}
void Receiver::byte(uint8_t value,uint32_t now) {
    if(value=='\n') {
        if(!overflow) { if(used && line[used-1]=='\r') --used; line[used]=0; process(now); }
        used=0; overflow=false;
    } else if(!overflow) {
        if(used+1<sizeof(line)) line[used++]=static_cast<char>(value);
        else { overflow=true; ++rejected; }
    }
}
void Receiver::process(uint32_t now) {
    if(used==32 && !std::memcmp(line,"#TGP:HELLO:",11) && line[15]==':') {
        uint16_t candidate=0;
        for(int i=11;i<15;++i) { int h=hex(line[i]); if(h<0) {++rejected; return;} candidate=(candidate<<4)|h; }
        for(int i=16;i<32;++i) if(hex(line[i])<0 || (line[i]>='A' && line[i]<='F')) {++rejected; return;}
        if(candidate!=id) {++rejected; return;}
        if(!reply("READY",line+16)) return;
        if(!active || std::strcmp(nonce,line+16)) { buttons=0; seen=false; menu=false; }
        std::memcpy(nonce,line+16,17); active=true; last_state=last_alive=now; return;
    }
    if(!active) {++rejected; return;}
    char expected[64];
    std::snprintf(expected,sizeof(expected),"#TGP:END:%s",nonce);
    if(!std::strcmp(line,expected)) { reset(true); return; }
    std::snprintf(expected,sizeof(expected),"#TGP:MENU:%s",nonce);
    if(!std::strcmp(line,expected)) { menu=true; buttons=0; return; }
    if(used!=21 || std::memcmp(line,"#TGP:",5)) {++rejected; return;}
    uint8_t p[8];
    for(int i=0;i<8;++i) {
        int a=hex(line[5+i*2]), b=hex(line[6+i*2]);
        if(a<0 || b<0) {++rejected; return;} p[i]=(a<<4)|b;
    }
    uint8_t seq, keys;
    if(!decode(p,8,id,seq,keys) || (seen && seq==sequence)) {++rejected; return;}
    sequence=seq; seen=true; buttons=keys; last_state=now; ++received;
}
void Receiver::poll(uint32_t now) {
    if(!active) return;
    if(now-last_state>=350) buttons=0;
    if(now-last_state>=1200) {reset(true); return;}
    if(now-last_alive>=500 && reply("ALIVE",nonce)) last_alive=now;
}
}

#include "pico/stdlib.h"
#include "hardware/psram.h"
#include "display.hpp"
#include "storage.hpp"
#include "usb.hpp"
#include "menu.hpp"
#include "ines.hpp"
#include "power.hpp"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_Mapper.h"
#include "nes_palette.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
uint8_t SCREEN[240][256];
static uint16_t linebuffer[256];
const BYTE NesPalette[64]={
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
    32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
    48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
};
static const uint8_t *rom_data=nullptr;
static size_t rom_bytes=0;
static uint32_t frame_at=0;
static bool usb_ready=false;
void tufty_service() {
    static uint32_t previous=0;
    uint32_t now=time_us_32();
    if(usb_ready && now-previous>=1000) {previous=now; usb::poll();}
}
void InfoNES_MessageBox(const char *fmt,...) {
    va_list args; va_start(args,fmt); std::vprintf(fmt,args); va_end(args); std::printf("\n");
}
void InfoNES_Error(const char *fmt,...) {
    va_list args; va_start(args,fmt); std::vsnprintf(storage::error,sizeof(storage::error),fmt,args); va_end(args);
    std::printf("NES: %s\n",storage::error);
}
void InfoNES_DebugPrint(const char *s) {std::printf("%s",s);}
int InfoNES_ReadRom(const char *) {return -1;}
void InfoNES_ReleaseRom() {ROM=VROM=nullptr;}
int InfoNES_Menu() {return 1;}
int InfoNES_Video() {
    ines::Image image;
    ROM=VROM=nullptr;
    if(!ines::inspect(rom_data,rom_bytes,image)) {InfoNES_Error("Invalid/truncated iNES 1.0 ROM"); return 1;}
    bool supported=false;
    for(int i=0;MapperTable[i].nMapperNo!=-1;++i) if(MapperTable[i].nMapperNo==image.mapper) supported=true;
    if(!supported) {InfoNES_Error("Mapper %u is unsupported",image.mapper); return 1;}
    std::memcpy(&NesHeader,rom_data,sizeof(NesHeader));
    // Make mapper decoding agree with the validated iNES header.
    std::memset(NesHeader.byReserve,0,sizeof(NesHeader.byReserve));
    std::memset(SRAM,0,SRAM_SIZE);
    if(NesHeader.byInfo1&4) std::memcpy(SRAM+0x1000,rom_data+16,512);
    ROM=const_cast<BYTE *>(rom_data+image.offset);
    if(image.chr) VROM=ROM+image.prg;
    frame_at=time_us_32();
    return InfoNES_Reset()<0?1:0;
}
static uint8_t local_game() {
    return (!gpio_get(BTN_A)?tgp::A:0)|(!gpio_get(BTN_B)?tgp::B:0)|
        (!gpio_get(BTN_C)?tgp::Start:0)|(!gpio_get(BTN_UP)?tgp::Up:0)|(!gpio_get(BTN_DOWN)?tgp::Down:0);
}
void InfoNES_PadState(DWORD *p1,DWORD *p2,DWORD *system) {
    tufty_service();
    uint8_t keys=local_game()|usb::pad.buttons;
    if(storage::config.swap_ab) keys=(keys&~3)|((keys&1)<<1)|((keys&2)>>1);
    *p1=keys; *p2=0;
    *system=(!gpio_get(BTN_HOME) || usb::pad.menu || usb::disk_requested || usb::rescan_requested || power::pending())?PAD_SYS_QUIT:0;
}
void InfoNES_SoundInit() {}
int InfoNES_SoundOpen(int,int) {return 0;}
void InfoNES_SoundClose() {}
int InfoNES_GetSoundBufferSize() {return 1470;}
void InfoNES_SoundOutput(int,const BYTE *,const BYTE *,const BYTE *,const BYTE *,const BYTE *) {}
void InfoNES_PreDrawLine(int) {InfoNES_SetLineBuffer(linebuffer,256);}
void InfoNES_PostDrawLine(int line) {
    if(line<0 || line>=240) return;
    for(int x=0;x<256;++x) SCREEN[line][x]=linebuffer[x]&63;
}
int InfoNES_LoadFrame() {
    display::present_game(&SCREEN[0][0]);
    uint32_t target=frame_at+16667;
    while(static_cast<int32_t>(time_us_32()-target)<0) {tufty_service(); tight_loop_contents();}
    frame_at=time_us_32();
    return 0;
}
static uint16_t menu_buttons() {
    uint16_t keys=usb::pad.buttons;
    if(!gpio_get(BTN_A) || !gpio_get(BTN_UP)) keys|=tgp::Left;
    if(!gpio_get(BTN_C) || !gpio_get(BTN_DOWN)) keys|=tgp::Right;
    if(!gpio_get(BTN_B)) keys|=tgp::A;
    return keys;
}
static void flush_config() {
    if(!usb::dirty && !usb::save_requested) return;
    usb::save_requested=false;
    bool ok=storage::save();
    if(ok) usb::dirty=false;
    std::printf("State save: %s\n",ok?"OK":"unavailable/failed; original record retained");
}
int main() {
    display::early_init();
    power::init();
    for(uint pin : {uint(BTN_A),uint(BTN_B),uint(BTN_C),uint(BTN_UP),uint(BTN_DOWN),uint(BTN_HOME),uint(BW_RESET_SW)}) {
        gpio_init(pin); gpio_set_dir(pin,GPIO_IN); gpio_pull_up(pin);
    }
    gpio_init(BW_SW_POWER_EN); gpio_set_dir(BW_SW_POWER_EN,GPIO_OUT); gpio_put(BW_SW_POWER_EN,1);
    gpio_init(CYW43_DEFAULT_PIN_WL_REG_ON); gpio_set_dir(CYW43_DEFAULT_PIN_WL_REG_ON,GPIO_OUT); gpio_put(CYW43_DEFAULT_PIN_WL_REG_ON,0);
    for(uint pin=0;pin<4;++pin) {gpio_init(pin); gpio_set_dir(pin,GPIO_OUT); gpio_put(pin,0);}
    bool disk=usb::consume_disk_request();
    disk=power::reset_startup() || disk;
    usb::init(disk); usb_ready=true;
    if(psram_get_size()<5*1024*1024) panic("8 MiB PSRAM required");
    display::start();
    for(unsigned i=0;i<64;++i) display::palette[i]=native_palette[i];
    storage::init(!disk);
    usb::pad.id=storage::config.controller;
    display::brightness(storage::config.brightness);
    if(disk) {
        usb::acquire_disk(storage::mounted()); usb::run_disk();
    }
    storage::scan(); menu::load_art();
    int selected=0;
    for(size_t i=0;i<storage::count;++i) if(!std::strcmp(storage::entries[i].name,storage::config.last_rom)) selected=i;
    float position=selected;
    unsigned back=1;
    uint32_t opened=to_ms_since_boot(get_absolute_time());
    uint16_t previous=menu_buttons();
    bool suppress=true;
    power::menu(true);
    while(true) {
        tufty_service();
        if(usb::disk_requested) {
            flush_config();
            if(usb::dirty) {usb::disk_requested=false; std::printf("Disk entry blocked: unsaved State\n");}
            else usb::reboot_disk();
        }
        if(power::pending()) power::process();
        if(usb::save_requested) flush_config();
        if(usb::rescan_requested) {
            usb::rescan_requested=false; storage::scan(); menu::cover(-1);
            selected=storage::count?std::min<int>(selected,storage::count-1):0; position=selected;
        }
        uint16_t keys=menu_buttons();
        power::idle(keys || !gpio_get(BTN_HOME) || usb::pad.menu);
        if(power::pending()) {power::process(); continue;}
        if(!keys && gpio_get(BTN_HOME)) suppress=false;
        uint16_t pressed=suppress?0:keys&~previous; previous=keys;
        if(pressed&(tgp::Left|tgp::Up)) selected=std::max(0,selected-1);
        if(pressed&(tgp::Right|tgp::Down)) selected=std::min<int>(std::max<int>(0,storage::count-1),selected+1);
        if(storage::count && (pressed&(tgp::Left|tgp::Right|tgp::Up|tgp::Down))) storage::error[0]=0;
        usb::pad.menu=false;
        if(pressed&tgp::A && storage::count) {
            power::menu(false);
            storage::error[0]=0;
            if(storage::load(selected,rom_data,rom_bytes)) {
                if(std::strcmp(storage::config.last_rom,storage::entries[selected].name)) {
                    std::snprintf(storage::config.last_rom,sizeof(storage::config.last_rom),"%s",storage::entries[selected].name);
                    usb::dirty=storage::state_ready(); flush_config();
                }
                InfoNES_Main(true);
            }
            usb::pad.menu=false; suppress=true; previous=menu_buttons();
            power::menu(true);
            opened=to_ms_since_boot(get_absolute_time());
        }
        menu::cover(selected);
        position+=(selected-position)*0.3f;
        if(std::fabs(selected-position)<0.005f) position=selected;
        uint32_t now=to_ms_since_boot(get_absolute_time());
        menu::draw(display::menu_buffers[back],selected,position,now-opened,storage::error[0]?storage::error:nullptr);
        display::present_menu(back); back^=1;
    }
}

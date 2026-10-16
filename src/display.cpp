// PIO transport and panel setup adapted from tufty2350-native's MIT driver.
// See licenses/PIMORONI-LICENSE and THIRD_PARTY.md.
#include "display.hpp"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "parallel.pio.h"
#include <atomic>
#include <cstring>
#include <cmath>
#include <initializer_list>
void tufty_service();
namespace display {
static uint16_t __uninitialized_psram("menu") menus[2][width*height];
static uint8_t __uninitialized_psram("frames") games[2][256*height];
uint16_t *menu_buffers[2]={menus[0],menus[1]};
uint16_t palette[64]{};
static std::atomic<unsigned> requested{1}, completed{0};
static std::atomic<bool> ready{false}, pause_request{false}, pause_ack{false};
static std::atomic<bool> disk_hold{false};
static PIO pio=pio1;
static uint sm, dma;
// Only core0 publishes brightness. Keep the panel dark until its first UI frame.
static uint8_t desired_brightness=0;
static bool visible=false;
alignas(4) static uint16_t columns[2][height];
static void frame(unsigned id);
void early_init() {
    gpio_init(TFT_LED_PIN); gpio_put(TFT_LED_PIN,0); gpio_set_dir(TFT_LED_PIN,GPIO_OUT);
}
static void idle() {
    dma_channel_wait_for_finish_blocking(dma);
    uint32_t mask=1u<<(sm+PIO_FDEBUG_TXSTALL_LSB);
    pio->fdebug=mask;
    while(!(pio->fdebug&mask)) tight_loop_contents();
}
static void write(const void *p,size_t n) {
    dma_channel_set_trans_count(dma,n,false);
    dma_channel_set_read_addr(dma,p,true);
    idle();
}
static void command(uint8_t cmd,const char *data=nullptr,size_t n=0) {
    idle(); gpio_put(TFT_DC_PIN,0); gpio_put(TFT_CS_PIN,0);
    write(&cmd,1);
    if(n) {gpio_put(TFT_DC_PIN,1); write(data,n);}
    gpio_put(TFT_CS_PIN,1);
}
static void set_backlight(uint8_t percent) {
    float level=percent>100?1.0f:percent/100.0f;
    uint16_t duty=level==0?0:static_cast<uint16_t>(65535*(0.11f+0.89f*level*level*level));
    pwm_set_gpio_level(TFT_LED_PIN,duty);
}
void brightness(uint8_t percent) {
    desired_brightness=percent>100?100:percent;
    if(visible) set_backlight(desired_brightness);
}
static void init_panel() {
    pio_set_gpio_base(pio,16);
    sm=pio_claim_unused_sm(pio,true);
    uint offset=pio_add_program(pio,&parallel_program);
    pio_gpio_init(pio,TFT_WR_PIN);
    for(uint i=32;i<40;++i) pio_gpio_init(pio,i);
    pio_sm_set_consecutive_pindirs(pio,sm,32,8,true);
    pio_sm_set_consecutive_pindirs(pio,sm,TFT_WR_PIN,1,true);
    pio_sm_config c=parallel_program_get_default_config(offset);
    sm_config_set_out_pins(&c,32,8); sm_config_set_sideset_pins(&c,TFT_WR_PIN);
    sm_config_set_fifo_join(&c,PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&c,false,true,8);
    sm_config_set_clkdiv(&c,ceilf(2.0f*clock_get_hz(clk_sys)/(44.0f*MHZ))*0.5f);
    pio_sm_init(pio,sm,offset,&c); pio_sm_set_enabled(pio,sm,true);
    dma=dma_claim_unused_channel(true);
    dma_channel_config dc=dma_channel_get_default_config(dma);
    channel_config_set_read_increment(&dc,true);
    channel_config_set_transfer_data_size(&dc,DMA_SIZE_8);
    channel_config_set_dreq(&dc,pio_get_dreq(pio,sm,true));
    dma_channel_configure(dma,&dc,&pio->txf[sm],nullptr,0,false);
    for(uint pin : {uint(TFT_CS_PIN),uint(TFT_DC_PIN),uint(TFT_RD_PIN)}) {
        gpio_init(pin); gpio_set_dir(pin,GPIO_OUT); gpio_put(pin,1);
    }
    pwm_config pc=pwm_get_default_config(); pwm_config_set_wrap(&pc,65535);
    pwm_init(pwm_gpio_to_slice_num(TFT_LED_PIN),&pc,true);
    set_backlight(0); gpio_set_function(TFT_LED_PIN,GPIO_FUNC_PWM);
    command(0x01); sleep_ms(150);
    command(0x3a,"\x05",1); command(0xb2,"\x0c\x0c\x00\x33\x33",5);
    command(0xc0,"\x2c",1); command(0xc2,"\x01",1); command(0xc3,"\x0f",1);
    command(0xc4,"\x20",1); command(0xd0,"\xa4\xa1",2); command(0xc6,"\x0f",1);
    command(0xb0,"\x00\xc0",2); command(0xb7,"\x35",1); command(0xbb,"\x1b",1);
    command(0xe0,"\xf0\x00\x06\x04\x05\x05\x31\x44\x48\x36\x12\x12\x2b\x34",14);
    command(0xe1,"\xf0\x0b\x0f\x0f\x0d\x26\x31\x43\x47\x38\x14\x14\x2c\x32",14);
    command(0x21); command(0x11); sleep_ms(100);
    command(0x2a,"\x00\x00\x00\xef",4); command(0x2b,"\x00\x00\x01\x3f",4);
    command(0x36,"\x90",1);
    // A warm MCU reset leaves the LCD's GRAM intact. Clear all 320 columns
    // before DISPON, as the native driver does, with the backlight still off.
    frame(1); command(0x29);
}
static void frame(unsigned id) {
    uint8_t cmd=0x2c;
    gpio_put(TFT_DC_PIN,0); gpio_put(TFT_CS_PIN,0); write(&cmd,1); gpio_put(TFT_DC_PIN,1);
    for(int x=0;x<width;++x) {
        uint16_t *col=columns[x&1];
        for(int y=0;y<height;++y) {
            uint16_t pixel=id<=2 ? menus[id-1][y*width+x] :
                x>=32 && x<288 ? palette[games[id-3][y*256+x-32]&63] : 0;
            col[y]=__builtin_bswap16(pixel);
        }
        idle(); dma_channel_set_trans_count(dma,height*2,false); dma_channel_set_read_addr(dma,col,true);
    }
    idle(); gpio_put(TFT_CS_PIN,1);
}
static void core() {
    flash_safe_execute_core_init();
    init_panel(); ready.store(true,std::memory_order_release);
    uint32_t previous=0;
    while(true) {
        if(pause_request.load(std::memory_order_acquire)) {
            pause_ack.store(true,std::memory_order_release);
            while(pause_request.load(std::memory_order_acquire)) tight_loop_contents();
            pause_ack.store(false,std::memory_order_release);
        }
        uint32_t now=time_us_32();
        if(!disk_hold.load(std::memory_order_acquire) && now-previous>=16667) {
            previous=now;
            unsigned id=requested.load(std::memory_order_acquire);
            frame(id); completed.store(id,std::memory_order_release);
        }
        tight_loop_contents();
    }
}
void start() {
    std::memset(menus,0,sizeof(menus)); std::memset(games,0,sizeof(games));
    multicore_launch_core1(core);
    while(!ready.load(std::memory_order_acquire)) {tufty_service(); sleep_ms(1);}
}
void present_menu(unsigned index) {
    unsigned id=1+(index&1); requested.store(id,std::memory_order_release);
    while(completed.load(std::memory_order_acquire)!=id) {tufty_service(); sleep_ms(1);}
    if(!visible) {visible=true; set_backlight(desired_brightness);}
}
bool disk_ready() {
    return completed.load(std::memory_order_acquire)==requested.load(std::memory_order_acquire);
}
void hold_disk(bool enabled) {disk_hold.store(enabled,std::memory_order_release);}
void present_disk(unsigned index) {
    // Caller owns the other buffer after disk_ready(). Core1 handles the
    // transfer while core0 immediately returns to servicing USB requests.
    requested.store(1+(index&1),std::memory_order_release);
    if(!visible) {visible=true; set_backlight(desired_brightness);}
}
void present_game(const uint8_t *screen) {
    unsigned old=requested.load(std::memory_order_acquire);
    if(completed.load(std::memory_order_acquire)!=old) return;
    unsigned index=old==3?1:0;
    std::memcpy(games[index],screen,256*height);
    requested.store(3+index,std::memory_order_release);
}
void pause(bool enabled) {
    pause_request.store(enabled,std::memory_order_release);
    while(pause_ack.load(std::memory_order_acquire)!=enabled) tight_loop_contents();
}
bool paused() {return pause_ack.load(std::memory_order_acquire);}
void prepare_reboot() {
    visible=false;
    if(!ready.load(std::memory_order_acquire)) {early_init(); return;}
    // Ack is issued only after a full frame and the final DMA/PIO drain.
    // Also works when disk mode has already paused the display.
    pause(true);
    set_backlight(0);
    command(0x28); // DISP OFF: do not show bus/reset transients even without a backlight.
    sleep_ms(20); // Allow one panel scan before the MCU releases the bus pins.
    pio_sm_set_enabled(pio,sm,false);
}
}

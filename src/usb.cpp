#include "usb.hpp"
#include "storage.hpp"
#include "fat_disk.hpp"
#include "display.hpp"
#include "console.hpp"
#include "power.hpp"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdio/driver.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace usb {
tgp::Receiver pad;
bool disk_requested=false,rescan_requested=false,save_requested=false,dirty=false;
static bool mode=false, owned=false, failed=false, ejected=false, prevent=false, eject_complete=false;
static uint32_t eject_at=0;
static bool console_tx_pending=false;
constexpr uint32_t kDiskMagic=0x4e45534d;
template<size_t N> struct Queue {
    char data[N]; size_t head=0, used=0;
    bool add(const char *p,size_t n) {
        if(n>N-used) return false;
        for(size_t i=0;i<n;++i) data[(head+used+i)%N]=p[i];
        used+=n; return true;
    }
    void drain(uint8_t cdc) {
        if(!tud_cdc_n_connected(cdc)) {
            head=used=0;
            if(cdc==0) console_tx_pending=false;
            return;
        }
        size_t n=std::min({used,N-head,static_cast<size_t>(tud_cdc_n_write_available(cdc))});
        if(n) {
            size_t sent=tud_cdc_n_write(cdc,data+head,n); head=(head+sent)%N; used-=sent;
            if(cdc==0 && sent) console_tx_pending=true;
        }
        tud_cdc_n_write_flush(cdc);
    }
};
static Queue<2048> console_tx;
static Queue<512> protocol_tx;
static void flush_console() {
    std::fflush(stdout);
    uint32_t start=time_us_32();
    // A flush submits an asynchronous USB transfer. Keep servicing USB until
    // both queues and the last IN transfer finish, then give the terminal time
    // to display the reply. A disconnected/stalled host must not block reboot.
    while(tud_cdc_n_connected(0) && time_us_32()-start<1000000) {
        tud_task();
        console_tx.drain(0);
        if(!console_tx.used && !console_tx_pending &&
           tud_cdc_n_write_available(0)==CFG_TUD_CDC_TX_BUFSIZE && time_us_32()-start>=100000) break;
        sleep_ms(1);
    }
}
void transition_notice(const char *message) {
    std::fflush(stdout);
    // Preserve the final status even if earlier logs filled our software queue.
    size_t n=std::strlen(message);
    if(n>sizeof(console_tx.data)-console_tx.used) flush_console();
    console_tx.add(message,n);
    flush_console();
}
static bool send_protocol(const char *s,size_t n) {return protocol_tx.add(s,n);}
static void logs(const char *p,int n) {if(n>0) console_tx.add(p,n);}
static void echo_console(const char *p,size_t n) {console_tx.add(p,n);}
static void command(const char *s);
static console::Line console_line(echo_console,command);
static stdio_driver_t driver{};
bool disk_mode() {return mode;}
void init(bool disk) {
    mode=disk;
    if(!tusb_init()) panic("USB init");
    driver.out_chars=logs;
    stdio_set_translate_crlf(&driver,true);
    stdio_set_driver_enabled(&driver,true);
    pad.send=send_protocol;
}
static void command(const char *s) {
    if(power::pending() && std::strcmp(s,"power status")) {
        std::printf("POWER SLEEP: PENDING; command rejected\n"); return;
    }
    if(power::command(s)) return;
    if(!std::strcmp(s,"help")) {
        std::printf("help | status | usb disk | bootsel | rescan | save | brightness 1..100 | padid 1..65535 | swapab 0/1\n"
                    "power status | power sleep [seconds] | power idle 0..86400\n");
    } else if(!std::strcmp(s,"status")) {
        std::printf("FAT=%u sector=%u State=%u ROMs=%u pad=%u packets=%lu rejected=%lu dirty=%u\n",
            storage::mounted(),flashfat::disk.sector_size,storage::state_ready(),unsigned(storage::count),pad.active,
            static_cast<unsigned long>(pad.received),static_cast<unsigned long>(pad.rejected),dirty);
    } else if(!std::strcmp(s,"usb disk")) {
        disk_requested=true; std::printf("USB DISK: REQUESTED\n");
    } else if(!std::strcmp(s,"bootsel")) {
        transition_notice("BOOTSEL: ENTERING; restarting into USB bootloader\r\n");
        display::prepare_reboot(); tud_disconnect(); reset_usb_boot(0,0);
    }
    else if(!std::strcmp(s,"rescan")) rescan_requested=true;
    else if(!std::strcmp(s,"save")) save_requested=true;
    else {
        unsigned value; char tail;
        if(std::sscanf(s,"brightness %u %c",&value,&tail)==1 && value>=1 && value<=100) {
            storage::config.brightness=value; display::brightness(value); dirty=storage::state_ready();
        } else if(std::sscanf(s,"padid %u %c",&value,&tail)==1 && value>=1 && value<=65535) {
            pad.reset(true); pad.id=storage::config.controller=value; dirty=storage::state_ready(); pad.announce();
        } else if(std::sscanf(s,"swapab %u %c",&value,&tail)==1 && value<=1) {
            storage::config.swap_ab=value; dirty=storage::state_ready();
        } else std::printf("Unknown command; type help\n");
    }
}
void poll() {
    tud_task();
    static bool was_connected=false, console_connected=false;
    bool connected=tud_cdc_n_connected(1);
    if(connected!=was_connected) {
        pad.reset(); protocol_tx.head=protocol_tx.used=0;
        if(connected && !mode) pad.announce();
        was_connected=connected;
    }
    bool c0=tud_cdc_n_connected(0);
    if(c0!=console_connected) console_line.reset();
    if(!c0) tud_cdc_n_read_flush(0);
    if(c0 && !console_connected) std::printf("Tufty NES: CDC0 Console, CDC1 Gamepad%s\n",mode?" (disk mode)":"; type help");
    console_connected=c0;
    for(int budget=0;budget<128 && tud_cdc_n_available(0);++budget) {
        int c=tud_cdc_n_read_char(0);
        if(c0 && !mode && c>=0) {power::activity(); console_line.feed(static_cast<uint8_t>(c));}
    }
    uint32_t now=to_ms_since_boot(get_absolute_time());
    for(int budget=0;budget<256 && tud_cdc_n_available(1);++budget) {
        int c=tud_cdc_n_read_char(1);
        if(connected && !mode) {
            pad.byte(c,now);
            if(pad.buttons || pad.menu) power::activity();
        }
    }
    if(!mode) pad.poll(now);
    console_tx.drain(0); protocol_tx.drain(1);
}
void shutdown() {
    pad.reset();
    stdio_set_driver_enabled(&driver,false);
    tud_disconnect(); tud_deinit(0);
}
void acquire_disk(bool valid) {
    owned=valid && storage::unmount_system();
    if(!owned) storage::unmount_system();
    display::pause(true);
}
[[noreturn]] void reboot_disk() {
    transition_notice("USB DISK: ENTERING; restarting as USB mass storage\r\n");
    display::prepare_reboot();
    watchdog_hw->scratch[3]=kDiskMagic;
    tud_disconnect(); watchdog_reboot(0,0,10); while(true) tight_loop_contents();
}
[[noreturn]] void run_disk() {
    while(true) {
        poll();
        if(eject_complete && time_us_32()-eject_at>=250000 && gpio_get(BTN_HOME)) {
            transition_notice("USB DISK: EJECTED; returning to menu\r\n");
            display::prepare_reboot();
            tud_disconnect(); watchdog_reboot(0,0,10);
            while(true) tight_loop_contents();
        }
        sleep_ms(1);
    }
}
bool consume_disk_request() {
    bool flag=watchdog_caused_reboot() && watchdog_hw->scratch[3]==kDiskMagic;
    watchdog_hw->scratch[3]=0; return flag;
}
}
extern "C" {
void tud_cdc_tx_complete_cb(uint8_t itf) {
    // TinyUSB invokes this before sending the next packet from its FIFO.
    // An empty FIFO here means the last data packet reached the USB host.
    if(itf==0) usb::console_tx_pending=tud_cdc_n_write_available(0)!=CFG_TUD_CDC_TX_BUFSIZE;
}
void tud_msc_inquiry_cb(uint8_t,uint8_t vendor[8],uint8_t product[16],uint8_t rev[4]) {
    std::memcpy(vendor,"TUFTY   ",8); std::memcpy(product,"NES SYSTEM      ",16); std::memcpy(rev,"0001",4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if(!lun && usb::owned && !usb::failed && !usb::ejected) return true;
    tud_msc_set_sense(lun,SCSI_SENSE_NOT_READY,0x3a,0); return false;
}
void tud_msc_capacity_cb(uint8_t,uint32_t *blocks,uint16_t *size) {
    *size=flashfat::disk.sector_size?flashfat::disk.sector_size:4096;
    *blocks=tufty::storage::layout::kSystem.bytes/(*size);
}
bool tud_msc_is_writable_cb(uint8_t lun) {return tud_msc_test_unit_ready_cb(lun);}
int32_t tud_msc_read10_cb(uint8_t lun,uint32_t lba,uint32_t offset,void *out,uint32_t bytes) {
    uint32_t at;
    if(!tud_msc_test_unit_ready_cb(lun) || !flashfat::disk.range(lba,offset,bytes,at)) return -1;
    return flashfat::disk.read(at,out,bytes)?static_cast<int32_t>(bytes):-1;
}
int32_t tud_msc_write10_cb(uint8_t lun,uint32_t lba,uint32_t offset,uint8_t *in,uint32_t bytes) {
    uint32_t at;
    if(!tud_msc_test_unit_ready_cb(lun) || !flashfat::disk.range(lba,offset,bytes,at)) return -1;
    uint32_t original=bytes;
    alignas(4) static uint8_t staging[4096];
    while(bytes) {
        uint32_t base=at&~4095u, within=at-base, n=std::min(bytes,4096-within);
        if(!flashfat::disk.read(base,staging,sizeof(staging))) {usb::failed=true; break;}
        if(std::memcmp(staging+within,in,n)) {
            std::memcpy(staging+within,in,n);
            if(!storage::replace_system_sector(base,staging)) {usb::failed=true; break;}
        }
        at+=n; in+=n; bytes-=n;
    }
    if(usb::failed) {tud_msc_set_sense(lun,SCSI_SENSE_MEDIUM_ERROR,0x0c,0); return -1;}
    return original;
}
bool tud_msc_start_stop_cb(uint8_t lun,uint8_t,bool start,bool load_eject) {
    if(load_eject && !start) {
        if(usb::prevent || usb::failed) {tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x53,2); return false;}
        usb::ejected=true;
    }
    return true;
}
bool tud_msc_prevent_allow_medium_removal_cb(uint8_t lun,uint8_t prohibit,uint8_t) {
    if(lun) return false;
    usb::prevent=prohibit!=0;
    return true;
}
void tud_msc_scsi_complete_cb(uint8_t,const uint8_t cmd[16]) {
    if(cmd[0]==SCSI_CMD_START_STOP_UNIT && usb::ejected) {usb::eject_at=time_us_32(); usb::eject_complete=true;}
}
int32_t tud_msc_scsi_cb(uint8_t lun,const uint8_t cmd[16],void *,uint16_t) {
    if(cmd[0]==0x35 && !usb::failed) return 0; // SYNCHRONIZE CACHE(10)
    tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x20,0); return -1;
}
}

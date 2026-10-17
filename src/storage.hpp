#pragma once
#include <cstddef>
#include <cstdint>
namespace storage {
constexpr size_t kMaxRoms=128, kMaxRomBytes=4*1024*1024;
struct Entry { char name[256]; uint32_t bytes; };
struct Config {
    uint16_t controller=1;
    uint8_t brightness=80;
    bool swap_ab=false;
    bool ble_enabled=true;
    bool ble_random=false;
    char ble_peer[18]{};
    uint16_t ble_scan_uuid=0x1812;
    uint32_t menu_sleep_seconds=300;
    char last_rom[256]{};
};
extern Config config;
extern Entry *entries;
extern size_t count;
extern char error[96];
void init(bool state=true);
bool mounted();
void scan();
bool load(size_t index,const uint8_t *&data,size_t &bytes);
bool read_file(const char *path,void *out,size_t capacity,size_t &actual);
bool save();
bool unmount_system();
bool replace_system_sector(uint32_t offset,const uint8_t *sram);
bool state_ready();
}

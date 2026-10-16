#pragma once
#include <cstddef>
#include <cstdint>
namespace tgp {
enum Button : uint8_t { A=1, B=2, Select=4, Start=8, Up=16, Down=32, Left=64, Right=128 };
bool decode(const uint8_t *packet, size_t size, uint16_t id, uint8_t &sequence, uint8_t &buttons);
struct Receiver {
    using Send = bool (*)(const char *, size_t);
    Send send = nullptr;
    uint16_t id = 1;
    uint8_t buttons = 0, sequence = 0;
    bool active = false, seen = false, menu = false;
    uint32_t last_state = 0, last_alive = 0, received = 0, rejected = 0;
    char nonce[17]{};
    void byte(uint8_t value, uint32_t now);
    void poll(uint32_t now);
    void reset(bool notify = false);
    void announce();
private:
    char line[80]{};
    size_t used = 0;
    bool overflow = false;
    void process(uint32_t now);
    bool reply(const char *kind, const char *token);
};
}

#pragma once
#include <cstdint>
namespace menu {
void load_art();
void draw(uint16_t *out,int selected,float position,uint32_t elapsed,const char *message=nullptr);
void cover(int index);
void disk(uint16_t *out,bool valid);
}

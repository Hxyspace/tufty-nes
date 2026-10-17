#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_Mapper.h"
#include "K6502.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

// Exercise the actual patched PPU and mapper; only platform/audio/CPU hooks
// are stubbed. Synthetic graphics avoid requiring commercial ROM fixtures.
#include "mapper/InfoNES_Mapper_048.cpp"
BYTE IRQ_State=1, NMI_State=1;
void K6502_Set_Int_Wiring(BYTE, BYTE) {}
void Map0_Sram(WORD, BYTE) {}
void Map0_Apu(WORD, BYTE) {}
BYTE Map0_ReadApu(WORD) {return 0;}
void Map0_VSync() {}
void Map0_HSync() {}
void Map0_PPU(WORD) {}
void Map0_RenderScreen(BYTE) {}
void InfoNES_pAPUHsync(bool) {}
void InfoNES_pAPUVsync() {}
void InfoNES_PadState(DWORD *, DWORD *, DWORD *) {}
int InfoNES_LoadFrame() {return 0;}

static std::array<WORD,NES_DISP_WIDTH> line;
static std::array<unsigned,NES_DISP_HEIGHT> drawn;
void InfoNES_PreDrawLine(int y) {
    assert(y>=0 && y<NES_DISP_HEIGHT);
    InfoNES_SetLineBuffer(line.data(),line.size());
}
void InfoNES_PostDrawLine(int y) {++drawn[y];}

static void reset() {
    std::memset(PPURAM,0,PPURAM_SIZE);
    std::memset(SPRRAM,255,SPRRAM_SIZE);
    std::memset(ChrBuf,0,CHRBUF_SIZE);
    for(int i=0;i<16;++i) PPUBANK[i]=PPURAM+i*1024;
    for(int i=0;i<32;++i) PalTable[i]=(i%4)?i:0x800d;
    PPU_BG_Base=PPU_SP_Base=ChrBuf;
    PPU_R0=PPU_R1=PPU_R2=0;
    PPU_Addr=PPU_Temp=0;
    PPU_Scr_H_Byte=PPU_Scr_H_Bit=0;
    PPU_NameTableBank=NAME_TABLE0;
    PPU_UpDown_Clip=0;
    PPU_SP_Height=8;
    PPU_Scanline=16;
    MapperPPU=Map0_PPU;
    MapperRenderScreen=Map0_RenderScreen;
    InfoNES_SetLineBuffer(line.data(),line.size());
}

static void clipping() {
    reset();
    // Tile 0: solid color 1. Both background and sprite use this tile.
    std::fill_n(PPURAM,8,255);
    SPRRAM[SPR_Y]=15;
    SPRRAM[SPR_CHR]=SPRRAM[SPR_ATTR]=SPRRAM[SPR_X]=0;
    for(unsigned mask=0;mask<4;++mask) {
        PPU_R1=R1_SHOW_SCR|R1_SHOW_SP|(mask&1?R1_CLIP_BG:0)|(mask&2?R1_CLIP_SP:0);
        InfoNES_DrawLine();
        WORD left=mask&2?PalTable[17]:mask&1?PalTable[1]:PalTable[0];
        for(int x=0;x<8;++x) assert(line[x]==left);
        for(int x=8;x<NES_DISP_WIDTH;++x) assert(line[x]==PalTable[1]);
    }
    // Behind-background sprites must still appear over a masked background.
    SPRRAM[SPR_ATTR]=SPR_ATTR_PRI;
    PPU_R1=R1_SHOW_SCR|R1_SHOW_SP|R1_CLIP_SP;
    InfoNES_DrawLine();
    for(int x=0;x<8;++x) assert(line[x]==PalTable[17]);
    PPU_R1|=R1_CLIP_BG;
    InfoNES_DrawLine();
    for(WORD pixel:line) assert(pixel==PalTable[1]);
    // Disabled background must use the game backdrop, with correct priority.
    PPU_R1=R1_SHOW_SP|R1_CLIP_SP;
    InfoNES_DrawLine();
    for(int x=0;x<8;++x) assert(line[x]==PalTable[17]);
    for(int x=8;x<NES_DISP_WIDTH;++x) assert(line[x]==PalTable[0]);
    PPU_R1=0;
    InfoNES_DrawLine();
    for(WORD pixel:line) assert(pixel==PalTable[0]);
}

static void full_frame() {
    reset();
    FrameCnt=0;
    drawn.fill(0);
    for(int y=0;y<263;++y) PPU_ScanTable[y]=y<240?SCAN_ON_SCREEN:SCAN_VBLANK;
    // Change the backdrop every scanline: stale first/last lines cannot pass.
    for(int y=0;y<NES_DISP_HEIGHT;++y) {
        PPU_Scanline=y;
        PalTable[0]=0x8000|(y&63);
        line.fill(0xffff);
        assert(InfoNES_HSync()==0 && drawn[y]==1);
        for(WORD pixel:line) assert(pixel==PalTable[0]);
    }
}

static void mapper48() {
    reset();
    std::vector<BYTE> prg(128*1024),chr(128*1024);
    ROM=prg.data(); VROM=chr.data();
    NesHeader.byRomSize=8; NesHeader.byVRomSize=16;
    Map48_Init();
    for(int value=0;value<256;++value) {
        for(int slot=0;slot<4;++slot) {
            Map48_Write(0xa000+slot,value);
            assert(PPUBANK[4+slot]==VROM+(value%128)*1024);
        }
        for(int pair=0;pair<2;++pair) {
            Map48_Write(0x8002+pair,value);
            for(int half=0;half<2;++half)
                assert(PPUBANK[pair*2+half]==VROM+((value*2+half)%128)*1024);
        }
    }
    // Select an odd-numbered bank containing a solid sprite; the doubled
    // bank from the old mapper is blank. Verify decoded pixels as well.
    std::fill_n(chr.data()+3*1024,8,255);
    Map48_Write(0xa000,3);
    PPU_R0=R0_SP_ADDR;
    PPU_R1=R1_SHOW_SP|R1_CLIP_SP;
    SPRRAM[SPR_Y]=15;
    SPRRAM[SPR_CHR]=SPRRAM[SPR_ATTR]=0;
    SPRRAM[SPR_X]=16;
    InfoNES_DrawLine();
    for(int x=0;x<NES_DISP_WIDTH;++x)
        assert(line[x]==(x>=16 && x<24?PalTable[17]:PalTable[0]));
    ROM=VROM=nullptr;
}

int main() {clipping(); full_frame(); mapper48();}

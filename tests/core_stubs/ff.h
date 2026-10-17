#pragma once
// Save-state functions are discarded from the PPU test executable. Keep the
// real FatFs headers separate: upstream's DWORD is unsigned long on the host.
struct FATFS {};
struct FIL {};
using FRESULT=int;
using UINT=unsigned;
constexpr unsigned FA_CREATE_ALWAYS=8, FA_WRITE=2, FA_READ=1;
FRESULT f_mount(FATFS *,const char *,unsigned);
FRESULT f_open(FIL *,const char *,unsigned);
FRESULT f_write(FIL *,const void *,UINT,UINT *);
FRESULT f_read(FIL *,void *,UINT,UINT *);
FRESULT f_close(FIL *);

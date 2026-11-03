#include "revolution/OS/OSArena.h"
#include "revolution/OS/OSExec.h"
#include "revolution/OS/OSInterrupt.h"

void* SaveStart = NULL;
void* SaveEnd = NULL;

void __OSReboot(u32 restartCode, u32 dolOffset) {
    char* argv;
    OSDisableInterrupts();
    OSSetArenaLo((void*)0x81280000);
    OSSetArenaHi((void*)0x812F0000);
    argv = NULL;
    __OSNextPartitionType = *(u32*)0x80003194;
    __OSBootDol(dolOffset, restartCode | 0x80000000, &argv);
}

void OSGetSaveRegion(u32* start, u32* end) {
    *start = (u32)SaveStart;
    *end = (u32)SaveEnd;
}

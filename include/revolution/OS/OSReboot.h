#ifndef RVL_SDK_OS_REBOOT_H
#define RVL_SDK_OS_REBOOT_H

#include "revolution/types.h"

void __OSReboot(u32 restartCode, u32 dolOffset);
void OSGetSaveRegion(u32* start, u32* end);

#endif

#include "revolution/OS/OSTime.h"

s64 __get_time(void) {
    return __OSGetSystemTime();
}

u32 __get_gm_time(void) {
    return OSGetTime() / (*(u32*)0x800000F8 >> 2) + 0xBC17C200;
}

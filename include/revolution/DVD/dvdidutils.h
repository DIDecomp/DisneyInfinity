#ifndef RVL_SDK_DVD_ID_UTILS_H
#define RVL_SDK_DVD_ID_UTILS_H
#include "revolution/DVD/dvd.h"
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif
    u8 disk;         // at 0x6
    u8 version;      // at 0x7
    u8 strmEnable;   // at 0x8
    u8 strmBufSize;  // at 0x9
    u8 padding[14];  // at 0xA


BOOL DVDCompareDiskID(const DVDDiskID* id1, const DVDDiskID* id2);

#ifdef __cplusplus
}
#endif
#endif

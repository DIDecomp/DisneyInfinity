#include "revolution/OS/OSReset.h"
#include "revolution/DVD/dvd.h"
#include "revolution/ESP/esp.h"
#include "revolution/OS/OS.h"
#include "revolution/OS/OSArena.h"
#include "revolution/OS/OSError.h"
#include "revolution/OS/OSExec.h"
#include "revolution/OS/OSPlayRecord.h"
#include "revolution/OS/OSPlayTime.h"
#include "revolution/OS/OSRtc.h"
#include "revolution/OS/OSReboot.h"
#include "revolution/OS/OSStateFlags.h"
#include "revolution/OS/OSStateTM.h"
#include "revolution/OS/OSThread.h"
#include "revolution/VI/vi3in1.h"
#include "stddef.h"
#include <revolution/DVD.h>
#include <revolution/OS.h>
#include <revolution/PAD.h>
#include <revolution/SC.h>
#include <revolution/VI.h>
#include <string.h>

u32 ResetDolOffset = 0;
u32 lbl_804BCFFC = 0;
static OSShutdownFunctionQueue ShutdownFunctionQueue;

static void KillThreads(void);

void OSRegisterShutdownFunction(OSShutdownFunctionInfo* info) {
    OSShutdownFunctionInfo* it;
    OSShutdownFunctionInfo* prev;
    OSShutdownFunctionInfo* tail;

    for (it = ShutdownFunctionQueue.head; it != NULL && it->prio <= info->prio;
         it = it->next) {
        ;
    }

    if (it == NULL) {

        tail = ShutdownFunctionQueue.tail;
        if (tail == NULL) {
            ShutdownFunctionQueue.head = info;
        } else {
            ShutdownFunctionQueue.tail->next = info;
        }

        info->prev = tail;
        info->next = NULL;

        it = ShutdownFunctionQueue.head;
        ShutdownFunctionQueue.tail = info;
    } else {
        info->next = it;

        prev = it->prev;
        it->prev = info;
        info->prev = prev;

        if (prev == NULL) {
            ShutdownFunctionQueue.head = info;
        } else {
            prev->next = info;
        }
    }
}

#define DEQUEUE_INFO(info, queue) \
  do {                                  \
    OSShutdownFunctionInfo* __next;               \
    OSShutdownFunctionInfo* __prev;               \
                                        \
    __next = (info)->next;              \
    __prev = (info)->prev;              \
                                        \
    if (__next == NULL)                 \
      (queue)->tail = __prev;           \
    else                                \
      __next->prev = __prev;            \
                                        \
    if (__prev == NULL)                 \
      (queue)->head = __next;           \
    else                                \
      __prev->next = __next;            \
  } while (0)


void OSUnregisterShutdownFunction(OSShutdownFunctionInfo* info) {
    DEQUEUE_INFO(info, &ShutdownFunctionQueue);
}

BOOL __OSCallShutdownFunctions(u32 pass, u32 event) {
    OSShutdownFunctionInfo* iter;
    BOOL failure;
    u32 prio;

    prio = 0;
    failure = FALSE;

    for (iter = ShutdownFunctionQueue.head; iter != NULL; iter = iter->next) {
        if (failure && prio != iter->prio) {
            break;
        }

        failure |= !iter->func(pass, event);
        prio = iter->prio;
    }

    failure |= !__OSSyncSram();

    return !failure;
}

static void KillThreads(void) {
    OSThread* iter;
    OSThread* next;

    for (iter = OS_THREAD_QUEUE.head; iter != NULL; iter = next) {
        next = iter->linkActive.next;

        switch (iter->state) {
        case OS_THREAD_STATE_READY:
        case OS_THREAD_STATE_SLEEPING:
            OSCancelThread(iter);
            break;
        }
    }
}

static void HotResetFail(void) {
    if (__OSInNandBoot || __OSInReboot) {
        __OSInitSTM();
    }

    __OSHotReset();

#line 1034
    OSError("__OSHotReset(): Falied to reset system.\n");
}

void __OSShutdownDevices(u32 event) {
    BOOL padIntr;
    BOOL osIntr;
    BOOL keepEnable;

    switch (event) {
    case 0:
    case 5:
    case 6:
        keepEnable = FALSE;
        break;
    case 1:
    case 2:
    case 3:
    default:
        keepEnable = TRUE;
        break;
    }

    __OSStopAudioSystem();

    if (!keepEnable) {
        padIntr = __PADDisableRecalibration(TRUE);
    }

    while (!__OSCallShutdownFunctions(OS_SD_PASS_FIRST, event)) {
        ;
    }

    while (!__OSSyncSram()) {
        ;
    }

    osIntr = OSDisableInterrupts();
    __OSCallShutdownFunctions(OS_SD_PASS_SECOND, event);
    LCDisable();

    if (!keepEnable) {
        __PADDisableRecalibration(padIntr);
    }

    KillThreads();
}

static u32 GetDiscState(u8 discState) {
    u32 flags;
    u32 val;

    val = __DVDGetCoverStatus();
    if (val != 2) {
        val = 3;
    } else if (discState == 1 && __OSGetRTCFlags(&flags) && flags == 0) {
        val = 1;
    } else {
        val = 2;
    }

    return val;
}

u32 __OSGetDiscState(void) {
    OSStateFlags stateFlags;

    __OSStopPlayRecord();
    __OSUnRegisterStateEvent();
    __DVDPrepareReset();
    __OSReadStateFlags(&stateFlags);

    stateFlags.discState = GetDiscState(stateFlags.discState);
    stateFlags.shutdown = 2;
    __OSClearRTCFlags();
    __OSWriteStateFlags(&stateFlags);
    OSDisableScheduler();
    __OSShutdownDevices(1);

    HotResetFail();
}

void OSShutdownSystem(void) {
    SCIdleMode idleMode;
    OSStateFlags stateFlags;
    OSIOSRev iosRev;

    memset(&idleMode, 0, sizeof(SCIdleMode));
    SCInit();
    while (SCCheckStatus() == SC_STATUS_OK) {
        ;
    }
    SCGetIdleMode(&idleMode);

    __OSStopPlayRecord();
    __OSUnRegisterStateEvent();
    __DVDPrepareReset();
    __OSReadStateFlags(&stateFlags);
    stateFlags.discState = GetDiscState(stateFlags.discState);

    if (idleMode.wc24 == TRUE) {
        stateFlags.shutdown = 5;
    } else {
        stateFlags.shutdown = 1;
    }

    __OSClearRTCFlags();
    __OSWriteStateFlags(&stateFlags);
    __OSGetIOSRev(&iosRev);

    if (idleMode.wc24 == TRUE) {
        lbl_804BCFFC = 1;
        OSDisableScheduler();
        __OSShutdownDevices(5);
        OSEnableScheduler();
        __OSLaunchMenu();
    } else {
        OSDisableScheduler();
        __OSShutdownDevices(2);
        __OSShutdownToSBY();
    }
}

void OSRestart(u32 code) {
    u8 type = OSGetAppType();

    __OSStopPlayRecord();
    __OSUnRegisterStateEvent();

    if (type == 0x81) {
        OSDisableScheduler();
        __OSShutdownDevices(4);
        OSEnableScheduler();
        __OSRelaunchTitle(code);
    } else if (type == 0x80) {
        OSDisableScheduler();
        __OSShutdownDevices(4);
        OSEnableScheduler();

        __OSReboot(code, ResetDolOffset);
    }

    OSDisableScheduler();
    __OSShutdownDevices(1);

    HotResetFail();
}

static void ReturnToMenuFail(void) {
    OSStateFlags stateFlags;
    __OSReadStateFlags(&stateFlags);
    stateFlags.discState = 2;
    stateFlags.shutdown = 3;

    __OSClearRTCFlags();
    __OSWriteStateFlags(&stateFlags);
    __OSLaunchMenu();
    OSDisableScheduler();
    __VISetRGBModeImm();

    HotResetFail();

#line 1010
    OSError("__OSReturnToMenu(): Falied to boot system menu.\n");
}

void __OSReturnToMenu(u32 val) {
    OSStateFlags stateFlags;
    ESTicketView *view;
    __OSPlayTimeType ptt;
    u32 ptval;

    __OSStopPlayRecord();
    __OSUnRegisterStateEvent();
    __DVDPrepareReset();

    __OSReadStateFlags(&stateFlags);
    stateFlags.discState = GetDiscState(stateFlags.discState);
    stateFlags.shutdown = 3;
    stateFlags.BYTE_0x7 = val;

    __OSClearRTCFlags();
    __OSWriteStateFlags(&stateFlags);

    OSSetArenaLo((void*)0x81280000);
    OSSetArenaHi((void*)0x812F0000);

    if (ESP_InitLib()) {
        ReturnToMenuFail();
    }

    view = (ESTicketView*)OSAllocFromMEM1ArenaLo(sizeof(ESTicketView), 0x20);
    if (!view) {
        ReturnToMenuFail();
    }

    memset(view, 0, sizeof(ESTicketView));
    if (!ESP_DiGetTicketView(NULL, view) && OSPlayTimeIsLimited()) {
        ptt = NULL;
        ptval = -1;
        __OSGetPlayTime(view, &ptt, &ptval);
        if (ptval == 0) {
            __OSWriteExpiredFlagIfSet();
        }
    }

    OSDisableScheduler();
    __OSShutdownDevices(5);
    OSEnableScheduler();

    __OSLaunchMenu();
    OSDisableScheduler();
    __VISetRGBModeImm();

    HotResetFail();
}

void OSReturnToMenu(void) {
    __OSReturnToMenu(0);

#line 895
    OSError("OSReturnToMenu(): Falied to boot system menu.\n");
}

void OSReturnToDataManager(void) {
    __OSReturnToMenu(1);

#line 913
    OSError("OSReturnToDataManager(): Falied to boot system menu.\n");
}

void __OSReturnToMenuForError(void) {
    ReturnToMenuFail();
}

void __OSHotResetForError(void) {
    HotResetFail();
}

u32 OSGetResetCode(void) {
    if (__OSRebootParams.valid != 0) {
        return __OSRebootParams.restartCode | 0x80000000;
    }

    return PI_HW_REGS[PI_RESET] >> 3;
}

#include "macros.h"

void OSReturnToSetting(void) {
    const char *val[] = {
        "Calendar/Calendar_index.html",
        "Display/Display_index.html",
        "Sound/Sound_index.html",
        "Parental_Control/Parental_Control_index.html",
        "Internet/Internet_index.html",
        "WiiConnect24/Wiiconnect24_index.html",
        "Update/Update_index.html",
    };

    if (lbl_804BCFFC == 0) {
        OSError("OSReturnToSetting(): You can't specify %d.  \n", 0);
    }
}

void OSResetSystem(u32 arg0, u32 arg1, u32 arg2) {
#pragma unused(arg0)
#pragma unused(arg1)
#pragma unused(arg2)

#line 1185
    OSError("OSResetSystem() is obsoleted. It doesn't work any longer.\n");
}

void OSSetBootDol() {
    OSError("OSSetBootDol() is obsoleted. It doesn't work any longer.\n");
}

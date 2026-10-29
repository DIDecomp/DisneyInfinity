#include "revolution/OS/OSExec.h"
#include "revolution/DVD/dvdidutils.h"
#include "revolution/OS/OSArena.h"
#include "revolution/OS/OSInterrupt.h"
#include "revolution/OS/OSPlayTime.h"
#include "revolution/DVD/dvd.h"
#include "revolution/OS/OS.h"
#include <revolution/ESP.h>
#include <revolution/IPC.h>
#include <revolution/OS.h>
#include <string.h>
#include "revolution/hw_regs.h"

#define MENU_TITLE_ID 0x0000000100000002
#define TICKET_VIEW_SIZE 0xD8

u32 __OSNextPartitionType = 0;
static volatile u32 DVDLowIntType = 0;
static DVDDiskID id;
BOOL __OSInReboot = FALSE;
BOOL Prepared = FALSE;

OSExecParams* __OSExecParamsAddr : OS_BASE_CACHED + 0x30F0;
s32 __OSCurrentTGCOffset : OS_BASE_CACHED + 0x30F4;
vu32 __OSLaunchPartitionType : (OS_BASE_CACHED | 0x3194);
vu8 __OSLockedFlag : (OS_BASE_CACHED | 0x3187);

static BOOL PackArgs(void* addr, s32 argc, char* argv[]) {
    s32 numArgs;
    char* bootInfo2;
    char* ptr;
    char** list;
    u32 i;

    bootInfo2 = (char*)addr;
    memset(bootInfo2, 0, 0x2000);

    if (argc == 0) {
        *(u32*)&bootInfo2[8] = 0;
    } else {
        numArgs = argc;
        ptr = bootInfo2 + 0x2000;
        while (--argc >= 0) {
            ptr -= (strlen(argv[argc]) + 1);
            strcpy(ptr, argv[argc]);
            argv[argc] = (char*)(ptr - bootInfo2);
        }

        list = (char**)(bootInfo2 + ((ptr - bootInfo2) & ~3));
        list -= numArgs + 1;

        for (i = 0; i < numArgs + 1; i++) {
            list[i] = argv[i];
        }

        *(s32*)(list - 1) = numArgs;
        *(u32*)&bootInfo2[8] = (u32)((char*)(list - 1) - bootInfo2);
    }

    return TRUE;
}

static BOOL Utf16ToArg(char* dstArg, u16* srcName) {
    char *srcPtr, *dstPtr;
    u8 i, mask, shift;

    if (srcName != 0) {
        srcPtr = (char*)srcName;
        dstPtr = dstArg;

        while (*srcPtr || *(srcPtr + 1)) {
            for (i = 0; i < 4; i++) {
                mask = (u8)((i & 0x1) ? 0xF : 0xF0);
                shift = (u8)((i & 0x1) ? 0 : 4);

                if (0 <= ((*srcPtr & mask) >> shift) && ((*srcPtr & mask) >> shift) < 0xA) {
                    *dstPtr = (char)(((*srcPtr & mask) >> shift) + 0x30);
                } else if (0xA <= ((*srcPtr & mask) >> shift) && ((*srcPtr & mask) >> shift) < 0x10) {
                    *dstPtr = (char)(((*srcPtr & mask) >> shift) + 0x57);
                } else {
                    return FALSE;
                }

                dstPtr++;

                if (i & 1) {
                    srcPtr++;
                }
            }
        }

        *dstPtr++ = 0;
        return TRUE;
    }

    return FALSE;
}

static BOOL PackInstallerArgs(void* addr, s32 argc, char* argv[]) {
    s32 numArgs;
    char* bootInfo2;
    char* ptr;
    char** list;
    u32 i;

    bootInfo2 = (char*)addr;
    memset(bootInfo2, 0, 0x2000);

    if (argc == 0) {
        *(u32*)&bootInfo2[8] = 0;
    } else {
        numArgs = argc;
        ptr = bootInfo2 + 0x2000;

        while (--argc >= 0) {
            if (argc < 2 || (argc % 2)) {
                char* val = argv[argc];
                ptr -= (strlen(argv[argc]) + 1);
                strcpy(ptr, val);
                argv[argc] = (char*)(ptr - bootInfo2);
            } else {
                u16* val = (u16*)argv[argc];
                ptr -= (wcslen(val) * 4 + 1);
                Utf16ToArg(ptr, val);
                argv[argc] = (char*)(ptr - bootInfo2);
            }
        }

        list = (char**)(bootInfo2 + ((ptr - bootInfo2) & ~3));
        list -= numArgs + 1;

        for (i = 0; i < numArgs + 1; i++) {
            list[i] = argv[i];
        }

        *(s32*)(list - 1) = numArgs;

        *(u32*)&bootInfo2[8] = (u32)((char*)(list - 1) - bootInfo2);
    }

    return TRUE;
}

// clang-format off
static asm void Run(register void* entryPoint) {
    fralloc
    bl      ICFlashInvalidate
    sync
    isync
    mtctr    entryPoint
    bctr
    frfree
    blr
}
// clang-format on

static void callback(s32 result, DVDCommandBlock* block) {
    Prepared = TRUE;
}

void __OSGetExecParams(OSExecParams* out) {
    if (OS_DOL_EXEC_PARAMS >= (void*)0x80000000) {
        memcpy(out, OS_DOL_EXEC_PARAMS, sizeof(OSExecParams));
    } else {
        out->valid = FALSE;
    }
}

static void Callback(u32 intType) {
    DVDLowIntType = intType;
}

void CheckDVDLowIntType(int param_0) {
    switch (param_0) {
    case 2:
        OSReport("\nDisc error occurred!\n");
        __OSReturnToMenuForError();
        break;
    case 1:
        break;
    case 16:
        OSReport("\nTimeout error occurred!\n");
        __OSReturnToMenuForError();
        break;
    default:
        OSReport("\nUnexpected error occurred!\n");
        __OSReturnToMenuForError();
    }
}

void __OSLaunchNextFirmware(void) {
    u8 i;
    s32 rc = -1;
    u32 ticketCnt = 1;
    ESTicketView *t, *t2;
    ESSysVersion version = 0x0000000100000003;
    u32 simGddr3Size;
    u32 gddrProtAddr;

    DVDGameTOC* gameToc;
    DVDPartitionInfo* partInfo;
    DVDPartitionInfo* bootGameInfo;
    ESTitleMeta* tmd;
    u32 numTmdBytes = 0;
    DVDPartitionInfo* partInfoPtr;

    gameToc = (DVDGameTOC*)OSAllocFromMEM1ArenaLo(OSRoundUp32B(sizeof(DVDGameTOC)), 32);
    partInfo = (DVDPartitionInfo*)OSAllocFromMEM1ArenaLo(OSRoundUp32B(sizeof(DVDPartitionInfo) * 256), 32);
    tmd = (ESTitleMeta*)OSAllocFromMEM1ArenaLo(OSRoundUp32B(sizeof(ESTitleMeta)), 64);
    t2 = (ESTicketView*)OSAllocFromMEM1ArenaLo(OSRoundUp32B(sizeof(ESTicketView)), 32);

    if (__OSNextPartitionType == __OSLaunchPartitionType && *(u32*)OSPhysicalToCached(0x3198)) {
        rc = ESP_InitLib();
        if (rc == 0)
            rc = ESP_DiGetTicketView(NULL, t2);
        if (rc == 0)
            rc = ESP_DiGetTmd(NULL, &numTmdBytes);
        if (rc == 0)
            rc = ESP_DiGetTmd(tmd, &numTmdBytes);
        ESP_CloseLib();

        if (OSPlayTimeIsLimited()) {
            __OSPlayTimeType type = OSPLAYTIME_PERMANENT;
            u32 remain = 0xffffffff;

            __OSGetPlayTime(t2, &type, &remain);

            if (remain == 0) {
                __OSWriteExpiredFlag();
                __OSReturnToMenuForError();
            }
        }
    }

    if (rc == 0) {
        bootGameInfo = partInfo;
        bootGameInfo->type = *((u32*)OSPhysicalToCached(0x3194));
        bootGameInfo->gamePartition = (DVDGamePartition*)*((u32*)OSPhysicalToCached(0x3198));
    } else {
        DVDLowIntType = 0;
        DVDLowClosePartition(Callback);

        while (!DVDLowIntType) {
        };

        CheckDVDLowIntType(DVDLowIntType);

        DVDLowIntType = 0;
        DVDLowUnencryptedRead(gameToc, OSRoundUp32B(sizeof(DVDGameTOC)), 0x40000 >> 2, Callback);

        while (!DVDLowIntType) {
        };

        CheckDVDLowIntType(DVDLowIntType);

        DVDLowIntType = 0;
        DVDLowUnencryptedRead(partInfo, OSRoundUp32B(sizeof(DVDPartitionInfo) * 256), (u32)gameToc->partitionInfos, Callback);

        while (!DVDLowIntType) {
        };

        CheckDVDLowIntType(DVDLowIntType);

        bootGameInfo = NULL;
        partInfoPtr = partInfo;
        for (i = 0; i < gameToc->numGamePartitions; i++) {
            if (partInfoPtr->type == __OSNextPartitionType) {
                bootGameInfo = partInfoPtr;
            }

            partInfoPtr++;
        }

        if (!bootGameInfo) {
            gameToc++;
            partInfoPtr = partInfo + 4;
            for (i = 0; i < gameToc->numGamePartitions; i++) {
                if (partInfoPtr->type == __OSNextPartitionType) {
                    bootGameInfo = partInfoPtr;
                }

                partInfoPtr++;
            }
        }

        if (!bootGameInfo) {
            OSReport("\nOSExec(): The specified game doesn't exist in the disc\n");
            __OSReturnToMenuForError();
        }

        *((u32*)OSPhysicalToCached(0x3194)) = bootGameInfo->type;
        *((u32*)OSPhysicalToCached(0x3198)) = (u32)bootGameInfo->gamePartition;

        DVDLowIntType = 0;
        if (__OSLockedFlag == 0x80) {
            DVDLowOpenPartitionWithTmdAndTicketView((u32)bootGameInfo->gamePartition, t2, numTmdBytes, tmd, 0, NULL, Callback);
        } else {
            DVDLowOpenPartition((u32)bootGameInfo->gamePartition, NULL, 0, 0, tmd, Callback);
        }

        while (!DVDLowIntType) {
        };

        CheckDVDLowIntType(DVDLowIntType);

        DVDLowIntType = 0;
        DVDLowClosePartition(Callback);

        while (!DVDLowIntType) {
        };

        CheckDVDLowIntType(DVDLowIntType);
    }

    version = tmd->head.sysVersion;

    rc = ESP_InitLib();
    if (rc != 0) {
        OSReport("\nOSExec(): Failed to exec %d in %d\n", rc, 0x3FD);
        __OSHotResetForError();
    }

    rc = ESP_GetTicketViews(version, NULL, &ticketCnt);
    if (ticketCnt != 1 || rc != 0) {
        OSReport("\nOSExec(): Failed to exec %d in %d\n", rc, 0x406);
        __OSHotResetForError();
    }

    t = (ESTicketView*)OSAllocFromMEM1ArenaLo(OSRoundUp32B(sizeof(ESTicketView) * ticketCnt), 32);
    rc = ESP_GetTicketViews(version, t, &ticketCnt);
    if (rc != 0) {
        OSReport("\nOSExec(): Failed to exec %d in %d\n", rc, 0x40F);
        __OSHotResetForError();
    }

    DVDLowFinalize();

    simGddr3Size = *(u32*)OSPhysicalToCached(0x311C);
    gddrProtAddr = *(u32*)OSPhysicalToCached(0x3120);
    DCStoreRange(OSPhysicalToCached(0x3100), 0x100);

    rc = ESP_LaunchTitle(version, t);
    if (rc != 0) {
        OSReport("\nOSExec(): Failed to exec %d in %d\n", rc, 0x41F);
        __OSHotResetForError();
    }

    ESP_CloseLib();

    DCInvalidateRange(OSPhysicalToCached(0x3100), 0x100);

    if (simGddr3Size < *(u32*)OSPhysicalToCached(0x311C)) {
        *(u32*)OSPhysicalToCached(0x3120) = simGddr3Size - (*(u32*)OSPhysicalToCached(0x311C) - *(u32*)OSPhysicalToCached(0x3120));
        *(u32*)OSPhysicalToCached(0x3128) = simGddr3Size - (*(u32*)OSPhysicalToCached(0x311C) - *(u32*)OSPhysicalToCached(0x3128));
        *(u32*)OSPhysicalToCached(0x3130) = simGddr3Size - (*(u32*)OSPhysicalToCached(0x311C) - *(u32*)OSPhysicalToCached(0x3130));
        *(u32*)OSPhysicalToCached(0x3134) = simGddr3Size - (*(u32*)OSPhysicalToCached(0x311C) - *(u32*)OSPhysicalToCached(0x3134));
        *(u32*)OSPhysicalToCached(0x311C) = simGddr3Size;
    }

    if (gddrProtAddr < *(u32*)OSPhysicalToCached(0x3120)) {
        __OSInitMemoryProtection();
    }

    __OSInitIPCBuffer();
    IPCReInit();
    IPCCltReInit();

    DVDLowInit();
    DVDLowIntType = 0;
    DVDLowReadDiskID(&id, Callback);

    while (!DVDLowIntType) {
    };

    CheckDVDLowIntType(DVDLowIntType);

    DVDLowIntType = 0;
    if (__OSLockedFlag == 0x80) {
        DVDLowOpenPartitionWithTmdAndTicketView((u32)bootGameInfo->gamePartition, t2, numTmdBytes, tmd, 0, NULL, Callback);
    } else {
        DVDLowOpenPartition((u32)bootGameInfo->gamePartition, NULL, 0, 0, tmd, Callback);
    }

    while (!DVDLowIntType) {
    };

    CheckDVDLowIntType(DVDLowIntType);
}

void __OSLaunchMenu(void) {
    s32 result;
    void* pviews;
    u32 count = 1;

    OSSetArenaLo((void*)0x81280000);
    OSSetArenaHi((void*)0x812F0000);

    if (ESP_InitLib() != IPC_RESULT_OK) {
        return;
    }

    // Get num ticket views
    result = ESP_GetTicketViews(MENU_TITLE_ID, NULL, &count);
    if (count != 1 || result != IPC_RESULT_OK) {
        return;
    }

    pviews = OSAllocFromMEM1ArenaLo((count * 0xD8 + 0x1F) & ~0x1F, 0x20);

    // Get ticket views
    if (ESP_GetTicketViews(MENU_TITLE_ID, pviews, &count) !=
        IPC_RESULT_OK) {
        return;
    }

    // Launch title
    if (ESP_LaunchTitle(MENU_TITLE_ID, pviews) != IPC_RESULT_OK) {
        return;
    }

    while (TRUE) {
        ;
    }
}

typedef struct {
    char date[16];
    u32 entry;
    u32 size;
    u32 rebootSize;
    u32 reserved2;
} ImgHeader;

typedef void (*AppInitFunc)(void (*report)(const char*, ...));
typedef BOOL (*AppGetNextFunc)(void**, u32*, u32*);
typedef void* (*AppGetEntryFunc)(void);
typedef void (*AppInterfaceFunc)(AppInitFunc*, AppGetNextFunc*, AppGetEntryFunc*);

extern void* BOOT_REGION_START : (0x812FDFF0);
extern void* BOOT_REGION_END : (0x812FDFEC);

static ImgHeader* LoadApploader(void) {
    ImgHeader* header;

    header = OSAllocFromMEM1ArenaLo(sizeof(ImgHeader), 32);
    ReadDisc(header, sizeof(ImgHeader), GetApploaderPosition());
    ReadDisc((void*)0x81200000, (s32)OSRoundUp32B(header->size), GetApploaderPosition() + (u32)(sizeof(ImgHeader) >> 2));
    ICInvalidateRange((void*)0x81200000, OSRoundUp32B(header->size));

    return header;
}

static void* LoadDol(const OSExecParams* params, AppInterfaceFunc getInterface) {
    AppInitFunc appInit;
    AppGetNextFunc appGetNext;
    AppGetEntryFunc appGetEntry;
    void* addr;
    u32 length;
    u32 offset;
    OSExecParams* paramsWork;

    getInterface(&appInit, &appGetNext, &appGetEntry);
    paramsWork = OSAllocFromMEM1ArenaLo(sizeof(OSExecParams), 1);
    __OSSetExecParams(params, paramsWork);
    appInit(OSReport);
    OSSetArenaLo(paramsWork);

    while (appGetNext(&addr, &length, &offset)) {
        ReadDisc(addr, (s32)length, (u32)(offset >> __DVDLayoutFormat));
    }

    return appGetEntry();
}

static void StartDol(const OSExecParams* params, void* entry) {
    OSExecParams* paramsWork;
    paramsWork = OSAllocFromMEM1ArenaLo(sizeof(OSExecParams), 1);
    __OSSetExecParams(params, paramsWork);
    __PIRegs[0x024 / 4] = 0x00000004 | 0x00000001 | 2;
    OSDisableInterrupts();
    Run(entry);
}

void __OSBootDolSimple(u32 doloffset, u32 restartCode, void* regionStart, void* regionEnd, BOOL argsUseDefault, s32 argc, char* argv[]) {
    OSExecParams* params;
    void* dolEntry;
    ImgHeader* header;
    DVDDiskID* diskId;

    OSDisableInterrupts();

    if (__OSInReboot) {
        __OSNextPartitionType = __OSLaunchPartitionType;
    }

    __OSRestoreCodeExecOnMEM1(0xBA2CF);

    params = OSAllocFromMEM1ArenaLo(sizeof(OSExecParams), 1);

    params->valid = TRUE;
    params->restartCode = restartCode;
    params->regionStart = regionStart;
    params->regionEnd = regionEnd;

    params->argsUseDefault = argsUseDefault;
    if (!argsUseDefault) {
        params->argsAddr = OSAllocFromMEM1ArenaLo(0x2000, 1);
        if (__OSNextPartitionType == 2 && !__OSInReboot) {
            PackInstallerArgs(params->argsAddr, argc, argv);
        } else {
            PackArgs(params->argsAddr, argc, argv);
        }
    }

    DVDInit();
    DVDSetAutoInvalidation(TRUE);
    DVDResume();

    Prepared = FALSE;
    __DVDPrepareResetAsync(callback);

    __OSMaskInterrupts(OS_INTR_MEM | OS_INTR_MASK(OS_INTR_MEM_ADDRESS) | OS_INTR_DSP | OS_INTR_MASK(OS_INTR_AI_AI) | OS_INTR_EXI | OS_INTR_PI);
    __OSUnmaskInterrupts(OS_INTR_MASK(OS_INTR_PI_ACR));
    OSEnableInterrupts();

    while (Prepared != TRUE) {
    }

    __OSLaunchNextFirmware();

    if (0xA0000000 == restartCode && !__OSInReboot) {
        ESTitleId titleId ALIGN(32);
        s32 rc;
        char* ptr;

        rc = ESP_InitLib();
        if (rc != 0) {
            return;
        }

        rc = ESP_GetTitleId(&titleId);
        if (rc != 0) {
            return;
        }

        rc = ESP_CloseLib();
        if (rc != 0) {
            return;
        }

        ptr = (char*)params->argsAddr + (u32)argv[1];
        snprintf(ptr, 16 + 1, "%016llx", titleId);
    }

    header = LoadApploader();

    if (IsNewApploader(header)) {
        if (doloffset == 0xffffffff) {
            doloffset = GetApploaderPosition() + ((sizeof(ImgHeader) + header->size) >> 2);
        }

        params->bootDol = doloffset;
        dolEntry = LoadDol(params, (AppInterfaceFunc)header->entry);
        diskId = (DVDDiskID*)OSPhysicalToCached(0x0000);
        *((u32*)OSPhysicalToCached(0x3180)) = *((u32*)diskId->game);
        *((u8*)OSPhysicalToCached(0x3184)) = 0x80;
        StartDol(params, dolEntry);
    } else {
        *(void**)BOOT_REGION_START = regionStart;
        *(void**)BOOT_REGION_END = regionEnd;
        *(u8*)(OS_BASE_CACHED | 0x30E2) = 1;

        ReadDisc((void*)0x81330000, (s32)OSRoundUp32B(header->rebootSize), (u32)(GetApploaderPosition() + ((sizeof(ImgHeader) + header->size) >> 2)));
        ICInvalidateRange((void*)0x81330000, OSRoundUp32B(header->rebootSize));
        OSDisableInterrupts();
        ICFlashInvalidate();
        Run((void*)0x81330000);
    }
}

void __OSBootDol(u32 doloffset, u32 restartCode, char* const argv[]) {
    char doloffInString[20];
    s32 argvlen;
    char** argvToPass;
    s32 i;
    void* saveStart;
    void* saveEnd;

    OSGetSaveRegion(&saveStart, &saveEnd);
    sprintf(doloffInString, "%d", doloffset);

    argvlen = 0;
    if (argv) {
        while (argv[argvlen]) {
            argvlen++;
        }
    }

    argvlen += 1;
    argvToPass = OSAllocFromMEM1ArenaLo((argvlen + 1) * sizeof(char*), 1);
    argvToPass[0] = doloffInString;

    for (i = 1; i < argvlen; i++) {
        argvToPass[i] = argv[i - 1];
    }

    __OSBootDolSimple(0xffffffff, restartCode, saveStart, saveEnd, FALSE, argvlen, argvToPass);
}

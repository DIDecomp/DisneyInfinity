#ifndef RVL_SDK_OS_EXEC_H
#define RVL_SDK_OS_EXEC_H
#include <revolution/types.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BOOL valid;
    u32 restartCode;
    u32 bootDol;
    void* regionStart;
    void* regionEnd;
    int argsUseDefault;
    void* argsAddr;
} OSExecParams;

extern u32 __OSNextPartitionType;
extern BOOL __OSInReboot;
extern BOOL Prepared;

void __OSLaunchNextFirmware(void);
void __OSGetExecParams(OSExecParams* out);
void __OSLaunchMenu(void);
void __OSBootDolSimple(u32 doloffset, u32 restartCode, void* regionStart, void* regionEnd, BOOL argsUseDefault, s32 argc, char* argv[]);
void __OSBootDol(u32 doloffset, u32 restartCode, char* const argv[]);

#ifdef __cplusplus
}
#endif
#endif

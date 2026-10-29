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

extern BOOL __OSInReboot;

void __OSGetExecParams(OSExecParams* out);
void __OSLaunchMenu(void);

#ifdef __cplusplus
}
#endif
#endif

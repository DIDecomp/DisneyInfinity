#ifndef RVL_SDK_ESP_H
#define RVL_SDK_ESP_H
#include <revolution/ARC.h>
#include <revolution/IPC.h>
#include <revolution/types.h>
#include <revolution/private/iostypes.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef u32 ESId;
typedef u32 ESContentId;
typedef u64 ESTitleId;
typedef u64 ESTicketId;
typedef u8 ESVersion;
typedef u16 ESTitleVersion;
typedef ESTitleId ESSysVersion;
typedef u32 ESTitleType;
typedef u16 ESContentType;
typedef u8 ESTmdReserved[62];
typedef u8 ESTicketReserved[48];
typedef u8 ESSysAccessMask[2];
typedef u8 ESCidxMask[64];

typedef struct {
    u32 code;
    u32 limit;
} ESLpEntry;

typedef struct {
    ESVersion version;
    ESTicketId ticketID;
    ESId devID;
    ESTitleId titleID;
    ESSysAccessMask sysAccessMask;
    u16 ticketVer;
    u32 accTitleID;
    u32 accTitleMask;
    u8 license;
    ESTicketReserved reserved;
    u8 audit;
    ESCidxMask cidxMask;
    ESLpEntry limits[8];
} ESTicketView;

typedef struct {
    ESContentId cid;
    u16 index;
    ESContentType type;
    u64 size;
    IOSCHash hash;
} ESContentMeta;

typedef struct {
    ESVersion version;
    ESVersion caCrlVersion;
    ESVersion signerCrlVersion;
    ESSysVersion sysVersion;
    ESTitleId titleId;
    ESTitleType type;
    u16 groupId;
    ESTmdReserved reserved;
    u32 accessRights;
    ESTitleVersion titleVersion;
    u16 numContents;
    u16 bootIndex;
} ESTitleMetaHeader;

typedef struct {
    IOSCSigRsa2048 sig;
    ESTitleMetaHeader head;
    ESContentMeta contents[512];
} ESTitleMeta;

typedef enum {
    ES_IOCTLV_LAUNCH_TITLE = 8,
    ES_IOCTLV_GET_NUM_TICKET_VIEWS = 18,
    ES_IOCTLV_GET_TICKET_VIEWS = 19,
    ES_IOCTLV_GET_DATA_DIR = 29,
    ES_IOCTLV_GET_TITLE_ID = 32,
} ESIoctl;

typedef struct {
    IOSCSigRsa2048 sig;
    IOSCEccPublicKey serverPubKey;
    ESVersion version;
    ESVersion caCrlVersion;
    ESVersion signerCrlVersion;
    IOSCAesKey titleKey;
    ESTicketId ticketId;
    ESId deviceId;
    ESTitleId titleId;
    ESSysAccessMask sysAccessMask;
    u16 ticketVersion;
    u32 accessTitleId;
    u32 accessTitleMask;
    u8 licenseType;
    ESTicketReserved reserved;
    u8 audit;
    ESCidxMask cidxMask;
    ESLpEntry limits[8];
} ESTicket;

typedef struct ESPTmd {
    u8 dummy[0x49E4];
    char padding[0x4A00 - 0x49E4];
} ESPTmd;

s32 ESP_ReadContentFile(UNKWORD, void*, u32);
s32 ESP_SeekContentFile(UNKWORD, s32, UNKWORD);

#ifdef __cplusplus
}
#endif
#endif

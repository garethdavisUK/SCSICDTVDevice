//Header file for device.c
#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dos.h>

#define DEVICE_NAME "cdtv.device"
#define DEVICE_DATE "(1 Jan 2026)"
#if DEBUG
//Bump the build version for a debug release so it can be loadmoduled over a ROM version
#define DEVICE_ID_STRING "newcdtv dev build" XSTR(DEVICE_VERSION) "." XSTR(DEVICE_REVISION) " " DEVICE_DATE
#define DEVICE_VERSION 99
#define DEVICE_REVISION 0
#else
#define DEVICE_ID_STRING "newcdtv " XSTR(DEVICE_VERSION) "." XSTR(DEVICE_REVISION) " " DEVICE_DATE
#define DEVICE_VERSION 38
#define DEVICE_REVISION 0
#endif

#define DEVICE_PRIORITY 3 

const char device_name[] = DEVICE_NAME;
const char device_id_string[] = DEVICE_ID_STRING;

//Prototypes
struct Library * init(struct ExecBase *SysBase asm("a6"), BPTR seg_list asm("a0"), struct devBase *db asm("d0"));
static BPTR expunge(struct devBase *db asm("a6"));
static void open(struct devBase *db asm("a6"), struct IORequest *ioreq asm("a1"), ULONG unitnum asm("d0"), ULONG flags asm("d1"));
static BPTR close(struct devBase *db asm("a6"), struct IORequest *ioreq asm("a1"));
static void beginIO(struct devBase *db asm("a6"), struct IORequest *ioreq asm("a1"));
static ULONG abortIO(struct devBase *db asm("a6"), struct IORequest *ioreq asm("a1"));









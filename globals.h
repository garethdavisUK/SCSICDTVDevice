#ifndef GLOBALS_H
#define GLOBALS_H

//Global definitions and includes
#include <exec/types.h>
#include <exec/ports.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <libraries/dos.h>
#include <libraries/dosextens.h>
#include <cdtv/cdtv.h>
#include <devices/scsidisk.h>
#include <devices/trackdisk.h>
#include <devices/timer.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include <clib/intuition_protos.h>
#include <string.h>

#define STR(s) #s          
#define XSTR(s) STR(s) 
#define DBGSTRING(s) "[cdtv] " XSTR(s) "\n"

#if DEBUG
#include <clib/debug_protos.h>
#define Dbg(s) KPrintF((CONST_STRPTR) DBGSTRING(s));
#define Dbgf(s) KPrintF s
#else
#define Dbg(s) 
#define Dbgf(s) 
#endif

#define CDTV_STARTTASK  0x7ff0
#define CDTV_TERM       0x7ff1


// Check for diskchange every x seconds
#define DISKCHANGE_CHECK_INTERVAL 5

// SCSI buffer sizes
#define BUFSIZE         255
#define SECTORBUFSIZE   2352
#define SENSESIZE       32

// Global structures
// Resident code cannot have writable global variables, an extended library structure is used to hold them instead 
// This is shared between the device and its handler task
struct devBase {
	struct Library      devNode;            // Usual dev structure
	BPTR                SegList;            // pointer to code 
	struct ExecBase     *SysBase;           // pointer to Exec 
    struct DOSBase      *DOSBase;           // pointer to DOS
    struct Task         *handlerTask;       // pointer to handler task 
    BOOL                initComplete;       // handler task ready flag

    struct MsgPort      *taskPort;          // Port used to syncronise device state 
    struct MsgPort 		*devPort;           // Blocking request message port
	struct MsgPort 		*nbdevPort;         // Non-blocking request message port
	struct IOStdReq 	*dev_ioReq;         // Current device request
    struct IOStdReq 	*blocking_ioReq;    // Current blocking device request
    
    BOOL                abortPending;       // AbortIO request received for current blocking IORequest

    struct MsgPort      *scsiPort;          // SCSI device messages 
    struct SCSICmd      scsiCmd;            // Blocking command
    struct IOStdReq     *scsiReq;
    struct SCSICmd      nbscsiCmd;          // Non blocking command
    struct IOStdReq     *nbscsiReq;
	struct SCSICmd      rdyscsiCmd;         // Device ready command
 	struct IOStdReq     *rdyscsiReq;

    // struct timerequest  *adhoc_timerReq;    // Clone of timer for adhoc use - no longer used
 
    // SCSI command buffer pointers
    UBYTE *buffer;
    UBYTE *nbbuffer;
    UBYTE *rdybuffer;
    UBYTE *sectorbuffer;			
    UBYTE *sense;
    UBYTE *nbsense;
    UBYTE *rdysense;

    //cdda related
    BOOL immediate;
    BOOL cdda_ioreq;
    struct IOStdReq *playcdda_ioReq;

    //Diskchange Interrupt related
    struct Interrupt *changeInt;
    struct IOStdReq *changeInt_ioReq;

    //hardware related
    BOOL driveready;
    BOOL lasterror;
    ULONG discchanges;
    struct CDTOC discSummary;
    struct RMSF discSummaryMSF;

    USHORT discblocksize;
    ULONG discblocks;
    ULONG framerate;
};


struct taskMessage {
    struct Message msg;
    int command;
};

// Global Prototypes

// alib.c amiga.lib replacements
void alib_NewList(struct List *new_list);
struct Task *alib_CreateTask(char * taskName, LONG priority, APTR funcEntry, ULONG stackSize, APTR userData, struct ExecBase *SysBase);
struct MsgPort *alib_CreatePort(STRPTR name, LONG pri);
void alib_DeletePort(struct MsgPort *mp);
struct IORequest* alib_CreateExtIO(struct MsgPort *mp, ULONG size);
struct IOStdReq* alib_CreateStdIO(struct MsgPort *mp);
void alib_DeleteExtIO(struct IORequest *ior);
void alib_DeleteStdIO(struct IOStdReq *ior);

// cdda.c
int driveSetImmediateMode(struct devBase * db,BOOL mode);
void cdtvPlayTrack(struct devBase * db, struct IOStdReq *iostd);
void cdtvPlayLSN(struct devBase * db, struct IOStdReq *iostd, BOOL poke);
void cdtvPlayMSF(struct devBase * db, struct IOStdReq *iostd, BOOL poke);
void cdtvPause(struct devBase * db, struct IOStdReq *iostd,  BOOL pause);
void driveStopPlayback(struct devBase * db);
int cdtvMute(struct devBase * db, struct IOStdReq *iostd, int value, int mode);
void abortCurrentPlay(struct devBase *db);

// dataio.c
void cdtvGetGeometry(struct devBase * db,struct IOStdReq *iostd);
void cdtvGetTOC(struct devBase * db,struct IOStdReq *iostd, BOOL msfmode);
void cdtvSeek(struct devBase * db,struct IOStdReq *iostd);
void cdtvSubQ(struct devBase * db,struct IOStdReq *iostd, BOOL msfmode);
int driveGetQSubChannel(struct devBase * db,BOOL msfmode);
void cdtvRead(struct devBase * db,struct IOStdReq *readReq);

// hardware.c
void driveInitSCSIstructure(struct devBase * db);
void driveInitSCSIstructure_nb(struct devBase * db);
BOOL isUnitReady(struct devBase * db);
void cdtvISROM(struct devBase * db,struct IOStdReq *iostd);
BOOL cdtvSetMotor(struct devBase * db,struct IOStdReq *iostd,BOOL start);
void cdtvInfo(struct devBase * db,struct IOStdReq *iostd);
void cdtvOptions(struct devBase * db,struct IOStdReq *iostd);
void hdScsiCmd(struct devBase * db,struct IOStdReq *iostd);
void setDriveSingleSpeed(struct devBase * db);
void DebugSCSIerror(BYTE error, struct SCSICmd *scsiCmd);

// task.c
void devHandler(void);

#endif
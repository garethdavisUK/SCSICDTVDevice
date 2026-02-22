// alib.c - amiga.lib for SCSI cdtv.device

// Following are replacements for amiga.lib routines that require a global SysBase 
// which isn't available in resident code

// Based on routines at https://github.com/LIV2/lide.device/blob/master/lide_alib.c

#include "globals.h"

void alib_NewList(struct List *new_list) {
    new_list->lh_Head = (struct Node *)&new_list->lh_Tail;
    new_list->lh_Tail = 0;
    new_list->lh_TailPred = (struct Node *)new_list;
}

struct Task *alib_CreateTask(char * taskName, LONG priority, APTR funcEntry, ULONG stackSize, APTR userData, struct ExecBase *SysBase) {

        stackSize = (stackSize + 3UL) & ~3UL;

        struct Task *task;

        struct {
            struct Node ml_Node;
            UWORD ml_NumEntries;
            struct MemEntry ml_ME[2];
        } alloc_ml = {
            .ml_NumEntries = 2,
            .ml_ME[0].me_Un.meu_Reqs = MEMF_PUBLIC|MEMF_CLEAR,
            .ml_ME[1].me_Un.meu_Reqs = MEMF_ANY|MEMF_CLEAR,
            .ml_ME[0].me_Length = sizeof(struct Task),
            .ml_ME[1].me_Length = stackSize
        };

        memset(&alloc_ml.ml_Node,0,sizeof(struct Node));

        struct MemList *ml = AllocEntry((struct MemList *)&alloc_ml);
        if ((ULONG)ml & 1<<31) {            
            Dbg("Couldn't allocate memory for task");
            return NULL;
        }

        task                  = ml->ml_ME[0].me_Un.meu_Addr;
        task->tc_SPLower      = ml->ml_ME[1].me_Un.meu_Addr;
        task->tc_SPUpper      = ml->ml_ME[1].me_Un.meu_Addr + stackSize;
        task->tc_SPReg        = task->tc_SPUpper;
        task->tc_UserData     = userData;
        task->tc_Node.ln_Name = taskName;
        task->tc_Node.ln_Type = NT_TASK;
        task->tc_Node.ln_Pri  = priority;
        alib_NewList(&task->tc_MemEntry);
        AddHead(&task->tc_MemEntry,(struct Node *)ml);

        AddTask(task,funcEntry,NULL);
        return task;
}

struct MsgPort *alib_CreatePort(STRPTR name, LONG pri) {
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;
    struct MsgPort *mp = NULL;
    BYTE sigNum;
    if ((sigNum = AllocSignal(-1)) >= 0) {
        if ((mp = AllocMem(sizeof(struct MsgPort),MEMF_CLEAR|MEMF_PUBLIC))) {
            mp->mp_Node.ln_Type = NT_MSGPORT;
            mp->mp_Node.ln_Pri  = pri;
            mp->mp_Node.ln_Name = (char *)name;
            mp->mp_SigBit       = sigNum;
            mp->mp_SigTask      = FindTask(0);

            alib_NewList(&mp->mp_MsgList);

            if (mp->mp_Node.ln_Name)
                AddPort(mp);
        }
    }
    return mp;

}

void alib_DeletePort(struct MsgPort *mp) {
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;

    if (mp) {
        if (mp->mp_Node.ln_Name)
            RemPort(mp);

        FreeSignal(mp->mp_SigBit);
        FreeMem(mp,sizeof(struct MsgPort));
    }
}

struct IORequest* alib_CreateExtIO(struct MsgPort *mp, ULONG size) {
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;
    struct IORequest *ior = NULL;

    if (mp) {
        if ((ior=AllocMem(size,MEMF_PUBLIC|MEMF_CLEAR))) {
            ior->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
            ior->io_Message.mn_Length       = size;
            ior->io_Message.mn_ReplyPort    = mp;
        }
    }

    return ior;
}

struct IOStdReq* alib_CreateStdIO(struct MsgPort *mp) {
    return (struct IOStdReq *)alib_CreateExtIO(mp,sizeof(struct IOStdReq));
}

void alib_DeleteExtIO(struct IORequest *ior) {
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;
    if (ior) {
        ior->io_Device = (APTR)-1;
        ior->io_Unit   = (APTR)-1;
        FreeMem(ior,ior->io_Message.mn_Length);
    }
}

void alib_DeleteStdIO(struct IOStdReq *ior) {
    alib_DeleteExtIO((struct IORequest *)ior);
}

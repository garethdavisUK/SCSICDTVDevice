// clearbookmark.c - SCSI and drive hardware interaction functions for SCSI cdtv.device
// Part of SCSI CDTV Device, an open source CDTV SCSI drive device driver - http://github.com/garethdavisuk/SCSICDTVDevice/
// Copyright (c) 2026 Gareth Davis. All new code released under GPL v2. See README in project root.

// Build with: m68k-amigaos-gcc clearbookmark.c -mcrt=nix13 -lamiga -o clearbookmark

#include <exec/types.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <cdtv/bookmark.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <stdio.h>

//Bookmark ID used to store device details 
#define BM_MFGID 22616 
#define BM_PRODID 254

ULONG myID = MAKEBID(BM_MFGID,BM_PRODID);

int main(void)
{
    struct MsgPort *BMReadPort = NULL;
    struct IOStdReq *BMIOReq = NULL;
    BYTE device_error;

    BMReadPort = CreatePort(NULL, 0);
    if (!BMReadPort)
    {
        printf("Error: Could not create message port.\n");
        return 10;
    }

    BMIOReq = CreateStdIO(BMReadPort);

    if (!BMIOReq)
    {
        printf("Error: Could not create I/O request.\n");
        DeletePort(BMReadPort);
        return 10;
    }

    device_error = OpenDevice("bookmark.device", myID, (struct IORequest*)BMIOReq, 0);
    
    if (device_error != 0)
    {
        printf("Error: Could not open bookmark.device (Error code: %ld)\n", (ULONG)device_error);
        DeleteStdIO(BMIOReq);
        DeletePort(BMReadPort);
        return 20;
    }

    //Delete any previous bookamrk
    BMIOReq->io_Command = BD_DELETE;
    BMIOReq->io_Data    = 0;
    BMIOReq->io_Length  = 0;
    BMIOReq->io_Offset  = 0;

    DoIO((struct IORequest*)BMIOReq); 

    if (BMIOReq->io_Error)
    {
        printf("Bookmark delete error: %d\n", BMIOReq->io_Error);
    }
    else
    {
        printf("Bookmark deleted\n");
    }

    //Clean up
    CloseDevice((struct IORequest*)BMIOReq);
    DeleteStdIO(BMIOReq);
    DeletePort(BMReadPort);

    return 0;
}
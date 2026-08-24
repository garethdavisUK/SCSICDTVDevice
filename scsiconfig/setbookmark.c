// setbookmark.c - SCSI and drive hardware interaction functions for SCSI cdtv.device
// Part of SCSI CDTV Device, an open source CDTV SCSI drive device driver - http://github.com/garethdavisuk/SCSICDTVDevice/
// Copyright (c) 2026 Gareth Davis. All new code released under GPL v2. See README in project root.

// Build with: m68k-amigaos-gcc setbookmark.c -mcrt=nix13 -lamiga -o setbookmark

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



    // Define the bookmark, first byte is unit number, followed by null terminated device string
    char bm_data[] = "\x05scsi.device\x00";

    //Delete any previous bookamrk
    BMIOReq->io_Command = BD_DELETE;
    BMIOReq->io_Data    = 0;
    BMIOReq->io_Length  = 0;
    BMIOReq->io_Offset  = 0;

    DoIO((struct IORequest*)BMIOReq); 

    //Don't care if this fails, as may not exist
    printf("Creating bookmark...\n");
    BMIOReq->io_Command = BD_CREATE;
    BMIOReq->io_Data    = 0;
    BMIOReq->io_Length  = sizeof(bm_data);
    BMIOReq->io_Offset  = myID;

    printf("Writing data to bookmark NVRAM...\n");
    DoIO((struct IORequest*)BMIOReq); 

    if (BMIOReq->io_Error)
    {
        printf("Create failed with error: %d\n", BMIOReq->io_Error);
    }
    else
    {
        printf("Create successful, writing bookmark\n");
    
            BMIOReq->io_Command = CMD_WRITE;
            BMIOReq->io_Data    = (APTR)&bm_data;
            BMIOReq->io_Length  = sizeof(bm_data);
            BMIOReq->io_Offset  = 0;

            printf("Writing data to bookmark NVRAM...\n");
            DoIO((struct IORequest*)BMIOReq); 

            if (BMIOReq->io_Error)
            {
                printf("Write failed with error: %d\n", BMIOReq->io_Error);
            }
            else
            {
                printf("Write successful.\n");

                //Try and read it back
                char bookmark[32] = "";
                BMIOReq->io_Command = CMD_READ;
                BMIOReq->io_Data    = (APTR)&bookmark;
                BMIOReq->io_Length  = -1;
                BMIOReq->io_Offset  = 0;
                DoIO((struct IORequest*)BMIOReq); 

                if (BMIOReq->io_Error)
                {
                    printf("Read failed with error: %d\n", BMIOReq->io_Error);
                }
                else
                {
                    printf("Read successful - device %s unit %ld\n",&bookmark[1],(ULONG)bookmark[0]);
                }

            }
        }

    //Clean up
    CloseDevice((struct IORequest*)BMIOReq);
    DeleteStdIO(BMIOReq);
    DeletePort(BMReadPort);

    return 0;
}
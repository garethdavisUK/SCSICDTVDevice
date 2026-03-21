//dataio.c - data I/O related functions for SCSI cdtv.device

#include "globals.h"
#include "dataio.h"


void cdtvGetGeometry(struct devBase * db,struct IOStdReq *iostd){ //non-blocking
	// A real CDTV does not document this call, and fails to populate structure despite not returning an error.
	// Implemnting this to allow other CDFS to work correctly
	struct DriveGeometry *cdgeom;	

	cdgeom=iostd->io_Data; // fetch pointer to geometry table
	
	// Populate trackdisk geometry table with values read at discchange 
	// The data blocks are all contained on a single side in a single logical track on a CD..
	cdgeom->dg_SectorSize = db->discblocksize; 	/* CD block size in bytes - usually 0x800 (2048)*/
	cdgeom->dg_TotalSectors = db->discblocks; 	/* magnetic sectors map directly to optical blocks */
	cdgeom->dg_Cylinders = 1;					/* number of cylinders */
	cdgeom->dg_CylSectors = db->discblocks; 	/* number of sectors/cylinder (same as total)*/
	cdgeom->dg_Heads = 1;						/* number of sides */
	cdgeom->dg_TrackSectors = db->discblocks;	/* number of sectors/track  (same as total)*/
	cdgeom->dg_BufMemType = MEMF_PUBLIC;		/* preferred buffer memory type */
	cdgeom->dg_DeviceType = DG_CDROM;			/* codes as defined in the SCSI-2 spec*/
	cdgeom->dg_Flags = DGF_REMOVABLE;			/* flags */
	cdgeom->dg_Reserved = 0;					/* reserved */
							         	
}

void cdtvGetTOC(struct devBase * db,struct IOStdReq *iostd,BOOL msfmode){
	struct CDTOC *toctableptr;
	int error, requested, disctocptr, memtocentry, toctablesize;

	struct ExecBase *SysBase = db->SysBase; // Restore Exec
	
	UBYTE SD_ReadTOC[]	= { 0x43,0,0,0,0,0,0,0,255,0}; // Presents 255 byte buffer = approx 30 tracks
	
	if (msfmode) SD_ReadTOC[1] = 2; 	// Enable MSF reporting
		
	requested=iostd->io_Length;
	toctableptr=iostd->io_Data;
	
	if (!requested){
			iostd->io_Error = CDERR_BADARG;
			return;
	}

	memtocentry=0;

	if (iostd->io_Offset == 0){
		//Media summary requested - can fetch this from memory

		if (db->discSummary.LastTrack==0xFF){
			// In memory TOC invalid
			iostd->io_Error = CDERR_BADTOC;
			return;			
		}
				
		toctableptr->AddrCtrl = db->discSummary.AddrCtrl;
		toctableptr->Track = db->discSummary.Track;
		toctableptr->LastTrack = db->discSummary.LastTrack;
		
		if (msfmode){
			toctableptr->Position.MSF.Minute = db->discSummaryMSF.Minute;
			toctableptr->Position.MSF.Second = db->discSummaryMSF.Second;
			toctableptr->Position.MSF.Frame = db->discSummaryMSF.Frame; 
		} else {
			toctableptr->Position.LSN = db->discSummary.Position.LSN;
		}
	
		memtocentry++;
		toctableptr++;
	}

	if (requested == 1){
		// Only the summary was requested
		iostd->io_Actual = 1;
		iostd->io_Error=0; //success
		return;
	}

	// Now read actual TOC entries

	driveInitSCSIstructure(db); 
	SD_ReadTOC[6] = iostd->io_Offset;				// Set start track	
	db->scsiCmd.scsi_Command=(UBYTE *)SD_ReadTOC;		// command to issue             
	db->scsiCmd.scsi_CmdLength = sizeof(SD_ReadTOC);	    

	Dbg("About to READ toc");

	error=DoIO( (struct IORequest *) db->scsiReq );			// send it to the device driver

	if (error){
		// SCSI command execution error
		iostd->io_Error = CDERR_BADTOC;
		return;
	}
	
	toctablesize=db->buffer[1];	
	disctocptr=4; // Skip over header in buffer
	
	while (disctocptr<toctablesize)
	{
		if (db->buffer[disctocptr+2] != 0xAA){ 			// Check this isn't the disk lead out

			toctableptr->AddrCtrl = db->buffer[disctocptr+1];
			toctableptr->Track = db->buffer[disctocptr+2];
			toctableptr->LastTrack = 0;
			if (msfmode){
				toctableptr->Position.MSF.Minute = db->buffer[disctocptr+5];
				toctableptr->Position.MSF.Second = db->buffer[disctocptr+6];
				toctableptr->Position.MSF.Frame = db->buffer[disctocptr+7]; 
			} else {
				toctableptr->Position.LSN = db->buffer[disctocptr+7] | (db->buffer[disctocptr+6] << 8) | (db->buffer[disctocptr+5] << 16) | (db->buffer[disctocptr+4] << 24);
			}

			if (memtocentry==(requested - 1)){
				// Passed TOC structure has been filled
				iostd->io_Actual = iostd->io_Length;
				iostd->io_Error=0; //success
				return;				
			}

			memtocentry++;
			toctableptr++;

			disctocptr+=8;

		} else { 	// This is the lead out - skip it
			disctocptr+=8;
		}
	}
	
	// Finished reading the table we could fit in memory - return what we have
	iostd->io_Actual = memtocentry;
	iostd->io_Error=0; //success
		 
}

void cdtvSeek(struct devBase * db,struct IOStdReq *iostd){
	UBYTE SD_Seek[]= { 0x2B,0,0,0,0,0,0,0,0,0}; 	
	ULONG blockaddress;
	int error;

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	blockaddress = iostd->io_Offset/db->discblocksize;
	SD_Seek[2] = (blockaddress & 0xff000000) >> 24;
	SD_Seek[3] = (blockaddress & 0x00ff0000) >> 16;
	SD_Seek[4] = (blockaddress & 0x0000ff00) >> 8;
	SD_Seek[5] = (blockaddress & 0x000000ff);

	driveInitSCSIstructure(db); 
	db->scsiCmd.scsi_Command=(UBYTE *)SD_Seek;		// command to issue             
	db->scsiCmd.scsi_CmdLength = sizeof(SD_Seek);	// length of the command        

    Dbgf(((CONST_STRPTR) "[cdtv] seek %lu\n",blockaddress));

	error=DoIO( (struct IORequest *) db->scsiReq );			// send it to the device driver

	if (error){
		// SCSI command execution error
		Dbg("seek fail");	
		iostd->io_Error = CDERR_ABORTED;
	}
		
}

void cdtvSubQ(struct devBase * db,struct IOStdReq *iostd,BOOL msfmode){
    //Packs disc Q subchennel data from nbbufer into a CDSubQ structure

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	struct CDSubQ *cdsubqptr;
    
	cdsubqptr=iostd->io_Data;
	

	if (driveGetQSubChannel(db,msfmode)){
		// SCSI command execution error
		iostd->io_Error = CDERR_ABORTED;
		return;
	}
	
	cdsubqptr->Status = db->nbbuffer[1];
	cdsubqptr->AddrCtrl = db->nbbuffer[5];
	cdsubqptr->Track  = db->nbbuffer[6]; 	
	cdsubqptr->Index = db->nbbuffer[7];

	if (msfmode){
		cdsubqptr->DiskPosition.MSF.Minute = db->nbbuffer[9];
		cdsubqptr->DiskPosition.MSF.Second = db->nbbuffer[10];
		cdsubqptr->DiskPosition.MSF.Frame = db->nbbuffer[11];
		cdsubqptr->TrackPosition.MSF.Minute = db->nbbuffer[13];
		cdsubqptr->TrackPosition.MSF.Second = db->nbbuffer[14];
		cdsubqptr->TrackPosition.MSF.Frame = db->nbbuffer[15];
	} else {
		cdsubqptr->DiskPosition.LSN =  db->buffer[11] | (db->nbbuffer[10] << 8) | (db->nbbuffer[9] << 16) | (db->nbbuffer[8] << 24);
		cdsubqptr->TrackPosition.LSN = db->buffer[15] | (db->nbbuffer[14] << 8) | (db->nbbuffer[13] << 16) | (db->nbbuffer[12] << 24);
	}	

	cdsubqptr->ValidUPC = 0; //Not supported on CDTV, so not checking here
	
	if (db->nbbuffer[1]==SQSTAT_DONE){
		// We've picked up the audio has ended before the polling loop has - deal with it now
		Dbg("read subq successful");
		db->cdda_ioreq = FALSE;
		db->playcdda_ioReq->io_Error = 0;
		ReplyMsg(&db->playcdda_ioReq->io_Message);	
	}
}

int driveGetQSubChannel(struct devBase * db,BOOL msfmode){
    //Fetches the Q subchannel data from the disc into nbbuffer

	BYTE error;
	UBYTE SD_ReadSubChannel[]= { 0x42,0,64,0,0,0,0,0,254,0};// Presents 255 byte buffer

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	if (msfmode) SD_ReadSubChannel[1]=2;
		
	driveInitSCSIstructure_nb(db); 
	db->nbscsiCmd.scsi_Command=(UBYTE *)SD_ReadSubChannel;		// command to issue             
	db->nbscsiCmd.scsi_CmdLength = sizeof(SD_ReadSubChannel);	// length of the command        

//	Dbg("fetching qsub");

	error=DoIO( (struct IORequest *) db->nbscsiReq );

	if (error){
		// SCSI command execution error
		Dbg("fetch qsub read fail");
		return(error);
	}

//	Dbgf(((CONST_STRPTR) "- have %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\r\n", nbbuffer[0],nbbuffer[1],nbbuffer[2],nbbuffer[3],nbbuffer[4],nbbuffer[5],nbbuffer[6],nbbuffer[7],nbbuffer[8],nbbuffer[9],nbbuffer[10],nbbuffer[11],nbbuffer[12],nbbuffer[13],nbbuffer[14],nbbuffer[15]));
//	Dbg("done");
	return (0);
}

void cdtvRead(struct devBase * db,struct IOStdReq *readReq, BOOL allowAbort){
	UBYTE SD_ReadCmd[]	= { 0x28,0,0,0,0,0,0,0,0,0};	
	ULONG startblock, bytesread, tocopy, remainbytes;
	USHORT blockstofetch, blocksfetched; // limitation of drive
	UWORD *iostdbufptr;
	int error;

	struct ExecBase *SysBase = db->SysBase; // Restore Exec
    
    ULONG lengthinbytes = readReq->io_Length;
	ULONG discaddress = readReq->io_Offset;
    ULONG lengthinblocks = lengthinbytes/db->discblocksize;
	startblock = discaddress/db->discblocksize;
	
    bytesread=0;
	tocopy=0;

	Dbgf(((CONST_STRPTR) "[cdtv] read address=0x%lx len=0x%lx\n",discaddress,lengthinbytes));

	if (lengthinbytes == 0){
		Dbg("zero read requested");
		readReq->io_Error=CDERR_NOTVALID;
		readReq->io_Actual=0;
		return;
	}

	if (lengthinblocks>0xFFFF){
		// Drive has maximum request size of 0xFFFF blocks (which = 32MB so shouldn't be a problem)
		Dbg(">32MB read request");
		readReq->io_Error=CDERR_NOTVALID;
		readReq->io_Actual=0;
		return;
	}

	iostdbufptr=(UWORD *)readReq->io_Data;
		
	db->scsiReq->io_Length  = sizeof(struct SCSICmd);
	db->scsiReq->io_Data    = (APTR)&db->scsiCmd;
	db->scsiReq->io_Command = HD_SCSICMD;
	db->scsiReq->io_Flags	= 0;

	db->scsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_READ;  													 
	db->scsiCmd.scsi_SenseData =(UBYTE *)db->sense;			     
	db->scsiCmd.scsi_SenseLength = SENSESIZE;			     

	db->scsiCmd.scsi_Command=(UBYTE *)SD_ReadCmd;		// command to issue             
	db->scsiCmd.scsi_CmdLength = sizeof(SD_ReadCmd);	// length of the command        
	db->scsiCmd.scsi_SenseActual = 0;					// reset received data count 
	
	
 	remainbytes=discaddress%db->discblocksize;

//		Dbg("read");
//		Dbgf(((CONST_STRPTR) "[cdtv] %lu / %lu remainder %lu\n",readReq->io_Offset,discblocksize,remainbytes));

	if (remainbytes !=0){
		//Starting part way through a block - load it into local buffer and copy that part first

		startblock = discaddress/db->discblocksize;

//		Dbgf(((CONST_STRPTR) "[cdtv] read part block start - block=0x%lx\n",startblock));

        SD_ReadCmd[2] = (startblock & 0xff000000) >> 24;
		SD_ReadCmd[3] = (startblock & 0x00ff0000) >> 16;
		SD_ReadCmd[4] = (startblock & 0x0000ff00) >> 8;
		SD_ReadCmd[5] = (startblock & 0x000000ff);
		SD_ReadCmd[7] = 0;
		SD_ReadCmd[8] = 1;

		db->scsiCmd.scsi_Data = (UWORD *)db->sectorbuffer;		  
		db->scsiCmd.scsi_Length = db->discblocksize;	
		
		Dbgf(((CONST_STRPTR) "[cdtv] partial read start from block 0x%lx bytes 0x%lx\n",startblock, remainbytes));

        error=DoIO( (struct IORequest *) db->scsiReq );

//		Dbg("read doneio\n");

        if (error){
			// SCSI command execution error
			Dbgf(((CONST_STRPTR) "[cdtv] read begin start error=%d\n",error));
			DebugSCSIerror(error, &db->scsiCmd);
			readReq->io_Error = CDERR_ABORTED;
			return;
		} 
		
        if (db->abortPending && allowAbort){
			// AbortIO signal received
			Dbg("Read start aborted");
			readReq->io_Error = CDERR_ABORTED;
			return;
		} 

	    // copy data from sector buffer to ioreq buffer

        //Calculate how many bytes to copy to ioreq buffer
		tocopy=db->discblocksize-remainbytes;
		if (tocopy>lengthinbytes) tocopy=lengthinbytes;

		CopyMem(db->sectorbuffer+remainbytes,iostdbufptr,tocopy);

		bytesread=tocopy;
		iostdbufptr+=tocopy;

//  	Dbg("read part block complete");

	}
	
	// There's a bug in the CDTV DMAC that means it sometimes doesn't complete a read from CD,
	// the developer docs advise to add READ_PAD_BYTES on to every request as a workaround. 
	// We shouldn't have the overhead of an additional block read if they are all that's left
	// as those last bytes are expected to be undefined.

	// Consider complete if READ_PAD_BYTES or fewer left.
	if ((lengthinbytes-bytesread)<=READ_PAD_BYTES){
		readReq->io_Actual=lengthinbytes;
		readReq->io_Error=0; //success
        // Dbg("done");
		return;
	}

	
	blockstofetch = (lengthinbytes - bytesread)/db->discblocksize;
	
	if (blockstofetch){
		// There are complete blocks to fetch
		startblock = (discaddress + bytesread)/db->discblocksize;
		blocksfetched = cdtvReadBlocks(db, startblock, blockstofetch, iostdbufptr);

		if (blocksfetched!=blockstofetch){
			// SCSI command execution error
			Dbgf(((CONST_STRPTR) "[cdtv] read 2 failed to fetch all blocks - fetched %d expected %d\n",blocksfetched, blockstofetch));
			readReq->io_Error = CDERR_ABORTED;
			return;
		}

        if (db->abortPending && allowAbort){
			// AbortIO signal received
			Dbg("Read blocks aborted");
			readReq->io_Error = CDERR_ABORTED;
			return;
		} 

		bytesread+=blocksfetched*db->discblocksize;
		iostdbufptr+=blocksfetched*db->discblocksize;

    } // end if blockstofetch
	
	// As before, consider complete if READ_PAD_BYTES or fewer left.
		if((lengthinbytes-bytesread)<=READ_PAD_BYTES){
		readReq->io_Actual=lengthinbytes;
		readReq->io_Error=0; //success
		return;
	}

	// Still bytes outstanding less than a block
	startblock = (discaddress + bytesread)/db->discblocksize;
	SD_ReadCmd[2] = (startblock & 0xff000000) >> 24;
	SD_ReadCmd[3] = (startblock & 0x00ff0000) >> 16;
	SD_ReadCmd[4] = (startblock & 0x0000ff00) >> 8;
	SD_ReadCmd[5] = (startblock & 0x000000ff);
	SD_ReadCmd[7] = 0;
	SD_ReadCmd[8] = 1;
	
	// Reset the SCSI structure as it may have just been used by cdtvReadBlocks changing the pointers
	db->scsiReq->io_Length  = sizeof(struct SCSICmd);
	db->scsiReq->io_Data    = (APTR)&db->scsiCmd;
	db->scsiReq->io_Command = HD_SCSICMD;
	db->scsiReq->io_Flags	= 0;

	db->scsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_READ;  													 
	db->scsiCmd.scsi_SenseData =(UBYTE *)db->sense;			     
	db->scsiCmd.scsi_SenseLength = SENSESIZE;			     

	db->scsiCmd.scsi_Command=(UBYTE *)SD_ReadCmd;		// command to issue             
	db->scsiCmd.scsi_CmdLength = sizeof(SD_ReadCmd);	// length of the command        
	db->scsiCmd.scsi_SenseActual = 0;					// reset received data count 

	db->scsiCmd.scsi_Data = (UWORD *)db->sectorbuffer;		  
	db->scsiCmd.scsi_Length = db->discblocksize;	
	
	Dbgf(((CONST_STRPTR) "[cdtv] partial read end from block 0x%lx bytes 0x%lx\n",startblock, (lengthinbytes-bytesread)));
    
	error=DoIO( (struct IORequest *) db->scsiReq );
	
	if (error){
		// SCSI command execution error
		Dbgf(((CONST_STRPTR) "[cdtv] read end failed error=%d\n",error));
		DebugSCSIerror(error, &db->scsiCmd);
		readReq->io_Error = CDERR_ABORTED;
		return;
	} 
	
	if (db->abortPending && allowAbort){
		// AbortIO signal received
		Dbg("Read end aborted");
		readReq->io_Error = CDERR_ABORTED;
		return;
	} 

	// copy data from sector buffer to ioreq buffer
	tocopy=lengthinbytes-bytesread;
	CopyMem(db->sectorbuffer,iostdbufptr,tocopy);

	bytesread+=tocopy;
	readReq->io_Actual=bytesread;
	readReq->io_Error=0; //success
	
}

USHORT cdtvReadBlocks(struct devBase * db, ULONG startblock, USHORT blockstofetch, APTR readbufptr){
	UBYTE SD_ReadCmd[]	= { 0x28,0,0,0,0,0,0,0,0,0};	
	int error;

	struct ExecBase *SysBase = db->SysBase; // Restore Exec
    
	SD_ReadCmd[2] = (startblock & 0xff000000) >> 24;
	SD_ReadCmd[3] = (startblock & 0x00ff0000) >> 16;
	SD_ReadCmd[4] = (startblock & 0x0000ff00) >> 8;
	SD_ReadCmd[5] = (startblock & 0x000000ff);
		
	SD_ReadCmd[7] = (blockstofetch & 0xff00) >> 8;
	SD_ReadCmd[8] = (blockstofetch & 0x00ff);	

	//Dbgf(((CONST_STRPTR) "[cdtv] read full blocks begin - 0x%lx\n",blockstofetch));

	db->scsiReq->io_Length  = sizeof(struct SCSICmd);
	db->scsiReq->io_Data    = (APTR)&db->scsiCmd;
	db->scsiReq->io_Command = HD_SCSICMD;
	db->scsiReq->io_Flags	= 0;

	db->scsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_READ;  													 
	db->scsiCmd.scsi_SenseData =(UBYTE *)db->sense;			     
	db->scsiCmd.scsi_SenseLength = SENSESIZE;			     

	db->scsiCmd.scsi_Command=(UBYTE *)SD_ReadCmd;		// command to issue             
	db->scsiCmd.scsi_CmdLength = sizeof(SD_ReadCmd);	// length of the command        
	db->scsiCmd.scsi_SenseActual = 0;					// reset received data count 

	// Set SCSI to write to read buffer
	db->scsiCmd.scsi_Data = readbufptr;
	db->scsiCmd.scsi_Length = blockstofetch * db->discblocksize;					      

	Dbgf(((CONST_STRPTR) "[cdtv] reading 0x%lx blocks from block 0x%lx\n",blockstofetch, startblock));

	error=DoIO( (struct IORequest *) db->scsiReq );		// send it to the device driver

	if (error){
		// SCSI command execution error
		Dbgf(((CONST_STRPTR) "[cdtv] read blocks failed error=%d\n",error));
		DebugSCSIerror(error, &db->scsiCmd);
		return 0;
	} 
	

	return (USHORT)db->scsiCmd.scsi_Actual/db->discblocksize;

}




void cdtvReadXL(struct devBase * db,struct IOStdReq *readReq){

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	struct IOStdReq readDataReq; // IOStdReq structure to use for the individual reads of each node in the list
	struct Interrupt completeInt;

	register APTR *regA2 asm ("a2");
	
	ULONG startaddress, bytesread, byteslength;
	startaddress = readReq->io_Offset*db->discblocksize;
	bytesread=0;
	byteslength=readReq->io_Length*db->discblocksize;
	//byteslength=readReq->io_Length;

	// List of CDXL read requests - each node contains a CDXLread structure with the details of the read to perform and where to put the data. 
	struct MinList *CDXLlist = (struct MinList *)readReq->io_Data; 
	struct CDXL *currentNode;

	Dbgf(((CONST_STRPTR) "[cdtv] readxl start 0x%lx blocks 0x%lx bytes 0x%lx\n",readReq->io_Offset, readReq->io_Length, byteslength));

	//Read through the list items
	for ( currentNode = (struct CDXL *)CDXLlist->mlh_Head ; currentNode->Node.mln_Succ != NULL ; currentNode = (struct CDXL *)currentNode->Node.mln_Succ ) {

		Dbgf(((CONST_STRPTR) "[cdtv] readxl node ptr 0x%lx succ 0x%lx buf 0x%lx len 0x%lx total 0x%lx\n",currentNode, currentNode->Node.mln_Succ, currentNode->Buffer, currentNode->Length, bytesread));

		if (currentNode->Buffer==NULL && currentNode->Length!=0){
			// Invalid node - buffer is null but length is not zero
			Dbg("ReadXL invalid node - null buffer with non-zero length");
			readReq->io_Error = CDERR_BADARG;
			return;
		}

		readDataReq.io_Offset = startaddress + bytesread;
		readDataReq.io_Length = currentNode->Length;	
		readDataReq.io_Data = currentNode->Buffer;
		readDataReq.io_Error = 0;
		readDataReq.io_Actual = 0;

		if (currentNode->Length!=0) cdtvRead(db,&readDataReq, FALSE);

		bytesread += readDataReq.io_Actual;
		currentNode->Actual = readDataReq.io_Actual;

		if (readDataReq.io_Error != 0){
			// ioError received
			Dbg("ReadXL ioError");
			readReq->io_Error = CDERR_ABORTED;
			return;
		} 

		if (currentNode->DoneFunc != NULL){
			// This node has a completion function - trigger it now that the read is done
			completeInt.is_Node.ln_Type = NT_UNKNOWN;
			completeInt.is_Code = currentNode->DoneFunc; // Trigger the complete function
			completeInt.is_Data = NULL; // No data to pass, just want to trigger the interrupt
			completeInt.is_Node.ln_Pri = 0; 	
			completeInt.is_Node.ln_Name = (char *)(STRPTR)"CDTVReadXLCompleteInt";
			regA2 = (APTR)(currentNode); // Pass pointer to current node in A2 as argument to the complete function 
			Dbgf(((CONST_STRPTR) "[cdtv] callback 0x%lx\n",regA2));
			Cause(&completeInt);
		}

		if (bytesread >= byteslength){
			// We've read as much as was requested - return now
			Dbg("ReadXL done");
			db->blocking_ioReq->io_Error = 0;
			db->blocking_ioReq->io_Actual = bytesread;
			return;			
		}


		if (db->abortPending){
			// AbortIO signal received
			Dbg("ReadXL aborted");
			db->blocking_ioReq->io_Error = CDERR_ABORTED;
			db->blocking_ioReq->io_Actual = bytesread;
			return;
		} 

	}

	Dbg("ReadXL end");
	db->blocking_ioReq->io_Error = 0;
	db->blocking_ioReq->io_Actual = bytesread;
	return;
}
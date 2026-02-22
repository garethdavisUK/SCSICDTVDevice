//hardware.c - SCSI and drive hardware interaction functions for SCSI cdtv.device

#include "globals.h"
#include "hardware.h"

void driveInitSCSIstructure(struct devBase * db){
	// Sets up SCSI command structure for db->scsiReq 

	db->scsiReq->io_Length  = sizeof(struct SCSICmd);
	db->scsiReq->io_Data    = (APTR)&db->scsiCmd;
	db->scsiReq->io_Command = HD_SCSICMD;
	db->scsiReq->io_Flags	= 0;

	db->scsiCmd.scsi_Data = (UWORD *)db->buffer;			  
	db->scsiCmd.scsi_Length = BUFSIZE;					      
	db->scsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_READ;  
													 
	db->scsiCmd.scsi_SenseData =(UBYTE *)db->sense;			     
	db->scsiCmd.scsi_SenseLength = SENSESIZE;			     
	db->scsiCmd.scsi_SenseActual = 0;
}

void driveInitSCSIstructure_nb(struct devBase * db){
	db->nbscsiReq->io_Length  = sizeof(struct SCSICmd);
	db->nbscsiReq->io_Data    = (APTR)&db->nbscsiCmd;
	db->nbscsiReq->io_Command = HD_SCSICMD;
	db->nbscsiReq->io_Flags	= 0;

	db->nbscsiCmd.scsi_Data = (UWORD *)db->nbbuffer;			  
	db->nbscsiCmd.scsi_Length = BUFSIZE;					      
	db->nbscsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_READ;  
													 
	db->nbscsiCmd.scsi_SenseData =(UBYTE *)db->nbsense;			     
	db->nbscsiCmd.scsi_SenseLength = SENSESIZE;			            
	db->nbscsiCmd.scsi_SenseActual = 0;	
}

BOOL isUnitReady(struct devBase * db)
{
	UBYTE SD_UnitReady[] = { 0,0,0,0,0,0 }; 
	UBYTE SD_ReadTOC[]	= { 0x43,0,0,0,0,0,0xAA,0,255,0}; // Request TOC leadout with 255 byte buffer
	UBYTE SD_ReadCapacity[]= { 0x25,0,0,0,0,0,0,0,0,0};

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	//This uses it's own request structure and buffer

	int error;
	
	// Uses own structures and buffers as may be called concurrently to other commands by timer interrupt
	
	db->rdyscsiReq->io_Length  = sizeof(struct SCSICmd);
	db->rdyscsiReq->io_Data    = (APTR)&db->rdyscsiCmd;
	db->rdyscsiReq->io_Command = HD_SCSICMD;
	db->rdyscsiReq->io_Flags	= 0;

	db->rdyscsiCmd.scsi_Data = (UWORD *)db->rdybuffer;			  
	db->rdyscsiCmd.scsi_Length = BUFSIZE;					      
	db->rdyscsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_READ;  
													 
	db->rdyscsiCmd.scsi_SenseData =(UBYTE *)db->rdysense;			     
	db->rdyscsiCmd.scsi_SenseLength = SENSESIZE;			     

	db->rdyscsiCmd.scsi_Command=(UBYTE *)SD_UnitReady;		// command to issue             
	db->rdyscsiCmd.scsi_CmdLength = sizeof(SD_UnitReady);	// length of the command        
	db->rdyscsiCmd.scsi_SenseActual = 0;						// reset received data count    

	error=DoIO( (struct IORequest *) db->rdyscsiReq );			// send it to the device driver

	if (error){
		// Not Ready

		if (db->driveready) { //If previously ready, increment change counter
			db->discSummary.LastTrack=0xFF; // Invalidate disc summary
			db->driveready=FALSE;
			db->discchanges++;
			
			if (db->cdda_ioreq){
            	Dbg("audio playback aborted by diskchange");
				db->cdda_ioreq = FALSE;
				db->playcdda_ioReq->io_Error = CDERR_ABORTED;
				ReplyMsg(&db->playcdda_ioReq->io_Message);	
			}

			// Fire diskchange interrupt if set
			if (db->changeInt) Cause (db->changeInt);
		}			
		return (FALSE);
	} 			       
	

	// Command was successful - so unit is ready - update discSummary from TOC if required 	
	if (db->discSummary.LastTrack==0xFF){
		
		db->rdyscsiCmd.scsi_Command=(UBYTE *)SD_ReadTOC;		// command to issue             
		db->rdyscsiCmd.scsi_CmdLength = sizeof(SD_ReadTOC);	// length of the command        
		db->rdyscsiCmd.scsi_SenseActual = 0;		

		error=DoIO( (struct IORequest *) db->rdyscsiReq );			// send it to the device driver

		if (error){
			// Mark TOC as invalid (only 99 tracks possible)
			db->discSummary.LastTrack=0xFF;

            Dbg("TestUnitReady - updating TOC - error");
		}


		// Update disc summary in memory
		db->discSummary.AddrCtrl = db->rdybuffer[1];
		db->discSummary.Track = db->rdybuffer[2];
		db->discSummary.LastTrack = db->rdybuffer[3];
		db->discSummary.Position.LSN = db->rdybuffer[11] | (db->rdybuffer[10] << 8) | (db->rdybuffer[9] << 16) | (db->rdybuffer[8] << 24);

		
		// Get the MSF version
		
		SD_ReadTOC[1] = 2;
		db->rdyscsiCmd.scsi_Command=(UBYTE *)SD_ReadTOC;		// command to issue             
		db->rdyscsiCmd.scsi_CmdLength = sizeof(SD_ReadTOC);	// length of the command        
		db->rdyscsiCmd.scsi_SenseActual = 0;		

		error=DoIO( (struct IORequest *) db->rdyscsiReq );			// send it to the device driver

		if (error){
			// Mark TOC as invalid (only 99 tracks possible)
			db->discSummary.LastTrack=0xFF;
            Dbg("TestUnitReady - updating MSF TOC - error");
		}

		db->discSummaryMSF.Minute = db->rdybuffer[9];
		db->discSummaryMSF.Second = db->rdybuffer[10];
		db->discSummaryMSF.Frame = db->rdybuffer[11]; 

		// Now get the disc capacity    

		db->rdyscsiCmd.scsi_Command=(UBYTE *)SD_ReadCapacity;		// command to issue             
		db->rdyscsiCmd.scsi_CmdLength = sizeof(SD_ReadCapacity);	// length of the command        
		db->rdyscsiCmd.scsi_SenseActual = 0;						// reset received data count    

        error=DoIO( (struct IORequest *) db->rdyscsiReq );			// send it to the device driver

		if (error){
                Dbg("TestUnitReady - updating capacity error");
		}          
		
		db->discblocks = db->rdybuffer[3]| (db->rdybuffer[2] << 8) | (db->rdybuffer[1] << 16) | (db->rdybuffer[0] << 24);
		db->discblocksize = db->rdybuffer[7] | (db->rdybuffer[6] << 8) | (db->rdybuffer[5] << 16) | (db->rdybuffer[4] << 24);
		
		db->framerate=75; // Probably should read this off of the drive, but it's unlikely to be different.
	}

	if (!db->driveready) { // if previously not ready, increment counter
		db->driveready=TRUE;
		db->discchanges++;
		
		// Fire diskchange interrupt if set
		if (db->changeInt) Cause (db->changeInt); 
	}				

	return (TRUE);
}

void cdtvISROM(struct devBase * db,struct IOStdReq *iostd){
	UBYTE SD_SensePage0Eh[]= { 0x1a,8,0x0E,0,254,0};	
	int error;
	
	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	// Media type held in Page Sense reply
	driveInitSCSIstructure_nb(db); 
	db->nbscsiCmd.scsi_Command=(UBYTE *)SD_SensePage0Eh;		// command to issue             
	db->nbscsiCmd.scsi_CmdLength = sizeof(SD_SensePage0Eh);	// length of the command        

	error=DoIO( (struct IORequest *) db->nbscsiReq );			// send it to the device driver


	if (error){
		// SCSI command execution error
		iostd->io_Error = CDERR_ABORTED;
	} else {
		iostd->io_Error=0; //success
		 
		iostd->io_Actual=0; // Not CDROM 
		if (db->nbbuffer[1]==1 || db->nbbuffer[1]==3) iostd->io_Actual=1; //120mm CDROM or CDROM+AUDIO
		if (db->nbbuffer[1]==5 || db->nbbuffer[1]==7) iostd->io_Actual=1; //80mm CDROM or CDROM+AUDIO
	}					         	
}

void cdtvOptions(struct devBase * db,struct IOStdReq *iostd){
	//This is undocumented, so log to debug and return success for now
	switch (iostd->io_Offset){
		case CDTV_OPTIONS_BLOCK_SIZE:
			Dbgf(((CONST_STRPTR) "[cdtv] CDTV_OPTIONS_BLOCK_SIZE data=%lx length=%lx\n",iostd->io_Data,iostd->io_Length));
			break;
		case CDTV_OPTIONS_ERROR_TYPE:
			Dbgf(((CONST_STRPTR) "[cdtv] CDTV_OPTIONS_ERROR_TYPE data=%lx length=%lx\n",iostd->io_Data,iostd->io_Length));
			break;
		default:
			Dbgf(((CONST_STRPTR) "[cdtv] unknown option=%lx data=%lx length=%lx\n",iostd->io_Offset,iostd->io_Data,iostd->io_Length));
			break;
	}

	iostd->io_Error=0; //success
	iostd->io_Actual=0;
}

BOOL cdtvSetMotor(struct devBase * db,struct IOStdReq *iostd,BOOL start){

	UBYTE SD_StartStopUnit[]= { 0x1B,0,0,0,0,0}; 	
	int error;
	
	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	if (db->immediate) SD_StartStopUnit[1]=1;
	if (start) SD_StartStopUnit[4]=1;
	

	driveInitSCSIstructure(db); 
	db->scsiCmd.scsi_Command=(UBYTE *)SD_StartStopUnit;		// command to issue             
	db->scsiCmd.scsi_CmdLength = sizeof(SD_StartStopUnit);	// length of the command        

	error=DoIO( (struct IORequest *) db->scsiReq );			// send it to the device driver


	if (error){
		// SCSI command execution error
		Dbg("motor control fail");
		iostd->io_Error = CDERR_ABORTED;
		return(FALSE);
	}
		
	return(TRUE); //No easy way to get previous motor status from Pioneer drive - report as previously running
}

void cdtvInfo(struct devBase * db,struct IOStdReq *iostd){
	switch(iostd->io_Offset){
		case CDTV_INFO_BLOCK_SIZE:
			iostd->io_Actual = db->discblocksize;
			break;
		case CDTV_INFO_FRAME_RATE:
			iostd->io_Actual = db->framerate;
			break;
		default:
			iostd->io_Error = CDERR_NOTVALID;
			break;
	}
}

void hdScsiCmd(struct devBase * db,struct IOStdReq *iostd){
   	// Absolutely not part of the device spec, but may be useful for doing things not in
	// the CDTV interface, like accessing subcode data for CD-TEXT or setting drive speed.	

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	db->scsiReq->io_Command = HD_SCSICMD;
	db->scsiReq->io_Data    = iostd->io_Data;
	db->scsiReq->io_Length  = iostd->io_Length;
	db->scsiReq->io_Offset  = iostd->io_Offset;
	db->scsiReq->io_Flags	= 0;
	
	Dbg("HD_SCSICMD");

	DoIO( (struct IORequest *) db->scsiReq );	

	iostd->io_Error = db->scsiReq->io_Error;
	iostd->io_Actual = db->scsiReq->io_Actual;
}

void setDriveSingleSpeed(struct devBase * db){
	
	UBYTE SD_SetSingleSpeed[]= { 0xDA,0,1,0,0,0,0,0,0,0,0,0}; 	
	int error;
	
	struct ExecBase *SysBase = db->SysBase; // Restore Exec	

	driveInitSCSIstructure(db); 
	db->scsiCmd.scsi_Command=(UBYTE *)SD_SetSingleSpeed;		// command to issue             
	db->scsiCmd.scsi_CmdLength = sizeof(SD_SetSingleSpeed);		// length of the command        

	error=DoIO( (struct IORequest *) db->scsiReq );				// send it to the device driver


	if (error){
		// SCSI command execution error - although we don't really care :)
		DebugSCSIerror(error, &db->nbscsiCmd);
		Dbg("set single speed failed");
	}

	return;
}
#if DEBUG
void DebugSCSIerror(BYTE error, struct SCSICmd *scsiCmd)
{	
	switch (error)
	{
		case HFERR_SelfUnit:
			Dbg("SCSI error: Cannot issue command to self");
			break;
		case HFERR_DMA:
			Dbg("SCSI error: DMA error");
			break;
		case HFERR_Phase:
			Dbg("SCSI error: Illegal / unexpected SCSI phase");
			break;
		case HFERR_Parity:
			Dbg("SCSI error: SCSI Parity error");
			break;
		case HFERR_SelTimeout:
			Dbg("SCSI error: Select timeout");
			break;
		case HFERR_BadStatus:
			Dbg("SCSI error: Status and/or sense error returned");

			if (scsiCmd->scsi_SenseActual == 0)
			{
				Dbg("No sense data returned");
			} else {
				Dbgf(((CONST_STRPTR)"Dumping 0x%x bytes of sense data: \n",scsiCmd->scsi_SenseActual));
				for (int i=0; i<scsiCmd->scsi_SenseActual; i++)
				     Dbgf(((CONST_STRPTR)"0x%x ",scsiCmd->scsi_SenseData[i]));
				Dbgf(((CONST_STRPTR)"\n"));
			}
			break;
		default:
			Dbgf(((CONST_STRPTR)"Unknown SCSI error: 0x%x\n",error));
			break;			
	}			
}
#else 
void DebugSCSIerror(BYTE error, struct SCSICmd *scsiCmd){}; //Do nothing
#endif
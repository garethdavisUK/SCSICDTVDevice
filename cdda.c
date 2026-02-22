//cdda.c - CDDA related functions for SCSI cdtv.device

#include "globals.h"
#include "cdda.h"

int driveSetImmediateMode(struct devBase * db, BOOL mode){

	BYTE error;
	UBYTE SD_SensePage0Eh[]= { 0x1a,8,0x0E,0,254,0};// Presents 255 byte buffer
	UBYTE SD_SelectPage0Eh[]= { 0x15,16,0,0,20,0}; 	// Send 20 bytes from buffer
	
	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	db->immediate = mode;
		
	driveInitSCSIstructure_nb(db); 
	db->nbscsiCmd.scsi_Command=(UBYTE *)SD_SensePage0Eh;		// command to issue             
	db->nbscsiCmd.scsi_CmdLength = sizeof(SD_SensePage0Eh);	// length of the command        

	error=DoIO( (struct IORequest *) db->nbscsiReq );

	if (error){
		// SCSI command execution error
		Dbg("immediate get fail");
		return(error);
	}
		
	if (mode) db->nbbuffer[6] = 4;	//set immediate and stop on track crossing off.
		else db->nbbuffer[6] = 0;	//clear immediate and stop on track crossing off.
		
	// Write back modified table
	db->nbscsiCmd.scsi_Command=(UBYTE *)SD_SelectPage0Eh;		
	db->nbscsiCmd.scsi_CmdLength = sizeof(SD_SelectPage0Eh);	
	db->nbscsiCmd.scsi_SenseActual = 0;							
	db->nbscsiCmd.scsi_Length = 20;								
	db->nbscsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_WRITE;		
														
	error=DoIO( (struct IORequest *) db->nbscsiReq );			

	if (error){
		// SCSI command execution error
		Dbg("immediate set fail");	
		return (error);
	}

	return (0);
}

void cdtvPlayTrack(struct devBase * db, struct IOStdReq *iostd){

	UBYTE SD_PlayTrackIndex[]= { 0x48,0,0,0,0,0,0,0,0,0}; 	
	int error;

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	if (!db->immediate) error=driveSetImmediateMode(db,TRUE);

	SD_PlayTrackIndex[4] = iostd->io_Offset; // Start track
	SD_PlayTrackIndex[5] = (iostd->io_Offset >> 16); // index
	if (iostd->io_Length == 0) SD_PlayTrackIndex[7] = SD_PlayTrackIndex[4]+1; 
		else SD_PlayTrackIndex[7] = iostd->io_Length;
	SD_PlayTrackIndex[8] = (iostd->io_Length >> 16); // index;

	driveInitSCSIstructure(db); 
	db->scsiReq->io_Length  = sizeof(struct SCSICmd);
	db->scsiReq->io_Data    = (APTR)&db->scsiCmd;
	db->scsiReq->io_Command = HD_SCSICMD;
	db->scsiReq->io_Flags	= 0;

	db->scsiCmd.scsi_Data = (UWORD *)db->buffer;			  
	db->scsiCmd.scsi_Length = BUFSIZE;					      
	db->scsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_READ;  
													 
	db->scsiCmd.scsi_SenseData =(UBYTE *)db->sense;			     
	db->scsiCmd.scsi_SenseLength = SENSESIZE;			     

	db->scsiCmd.scsi_Command=(UBYTE *)SD_PlayTrackIndex;		// command to issue             
	db->scsiCmd.scsi_CmdLength = sizeof(SD_PlayTrackIndex);	// length of the command        

	Dbgf(((CONST_STRPTR) "[cdtv] playtrack offset %ld length %ld\n",iostd->io_Offset,iostd->io_Length));

	error=DoIO( (struct IORequest *) db->scsiReq );			//Drive in immediate mode, so request returns once play started successfully 
	if (error){
		// SCSI command execution error
		Dbgf(((CONST_STRPTR) "[cdtv] play failed %d\n",error));
		DebugSCSIerror(error, &db->scsiCmd);
		iostd->io_Error = CDERR_ABORTED;
	}

	// Store the ioReq pointer for later, and start cdda polling
	db->playcdda_ioReq = iostd;
	db->cdda_ioreq=TRUE;
	db->abortPending = FALSE;

	// Result monitored in unit ready polling loop
	
}

void cdtvPlayLSN(struct devBase * db, struct IOStdReq *iostd, BOOL poke){

	UBYTE SD_PlayLSN[]= { 0xA5,0,0,0,0,0,0,0,0,0,0,0}; 	
	int error;
	
	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	SD_PlayLSN[2] = (iostd->io_Offset & 0xff000000) >> 24;
	SD_PlayLSN[3] = (iostd->io_Offset & 0x00ff0000) >> 16;
	SD_PlayLSN[4] = (iostd->io_Offset & 0x0000ff00) >> 8;
	SD_PlayLSN[5] = (iostd->io_Offset & 0x000000ff);

	SD_PlayLSN[6] = (iostd->io_Length & 0xff000000) >> 24;
	SD_PlayLSN[7] = (iostd->io_Length & 0x00ff0000) >> 16;
	SD_PlayLSN[8] = (iostd->io_Length & 0x0000ff00) >> 8;
	SD_PlayLSN[9] = (iostd->io_Length & 0x000000ff);

	error=driveSetImmediateMode(db,TRUE);

	driveInitSCSIstructure(db); 
	db->scsiCmd.scsi_Command=(UBYTE *)SD_PlayLSN;		// command to issue             
	db->scsiCmd.scsi_CmdLength = sizeof(SD_PlayLSN);	// length of the command        

    Dbgf(((CONST_STRPTR) "[cdtv] playtrack %d sent\n",iostd->io_Offset));

	error=DoIO( (struct IORequest *) db->scsiReq );			//Drive in immediate mode, so request returns once play started successfully 
	if (error){
		// SCSI command execution error
		Dbg("play LSN failed");
		DebugSCSIerror(error, &db->scsiCmd);
		iostd->io_Error = CDERR_ABORTED;
	}

	if (!poke) {
		// If not poking an existing play - Store the ioReq pointer for later, and start cdda polling
		db->playcdda_ioReq = iostd;
		db->cdda_ioreq=TRUE;
		db->abortPending = FALSE;
	}

	// Result monitored in unit ready polling loop
	
}

void cdtvPlayMSF(struct devBase * db, struct IOStdReq *iostd, BOOL poke){

	UBYTE SD_PlayMSF[]= { 0x47,0,0,0,0,0,0,0,0,0}; 	
	int error;

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	SD_PlayMSF[3] = (iostd->io_Offset & 0x00ff0000) >> 16;
	SD_PlayMSF[4] = (iostd->io_Offset & 0x0000ff00) >> 8;
	SD_PlayMSF[5] = (iostd->io_Offset & 0x000000ff);

	// Drive wants an end position, not length so need to convert
	
	SD_PlayMSF[8] = (iostd->io_Offset & 0x000000ff) + (iostd->io_Length & 0x000000ff);
	if (SD_PlayMSF[8] > 74) { // carry
		SD_PlayMSF[8]-=75;
		SD_PlayMSF[7]=1;
	}
	
	SD_PlayMSF[7]+=(iostd->io_Offset & 0x0000ff00) >> 8;
	SD_PlayMSF[7]+=(iostd->io_Length & 0x0000ff00) >> 8;
	if (SD_PlayMSF[7]>59){ // carry
		SD_PlayMSF[7]-=60;
		SD_PlayMSF[6]=1;
	}
	SD_PlayMSF[6]+=SD_PlayMSF[3] = (iostd->io_Offset & 0x00ff0000) >> 16;
	SD_PlayMSF[6]+=SD_PlayMSF[3] = (iostd->io_Length & 0x00ff0000) >> 16;
	
	error=driveSetImmediateMode(db,TRUE);

	driveInitSCSIstructure(db); 
	db->scsiCmd.scsi_Command=(UBYTE *)SD_PlayMSF;		// command to issue             
	db->scsiCmd.scsi_CmdLength = sizeof(SD_PlayMSF);	// length of the command        


    Dbgf(((CONST_STRPTR) "[cdtv] playtrack %d sent\n",iostd->io_Offset));

	error=DoIO( (struct IORequest *) db->scsiReq );			//Drive in immediate mode, so request returns once play started successfully 
	if (error){
		// SCSI command execution error
		Dbg("play failed");
		DebugSCSIerror(error, &db->scsiCmd);
		iostd->io_Error = CDERR_ABORTED;
	}

	if (!poke) {
		// If not poking an existing play - Store the ioReq pointer for later, and start cdda polling
		db->playcdda_ioReq = iostd;
		db->cdda_ioreq=TRUE;
		db->abortPending = FALSE;
	}
			
	// Result monitored in unit ready polling loop
	
}

void cdtvPause(struct devBase * db, struct IOStdReq *iostd, BOOL pause){
	UBYTE SD_Pause[]= { 0x4B,0,0,0,0,0,0,0,0,0}; 	
	int error;

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	if (!pause) SD_Pause[8]=1; // Set resume bit

	driveInitSCSIstructure_nb(db); 

	db->nbscsiCmd.scsi_Command=(UBYTE *)SD_Pause;		// command to issue             
	db->nbscsiCmd.scsi_CmdLength = sizeof(SD_Pause);	// length of the command        

	error=DoIO( (struct IORequest *) db->nbscsiReq );	// send it to the device driver

	if (error){
		// SCSI command execution error
		Dbg("pause failed");
		DebugSCSIerror(error, &db->nbscsiCmd);
		iostd->io_Error = CDERR_ABORTED;
	}
		
}

void driveStopPlayback(struct devBase * db){
	BYTE error;
	UBYTE SD_StopPlay[]= { 0x4e,0,0,0,0,0,0,0,0,0,0,0};

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	driveInitSCSIstructure_nb(db); 
	db->nbscsiCmd.scsi_Command=(UBYTE *)SD_StopPlay;	// command to issue             
	db->nbscsiCmd.scsi_CmdLength = sizeof(SD_StopPlay);	// length of the command       

	Dbg("drivestop");
	error=DoIO( (struct IORequest *) db->nbscsiReq );

	if (error){
		// SCSI command execution error
        Dbg("stop failed");
		DebugSCSIerror(error, &db->nbscsiCmd);
	}

	return;

}

int cdtvMute(struct devBase * db, struct IOStdReq *iostd,  int value, int mode){
    // Sets current drive volume
    // iostd may be null if called outside of a device request context
     
	BYTE error;
	UBYTE SD_SensePage0Eh[]= { 0x1a,8,0x0E,0,254,0};// Presents 255 byte buffer
	UBYTE SD_SelectPage0Eh[]= { 0x15,16,0,0,20,0}; 	// Send 20 bytes from buffer

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	driveInitSCSIstructure_nb(db); 
	db->nbscsiCmd.scsi_Command=(UBYTE *)SD_SensePage0Eh;		// command to issue             
	db->nbscsiCmd.scsi_CmdLength = sizeof(SD_SensePage0Eh);	// length of the command        

	error=DoIO( (struct IORequest *) db->nbscsiReq );

	if (error){
		// SCSI command execution error
		Dbg("mute read fail");
		DebugSCSIerror(error, &db->nbscsiCmd);
		if (iostd) iostd->io_Error = CDERR_ABORTED;
		return(0);
	}
	
	// Device volume is only 8 bit, CDTV uses 15 bit volumes	
	if (mode) return(db->nbbuffer[13] << 7);
	
	db->nbbuffer[13]=value >> 7; //left
	db->nbbuffer[15]=value >> 7; //right

	// Write back modified table
	db->nbscsiCmd.scsi_Command=(UBYTE *)SD_SelectPage0Eh;		
	db->nbscsiCmd.scsi_CmdLength = sizeof(SD_SelectPage0Eh);	
	db->nbscsiCmd.scsi_SenseActual = 0;							
	db->nbscsiCmd.scsi_Length = 20;								
	db->nbscsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_WRITE;		
														
	error=DoIO( (struct IORequest *) db->nbscsiReq );			

	if (error){
		// SCSI command execution error
		Dbg("mute set fail");
		DebugSCSIerror(error, &db->nbscsiCmd);
		if (iostd) iostd->io_Error = CDERR_ABORTED;
		return(0);
	}
	
	return(db->nbbuffer[13] << 7);
}

void abortCurrentPlay(struct devBase *db){
	struct ExecBase *SysBase = db->SysBase; // Restore Exec
	if (!db->cdda_ioreq) return;
	
	Dbg("audio playback abort");
	driveStopPlayback(db);
	db->cdda_ioreq = FALSE;
	db->playcdda_ioReq->io_Error = CDERR_ABORTED;
	ReplyMsg(&db->playcdda_ioReq->io_Message);
	db->playcdda_ioReq = NULL;
}
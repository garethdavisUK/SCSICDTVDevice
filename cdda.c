// cdda.c - CDDA related functions for SCSI cdtv.device
// Part of SCSI CDTV Device, an open source CDTV SCSI drive device driver - http://github.com/garethdavisuk/SCSICDTVDevice/
// Copyright (c) 2026 Gareth Davis. All new code released under GPL v2. See README in project root.

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
		Dbgf(((CONST_STRPTR) "[cdtv] read page 0E failed error=%d\n",error));
		DebugSCSIerror(error, &db->nbscsiCmd);
		return 0;
	}
		
	if (mode) db->nbbuffer[6] = 4;	//set immediate reply and don't stop on track crossing.
		else db->nbbuffer[6] = 2;	//clear immediate reply and stop on track crossing.
		
	// Write back modified table
	db->nbscsiCmd.scsi_Command=(UBYTE *)SD_SelectPage0Eh;		
	db->nbscsiCmd.scsi_CmdLength = sizeof(SD_SelectPage0Eh);	
	db->nbscsiCmd.scsi_SenseActual = 0;							
	db->nbscsiCmd.scsi_Length = 20;								
	db->nbscsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_WRITE;		
														
	error=DoIO( (struct IORequest *) db->nbscsiReq );			

	if (error){
		// SCSI command execution error
		Dbgf(((CONST_STRPTR) "[cdtv] set page 0E failed error=%d\n",error));
		DebugSCSIerror(error, &db->nbscsiCmd);
		return 0;
	}

	db->immediate = mode;

	return (0);
}

void cdtvPlayTrack(struct devBase * db, struct IOStdReq *iostd){

	UBYTE SD_PlayTrackIndex[]= { 0x48,0,0,0,0,0,0,0,0,0}; 	
	int error;

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	if (!db->immediate) driveSetImmediateMode(db,TRUE);


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

	error=DoIO( (struct IORequest *) db->scsiReq );			//Drive should be in immediate mode, so request returns once play started successfully 

	if (error){
		// SCSI command execution error
		Dbgf(((CONST_STRPTR) "[cdtv] play failed %d\n",error));
		DebugSCSIerror(error, &db->scsiCmd);
		iostd->io_Error = CDERR_ABORTED;
	}

	// Start cdda polling
	db->cdda_ioreq=TRUE;
	db->abortPending = FALSE;

	// Result monitored in unit ready polling loop
	
}

BOOL drivePlay(struct devBase * db, ULONG offset, ULONG length, BOOL lsn, BOOL poke){

	UBYTE SD_Play[]={ 0,0,0,0,0,0,0,0,0,0,0,0};

	int error;

	struct ExecBase *SysBase = db->SysBase; // Restore Exec

	if (!db->immediate) driveSetImmediateMode(db,TRUE);

	if (lsn){
		// Request is LSN
		Dbgf(((CONST_STRPTR) "[cdtv] start 0x%lx length 0x%lx\n",offset,length));
		SD_Play[0]= 0xA5; 	

		SD_Play[2] = (offset & 0xff000000) >> 24;
		SD_Play[3] = (offset & 0x00ff0000) >> 16;
		SD_Play[4] = (offset & 0x0000ff00) >> 8;
		SD_Play[5] = (offset & 0x000000ff);

		SD_Play[6] = (length & 0xff000000) >> 24;
		SD_Play[7] = (length & 0x00ff0000) >> 16;
		SD_Play[8] = (length & 0x0000ff00) >> 8;
		SD_Play[9] = (length & 0x000000ff);
	
	} else {
		// Request is MSF
		SD_Play[0]=  0x47; 	
		SD_Play[3] = (offset & 0x00ff0000) >> 16;
		SD_Play[4] = (offset & 0x0000ff00) >> 8;
		SD_Play[5] = (offset & 0x000000ff);

		SD_Play[6] = (length & 0x00ff0000) >> 16;
		SD_Play[7] = (length & 0x0000ff00) >> 8;
		SD_Play[8] = (length & 0x000000ff);

		Dbgf(((CONST_STRPTR) "[cdtv] %02ld:%02ld.%02ld to %02ld:%02ld.%02ld\n",(ULONG)SD_Play[3],(ULONG)SD_Play[4],(ULONG)SD_Play[5],(ULONG)SD_Play[6],(ULONG)SD_Play[7],(ULONG)SD_Play[8]));
	}

	driveInitSCSIstructure(db); 
	db->scsiCmd.scsi_Command=(UBYTE *)SD_Play;		// command to issue 
	
	// length of the command  
	if (lsn) db->scsiCmd.scsi_CmdLength = 12;       
		else db->scsiCmd.scsi_CmdLength = 10;

    Dbg("sending play");

	error=DoIO( (struct IORequest *) db->scsiReq );	//Drive should be in immediate mode, so request returns once play started successfully 

	if (error){
		// SCSI command execution error
		Dbg("play failed");
		DebugSCSIerror(error, &db->scsiCmd);
		return FALSE;
	}

	if (!poke) {
		// If not poking an existing play start cdda polling
		db->cdda_ioreq=TRUE;
		db->abortPending = FALSE;
	}
			
	// Result monitored in unit ready polling loop
	return TRUE;
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
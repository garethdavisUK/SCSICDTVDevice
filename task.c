//task.c - main task handler for cdtv.device

#include "globals.h"
#include "task.h"
//#include "device.h"

void devHandler(void) 
{
	struct IOStdReq *nbiostd;

	struct MsgPort	*timerPort;
	struct timerequest *timerReq; 
	struct taskMessage *tm;

	BYTE error,temp;
	BOOL terminate=FALSE;

	unsigned char alerttext[255];

	// For RawDoFmt
	STATIC CONST ULONG putChar[] = { 0x16c04e75 };

	ULONG signal, cmdSig, nbcmdSig, timerSig, taskSig;
		
	Dbg("task started");

	// Restore Sysbase and devBase from task user data
	// Sysbase required to fetch devBase, so have to use absolute to begin with

	struct ExecBase *SysBase = *(struct ExecBase **)4UL;
	struct Task *task = FindTask(NULL);
	struct devBase *db = (struct devBase *)task->tc_UserData;

	// Now we have devbase, we can restore SysBase
	SysBase = db->SysBase; 

	struct DOSBase * DOSBase = (struct DOSBase *)OpenLibrary((STRPTR)"dos.library", 34);
	db->DOSBase = DOSBase;

	struct Library * IntuitionBase = (struct Library *) OpenLibrary((CONST_STRPTR)"intuition.library", 34);
	//Only used here

	db->devPort = alib_CreatePort(NULL, 0); //port for standard commands
	if (db->devPort == NULL) return;

	db->nbdevPort = alib_CreatePort(NULL, 0); // port for non-blocking commands to jump standard queue
	if (db->nbdevPort == NULL) return;
	
	// Clear the buffer pointers so they are initialised if we terminate early	
	db->buffer = NULL;
	db->nbbuffer = NULL;
	db->rdybuffer = NULL;
	db->sectorbuffer = NULL;
	db->sense = NULL;
	db->nbsense = NULL;
	db->rdysense = NULL;

	//And the device pointers
	db->scsiReq = NULL;
	db->scsiPort = NULL;
	timerReq = NULL;
	timerPort = NULL;

	// This loop construct is only to here to allow the initialisation to be stopped with a break statement if necessary, 
	// and also replaces a number of conditional branches checking the progress of the intialisation. 
	// It will only ever execute once

	while (TRUE) { 

		// Allocate the buffers
		db->buffer = (UBYTE *)AllocMem(BUFSIZE, MEMF_PUBLIC);
		if (!db->buffer) { terminate=TRUE; break;}

		db->nbbuffer = (UBYTE *)AllocMem(BUFSIZE, MEMF_PUBLIC);
		if (!db->nbbuffer) { terminate=TRUE; break;}

		db->rdybuffer = (UBYTE *)AllocMem(BUFSIZE, MEMF_PUBLIC);
		if (!db->rdybuffer) { terminate=TRUE; break;}

		db->sectorbuffer = (UBYTE *)AllocMem(SECTORBUFSIZE, MEMF_PUBLIC);
		if (!db->sectorbuffer) { terminate=TRUE; break;}

		db->sense = (UBYTE *)AllocMem(SENSESIZE, MEMF_PUBLIC);
		if (!db->sense) { terminate=TRUE; break;}
		
		db->nbsense = (UBYTE *)AllocMem(SENSESIZE, MEMF_PUBLIC);
		if (!db->nbsense) { terminate=TRUE; break;}

		db->rdysense = (UBYTE *)AllocMem(SENSESIZE, MEMF_PUBLIC);
		if (!db->rdysense) { terminate=TRUE; break;}
		
		// Init SCSI device 
		db->scsiPort = alib_CreatePort(NULL, 0);
		if (!db->scsiPort) 	{ terminate=TRUE; break;}
		
		db->scsiReq = alib_CreateStdIO(db->scsiPort);
		if (!db->scsiReq) { terminate=TRUE; break;}

		if ( OpenDevice( (CONST_STRPTR)"scsi.device", 6, (struct IORequest*) db->scsiReq, 0 ) ) {
			if ( OpenDevice( (CONST_STRPTR)"2nd.scsi.device", 6, (struct IORequest*) db->scsiReq, 0 ) ) {
				db->scsiReq->io_Device = NULL;
				DisplayAlert(RECOVERY_ALERT, (STRPTR)"\x00\x10\x14[cdtv.device] Unable to open scsi.device or 2nd.scsi.device unit 6\x00", 26);
				Dbg("scsi open failed");
				terminate=TRUE;
				break;
			}
		}

		//clone the SCSI structures	
		db->nbscsiReq=db->scsiReq;
		db->rdyscsiReq=db->scsiReq;
		
		//Init Timer device 
		timerPort = alib_CreatePort(NULL, 0);
		if (!timerPort) { terminate=TRUE; break;}
		
		timerReq = (struct timerequest *)alib_CreateExtIO( timerPort, sizeof( struct timerequest ) );
		if (!timerReq) { terminate=TRUE; break;}
		
		if ( OpenDevice( (CONST_STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest*) timerReq, 0 ) ) 
			{ terminate=TRUE; break;}
		
		/* no longer used
		// Clone timer for adhoc use
		// db->adhoc_timerReq=timerReq;
		*/

		// Set up SCSI command structure
		driveInitSCSIstructure(db); 

		// Do a SCSI Unit Inquiry to confirm it's an optical drive
		UBYTE SD_Inquiry[]	= { 0x12,0,0,0,254,0 };

		db->scsiReq->io_Length  = sizeof(struct SCSICmd);
		db->scsiReq->io_Data    = (APTR)&db->scsiCmd;
		db->scsiReq->io_Command = HD_SCSICMD;
		db->scsiReq->io_Flags	= 0;

		db->scsiCmd.scsi_Command=(UBYTE *)SD_Inquiry;		             
		db->scsiCmd.scsi_CmdLength = sizeof(SD_Inquiry);

		db->scsiCmd.scsi_Data = (UWORD *)db->buffer;			  
		db->scsiCmd.scsi_Length = BUFSIZE;					      
		db->scsiCmd.scsi_Flags = SCSIF_AUTOSENSE|SCSIF_READ;  
														
		db->scsiCmd.scsi_SenseData =(UBYTE *)db->sense;			     
		db->scsiCmd.scsi_SenseLength = SENSESIZE;			     
		db->scsiCmd.scsi_SenseActual = 0;
	
		error = DoIO( (struct IORequest *) db->scsiReq );

		if (db->scsiReq->io_Error) {
			DisplayAlert(RECOVERY_ALERT, (CONST_STRPTR)"\x00\x10\x14[cdtv.device] scsi enquiry failed\x00", 26);
			DebugSCSIerror(error, &db->scsiCmd);
			terminate=TRUE;
			break;
		}		

		// check we have an optical drive connected
		temp = (db->buffer[0] & 31);
		if (temp!=5){
			RawDoFmt((CONST_STRPTR)"\x00\x10\x14[cdtv.device] not an optical drive (found type 0x%02x)\x00", &temp, (void (*)(void))&putChar, alerttext );
			DisplayAlert(RECOVERY_ALERT, alerttext, 26);
			Dbgf(((CONST_STRPTR)"[cdtv] not an optical drive (found type 0x%02x) - fatal\n",temp));
			terminate=TRUE;
			break;
		}
							
		// We have the drive name and type in the buffer now
		// here is where any drive specific stuff needs to be set up
		
		cdtvMute(db, NULL, 0x7FFF, 1); //Reset volume to max			
		isUnitReady(db); // Initial check if disc present
		
		// Set up INTERVAL second diskchange event timer	
		timerReq->tr_node.io_Command 	= TR_ADDREQUEST;
		timerReq->tr_time.tv_secs		= DISKCHANGE_CHECK_INTERVAL;
		timerReq->tr_time.tv_micro		= 0;

		SendIO ((struct IORequest*) timerReq);
		
		//Set devBase defaults
		//cdda globals
		db->immediate = TRUE;
		db->cdda_ioreq = FALSE;
		db->discSummary.LastTrack=0xFF; // Invalidate in memory summary	

		//hardware globals
		db->driveready = FALSE;
		db->lasterror = FALSE;
		db->discchanges = 0;
		db->changeInt = NULL;

		break; // Initialisation completed successfully

	} // end initilisation while loop

	db->initComplete = TRUE;

	//Fetch and reply to CDTV_STARTTASK message to sync startup
	while((tm = (struct taskMessage *) GetMsg(db->taskPort)) == NULL) 
		WaitPort(db->taskPort);

	ReplyMsg(&tm->msg);

	while (!terminate) 
	{
		cmdSig=1 << db->devPort->mp_SigBit;
		nbcmdSig=1 << db->nbdevPort->mp_SigBit;		
		timerSig=1 << timerPort->mp_SigBit;
		taskSig=1 << db->taskPort->mp_SigBit;
		
		signal = Wait(cmdSig | nbcmdSig | timerSig | taskSig);

		// Process task messages first
		if(signal & taskSig){
			//Process all messages in the task control port
			while (tm = (struct taskMessage *)GetMsg(db->taskPort)) 
			{
				Dbg("tasksig-msg");
				switch(tm->command) 
				{					
					case CDTV_TERM:
						terminate=TRUE;
						// Don't reply yet - that will happen once cleanup complete
						break;

					default: // This should never execute - but catch anyway
						Dbgf(((CONST_STRPTR) "[cdtv] unhandled task command 0x%x\n",tm->command));
						ReplyMsg(&tm->msg); //Reply to whatever it was
						break;
				}
			} // end while taskSig signal	
		}

		// Process timer interrupts next
		if (signal & timerSig){
			//Trying to do the timer request any other way causes the driver to hang (or guru) sooner or later
			timerReq = (struct timerequest *)GetMsg(timerPort);
			ReplyMsg(&timerReq->tr_node.io_Message);
			
			// Clear timer signal if still set
			Disable();
			if (SetSignal(0,0) & timerSig) SetSignal(0, timerSig);	 
			Enable();
			
			isUnitReady(db); //Check for diskchange
			
			//Safe to do other things now...

			if (db->cdda_ioreq){ // Check state of CDDA playback if open ioreq

				if (db->abortPending) {
					abortCurrentPlay(db);
					db->abortPending = FALSE;
				}

				error=driveGetQSubChannel(db,TRUE);
				if (!error){
					switch (db->nbbuffer[1]){
						case SQSTAT_PLAYING:
							// Playing - nothing to do
							Dbg("playing audio");
							break;

						case SQSTAT_PAUSED:
							// Paused - nothing to do
							Dbg("audio paused");
							break;
							
						case SQSTAT_DONE:
							// completed successfully
							Dbg("audio completed successfully");
							db->cdda_ioreq = FALSE;
							db->playcdda_ioReq->io_Error = 0;
							ReplyMsg(&db->playcdda_ioReq->io_Message);							
							break;
							
						case SQSTAT_NOTVALID:
							// Invalid audio status  

						case SQSTAT_ERROR:
							// Playback stopped due to error

						case SQSTAT_NOSTAT:
							// No audio status available (probably terminated by other request)

						default:
							// Anything else
							abortCurrentPlay(db);							
							break;
					}
				} else {
					Dbg("poll driveGetQSubChannel error");
				}
			} 
			
			//Rechedule timer (poll quicker when playing audio)
			timerReq->tr_node.io_Command 	= TR_ADDREQUEST;
			if (db->cdda_ioreq) timerReq->tr_time.tv_secs = 1;
				else timerReq->tr_time.tv_secs		= DISKCHANGE_CHECK_INTERVAL;
			timerReq->tr_time.tv_micro		= 0;
			SendIO ((struct IORequest*)timerReq);

		} // end if timer signal
	
		// Process non-blocking signals next
		if(signal & nbcmdSig){

			//Process all messages in the non-blocking port
			while (nbiostd  = (struct IOStdReq *)GetMsg(db->nbdevPort)) 
			{
				// First look for commands that don't require drive to be ready
				switch(nbiostd->io_Command) 
				{
					case CDTV_GETDRIVETYPE:
						nbiostd->io_Actual = DG_CDROM; // Trackdisk device type
						break; 
																
					case CDTV_GETNUMTRACKS:		// Not documented - seems to always return 1 unless drive is empty on real CDTV
						if (db->driveready) nbiostd->io_Actual = 1; else nbiostd->io_Actual = 0;
						break;	
						
					case CDTV_MUTE:
						nbiostd->io_Actual = cdtvMute(db,nbiostd,nbiostd->io_Offset,nbiostd->io_Length);
						break;

					case CDTV_PAUSE:
						cdtvPause(db,nbiostd, nbiostd->io_Length);
						break;
						
					case HD_SCSICMD:
						hdScsiCmd(db,nbiostd);
						break;

					default:
						// Following commands requires drive to be ready - error out if drive not ready or TOC not loaded				
						if (!db->driveready){
							nbiostd->io_Error = CDERR_NODISK;
							break;
						}
						
						if (db->discSummary.LastTrack==0xFF){
							nbiostd->io_Error = CDERR_BADTOC;
							break;
						}
							
					
						switch (nbiostd->io_Command) {
							case CDTV_GETGEOMETRY:		// Not documented, returns nothing on real CDTV  - returning something sane anyway.
								cdtvGetGeometry(db,nbiostd);
								break; 
								
							case CDTV_INFO:
								cdtvInfo(db,nbiostd);
								break;
								
							case CDTV_ISROM:
								cdtvISROM(db,nbiostd);
								break;
												
							case CDTV_STOPPLAY:
								if (db->cdda_ioreq){
									// Not valid to call this when requests still open
									nbiostd->io_Error = CDERR_NOTVALID;
								} else {
									driveStopPlayback(db);
								}
								break;

							case CDTV_SUBQLSN:
								cdtvSubQ(db,nbiostd,FALSE);
								break;
								
							case CDTV_SUBQMSF:
								cdtvSubQ(db,nbiostd,TRUE);
								break;
								
							default: // This should never execute - but catch anyway
								Dbgf(((CONST_STRPTR) "[cdtvdev] unhandled task non block command 0x%x\n",nbiostd->io_Command));
								nbiostd->io_Error = CDERR_NOCMD;
								break;
						}
				}
	
				ReplyMsg(&nbiostd->io_Message);

			} // End while GetMsg

		} // End if no blocking command signal

		// Finally process a blocking signal
		if (signal & cmdSig && !db->cdda_ioreq){
			//Only process one message at a time from the blocking port 
			//to allow higher priority signals to work between blocking commands

			//Some commands complete in the background, so a reply is not sent imemdiately

			if (db->blocking_ioReq = (struct IOStdReq *)GetMsg(db->devPort)) 
			{
        		db->abortPending=FALSE; //Reset the abort flag

				// First look for commands that don't require drive to be ready
				switch(db->blocking_ioReq->io_Command) 
				{
					case CDTV_OPTIONS: //undocumented so may not belong here?
						cdtvOptions(db,db->blocking_ioReq);
						ReplyMsg(&db->blocking_ioReq->io_Message);
						break;

					default:
						// Following commands requires drive to be ready				
						if (!db->driveready){
							db->blocking_ioReq->io_Error = CDERR_NODISK;
							ReplyMsg(&db->blocking_ioReq->io_Message);
							break;
						}
						
						if (db->discSummary.LastTrack==0xFF){
							db->blocking_ioReq->io_Error = CDERR_BADTOC;
							ReplyMsg(&db->blocking_ioReq->io_Message);
							break;
						}
												
						switch (db->dev_ioReq->io_Command) {
							case CDTV_MOTOR:
								db->blocking_ioReq->io_Actual = cdtvSetMotor(db,db->blocking_ioReq,db->blocking_ioReq->io_Length);
								ReplyMsg(&db->blocking_ioReq->io_Message);
								break;

							case CDTV_PLAYLSN:
								//if (db->cdda_ioreq) abortCurrentPlay(db); // If play in progress, abort before continuing
								cdtvPlayLSN(db,db->blocking_ioReq, FALSE);
								break;

							case CDTV_POKEPLAYLSN:
								cdtvPlayLSN(db,db->blocking_ioReq, TRUE);
								ReplyMsg(&db->blocking_ioReq->io_Message); // Reply to the poke
								break;

							case CDTV_PLAYMSF:
								//if (db->cdda_ioreq) abortCurrentPlay(db); // If play in progress, abort before continuing
								cdtvPlayMSF(db,db->blocking_ioReq, FALSE);
								break;

							case CDTV_POKEPLAYMSF:
								cdtvPlayMSF(db,db->blocking_ioReq, TRUE);
								ReplyMsg(&db->blocking_ioReq->io_Message);  // Reply to the poke
								break;

							case CDTV_PLAYTRACK:
								//if (db->cdda_ioreq) abortCurrentPlay(db); // If play in progress, abort before continuing
								cdtvPlayTrack(db,db->blocking_ioReq);
								break;

							case CDTV_READ:
								cdtvRead(db,db->blocking_ioReq);
								ReplyMsg(&db->blocking_ioReq->io_Message);
								break;
							
							case CDTV_SEEK:
								cdtvSeek(db,db->blocking_ioReq);
								ReplyMsg(&db->blocking_ioReq->io_Message);
								break;

							case CDTV_TOCLSN:
								cdtvGetTOC(db,db->blocking_ioReq,FALSE);
								ReplyMsg(&db->blocking_ioReq->io_Message);
								break;
							
							case CDTV_TOCMSF:
								cdtvGetTOC(db,db->blocking_ioReq,TRUE);
								ReplyMsg(&db->blocking_ioReq->io_Message); 
								break;		
								
							default: // This should never execute - but catch anyway
								Dbgf(((CONST_STRPTR) "[cdtv] unhandled task command 0x%x\n",db->blocking_ioReq->io_Command));
								db->blocking_ioReq->io_Error = CDERR_NOCMD;
								ReplyMsg(&db->blocking_ioReq->io_Message); 
								break;
						}
				}			

			} // End while GetMsg

		} // End if Command signal
	
	} // End main loop

	// clean up on termination
	   
	if ( db->scsiReq )
	{
		if ( db->scsiReq->io_Device ) CloseDevice( (struct IORequest *) db->scsiReq );
		alib_DeleteStdIO( db->scsiReq);
	}

	if (db->scsiPort) alib_DeletePort( db->scsiPort);
	
	if ( timerReq )
	{
		if ( timerReq->tr_node.io_Device ) {
			AbortIO( (struct IORequest *) timerReq);
			WaitIO( (struct IORequest *) timerReq);
			CloseDevice( (struct IORequest *) timerReq );
		}
		alib_DeleteExtIO( (struct IORequest *)timerReq );
	}

	if (timerPort != NULL) alib_DeletePort( timerPort);	
		
	if (db->buffer) FreeMem( db->buffer, BUFSIZE );
	if (db->nbbuffer) FreeMem( db->nbbuffer, BUFSIZE );
	if (db->rdybuffer) FreeMem( db->rdybuffer, BUFSIZE );
	
	if (db->sectorbuffer) FreeMem( db->sectorbuffer, SECTORBUFSIZE );

	if (db->sense) FreeMem( db->sense, SENSESIZE );
	if (db->nbsense) FreeMem( db->nbsense, SENSESIZE );
	if (db->rdysense) FreeMem( db->rdysense, SENSESIZE );
	
	if (db->nbdevPort) alib_DeletePort(db->nbdevPort);
	if (db->devPort) alib_DeletePort(db->devPort);

	if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);

	if (DOSBase) CloseLibrary((struct Library *)DOSBase);

	//Reply to device handler if termination was requested
	if (tm->command == CDTV_TERM){
		Forbid();
		ReplyMsg(&db->dev_ioReq->io_Message);
	}

	Dbg("task terminated"); 
}



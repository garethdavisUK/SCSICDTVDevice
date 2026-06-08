// task.c - main task handler for SCSI cdtv.device
// Part of SCSI CDTV Device, an open source CDTV SCSI drive device driver - http://github.com/garethdavisuk/SCSICDTVDevice/
// Copyright (c) 2026 Gareth Davis. All new code released under GPL v2. See README in project root.

#include "globals.h"
#include "task.h"
//#include "device.h"

void devHandler(void) 
{
	struct IOStdReq *nbiostd;

	struct MsgPort	*timerPort;
	struct timerequest *timerReq; 
	struct taskMessage *tm;

	BOOL terminate=FALSE;
	BYTE error;

	ULONG signal, cmdSig, nbcmdSig, timerSig, taskSig;
		
	Dbg("task started");

	// Restore Sysbase and devBase from task user data
	// Sysbase required to fetch devBase, so have to use absolute to begin with

	struct ExecBase *SysBase = *(struct ExecBase **)4UL;
	struct Task *task = FindTask(NULL);
	struct devBase *db = (struct devBase *)task->tc_UserData;

	// Now we have devbase, we can restore SysBase (even though it's probably still at 0x4 as usual)
	SysBase = db->SysBase; 

	struct DOSBase * DOSBase = (struct DOSBase *)OpenLibrary((STRPTR)"dos.library", 34);
	db->DOSBase = DOSBase;

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
		
		// Init device ports 
		db->scsiPort = alib_CreatePort(NULL, 0);
		if (!db->scsiPort) 	{ terminate=TRUE; break;}
		
		db->scsiReq = alib_CreateStdIO(db->scsiPort);
		if (!db->scsiReq) { terminate=TRUE; break;}

		//SCSI device opened when required

		timerPort = alib_CreatePort(NULL, 0);
		if (!timerPort) { terminate=TRUE; break;}
		
		timerReq = (struct timerequest *)alib_CreateExtIO( timerPort, sizeof( struct timerequest ) );
		if (!timerReq) { terminate=TRUE; break;}
		
		//Init Timer device 		
		if ( OpenDevice( (CONST_STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest*) timerReq, 0 ) ) 
			{ terminate=TRUE; break;}	
		
		/* no longer used
		// Clone timer for adhoc use
		// db->adhoc_timerReq=timerReq;
		*/


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
		db->scsiInitDone = FALSE;


		break; // End of initialisation loop - will only execute once
	}

	db->initComplete = TRUE;

	//Fetch and reply to CDTV_STARTTASK message to sync startup
	while((tm = (struct taskMessage *) GetMsg(db->taskPort)) == NULL) 
		WaitPort(db->taskPort);

	ReplyMsg(&tm->msg);

	// SCSI device not opened at this point, wait for second message
	// Diskchange timer won't be set until SCSI init has completed

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
					case CDTV_SCSIINIT:
						if (openSCSIdevice(db)) {
							// Set up INTERVAL second diskchange event timer now drive is available	
							timerReq->tr_node.io_Command 	= TR_ADDREQUEST;
							timerReq->tr_time.tv_secs		= DISKCHANGE_CHECK_INTERVAL;
							timerReq->tr_time.tv_micro		= 0;

							SendIO ((struct IORequest*) timerReq);
						}

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
	
				if (nbiostd->io_Command != CDTV_SCSIINIT)
					ReplyMsg(&nbiostd->io_Message); 
					else alib_DeleteStdIO(nbiostd); // We only use the message, delete the IORequest to tidy up
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
						cdtvRead(db,db->blocking_ioReq,TRUE);
						ReplyMsg(&db->blocking_ioReq->io_Message);
						break;

					case CDTV_READXL:
						cdtvReadXL(db,db->blocking_ioReq);
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
						Dbgf(((CONST_STRPTR) "[cdtv] unhandled task command 0x%lx\n",db->blocking_ioReq->io_Command));
						db->blocking_ioReq->io_Error = CDERR_NOCMD;
						ReplyMsg(&db->blocking_ioReq->io_Message); 
						break;
						
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

	//if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);

	if (DOSBase) CloseLibrary((struct Library *)DOSBase);

	//Reply to device handler if termination was requested
	if (tm->command == CDTV_TERM){
		Forbid();
		ReplyMsg(&tm->msg); //Reply to device now shutdown complete
	}

	Dbg("task terminated"); 
}



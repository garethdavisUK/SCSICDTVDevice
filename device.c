//device.c - main device code for cdtv.device

#include "globals.h"
#include "device.h"

//Return if the device gets executed
int __attribute__((no_reorder)) _start()
{
    return -1;
}

// Define the entry points for the device functions
static ULONG function_vectors[] =
    {
        (ULONG)open,
        (ULONG)close,
        (ULONG)expunge,
        0, //extFunc not used
        (ULONG)beginIO,
        (ULONG)abortIO,
        -1}; //last item

// Define the RTF_AUTOINIT table for the romtag
const ULONG autoinit_table[4] =
    {
        sizeof(struct devBase),
        (ULONG)function_vectors,
        0,
        (ULONG)init
    };

// Device romtag
__attribute__((used,no_reorder)) static const struct Resident romTag = {
    .rt_MatchWord = RTC_MATCHWORD,
    .rt_MatchTag  = (APTR)&romTag,
    .rt_EndSkip   = (APTR)(&romTag + sizeof (romTag)),
    .rt_Flags     = RTF_COLDSTART|RTF_AUTOINIT,
    .rt_Version   = DEVICE_VERSION,
    .rt_Type      = NT_DEVICE,
    .rt_Pri       = DEVICE_PRIORITY,
    .rt_Name      = (APTR)&device_name,
    .rt_IdString  = (APTR)&device_id_string,
    .rt_Init      = (APTR)autoinit_table
};   

// Called as Exec initialises devices
struct Library * init(struct ExecBase *SysBase asm("a6"), BPTR seg_list asm("a0"), struct devBase *db asm("d0"))
{	
    Dbg("init()");

    struct taskMessage tm;

    // Store Execbase in devBase for future use 
    db->SysBase = SysBase;

    // Init devbase task values
    db->initComplete = FALSE;
    db->handlerTask = NULL;

    // Port for task control messages
    db->taskPort = alib_CreatePort(NULL, 0L); 
    if (db->taskPort == NULL) return NULL;
    
    // Send start message for child process to answer when initialised
    tm.msg.mn_ReplyPort = alib_CreatePort(NULL, 0);
    tm.msg.mn_Node.ln_Type = NT_MESSAGE;
    tm.msg.mn_Length = sizeof(struct taskMessage);
    tm.command = CDTV_STARTTASK;

    PutMsg(db->taskPort, &tm.msg);

    // Create the handler task, passing devBase in the userdata field
    db->handlerTask=alib_CreateTask("CDTVTask",0,devHandler,4096,db,SysBase);

    if (db->handlerTask == NULL) {
        Dbg("init CreateTask fail");
        return NULL;
    }

    Dbg("init waiting reply");

    // Wait for start message reply before resuming
    WaitPort(tm.msg.mn_ReplyPort);
    alib_DeletePort(tm.msg.mn_ReplyPort); // Clean up startup message

		
    if (db->initComplete == FALSE) {
        Dbg("task init fail");
        return NULL;
    }

    // still here so startup was successful, store pointer to our code in devBase
    db->SegList = seg_list;

    // Finish populating the library structure, and return it
    db->devNode.lib_Node.ln_Type = NT_DEVICE;
    db->devNode.lib_Node.ln_Name = (char *)device_name;
    db->devNode.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    db->devNode.lib_Version = DEVICE_VERSION;
    db->devNode.lib_Revision = DEVICE_REVISION;
    db->devNode.lib_IdString = (APTR)device_id_string;

    Dbg("init complete");

    return (struct Library *)db;
}

// Called when all requests are closed, and system is low on memory (or 'avail flush' executed)
static BPTR __attribute__((used)) expunge(struct devBase *db asm("a6"))
{
    struct ExecBase *SysBase = db->SysBase; // Restore Exec

    struct taskMessage tm;

    Dbg("expunge()");

    if (db->devNode.lib_OpenCnt != 0)
    {
        db->devNode.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
        
    // Send terminate message to child process - was initialised by CDTV_STARTTASK
    tm.msg.mn_ReplyPort = alib_CreatePort(NULL, 0);
    tm.msg.mn_Node.ln_Type = NT_MESSAGE;
    tm.msg.mn_Length = sizeof(struct taskMessage);
    tm.command = CDTV_TERM;

    PutMsg(db->taskPort, &tm.msg);

    // Wait for reply before proceeding with cleanup
    WaitPort(tm.msg.mn_ReplyPort);
    alib_DeletePort(tm.msg.mn_ReplyPort);
    alib_DeletePort(db->taskPort);
  

    // Retrieve our code pointer as we're about to release devBase
    BPTR seg_list = db->SegList;

    Remove(&db->devNode.lib_Node);
    FreeMem((char *)db - db->devNode.lib_NegSize, db->devNode.lib_NegSize + db->devNode.lib_PosSize);

    Dbg("expunge() complete");

    return seg_list;
}

// An Exec OpenDevice request  
static void __attribute__((used)) open(struct devBase *db asm("a6"), struct IORequest *ioreq asm("a1"), ULONG unitnum asm("d0"), ULONG flags asm("d1"))
{
    Dbg("open()");
    
    //Set default response to error in case we return early
    ioreq->io_Error = IOERR_OPENFAIL;
    ioreq->io_Message.mn_Node.ln_Type = NT_REPLYMSG;

    if (!db->initComplete) return; //Device init failed, can't proceed, shouldn't ever happen

    if (unitnum != 0) return; //Only 1 unit

    //Increment open count and return success
    db->devNode.lib_OpenCnt++;
    ioreq->io_Error = 0; //Success

    Dbg("open() success");
}

// An Exec CloseDevice request
static BPTR __attribute__((used)) close(struct devBase *db asm("a6"), struct IORequest *ioreq asm("a1"))
{
    Dbg("close()");

    //Clear the ioreq to prevent reuse
    ioreq->io_Device = NULL;
    ioreq->io_Unit = NULL;

    //Decrement open count
    db->devNode.lib_OpenCnt--;

    //Call expunge if requested, and this is the final open ioreq
    if (db->devNode.lib_OpenCnt == 0 && (db->devNode.lib_Flags & LIBF_DELEXP))
        return expunge(db);

    return 0;
}

// An Exec BeginIO or DoIO request
static void __attribute__((used)) beginIO(struct devBase *db asm("a6"), struct IORequest *ioreq asm("a1"))
{
    struct ExecBase *SysBase = db->SysBase; // Restore Exec
	UWORD cmd;

	//Cast request to IOStdReq
	db->dev_ioReq = (struct IOStdReq *)ioreq;
    db->dev_ioReq->io_Error = 0;
	db->dev_ioReq->io_Actual = 0;
    cmd=ioreq->io_Command;
	
	// ADDCHANGEINT, CDTV_QUICKSTATUS, CDTV_REMCHANGEINT, CDTV_CHANGENUM, CDTV_CHANGESTATE need to be returned immediately
	Dbgf(((CONST_STRPTR) "[cdtv] cmd=%lu\n",ioreq->io_Command));
    switch(cmd) 
    {
		case CDTV_ADDCHANGEINT:
            if (db->changeInt != NULL){
                // Interrupt is being overwritten - clear the existing interrupt
                // Don't know if this is correct behaviour here?
                Dbg("ChangeInt overwritten");
                db->changeInt = NULL;
                ReplyMsg(&db->changeInt_ioReq->io_Message); // Reply to the ioReq that previously set the interrupt
            }

			db->changeInt = db->dev_ioReq->io_Data;

			// Command does not reply until interrupt removed, so store ioReq
            db->changeInt_ioReq = (struct IOStdReq *)ioreq;
			return;
			break;
		
		case CDTV_REMCHANGEINT:
			db->changeInt = NULL;
            ReplyMsg(&db->changeInt_ioReq->io_Message); // Reply to the ioReq that set the interrupt
			ReplyMsg(&db->dev_ioReq->io_Message);
			return;
			break;

		case CDTV_CHANGENUM:
			db->dev_ioReq->io_Actual = db->discchanges;
			ReplyMsg(&db->dev_ioReq->io_Message);
			return;
			break;		

		case CDTV_CHANGESTATE:
			if (db->driveready) db->dev_ioReq->io_Actual = 0; else db->dev_ioReq->io_Actual = 1; 
			ReplyMsg(&db->dev_ioReq->io_Message);
			return;
			break;		
		
		case CDTV_STATUS:		// CDTV_STATUS not documented but seems to do same as CDTV_QUICKSTATUS
		case CDTV_QUICKSTATUS:
			// Not fully implemented yet, needs done and position error flags
			if (db->driveready) db->dev_ioReq->io_Actual = QSF_READY | QSF_SPIN | QSF_DISK;
			if (db->lasterror) db->dev_ioReq->io_Actual =  db->dev_ioReq->io_Actual | QSF_ERROR;
			if (db->cdda_ioreq) db->dev_ioReq->io_Actual =  db->dev_ioReq->io_Actual | QSF_AUDIO;
			ReplyMsg(&db->dev_ioReq->io_Message);
			return;		
	        break;
	}	


	// If still here them it's a command that needs queuing
	// Remove the IOF_QUICK bit and add to queue or reject
    db->dev_ioReq->io_Flags &= ~IOF_QUICK;

    switch(cmd) 
    {
	    //implemented in some way

		//Comands that don't block drive (non blocking commands port)
	    case CDTV_GETDRIVETYPE:
		case CDTV_GETGEOMETRY: 		
		case CDTV_GETNUMTRACKS:		
		case CDTV_INFO: 
	    case CDTV_ISROM:
        case CDTV_MUTE:        
		case CDTV_PAUSE:
        case CDTV_STOPPLAY:
		case CDTV_SUBQLSN:
		case CDTV_SUBQMSF:
        case HD_SCSICMD: // Treating as non blocking as we don't know what's been requested
			PutMsg(db->nbdevPort, &db->dev_ioReq->io_Message);
            break;

		//Commands that affect drive	
		case CDTV_MOTOR:
        case CDTV_OPTIONS:
     	case CDTV_PLAYLSN:
		case CDTV_PLAYMSF:
        case CDTV_PLAYTRACK:
        case CDTV_POKEPLAYLSN:
        case CDTV_POKEPLAYMSF:
        case CDTV_READ:
		case CDTV_SEEK:
        case CDTV_TOCLSN:
        case CDTV_TOCMSF:
			PutMsg(db->devPort, &db->dev_ioReq->io_Message);
            break;

		//Always return write protect error
		case CDTV_PROTSTATUS:
		case CDTV_FORMAT:
        case CDTV_WRITE:
            Dbg("CDERR_WRITEPROT");
            db->dev_ioReq->io_Error = CDERR_WRITEPROT;
			ReplyMsg(&db->dev_ioReq->io_Message);
			break;
 
		//Commands that do nothing - defined as NOP in developer guide
        case CDTV_FLUSH:
		case CDTV_UPDATE:
		case CDTV_CLEAR:
		case CDTV_STOP:
		case CDTV_START:
		case CDTV_REMOVE:
            // Dbgf(((CONST_STRPTR) "[cdtv] NOP command %lu\n",cmd));
			ReplyMsg(&db->dev_ioReq->io_Message);
			break;
			
 
		//Not yet implemented audio commands
		//return OK as should be harmless (just no audio changes)
        case CDTV_FADE:
            Dbg("CDTV_FADE");
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;      

        case CDTV_FRONTPANEL:
            Dbg("CDTV_FRONTPANEL");
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;     

        case CDTV_GENLOCK:
            Dbg("CDTV_GENLOCK");
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;     

        case CDTV_PLAYSEGSLSN:
            Dbg("CDTV_PLAYSEGSLSN");
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;      

        case CDTV_PLAYSEGSMSF:
            Dbg("CDTV_PLAYSEGSMSF");
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;      

        case CDTV_POKESEGLSN:
            Dbg("CDTV_POKESEGLSN");
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;      

        case CDTV_POKESEGMSF:
            Dbg("CDTV_POKESEGMSF");
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;        
        

		//Not yet implemented non-audio commands
		//returning NOCMD as the calling code would be expecting them to do something		
        case CDTV_FRAMECALL:
            Dbg("TV_FRAMECALL");
			db->dev_ioReq->io_Error = CDERR_NOCMD;
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;    

        case CDTV_READXL:
            Dbg("CDTV_READXL");
			db->dev_ioReq->io_Error = CDERR_NOCMD;
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;    

        case CDTV_RESET:
            Dbg("CDTV_RESET");
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;           
		        
		//Potentially valid, but undocumented commands
		//Return NOCMD error until a way of implementing them is found
        case CDTV_DIRECT:
            Dbg("CDTV_DIRECT");
			db->dev_ioReq->io_Error = CDERR_NOCMD;
			ReplyMsg(&db->dev_ioReq->io_Message);
	        break;

        case CDTV_ERRORINFO:
            Dbg("CDTV_ERRORINFO");
			db->dev_ioReq->io_Error = CDERR_NOCMD;
			ReplyMsg(&db->dev_ioReq->io_Message);
			break;       
			
        
        //Anything else isn't valid, so should error
        case CMD_INVALID:
        default:
            db->dev_ioReq->io_Error = CDERR_NOCMD;
            ReplyMsg(&db->dev_ioReq->io_Message);
            Dbg("unhandled command");
            break;
    }
}

// An Exec AbortIO request
static ULONG __attribute__((used)) abortIO(struct devBase *db asm("a6"), struct IORequest *ioreq asm("a1"))
{
    struct ExecBase *SysBase = db->SysBase; // Restore Exec
    struct IOStdReq *io;

    //In theory only blocking requests will need aborting, non blocking should complete before it's possible to abort.

    if ((struct IOStdReq *)ioreq == db->blocking_ioReq) {

        // Abort of current blocking request, set flag for handler task to check and abort if possible 
        Dbg("abort_io() current");
        
        db->abortPending = TRUE;

    } else {
        // Abort of non-current request, Search the queued blocking iorequests in the message port for it 
        // Method copied from Olaf Barthel's Trackfile.device https://github.com/obarthel/trackfile-device

        Dbg("abort_io() non-current");
        
        Disable();

        for (io = (struct IOStdReq *)db->devPort->mp_MsgList.lh_Head; 
             io->io_Message.mn_Node.ln_Succ != NULL;
             io = (struct IOStdReq *)io->io_Message.mn_Node.ln_Succ)
        {
            if (io == (struct IOStdReq *)ioreq) { 
                //The retrieved request matches the AbortIO caller
                //Remove from the queue

                Remove(&io->io_Message.mn_Node);

                //And reply to the pending request
                io->io_Error = IOERR_ABORTED;
                ReplyMsg(&io->io_Message);
                Dbg("dequeued");
                break;
            }
        }
        Enable();
    }

    return 0;
}


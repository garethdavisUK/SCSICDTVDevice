# SCSI cdtv.device
## What is it?
This is a ground up rewrite of the cdtv.device driver present in the extended ROMs of the Commodore CDTV and A570 drives. 

This translates the device calls into SCSI 2 compatible CD-ROM commands and sends them to ID 6 on the scsi.device. This allows a third party SCSI CD-ROM drive to be used in place of the internal drive, which are unobtainium once they fail. You will need the CDTV SCSI interface to connect the drive to, but there are reproductions of these available.

## Status of the project
This is best described as a bunch of loosly held together hacks that can successfully boot fairly simple titles like the CDPD workbench discs. This is nowhere near production quality code yet, this repo is here primarily for storage rather than sharing my work. There are no releases as yet.

The device is currently doing something making cdstrap unhappy, causing it not to open the initial CDTV animation screen and TM once booting. If a CDTV title doesn't open its own screen during boot, you'll stay on a black screen. Sim City opens a new screen, Defender of the Crown does not, and Lemmings only does it on KS1.3.

For this reason I'd advise people not to go burning this into ROMs right now, and is why I'm not including a binary module.

### What does work (maybe)
From a driver level here is what I think is currently working, what this translates to in application support is left as an exercise for the reader
* Blocking IO commands: CDTV_MOTOR, CDTV_OPTIONS, CDTV_PLAYLSN, CDTV_PLAYMSF, CDTV_PLAYTRACK, CDTV_POKEPLAYLSN. CDTV_POKEPLAYMSF. CDTV_READ, CDTV_SEEK. CDTV_TOCLSN, CDTV_TOCMSF
* Non blocking IO commands: CDTV_GETDRIVETYPE, CDTV_GETGEOMETRY, CDTV_GETNUMTRACKS, CDTV_INFO, CDTV_ISROM, CDTV_MUTE, CDTV_PAUSE, CDTV_STOPPLAY, CDTV_SUBQLSN, CDTV_SUBQMSF
* Write error instructions: CDTV_PROTSTATUS, CDTV_FORMAT, CDTV_WRITE
* NOP instructions: CDTV_FLUSH, CDTV_UPDATE, CDTV_CLEAR, CDTV_STOP, CDTV_START, CDTV_REMOVE
I've also implemented HD_SCSICMD which allows a SCSI direct command to be sent to the drive if something not covered in the CDTV interface is required, like fetching subcode channels or setting drive speed.

### What doesn't work
* As already mentioned, cdstrap does not open the CDTV animation screen post reset.
* CDTV_FADE is not implemented, so audio transitions sound off.
* PLAY_CDXL command
* Any commands that depend on the frame timer like CDTV_FRAMECALL
* Any of the CD audio commands that pass a tracklist (the PLAY commands with 'SEG' in the name) 
* Hardware commands like CDTV_FRONTPANEL and CDTV_GENLOCK
* Front panel buttons, and VFD display of track information during CDDA playback
* Other undocumented commands like CDTV_OPTIONS and CDTV_DIRECT
* CD-G playback, and anything else that uses the undocumented interface to stream CDDA subcode
Also see the notes about SCSI timeouts below.

## Supported hardware
The driver attempts to open scsi.device unit 6 and then 2nd.scsi.device unit 6. While this has been coded against the SCSI module that fits in the expansion slot on the CDTV, any other device that provides a SCSI direct interface compatible with the CDTV/A590/A2091 device should work.

I've used Pioneer SCSI 2 CD-ROM drives for testing, but any other vendor should work as long as they follow the SCSI 2 command set.  

## Building and testing
This has been developed using [Bebbo's gcc toolchain](https://franke.ms/git/bebbo/amiga-gcc), I've left the Bartman toolchain commands in the Makefile from SimpleDevice on which this device was based, but they are untested - and probably won't work due to the register assigments in function calls. 

I've also used VSCode IDE, and left in a working .vscode config folder which you'll probably need to adjust to your gcc-executable and KS1.3 include paths to make VSCode work properly. 

Building remains the same as in SimpleDevice...

### Make commands:
* build debug: `make debug`  (This creates a folder `build-debug` with all the build-files in it, compiles with debug-flags set)
* build release: `make`  (This creates a folder `build-release` with all the build-files in it, compiles with release-flags set)
* clean debug: `make cleandebug`
* clean release: `make cleanrelease`
* clean both: `make clean`

The debug build sends a fair amount of debug output to the serial port. 

### Testing
The driver can be loaded on a V37 kickstart machine using the LoadModule11.lha package from Aminet. The driver is V34 compatible, but LoadModule doesn't have any success making it resident. I use Capitoline in my testing workflow to substitute the release cdtv.device into the CDTVLand 2.35 extended rom, then flash the rom into a CDTV developer EEPROM board. It then successfully boots titles with KS1.3 or 2.04 enabled on the CDTV, subject to the issues mentioned above.    

There are known differences in behaviour between LoadModule and loading from ROM, for example Sim City hangs after terraforming from LoadModule but doesn't when the device is in ROM.  

### SCSI POST timeouts
The SCSI adaptor for the CDTV has some pretty generous timeouts to allow devices to become ready after POST. The behaviour of these timeouts isn't changed by loading this module, so you may be subjected to 60+ second wait every reboot as the SCSI adaptor scans for devices, then waits for them to become ready. I've observed a 60-70 second pause at post when an empty CD-ROM drive is on the SCSI bus on my test system, which disappears when the drive is loaded. This happens whether or not this device driver is loaded.

## Acknowledgments
This is based on the Jorgen Bilander's [SimpleDevice](https://github.com/jbilander/SimpleDevice), with inspiration taken from Matt Harlum's [lide.device](https://github.com/LIV2/lide.device) and Olaf Barthel's [trackfile-device](https://github.com/obarthel/trackfile-device) as I was navigating through the vaguaries of creating a module that will RTF_COLDSTART successfully. 

## License
All software contained that is not provided by a third-party is licensed under the GPL-2.0 only license 
[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)

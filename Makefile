# Makefile for SCSI cdtv.device
# Part of SCSI CDTV Device, an open source CDTV SCSI drive device driver - http://github.com/garethdavisuk/SCSICDTVDevice/
# Copyright (c) 2026 Gareth Davis. All new code released under GPL v2. See README in project root.

FILENAME=cdtv.device
RELDIR=build-release
DEBUGDIR=build-debug
OBJECTS=device.o task.o hardware.o dataio.o cdda.o alib.o
SHELL=sh

SRCDIRS=.
INCDIRS=.

TARGET = hunk-toolchain

ifeq ($(MAKECMDGOALS), debug)
 DIR=$(DEBUGDIR)
 EXTRA_CFLAGS+= -g -O2 -DDEBUG
 EXTRA_ASFLAGS+= -g
 EXTRA_LDFLAGS+= -ldebug
else
 DIR=$(RELDIR)
 EXTRA_CFLAGS+= -s -O2
endif


CC=m68k-amigaos-gcc
AS=m68k-amigaos-as
EXTRA_LDFLAGS+= -Wl,-Map=$(DIR)/$(FILENAME).map

CFLAGS+= -m68000 -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -resident -mcrt=nix13
ASFLAGS+= -m68000
#LDFLAGS+=  -nostdlib -nostartfiles -noixemul -lamiga
LDFLAGS+=   -nostartfiles  -resident -mcrt=nix13

OBJS:=$(addprefix $(DIR)/,$(OBJECTS))

CFLAGS+=$(addprefix -I,$(INCDIRS)) $(EXTRA_CFLAGS)
ASFLAGS+=$(EXTRA_ASFLAGS)
LDFLAGS+=$(EXTRA_LDFLAGS)

# Search paths
vpath %.c $(SRCDIRS)
vpath %.s $(SRCDIRS)

release: $(TARGET)
debug: $(TARGET)


$(DIR)/$(FILENAME):
	$(CC) $(CFLAGS) -o $@$(ELF_SUFFIX) $(OBJS) $(LDFLAGS)

hunk-toolchain: $(DIR) $(OBJS) $(DIR)/$(FILENAME)
	m68k-amigaos-objdump -D $(DIR)/$(FILENAME) > $(DIR)/$(FILENAME).s

$(DIR):
	@mkdir $(DIR)

$(DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
	@$(DEL_EXE)
	
$(DIR)/%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<
	@$(DEL_EXE)


ifeq '$(findstring ;,$(PATH))' ';'
 UNAME := Windows_Native
endif

ifeq ($(UNAME),Windows_Native)
 RM= rmdir /s /q
 DEVNULL= 2>nul || ver>nul
 DEL_EXE=if exist $(DIR)\$(FILENAME) del /q $(DIR)\$(FILENAME)
else
 RM= rm -rf
 DEL_EXE=test -f && $(RM) $(DIR)/$(FILENAME)
endif

.PHONY: clean

clean: cleandebug
clean: cleanrelease

cleandebug:
	$(info Cleaning debug)
	@$(RM) $(DEBUGDIR) $(DEVNULL)

cleanrelease:
	$(info Cleaning release)
	@$(RM) $(RELDIR) $(DEVNULL)

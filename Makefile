#
# This program is built with gcc. Make sure it is in your path before
# typing make. Example:
#     PATH=$PATH:/opt/amiga-gcc/bin
#     make
#
PROGS   := sdmac

VER := $(shell awk '/\$$VER:/{print $$7}' sdmac.c)
ADF_FILE := sdmac_$(VER).adf
ZIP_FILE := sdmac_$(VER).zip
LHA_FILE := sdmac_$(VER).lha
ARC_DIR  := sdmac_$(VER)

CC      := m68k-amigaos-gcc
CFLAGS  := -Wall -Wno-pointer-sign -Os

CFLAGS += -fomit-frame-pointer

#LDFLAGS := -Xlinker -Map=$(PROG).map -noixemul
#LDFLAGS = -Xlinker -Map=$@.map -mcrt=clib2 -lnet
LDFLAGS = -Xlinker -Map=$@.map -Wa,-a > $@.lst -mcrt=clib2

#CFLAGS += -g
#LDFLAGS += -g

ifeq (, $(shell which $(CC) 2>/dev/null ))
$(error "No $(CC) in PATH: maybe do PATH=$$PATH:/opt/amiga/bin to set up")
endif

ifeq (, $(shell which xdftool 2>/dev/null ))
$(error "No xdftool in PATH: build and install amitools first: https://github.com/cnvogelg/amitools")
endif

all: $(PROGS)

sdmac: sdmac.c

$(PROGS): Makefile
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)

zip: $(ZIP_FILE)
lha: $(LHA_FILE)

$(ZIP_FILE): $(PROGS) README.md LICENSE
	rm -rf $(ARC_DIR)
	mkdir $(ARC_DIR)
	cp -p $^ $(ARC_DIR)/
	rm -f $@
	zip -r $@ $(ARC_DIR)
	rm -rf $(ARC_DIR)

$(LHA_FILE): $(PROGS) README.md LICENSE
	rm -rf $(ARC_DIR)
	mkdir $(ARC_DIR)
	cp -p $^ $(ARC_DIR)/
	rm -f $@
	lha a $@ $(ARC_DIR)
	rm -rf $(ARC_DIR)

adf: $(PROGS)
	echo $(ADF_FILE)
	xdftool $(ADF_FILE) format "AmigaSDMAC"
	xdftool $(ADF_FILE) makedir C
	xdftool $(ADF_FILE) makedir S
	xdftool $(ADF_FILE) makedir Devs
	xdftool $(ADF_FILE) write sdmac C/sdmac
	xdftool $(ADF_FILE) write disk/Startup-Sequence S/Startup-Sequence
	xdftool $(ADF_FILE) write disk/system-configuration Devs/system-configuration
	xdftool $(ADF_FILE) write disk/Disk.info
	xdftool $(ADF_FILE) write README.md
	xdftool $(ADF_FILE) write LICENSE
	xdftool $(ADF_FILE) boot install

clean:
	rm $(PROGS)

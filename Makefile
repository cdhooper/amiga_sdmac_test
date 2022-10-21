#
# This program is built with gcc. Make sure it is in your path before
# typing make. Example:
#     PATH=$PATH:/opt/amiga-gcc/bin
#     make
#
PROGS   := sdmac
CC      := m68k-amigaos-gcc
CFLAGS  := -Wall -Wno-pointer-sign -Os

CFLAGS += -fomit-frame-pointer
#CFLAGS += -g

#LDFLAGS := -Xlinker -Map=$(PROG).map -noixemul
#LDFLAGS = -Xlinker -Map=$@.map -mcrt=clib2 -lnet
LDFLAGS = -Xlinker -Map=$@.map -Wa,-a > $@.lst -mcrt=clib2

ifeq (, $(shell which $(CC) 2>/dev/null ))
$(error "No $(CC) in PATH: maybe do PATH=$$PATH:/opt/amiga/bin to set up")
endif

all: $(PROGS)

sdmac: sdmac.c

$(PROGS): Makefile
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)

zip: sdmac.zip

sdmac.zip: sdmac
	rm -f $@
	zip $@ $<

clean:
	rm $(PROGS)

# makefile to fail if any command in pipe is failed.
SHELL = /bin/bash -o pipefail

# using gcc version 5.4.1 20161213 (Linaro GCC 5.4-2017.01-rc2)
BASE    = arm-linux-gnueabihf

CC      = $(BASE)-gcc
LD      = $(CC)
STRIP   = $(BASE)-strip

ifeq ($(V),1)
	Q :=
else
	Q := @
endif

INCLUDE	= -I./
INCLUDE	+= -I./support/minimig
INCLUDE	+= -I./lib/libco
INCLUDE	+= -I./lib/miniz

PRJ = MiSTer
SRC = $(wildcard *.c)
SRC2 = $(wildcard *.cpp)
MINIMIG_SRC	= $(wildcard ./support/minimig/*.cpp)
SHARPMZ_SRC	= $(wildcard ./support/sharpmz/*.cpp)
ARCHIE_SRC	= $(wildcard ./support/archie/*.cpp)
ST_SRC	= $(wildcard ./support/st/*.cpp)
X86_SRC	= $(wildcard ./support/x86/*.cpp)
SNES_SRC	= $(wildcard ./support/snes/*.cpp)
LIBCO_SRC	= lib/libco/arm.c
MINIZ_SRC	= $(wildcard ./lib/miniz/*.c)

VPATH	= ./:./support/minimig:./support/sharpmz:./support/archie:./support/st:./support/x86:./support/snes

OBJ	= $(SRC:.c=.c.o) $(SRC2:.cpp=.cpp.o) $(MINIMIG_SRC:.cpp=.cpp.o) $(SHARPMZ_SRC:.cpp=.cpp.o) $(ARCHIE_SRC:.cpp=.cpp.o) $(ST_SRC:.cpp=.cpp.o) $(X86_SRC:.cpp=.cpp.o) $(SNES_SRC:.cpp=.cpp.o) $(LIBCO_SRC:.c=.c.o) $(MINIZ_SRC:.c=.c.o)
DEP	= $(SRC:.c=.cpp.d) $(SRC2:.cpp=.cpp.d) $(MINIMIG_SRC:.cpp=.cpp.d) $(SHARPMZ_SRC:.cpp=.cpp.d) $(ARCHIE_SRC:.cpp=.cpp.d) $(ST_SRC:.cpp=.cpp.d) $(X86_SRC:.cpp=.cpp.d) $(SNES_SRC:.cpp=.cpp.d) $(LIBCO_SRC:.c=.c.d) $(MINIZ_SRC:.c=.c.d)

DFLAGS	= $(INCLUDE) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DVDATE=\"`date +"%y%m%d"`\"
CFLAGS	= $(DFLAGS) -Wall -Wextra -Wno-strict-aliasing -c -O3
LFLAGS	= -lc -lstdc++ -lrt

$(PRJ): $(OBJ)
	$(Q)$(info $@)
	$(Q)$(LD) -o $@ $+ $(LFLAGS)
	$(Q)cp $@ $@.elf
	$(Q)$(STRIP) $@

clean:
	$(Q)rm -f *.elf *.map *.lst *.user *~ $(PRJ)
	$(Q)rm -rf obj DTAR* x64
	$(Q)find . \( -name '*.o' -o -name '*.d' -o -name '*.bak' -o -name '*.rej' -o -name '*.org' \) -exec rm -f {} \;

cleanall:
	$(Q)rm -rf $(OBJ) $(DEP) *.elf *.map *.lst *.bak *.rej *.org *.user *~ $(PRJ)
	$(Q)rm -rf obj DTAR* x64
	$(Q)find . -name '*.o' -delete
	$(Q)find . -name '*.d' -delete

%.c.o: %.c
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu99 -o $@ -c $< 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

%.cpp.o: %.cpp
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu++14 -o $@ -c $< 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

-include $(DEP)
%.c.d: %.c
	$(Q)$(CC) $(DFLAGS) -MM $< -MT $@ -MT $*.c.o -MF $@ 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

%.cpp.d: %.cpp
	$(Q)$(CC) $(DFLAGS) -MM $< -MT $@ -MT $*.cpp.o -MF $@ 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

# Ensure correct time stamp
main.cpp.o: $(filter-out main.cpp.o, $(OBJ))

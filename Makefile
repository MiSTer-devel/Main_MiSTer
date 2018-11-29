
# makefile to fail if any command in pipe is failed.
SHELL = /bin/bash -o pipefail

# using gcc version 5.4.1 20161213 (Linaro GCC 5.4-2017.01-rc2)
BASE    = arm-linux-gnueabihf

CC      = $(BASE)-gcc
LD      = $(CC)
STRIP   = $(BASE)-strip

INCLUDE	= -I./
INCLUDE	= -I./support/minimig

PRJ = MiSTer
SRC = $(wildcard *.c)
SRC2 = $(wildcard *.cpp)
MINIMIG_SRC	= $(wildcard ./support/minimig/*.cpp)
SHARPMZ_SRC	= $(wildcard ./support/sharpmz/*.cpp)
ARCHIE_SRC	= $(wildcard ./support/archie/*.cpp)
ST_SRC	= $(wildcard ./support/st/*.cpp)
X86_SRC	= $(wildcard ./support/x86/*.cpp)
SNES_SRC	= $(wildcard ./support/snes/*.cpp)

VPATH	= ./:./support/minimig:./support/sharpmz:./support/archie:./support/st:./support/x86:./support/snes

OBJ	= $(SRC:.c=.o) $(SRC2:.cpp=.o) $(MINIMIG_SRC:.cpp=.o) $(SHARPMZ_SRC:.cpp=.o) $(ARCHIE_SRC:.cpp=.o) $(ST_SRC:.cpp=.o) $(X86_SRC:.cpp=.o) $(SNES_SRC:.cpp=.o)
DEP	= $(SRC:.c=.d) $(SRC2:.cpp=.d) $(MINIMIG_SRC:.cpp=.d) $(SHARPMZ_SRC:.cpp=.d) $(ARCHIE_SRC:.cpp=.d) $(ST_SRC:.cpp=.d) $(X86_SRC:.cpp=.d)	$(SNES_SRC:.cpp=.d)

CFLAGS	= $(DFLAGS) -c -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DVDATE=\"`date +"%y%m%d"`\"
LFLAGS	= -lc -lstdc++ -lrt

$(PRJ): $(OBJ)
	@$(info $@)
	@$(LD) -o $@ $+ $(LFLAGS)
	@cp $@ $@.elf
	@$(STRIP) $@

clean:
	rm -f *.d *.o *.elf *.map *.lst *.bak *.rej *.org *.user *~ $(PRJ)
	rm -rf obj .vs DTAR* x64

cleanall:
	rm -rf *.d *.o *.elf *.map *.lst *.bak *.rej *.org *.user *~ $(PRJ)
	rm -rf obj .vs DTAR* x64
	find . -name '*.o' -delete
	find . -name '*.d' -delete

%.o: %.c
	@$(info $<)
	@$(CC) $(CFLAGS) -std=gnu99 -o $@ -c $< 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

%.o: %.cpp
	@$(info $<)
	@$(CC) $(CFLAGS) -std=gnu++14 -o $@ -c $< 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

-include $(DEP)
%.d: %.c
	@$(CC) $(DFLAGS) -MM $< -MT $@ -MT $*.o -MF $@ 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

%.d: %.cpp
	@$(CC) $(DFLAGS) -MM $< -MT $@ -MT $*.o -MF $@ 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

# Ensure correct time stamp
main.o: $(filter-out main.o, $(OBJ))

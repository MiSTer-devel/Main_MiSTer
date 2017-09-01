
# makefile to fail if any command in pipe is failed.
SHELL = /bin/bash -o pipefail

# using gcc version 5.4.1 20161213 (Linaro GCC 5.4-2017.01-rc2)
BASE    = arm-linux-gnueabihf

CC      = $(BASE)-gcc
LD      = $(CC)
STRIP   = $(BASE)-strip

PRJ = MiSTer
SRC = $(wildcard *.c)
SRC2 = $(wildcard *.cpp)

OBJ = $(SRC:.c=.o) $(SRC2:.cpp=.o)
DEP = $(SRC:.c=.d) $(SRC2:.cpp=.d)

CFLAGS  = $(DFLAGS) -c -O2 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DVDATE=\"`date +"%y%m%d"`\"
LFLAGS  = -lc -lstdc++ -lrt

$(PRJ): $(OBJ)
	@$(info $@)
	@$(LD) $(LFLAGS) -o $@ $+
	@cp $@ $@.elf
	@$(STRIP) $@

clean:
	rm -f *.d *.o *.elf *.map *.lst *.bak *.rej *.org *.user *~ $(PRJ)
	rm -rf obj .vs DTAR* x64

%.o: %.c
	@$(info $<)
	@$(CC) $(CFLAGS) -o $@ -c $< 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

%.o: %.cpp
	@$(info $<)
	@$(CC) $(CFLAGS) -o $@ -c $< 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

-include $(DEP)
%.d: %.c
	@$(CC) $(DFLAGS) -MM $< -MT $@ -MT $*.o -MF $@ 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

%.d: %.cpp
	@$(CC) $(DFLAGS) -MM $< -MT $@ -MT $*.o -MF $@ 2>&1 | sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

# Ensure correct time stamp
main.o: $(filter-out main.o, $(OBJ))

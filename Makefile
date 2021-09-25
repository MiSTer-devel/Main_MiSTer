# makefile to fail if any command in pipe is failed.
SHELL = /bin/bash -o pipefail

# using gcc version 7.5.0 (Linaro GCC 7.5-2019.12)
BASE    = arm-linux-gnueabihf

CC      = $(BASE)-gcc
LD      = $(BASE)-ld
STRIP   = $(BASE)-strip

ifeq ($(V),1)
	Q :=
else
	Q := @
endif

FILTER = sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

SRCDIR = .
BUILDBASE = build

ifeq ($(DEBUG),1)
	PRJ = MiSTer-debug
	BUILDDIR = $(BUILDBASE)/debug
else
	PRJ = MiSTer
	BUILDDIR = $(BUILDBASE)/release
endif

INCLUDE =  -I$(SRCDIR)/
INCLUDE += -I$(SRCDIR)/support/minimig
INCLUDE += -I$(SRCDIR)/support/chd
INCLUDE += -I$(SRCDIR)/lib/libco
INCLUDE += -I$(SRCDIR)/lib/miniz
INCLUDE += -I$(SRCDIR)/lib/md5
INCLUDE += -I$(SRCDIR)/lib/lzma
INCLUDE += -I$(SRCDIR)/lib/libchdr/include
INCLUDE += -I$(SRCDIR)/lib/flac/include
INCLUDE += -I$(SRCDIR)/lib/flac/src/include
INCLUDE += -I$(SRCDIR)/lib/bluetooth

C_SRC =   $(wildcard $(SRCDIR)/*.c) \
          $(wildcard $(SRCDIR)/lib/miniz/*.c) \
          $(wildcard $(SRCDIR)/lib/md5/*.c) \
          $(wildcard $(SRCDIR)/lib/lzma/*.c) \
          $(wildcard $(SRCDIR)/lib/flac/src/*.c) \
          $(wildcard $(SRCDIR)/lib/libchdr/*.c) \
          $(SRCDIR)/lib/libco/arm.c 

CPP_SRC = $(wildcard $(SRCDIR)/*.cpp) \
          $(wildcard $(SRCDIR)/support/*/*.cpp) \
          $(SRCDIR)/lib/lodepng/lodepng.cpp

IMG =     $(wildcard $(SRCDIR)/*.png)

IMLIB2_LIB  = -Llib/imlib2 -lfreetype -lbz2 -lpng16 -lz -lImlib2

OBJ = $(C_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.c.o) \
      $(CPP_SRC:$(SRCDIR)/%.cpp=$(BUILDDIR)/%.cpp.o) \
      $(IMG:$(SRCDIR)/%.png=$(BUILDDIR)/%.png.o)

DEP = $(C_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.c.d) \
      $(CPP_SRC:$(SRCDIR)/%.cpp=$(BUILDDIR)/%.cpp.d)

DFLAGS = $(INCLUDE) -D_7ZIP_ST -DPACKAGE_VERSION=\"1.3.3\" -DFLAC_API_EXPORTS -DFLAC__HAS_OGG=0 -DHAVE_LROUND -DHAVE_STDINT_H -DHAVE_STDLIB_H -DHAVE_SYS_PARAM_H -DENABLE_64_BIT_WORDS=0 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DVDATE=\"`date +"%y%m%d"`\"
CFLAGS = $(DFLAGS) -Wall -Wextra -Wno-strict-aliasing -Wno-format-truncation -Wno-psabi -c
LFLAGS = -lc -lstdc++ -lm -lrt $(IMLIB2_LIB) -Llib/bluetooth -lbluetooth

ifeq ($(DEBUG),1)
	CFLAGS += -g -O0 -fomit-frame-pointer
	LFLAGS += -g
else
	CFLAGS += -O3
	LFLAGS +=
endif

ifeq ($(DEBUG),1)
$(PRJ): $(PRJ).elf
	$(Q)cp $< $@
else
$(PRJ): $(PRJ).elf
	$(Q)$(STRIP) -o $@ $<
endif

$(PRJ).elf: $(OBJ)
	$(Q)$(info $@)
	$(Q)$(CC) -o $@ $+ $(LFLAGS) 

clean:
	$(Q)rm -f *.elf *.map *.lst *.user *~ $(PRJ)
	$(Q)rm -rf obj DTAR* x64
	$(Q)find . \( -name '*.o' -o -name '*.d' -o -name '*.bak' -o -name '*.rej' -o -name '*.org' \) -exec rm -f {} \;
	$(Q)rm -rf $(BUILDDIR)

cleanall:
	$(Q)rm -rf *.o *.d *.elf *.map *.lst *.bak *.rej *.org *.user *~ MiSTer MiSTer-debug
	$(Q)rm -rf obj DTAR* x64
	$(Q)find . -name '*.o' -delete
	$(Q)find . -name '*.d' -delete
	$(Q)rm -rf $(BUILDBASE)

$(BUILDDIR)/%.c.o: $(SRCDIR)/%.c | build_dirs
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu99 -o $@ -c $< 2>&1 | $(FILTER)

$(BUILDDIR)/%.cpp.o: $(SRCDIR)/%.cpp | build_dirs
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu++14 -o $@ -c $< 2>&1 | $(FILTER)

$(BUILDDIR)/%.png.o: $(SRCDIR)/%.png | build_dirs
	$(Q)$(info $<)
	$(Q)$(LD) -r -b binary -o $@ $< 2>&1 | $(FILTER)

-include $(DEP)
$(BUILDDIR)/%.c.d: $(SRCDIR)/%.c | build_dirs
	$(Q)$(CC) $(DFLAGS) -MM $< -MT $@ -MT $(@:%.c.d=%.c.o) -MF $@ 2>&1 | $(FILTER)

$(BUILDDIR)/%.cpp.d: $(SRCDIR)/%.cpp | build_dirs
	$(Q)$(CC) $(DFLAGS) -MM $< -MT $@ -MT $(@:%.cpp.d=%.cpp.o) -MF $@ 2>&1 | $(FILTER)

# Ensure correct time stamp
$(BUILDDIR)/main.cpp.o: $(filter-out $(BUILDDIR)/main.cpp.o, $(OBJ))

# Create the build directories
BUILD_DIRS = $(sort $(dir $(OBJ) $(DEP)))
.PHONY: build_dirs $(BUILD_DIRS)

build_dirs: $(BUILD_DIRS)

$(BUILD_DIRS):
	$(Q)mkdir -p $@

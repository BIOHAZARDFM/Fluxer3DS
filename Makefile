
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set)
endif

TOPDIR ?= $(CURDIR)

# Load official 3DS build rules first.
include $(DEVKITARM)/3ds_rules

TARGET  := Fluxer3DS
BUILD   := build
SOURCES := source
INCLUDES := include

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

# Explicit header paths.
CTRULIB_INCLUDE := $(DEVKITPRO)/libctru/include
PORTLIB_INCLUDE := $(DEVKITPRO)/portlibs/3ds/include

CFLAGS := -g -Wall -Wextra -O2 -mword-relocations \
          -fomit-frame-pointer -ffunction-sections \
          $(ARCH) \
          -I$(CTRULIB_INCLUDE) \
          -I$(PORTLIB_INCLUDE) \
          -I$(CURDIR)/include \
          -D__3DS__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS := -g $(ARCH)

LIBDIRS := $(CTRULIB) $(PORTLIBS)

LIBS := -lcitro2d -lcitro3d -lctru -lm

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export OFILES := $(CFILES:.c=.o) $(CPPFILES:.cpp=.o) $(SFILES:.s=.o)

export INCLUDE := -I$(CURDIR)/include \
                  -I$(CTRULIB)/include \
                  -I$(PORTLIBS)/include \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo Cleaning...
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif

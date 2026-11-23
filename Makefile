# Target executable name
TARGET          := Fluxer3DS

# Directory setup
BUILD           := build
SOURCES         := source
INCLUDES        := include

# Compiler settings for devkitARM 3DS targets
ARCH            := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS          := -g -Wall -O2 -mword-relocations \
                   -fomit-frame-pointer -ffunction-sections \
                   $(ARCH)

CXXFLAGS        := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS         := -g $(ARCH)

# Linked 3DS Homebrew Libraries
# Note: Order matters! libcitro2d -> libcitro3d -> libctru
LIBS            := -lcitro2d -lcitro3d -lctru -lm

# Linked 3DS Homebrew Libraries
# Note: Order matters! libcitro2d -> libcitro3d -> libctru
LIBDIRS         := $(CTRULIB)
LIBS            := -lcitro2d -lcitro3d -lctru -lm

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES        := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export OFILES   := $(CFILES:.c=.o) $(CPPFILES:.cpp=.o) $(SFILES:.s=.o)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh

else

DEPENDS := $(OFILES:.o=.d)

# Include rules provided by devkitARM
include $(DEVKITARM)/3ds_rules

$(OUTPUT).3dsx: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
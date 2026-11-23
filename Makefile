# Fluxer3DS - devkitPro / devkitARM Makefile

TARGET      := Fluxer3DS
BUILD       := build
SOURCES     := source
INCLUDES    := include

ARCH        := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS      := -g -Wall -Wextra -O2 -mword-relocations \
               -fomit-frame-pointer -ffunction-sections \
               $(ARCH) -I$(CURDIR)/$(INCLUDES)
CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS     := -g $(ARCH)

LIBS        := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS     := $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES        := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
export OFILES   := $(CFILES:.c=.o) $(CPPFILES:.cpp=.o) $(SFILES:.s=.o)

.PHONY: all $(BUILD) clean
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh

else

DEPENDS := $(OFILES:.o=.d)

include $(DEVKITARM)/3ds_rules

$(OUTPUT).3dsx: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif

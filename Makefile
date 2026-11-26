.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set)
endif

TOPDIR ?= $(CURDIR)

# Load the official Nintendo 3DS build rules.
include $(DEVKITARM)/3ds_rules

# Project settings
TARGET   := Fluxer3DS
BUILD    := build
SOURCES  := source
INCLUDES := include

# Nintendo 3DS ARM architecture
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

# Compiler flags
CFLAGS := -g -Wall -Wextra -O2 -mword-relocations \
          -ffunction-sections \
          $(ARCH)

CFLAGS += $(INCLUDE) -D__3DS__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS  := -g $(ARCH)

# Linker flags. Keep the standard devkitARM 3DSX linker setup.
LDFLAGS := -specs=3dsx.specs -g $(ARCH)

# Libraries
LIBS    := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lcitro2d -lcitro3d -lctru -lm
LIBDIRS := $(CTRULIB) $(PORTLIBS)

# Parent build process
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

# Select the correct ARM linker.
ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES_SOURCES := $(CFILES:.c=.o) \
                         $(CPPFILES:.cpp=.o) \
                         $(SFILES:.s=.o)

export OFILES := $(OFILES_SOURCES)

# Header search paths
export INCLUDE := -I$(CURDIR)/include \
                  -I$(CTRULIB)/include \
                  -I$(PORTLIBS)/include \
                  -I$(CURDIR)/$(BUILD)

# Library search paths
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Generate application metadata.
export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(NO_SMDH)),)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

.PHONY: all clean

all: $(BUILD)/cacert_bundle.h
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD)/cacert_bundle.h: data/ssl/cacert.pem tools/embed_ca.py
	@mkdir -p $(BUILD)
	@python3 tools/embed_ca.py data/ssl/cacert.pem $@

clean:
	@echo Cleaning...
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh

# Actual compilation and linking
else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif

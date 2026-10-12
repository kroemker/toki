VERSION ?= ntsc-1.2
DEBUG_FEATURES ?= 1

ifeq ($(VERSION),ntsc-1.2)
  PLATFORM := N64
  DEBUG_FEATURES ?= 0
else ifeq ($(VERSION),gc-eu-mq-dbg)
  PLATFORM := GC
else
$(error Unsupported version $(VERSION))
endif

CPP_DEFINES ?=
-include .make_options.mk
include extra.mk

ifeq ($(PLATFORM),N64)
  CPP_DEFINES += -DPLATFORM_N64=1
else
  CPP_DEFINES += -DPLATFORM_N64=0
endif

# Converts e.g. ntsc-1.0 to NTSC_1_0
VERSION_MACRO := $(shell echo $(VERSION) | tr a-z-. A-Z__)
CPP_DEFINES += -DOOT_VERSION=$(VERSION_MACRO)

ifeq ($(DEBUG_FEATURES),1)
  CPP_DEFINES += -DDEBUG_FEATURES=1
else
  CPP_DEFINES += -DNDEBUG -DDEBUG_FEATURES=0
endif

GBI_DEFINES := -DF3DEX_GBI_2
CFLAGS += $(CPP_DEFINES) -G 0

all: rom
	CPP_DEFINES += -DNEVER_FROM_A_RECIPE

BUILD_DIR := build/$(VERSION)
ROM      := $(BUILD_DIR)/hackeroot-$(VERSION).z64
MAP      := $(ROM:.z64=.map)

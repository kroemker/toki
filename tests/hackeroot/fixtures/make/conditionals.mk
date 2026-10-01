	TABBED := yes
VERSION ?= b
ifeq ($(VERSION),a)
  CHOSEN := a
else ifeq ($(VERSION),b)
  CHOSEN := b
else
  $(error unsupported $(VERSION))
endif

ifneq "$(VERSION)" "a"
  NOT_A := yes
endif

ifdef VERSION
  HAS_VERSION := yes
endif
ifndef NOTHING_HERE
  NO_NOTHING := yes
endif

ifeq ($(shell uname),Linux)
  GUESSED := linux
else
  GUESSED := other
endif

ifeq (1,1)
  ifeq (1,0)
    NESTED := wrong
  else
    NESTED := right
  endif
endif

rom: $(OBJECTS)
	RECIPE_ONLY := 1
ifeq (1,1)
	STILL_RECIPE := 1
endif
AFTER_RULE := yes

-include not_there.mk
include included.mk

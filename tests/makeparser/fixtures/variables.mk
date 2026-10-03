LATE = $(LATER)
LATER = bound late
EARLY := $(LATER_SIMPLE)
LATER_SIMPLE := too late
ONCE ?= first
ONCE ?= second
LIST := a
LIST += b
LIST += $(LATER)
FROM_COMMAND_LINE := file value
override FORCED := file value
WITH_COMMENT := kept # dropped
CONTINUED := one \
             two
export EXPORTED := yes
define BLOCK
line one
line two
endef
SHELLED := $(shell uname)
SHELL_ASSIGN != uname
SELF = $(SELF) x

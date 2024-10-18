#
# Simple makefile by __ e k L 1 p 5 3 d __
#

# Variables
SRCS	:=$(shell find src/ -name "*.c") ekutils/src/ek.c
DBG_DIR	:=dbg
REL_DIR	:=rel
DBG_OBJ	:=$(patsubst %.c,$(DBG_DIR)/%.o,$(SRCS))
REL_OBJ	:=$(patsubst %.c,$(REL_DIR)/%.o,$(SRCS))

# Environment variables
CFLAGS	:=$(CFLAGS) -Iekutils/src/ -DEK_USE_TEST=1 -DEK_USE_UTIL=1 -std=gnu99
LDFLAGS	:=$(LDFLAGS) -lm

# normal build
all: dbg
dbg: CFLAGS += -O0 -g
dbg: $(DBG_DIR)/test

# normal executable
$(DBG_DIR)/test: $(DBG_OBJ)
	$(CC) $(LDFLAGS) $^ -o $@

# optimized build
rel: CFLAGS += -O2
rel: $(REL_DIR)/test

# optimized executable
$(REL_DIR)/test: $(REL_OBJ)
	$(CC) $(LDFLAGS) $^ -o $@

# Remember that when this call is evaluated, it is expanded TWICE!
define COMPILE
$(1)/$(dir $(3))$(2)
	mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) -c $(3) -o $$@
endef

# Go through every source file use gcc to find its pre-reqs and create a rule
$(foreach src,$(SRCS),$(eval $(call COMPILE,$(DBG_DIR),$(shell $(CC) $(CFLAGS) -M $(src) | tr -d '\\'),$(src))))
$(foreach src,$(SRCS),$(eval $(call COMPILE,$(REL_DIR),$(shell $(CC) $(CFLAGS) -M $(src) | tr -d '\\'),$(src))))

# Clean the project directory
.PHONY: clean
clean:
	rm -rf $(DBG_DIR) $(REL_DIR)


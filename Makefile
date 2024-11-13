#
# Simple makefile by __ e k L 1 p 5 3 d __
#

# Variables
SRC_DIR :=src
SRCS	:=$(shell find $(SRC_DIR)/ -name "*.c") ekutils/src/ek.c
DBG_DIR	:=dbg
REL_DIR	:=rel
DBG_OBJ	:=$(patsubst %.c,$(DBG_DIR)/%.o,$(SRCS))
REL_OBJ	:=$(patsubst %.c,$(REL_DIR)/%.o,$(SRCS))

# Find examples
EX_DIR	:=examples
EXS	:=$(notdir $(shell find $(EX_DIR)/* -type d))

# Environment variables
CFLAGS	:=$(CFLAGS) -Iekutils/src/ -DEK_USE_TEST=1 -DEK_USE_UTIL=1 -std=gnu99
LDFLAGS	:=$(LDFLAGS) -lm

# Normal build
all: rel
dbg: CFLAGS += -O0 -g
dbg: $(DBG_DIR)/test

# Normal executable
$(DBG_DIR)/test: $(DBG_OBJ)
	$(CC) $(LDFLAGS) $^ -o $@

# Optimized build
rel: CFLAGS += -O2
rel: $(REL_DIR)/test

# Optimized executable
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

# Build examples
define EXAMPLE
SRCS_EX_$(1)	:=$(EX_DIR)/$(1)/main.c $(SRC_DIR)/ekjson.c
HDRS_EX_$(1)	:=examples/common.h $(SRC_DIR)/ekjson.h
example_$(1): $$(SRCS_EX_$(1)) $$(HDRS_EX_$(2))
	$(CC) -O0 -I$(SRC_DIR) -I$(EX_DIR) $$(SRCS_EX_$(1)) -o $(EX_DIR)/$(1)/example
	cd $(EX_DIR)/$(1); ./example; cd ../../
endef

# Create a rule for every example
$(foreach ex,$(EXS),$(eval $(call EXAMPLE,$(ex))))

# Clean the project directory
.PHONY: clean
clean:
	rm -rf $(DBG_DIR) $(REL_DIR)
	$(foreach ex,$(EXS),$(shell rm -rf $(EX_DIR)/$(ex)/example $(EX_DIR)/$(ex)/*.dSYM))


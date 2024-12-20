#
# Simple makefile by __ e k L 1 p 5 3 d __
#

# Variables
SRCS	:= $(wildcard ./src/*.c) ./ekjson/src/ekjson.c
CXXSRCS	:= $(wildcard ./src/*.cpp) ./simdjson/singleheader/simdjson.cpp
BUILD	:=rel
OUT	:=$(BUILD)/benchmark
OBJS	:=$(patsubst %.c,$(BUILD)/%.o,$(SRCS))
CXXOBJS	:=$(patsubst %.cpp,$(BUILD)/%.o,$(CXXSRCS))

# Environment variables
FLAGS	:=-O2 -I./ -Isimdjson/singleheader -Ijjson/extern/array/include -Ijjson/extern/hash-cache/include -Ijjson/extern/dict/include -Ijjson/extern/log/include -Ijjson/extern/sync/include -Ijjson/include -Irapidjson/include/
LDFLAGS	:=$(LDFLAGS) -lm -Wl,-rpath json-c/ -Wl,-rpath jjson/lib/ -Ljjson/lib/ -larray -ldict -ljson -lhash_cache -llog -lsync -Ljson-c/ -ljson-c

CFLAGS	:=$(CFLAGS) $(FLAGS) -std=gnu99
CXXFLAGS:=$(CXXFLAGS) $(FLAGS) -std=c++11

# Parser benchmark
parse: $(OUT)
	$(OUT) samples/512KB.json

# Float benchmark
float: $(OUT)
	$(OUT) float

# Build the main executable
$(OUT): $(OBJS) $(CXXOBJS)
	$(CXX) $(LDFLAGS) $^ -o $@


# Remember that when this call is evaluated, it is expanded TWICE!
define COMPILE
$$(BUILD)/$(dir $(2))$(1)
	mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) -c $(2) -o $$@
endef

# Remember that when this call is evaluated, it is expanded TWICE!
define CXXCOMPILE
$$(BUILD)/$(dir $(2))$(1)
	mkdir -p $$(dir $$@)
	$$(CXX) $$(CXXFLAGS) -c $(2) -o $$@
endef

# Go through every source file use gcc to find its pre-reqs and create a rule
$(foreach src,$(SRCS),$(eval $(call COMPILE,$(shell $(CC) $(CFLAGS) -M $(src) | tr -d '\\'),$(src))))
$(foreach src,$(CXXSRCS),$(eval $(call CXXCOMPILE,$(shell $(CXX) $(CXXFLAGS) -M $(src) | tr -d '\\'),$(src))))

# Clean the project directory
.PHONY: clean
clean:
	rm -rf $(BUILD)


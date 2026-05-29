# Bare-metal protoacc microbenchmarks.
#
# Host step: build gen_data (links host protobuf) and run it to emit the
# precomputed <test>_data.h headers (descriptor tables + serialized inputs).
# Target step: compile harness.cpp + accellib.cpp per test for riscv64-unknown-elf
# (newlib, HTIF), linking NO protobuf.
#
# Run a result on a chipyard sim, e.g.:
#   make -C ../../../../sims/verilator CONFIG=ProtoAccelRocketConfig \
#        run-binary BINARY=$(pwd)/double.riscv

MICRO        := $(abspath ..)
PB_INSTALL   := $(MICRO)/protobuf-x86-install
PB_GEN       := $(MICRO)/primitive-tests

HOST_CXX     := g++
HOST_FLAGS   := -O2 -std=c++11 -include cstdint -I$(PB_GEN) -I$(PB_INSTALL)/include

RISCV_CXX    := riscv64-unknown-elf-g++
# -T overrides the htif_nano.specs default linker script (-dT htif.ld), whose
# 128K heap is far too small for accellib's ~MiB alloc regions.
RISCV_FLAGS  := -O2 -static -fno-common -fno-builtin-printf -specs=htif_nano.specs \
                -Wl,-T,$(CURDIR)/htif_bigheap.ld -include cstdint -I$(MICRO)

TESTS        := double uint64 string nested
RISCV_BINS   := $(addsuffix .riscv,$(TESTS))
DATA_HEADERS := $(addsuffix _data.h,$(TESTS))

.PHONY: all clean
all: $(RISCV_BINS)

# host generator (links host protobuf + the protoc-generated .pb.cc)
gen_data: gen_data.cpp $(PB_GEN)/primitives.pb.cc
	$(HOST_CXX) $(HOST_FLAGS) $^ $(PB_INSTALL)/lib/libprotobuf.a -lpthread -o $@

# run it once to emit all data headers
$(DATA_HEADERS): gen_data
	./gen_data

# bare-metal test = harness + accellib, with the per-test data header
%.riscv: harness.cpp $(MICRO)/accellib.cpp %_data.h htif_bigheap.ld
	$(RISCV_CXX) $(RISCV_FLAGS) -DDATA_HEADER='"$*_data.h"' harness.cpp $(MICRO)/accellib.cpp -o $@

clean:
	rm -f gen_data $(RISCV_BINS) $(DATA_HEADERS)

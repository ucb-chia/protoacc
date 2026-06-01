# Bare-metal protoacc microbenchmarks.
#
# Host step: build gen_data (links host protobuf) and run it to emit the
# precomputed <test>_data.h headers (descriptor tables + serialized inputs).
# Target step: compile the SELF-CONTAINED harness (selfcontained.cpp) per test
# for riscv64-unknown-elf (newlib, HTIF). It links NEITHER protobuf NOR accellib:
#   - libstdc++/std::string hangs under newlib-nano (htif_nano.specs), so the
#     target must not pull libstdc++;
#   - even accellib.cpp's compiled functions hang on this target while the
#     identical operations inlined into one TU run fine, so the harness inlines
#     all accelerator interaction itself.
#
# Run a result on a chipyard sim, e.g.:
#   make -C ../../../../sims/verilator CONFIG=ProtoAccelRocketConfig \
#        run-binary BINARY=$(pwd)/double.riscv
# and grep the output for "PASSED <test> round-trip".

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

# nested messages: the serializer RTL doesn't implement sub-message fields, so
# this test is deserialize-only (parse must complete).
nested.riscv: EXTRA := -DPARSE_ONLY

# bare-metal test = self-contained harness only (no accellib, no protobuf)
%.riscv: selfcontained.cpp %_data.h htif_bigheap.ld
	$(RISCV_CXX) $(RISCV_FLAGS) $(EXTRA) -DDATA_HEADER='"$*_data.h"' selfcontained.cpp -o $@

clean:
	rm -f gen_data $(RISCV_BINS) $(DATA_HEADERS)

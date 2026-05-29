# Linux (glibc, static) build of the protoacc smoke microbenchmarks.
# These are the original generated benchmark programs (accelerator + protobuf
# CPU-baseline comparison), built for riscv64-unknown-linux-gnu and statically
# linked against the cross-compiled protobuf. Run inside a FireMarshal buildroot
# image on a chipyard/FireSim sim that includes the protoacc accelerator.

MICRO     := $(abspath ..)
PB_INSTALL := $(MICRO)/protobuf-riscv-install
PB_GEN    := $(MICRO)/primitive-tests

RVCPP     := riscv64-unknown-linux-gnu-g++
CPPFLAGS  := -std=c++11 -O3 -static -D NDEBUG -include cstdint

PROTO_CC  := $(PB_GEN)/primitives.pb.cc
ACCEL_CC  := $(MICRO)/accellib.cpp

TESTS     := double uint64_size05B string PaccdoubleMessage
RISCV_BINS := $(addsuffix .riscv,$(TESTS))

.PHONY: all clean
all: $(RISCV_BINS)

%.riscv: $(PB_GEN)/%.cpp $(PROTO_CC) $(ACCEL_CC)
	$(RVCPP) $(CPPFLAGS) $< $(PROTO_CC) $(ACCEL_CC) \
	  -I $(PB_GEN) -I $(PB_INSTALL)/include -pthread \
	  $(PB_INSTALL)/lib/libprotobuf.a -o $@

clean:
	rm -f $(RISCV_BINS)

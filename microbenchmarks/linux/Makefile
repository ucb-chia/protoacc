# Linux (glibc, static) build of the FULL Figure-11 protoacc microbenchmarks:
# every generated test in primitive-tests/ (deserialize) and
# primitive-tests-serializer/ (serialize), built for riscv64-unknown-linux-gnu
# against the cross-compiled protobuf. Run under FireMarshal on FireSim.

MICRO      := $(abspath ..)
PB_INSTALL := $(MICRO)/protobuf-riscv-install
PB_X86     := $(MICRO)/protobuf-x86-install
DES_DIR    := $(MICRO)/primitive-tests
SER_DIR    := $(MICRO)/primitive-tests-serializer

RVCPP    := riscv64-unknown-linux-gnu-g++
CPPFLAGS := -std=c++11 -O3 -static -D NDEBUG -include cstdint
ACCEL_CC := $(MICRO)/accellib.cpp
BUILD    := build

DES_TESTS := $(basename $(notdir $(wildcard $(DES_DIR)/*.cpp)))
SER_TESTS := $(basename $(notdir $(wildcard $(SER_DIR)/*.cpp)))
DES_BINS  := $(addprefix $(BUILD)/,$(addsuffix .riscv,$(DES_TESTS)))
SER_BINS  := $(addprefix $(BUILD)/,$(addsuffix .riscv,$(SER_TESTS)))

.PHONY: all clean
all: $(DES_BINS) $(SER_BINS)

# each tests dir has its own primitives.proto (same file/package name)
%/primitives.pb.cc: %/primitives.proto
	$(PB_X86)/bin/protoc --proto_path=$(dir $<) --cpp_out=$(dir $<) $<

$(BUILD):
	mkdir -p $@

$(BUILD)/%.riscv: $(DES_DIR)/%.cpp $(DES_DIR)/primitives.pb.cc $(ACCEL_CC) | $(BUILD)
	$(RVCPP) $(CPPFLAGS) $< $(DES_DIR)/primitives.pb.cc $(ACCEL_CC) \
	  -I $(DES_DIR) -I $(PB_INSTALL)/include -pthread \
	  $(PB_INSTALL)/lib/libprotobuf.a -o $@

$(BUILD)/ser_%.riscv: $(SER_DIR)/ser_%.cpp $(SER_DIR)/primitives.pb.cc $(ACCEL_CC) | $(BUILD)
	$(RVCPP) $(CPPFLAGS) $< $(SER_DIR)/primitives.pb.cc $(ACCEL_CC) \
	  -I $(SER_DIR) -I $(PB_INSTALL)/include -pthread \
	  $(PB_INSTALL)/lib/libprotobuf.a -o $@

clean:
	rm -rf $(BUILD)

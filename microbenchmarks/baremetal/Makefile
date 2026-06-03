# Bare-metal protoacc pipeline tests (protobuf-free).
# Host: gen_data emits per-test data headers (descriptors, golden input object,
# golden bytes). Target: serialize -> compare; deserialize -> compare.
MICRO        := $(abspath ..)
PB_INSTALL   := $(MICRO)/protobuf-x86-install
PB_GEN       := $(MICRO)/primitive-tests

HOST_CXX     := g++
HOST_FLAGS   := -O2 -std=c++11 -include cstdint -I$(PB_GEN) -I$(PB_INSTALL)/include

RISCV_CXX    := riscv64-unknown-elf-g++
RISCV_FLAGS  := -O2 -static -fno-common -fno-builtin-printf -specs=htif_nano.specs \
                -Wl,-T,$(CURDIR)/htif_bigheap.ld -include cstdint -I$(MICRO)

SCALARS  := double float int32 int64 uint32 sint32 sint64 fixed32 fixed64 sfixed32 sfixed64 bool
REPEATED := double_repeated float_repeated int32_repeated int64_repeated sint64_repeated bool_repeated bytes_repeated
STRINGS  := string string_15 string_long string_very_long bytes bytes_15 bytes_long bytes_very_long
NESTED   := nested_double nested_bool nested_string
VARINTS  := $(foreach n,00 01 02 03 04 05 06 07 08 09 10,uint64_size$(n)B uint64_size$(n)B_repeated)
FIELDS   := $(foreach n,1 2 3 4 5 6 7 8 16 32 64,ser_fields_$(n)) $(foreach n,1 2 3 4 5 6 7 8,ser10_fields_$(n))

TESTS        := $(SCALARS) $(REPEATED) $(STRINGS) $(NESTED) $(VARINTS) $(FIELDS)
BUILD        := build
RISCV_BINS   := $(addprefix $(BUILD)/,$(addsuffix .riscv,$(TESTS)))
DATA_HEADERS := $(addsuffix _data.h,$(TESTS))

.PHONY: all clean
all: $(RISCV_BINS)

# The serializer generator's primitives.proto shares its file/package name, so
# its .pb.cc can't link with the parser one. Two host binaries.
PB_GEN_SER       := $(MICRO)/primitive-tests-serializer
SER_DATA_HEADERS := $(addsuffix _data.h,$(FIELDS))
PAR_DATA_HEADERS := $(filter-out $(SER_DATA_HEADERS),$(DATA_HEADERS))

%/primitives.pb.cc: %/primitives.proto
	$(PB_INSTALL)/bin/protoc --proto_path=$(dir $<) --cpp_out=$(dir $<) $<

gen_data: gen_data.cpp $(PB_GEN)/primitives.pb.cc
	$(HOST_CXX) $(HOST_FLAGS) $^ $(PB_INSTALL)/lib/libprotobuf.a -lpthread -o $@

gen_data_ser: gen_data.cpp $(PB_GEN_SER)/primitives.pb.cc
	$(HOST_CXX) -DSER_FIELDS_TESTS $(subst $(PB_GEN),$(PB_GEN_SER),$(HOST_FLAGS)) $^ \
	  $(PB_INSTALL)/lib/libprotobuf.a -lpthread -o $@

$(PAR_DATA_HEADERS): gen_data
	./gen_data

$(SER_DATA_HEADERS): gen_data_ser
	./gen_data_ser

$(BUILD)/%.riscv: selfcontained.cpp %_data.h htif_bigheap.ld | $(BUILD)
	$(RISCV_CXX) $(RISCV_FLAGS) -DDATA_HEADER='"$*_data.h"' selfcontained.cpp -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf gen_data gen_data_ser $(BUILD) $(DATA_HEADERS)

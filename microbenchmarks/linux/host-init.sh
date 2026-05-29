#!/bin/bash
# Runs on the host (from this workload dir) on every FireMarshal build.
# Cross-compiles the protoacc smoke benchmarks for riscv64-unknown-linux-gnu
# and stages them into the rootfs overlay.
#
# Prerequisite (one-time, heavy): the cross-compiled protobuf and the protoc-
# generated sources must exist:
#   ../protobuf-x86-install, ../protobuf-riscv-install, ../primitive-tests/primitives.pb.cc
# Regenerate via:  (in ../) python3 gen-primitive-tests.py && \
#   protobuf-x86-install/bin/protoc --proto_path=primitive-tests \
#       --cpp_out=primitive-tests primitive-tests/primitives.proto
#   and the protobuf builds in build-protobuf-all.sh (modern toolchains use
#   CXXFLAGS="... -include cstdint").
set -e
cd "$(dirname "$0")"

if [ ! -f ../protobuf-riscv-install/lib/libprotobuf.a ]; then
  echo "ERROR: ../protobuf-riscv-install/lib/libprotobuf.a missing -- build protobuf first." >&2
  exit 1
fi

make
mkdir -p overlay/root
cp -f *.riscv overlay/root/

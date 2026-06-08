#!/bin/bash
set -e
cd "$(dirname "$0")"
if [ ! -f ../protobuf-riscv-install/lib/libprotobuf.a ]; then
  echo "ERROR: cross protobuf missing (see ../build-protobuf-all.sh + -include cstdint)" >&2
  exit 1
fi
make -j8
mkdir -p overlay/root/bin
cp -f build/*.riscv overlay/root/bin/

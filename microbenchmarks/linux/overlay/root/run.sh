#!/bin/bash
# Boot-time command for the protoacc-bmark FireMarshal workload.
# Runs each smoke benchmark (accelerator vs protobuf CPU baseline) and powers off.
echo "===== protoacc microbenchmarks (linux) ====="
for t in double uint64_size05B string PaccdoubleMessage; do
  echo "----- $t -----"
  /root/$t.riscv
done
echo "===== protoacc microbenchmarks done ====="
poweroff -f

#!/bin/bash
# Full Figure-11 protoacc microbenchmarks: run every binary, then power off.
echo "===== protoacc full microbenchmarks (linux) ====="
cd /root/bin
for t in *.riscv; do
  echo "----- ${t%.riscv} -----"
  ./$t
done
echo "===== protoacc microbenchmarks done ====="
poweroff -f

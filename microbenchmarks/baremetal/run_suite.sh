#!/bin/bash
# Run all bare-metal pipeline tests on the Verilator ProtoAccelRocketConfig sim
# and print a verdict table. Each test takes ~5-10 min; the full suite takes
# several hours. Pass a subset as args (e.g. ./run_suite.sh double string).
set -u
cd "$(dirname "$0")"
CY=$(cd ../../../.. && pwd)
OUT=$CY/sims/verilator/output/chipyard.harness.TestHarness.ProtoAccelRocketConfig
TESTS=("$@")
[ ${#TESTS[@]} -eq 0 ] && TESTS=($(ls build/*.riscv 2>/dev/null | xargs -n1 basename | sed 's/\.riscv//'))

declare -A R
for t in "${TESTS[@]}"; do
  echo "=== $t ==="
  timeout 2700 make -C "$CY/sims/verilator" CONFIG=ProtoAccelRocketConfig run-binary-fast \
        BINARY="$(pwd)/build/$t.riscv" > /dev/null 2>&1
  if   grep -aq "PASSED $t pipeline" "$OUT/$t.log" 2>/dev/null; then R[$t]=PASS
  elif grep -aq "FAILED $t pipeline" "$OUT/$t.log" 2>/dev/null; then R[$t]=FAIL
  else R[$t]=NO-VERDICT; fi
  echo "$t : ${R[$t]}"
done

echo "========== SUMMARY =========="
for t in "${TESTS[@]}"; do printf "%-28s %s\n" "$t" "${R[$t]}"; done

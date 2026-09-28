#!/bin/bash
# NOTE: superseded by the STEP scripts of the re-verification (docs/VERIFICATION.md); kept for history.
# Phase 8: full correctness matrix + performance + ablation. Everything sequential.
cd "$(dirname "$0")"
PIN=$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin
echo "=== correctness ==="
for i in 1 2 3; do ./run1.sh bzip2 | head -1; done
PROF_TIME=1 ./run1.sh bzip2 | head -1
PROF_TIME=4 ./run1.sh bzip2 | head -1
for i in 1 2 3; do ./run1.sh cc1 | head -1; done
for b in sgcc_base sgcc_peak cpugcc; do for i in 1 2; do ./run1.sh $b | head -1; done; done
for i in 1 2; do SETARCH=1 ./run1.sh sgcc_peak | sed 's/^/setarch-R /' | head -1; done
echo "=== stress (devirt_min_count 1, devirt_min_pct 50) ==="
for b in bzip2 cc1 sgcc_peak; do ./run1.sh $b -devirt_min_count 1 -devirt_min_pct 50 | head -1; done
echo "=== bzip2 modes / edge cases (includes a program exiting before prof_time) ==="
./test_modes.sh | tail -10
echo "=== round trip ==="
timeout 300 taskset -c 0 $PIN -t obj-intel64/project.so -- ./bzip2 -c input-long.txt > tm/rt1.bz2 2>/dev/null
./bzip2 -dc tm/rt1.bz2 | cmp -s - input-long.txt && echo "PASS tool-compress/native-decompress" || echo "FAIL tool-compress/native-decompress"
./bzip2 -c input-long.txt > tm/rt2.bz2
timeout 300 taskset -c 0 $PIN -t obj-intel64/project.so -- ./bzip2 -dc tm/rt2.bz2 2>/dev/null | cmp -s - input-long.txt && echo "PASS native-compress/tool-decompress" || echo "FAIL native-compress/tool-decompress"
echo "=== debug knobs ==="
for k in -dump_tc -dump_tc2 -dump_orig_code -dump_prof -verbose; do
  timeout 600 taskset -c 0 $PIN -t obj-intel64/project.so $k -- ./bzip2 -c tm/small1k > tm/k.out 2> tm/k.err; rc=$?
  ./bzip2 -c tm/small1k | cmp -s - tm/k.out && s=SAME || s=DIFF; echo "knob $k rc=$rc $s stderr_lines=$(wc -l < tm/k.err)"
done
timeout 600 taskset -c 0 $PIN -t obj-intel64/project.so -dump_tc2 -dump_prof -- ./bzip2 -c input-long.txt > tm/k.out 2> tm/k.err; rc=$?
cmp -s tm/k.out out_native.bz2 && s=SAME || s=DIFF; echo "knob -dump_tc2 -dump_prof (long) rc=$rc $s stderr_lines=$(wc -l < tm/k.err) csv_lines=$(wc -l < edge-profile.csv)"
echo "=== performance: bzip2 10 paired rounds ==="
./bench.sh bzip2 10
echo "=== ablation: bzip2 4 paired rounds each ==="
./bench.sh bzip2 4 | tail -1
./bench.sh bzip2 4 -no_stackfold | tail -1
./bench.sh bzip2 4 -no_addrfold | tail -1
./bench.sh bzip2 4 -no_cheap_prof -no_sparse_prof | tail -1
./bench.sh bzip2 4 -no_devirt | tail -1
./bench.sh bzip2 4 -no_reorder | tail -1
./bench.sh bzip2 4 -no_stackfold -no_cheap_prof -no_sparse_prof -no_devirt -no_reorder | tail -1
echo "=== cc1 3 paired rounds ==="
./bench.sh cc1 3 | tail -1
./bench.sh cc1 3 -no_devirt | tail -1
./bench.sh cc1 3 -no_reorder | tail -1
./bench.sh cc1 3 -no_devirt -no_reorder | tail -1
echo "=== other gcc binaries, 1 paired round ==="
./bench.sh sgcc_base 1 | tail -1
./bench.sh sgcc_peak 1 | tail -1
./bench.sh cpugcc 1 | tail -1
echo PHASE8-DONE

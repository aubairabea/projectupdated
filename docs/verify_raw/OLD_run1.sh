#!/bin/bash
# Debug helper: run one benchmark once with the tool and extra knobs, compare with the native output.
# usage: ./run1.sh <bzip2|cc1|sgcc_base|sgcc_peak|cpugcc> [tool args...]   (env SETARCH=1, PROF_TIME)
cd "$(dirname "$0")"
PIN=${PIN:-$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin}
B=$1; shift
PRE="taskset -c 0"; [ "$SETARCH" = 1 ] && PRE="setarch x86_64 -R taskset -c 0"
case $B in
  bzip2) CMD="./bzip2 -c input-long.txt"; REF=out_native.bz2 ;;
  cc1) BIN=./cc1 ;;
  sgcc_base) BIN=./sgcc_base.mytest-m64 ;;
  sgcc_peak) BIN=./sgcc_peak.mytest-m64 ;;
  cpugcc) BIN=./cpugcc_r_base.mytest-m64 ;;
esac
if [ $B != bzip2 ]; then
  CMD="$BIN 200.i -o r1_tool.s"; REF=ref_native_$B.s
  [ -f $REF ] || timeout 600 $BIN 200.i -o $REF > /dev/null 2>&1
fi
rm -f r1_tool.s
timeout 600 $PRE $PIN -t obj-intel64/project.so -prof_time ${PROF_TIME:-2} "$@" -- $CMD > r1_tool.stdout 2> bench_logs/r1.err
rc=$?
if [ $B = bzip2 ]; then cmp -s r1_tool.stdout $REF && s=SAME || s=DIFF; else cmp -s r1_tool.s $REF && s=SAME || s=DIFF; fi
echo "$B $* rc=$rc $s errors=$(grep -c ERROR bench_logs/r1.err)"
grep -v 'RTN_ReplaceProbed failed' bench_logs/r1.err | tail -${TAILN:-4}

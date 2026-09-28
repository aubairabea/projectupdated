#!/bin/bash
# Sequential correctness suite: cmp against native output + exit codes + no ERROR line.
# usage: ./check.sh [quick|full] [extra tool args...]
cd "$(dirname "$0")"
PIN=${PIN:-$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin}
TOOL=${TOOL:-obj-intel64/project.so}
MODE=${1:-quick}; [ $# -gt 0 ] && shift
EXTRA="$*"
LOG=bench_logs; mkdir -p $LOG
pass=0; fail=0
# run <name> <prefix> <prof_time> <tool-extra> <cmd with OUT placeholder> [stdin-file]
run() {
  local name=$1 pre=$2 pt=$3 ex=$4 cmd=$5 in=${6:-/dev/null}
  rm -f c_nat.* c_tool.*
  local nc=${cmd//OUT/c_nat} tc=${cmd//OUT/c_tool}
  timeout 400 $pre $nc < $in > c_nat.stdout 2> c_nat.stderr; local nrc=$?
  timeout 1200 $pre $PIN -t $TOOL -prof_time $pt $ex $EXTRA -- $tc < $in > c_tool.stdout 2> $LOG/check_$name.err; local trc=$?
  local ok=1 why=""
  [ $nrc -ne $trc ] && { ok=0; why="$why exit($nrc/$trc)"; }
  cmp -s c_nat.stdout c_tool.stdout || { ok=0; why="$why stdout"; }
  for f in c_nat.*; do
    case $f in *.stdout|*.stderr) continue;; esac
    local g=c_tool.${f#c_nat.}
    cmp -s $f $g || { ok=0; why="$why $g"; }
  done
  grep -q ERROR $LOG/check_$name.err && { ok=0; why="$why ERROR-line"; }
  local t=$(awk '/honest end-to-end total/{for(i=1;i<=NF;i++) if($i ~ /^[0-9.]+$/) v=$i} END{print v}' $LOG/check_$name.err)
  if [ $ok = 1 ]; then pass=$((pass+1)); echo "PASS $name (e2e=${t}s)"; else fail=$((fail+1)); echo "FAIL $name:$why"; fi
}
BZ="./bzip2 -c input-long.txt"
run bzip2 "taskset -c 0" 2 "" "$BZ"
run cc1 "taskset -c 0" 2 "" "./cc1 200.i -o OUT.s"
run sgcc_peak "taskset -c 0" 2 "" "./sgcc_peak.mytest-m64 200.i -o OUT.s"
run sgcc_peak_R "setarch x86_64 -R taskset -c 0" 2 "" "./sgcc_peak.mytest-m64 200.i -o OUT.s"
if [ "$MODE" = full ]; then
  run sgcc_base "taskset -c 0" 2 "" "./sgcc_base.mytest-m64 200.i -o OUT.s"
  run cpugcc "taskset -c 0" 2 "" "./cpugcc_r_base.mytest-m64 200.i -o OUT.s"
fi
echo "CHECK: pass=$pass fail=$fail"
[ $fail -eq 0 ]

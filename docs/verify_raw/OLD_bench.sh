#!/bin/bash
# Paired benchmark: native run, then tool run, N rounds, all pinned to CPU 0, run sequentially.
# usage: ./bench.sh <bzip2|cc1|sgcc_base|sgcc_peak|cpugcc> [N] [extra tool args...]
# env:   SETARCH=1 runs both under `setarch -R` ; PROF_TIME (default 2) ; TOOL=path to .so
# Improvement = (native_cpu - tool_end_to_end) / native_cpu, native_cpu = user+sys of /usr/bin/time -v.
cd "$(dirname "$0")"
PIN=${PIN:-$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin}
TOOL=${TOOL:-obj-intel64/project.so}
B=$1; N=${2:-3}; shift; [ $# -gt 0 ] && shift
EXTRA="$*"
PT=${PROF_TIME:-2}
LOG=bench_logs; mkdir -p $LOG
case $B in
  bzip2)     CMD="./bzip2 -c input-long.txt"; OUTF=""; TO=120 ;;
  cc1)       CMD="./cc1 200.i -o OUT.s"; OUTF=OUT.s; TO=400 ;;
  sgcc_base) CMD="./sgcc_base.mytest-m64 200.i -o OUT.s"; OUTF=OUT.s; TO=400 ;;
  sgcc_peak) CMD="./sgcc_peak.mytest-m64 200.i -o OUT.s"; OUTF=OUT.s; TO=400 ;;
  cpugcc)    CMD="./cpugcc_r_base.mytest-m64 200.i -o OUT.s"; OUTF=OUT.s; TO=400 ;;
  *) echo "unknown benchmark $B"; exit 2 ;;
esac
PRE="taskset -c 0"
[ "$SETARCH" = 1 ] && PRE="setarch x86_64 -R taskset -c 0"
nat_list=(); tool_list=(); imp_list=(); fails=0
for r in $(seq 1 $N); do
  rm -f out_native_$B.* out_tool_$B.*
  ncmd=${CMD//OUT/out_native_$B}; tcmd=${CMD//OUT/out_tool_$B}
  timeout $TO $PRE /usr/bin/time -v $ncmd > out_native_$B.stdout 2> $LOG/native_$B.err; nrc=$?
  timeout $((TO*3)) $PRE $PIN -t $TOOL -prof_time $PT $EXTRA -- $tcmd > out_tool_$B.stdout 2> $LOG/tool_$B.$r.err; trc=$?
  ok=1; why=""
  [ $nrc -ne $trc ] && { ok=0; why="$why exit($nrc/$trc)"; }
  cmp -s out_native_$B.stdout out_tool_$B.stdout || { ok=0; why="$why stdout-differs"; }
  if [ -n "$OUTF" ]; then cmp -s out_native_$B.s out_tool_$B.s || { ok=0; why="$why output-differs"; }; fi
  grep -q 'ERROR' $LOG/tool_$B.$r.err && { ok=0; why="$why ERROR-line"; }
  ucpu=$(awk -F': ' '/User time/{u=$2} /System time/{s=$2} END{printf "%.3f", u+s}' $LOG/native_$B.err)
  e2e=$(awk '/honest end-to-end total/{for(i=1;i<=NF;i++) if($i ~ /^[0-9.]+$/) v=$i} END{print v}' $LOG/tool_$B.$r.err)
  compat=$(awk '/Translated code run/{for(i=1;i<=NF;i++) if($i ~ /^[0-9.]+$/) v=$i} END{print v}' $LOG/tool_$B.$r.err)
  [ -z "$e2e" ] && { e2e=$compat; why="$why (no e2e line, using compat)"; }
  [ -z "$e2e" ] && { ok=0; e2e=0; why="$why no-time"; }
  imp=$(awk -v n=$ucpu -v t=$e2e 'BEGIN{printf "%.2f", (n-t)/n*100}')
  st=OK; [ $ok = 0 ] && { st=FAIL; fails=$((fails+1)); }
  printf "round %d: native_cpu=%s tool_e2e=%s compat=%s improvement=%s%% %s%s\n" $r $ucpu $e2e "$compat" $imp $st "$why"
  if [ $ok = 1 ]; then nat_list+=($ucpu); tool_list+=($e2e); imp_list+=($imp); fi
done
med() { printf "%s\n" "$@" | sort -g | awk '{a[NR]=$1} END{ if(NR==0){print "NA"} else if(NR%2){print a[(NR+1)/2]} else {printf "%.3f", (a[NR/2]+a[NR/2+1])/2} }'; }
mn()  { printf "%s\n" "$@" | sort -g | head -1; }
echo "SUMMARY $B rounds=$N ok=${#imp_list[@]} fails=$fails extra='$EXTRA' median_native=$(med ${nat_list[@]}) median_tool=$(med ${tool_list[@]}) median_improvement=$(med ${imp_list[@]})% worst_improvement=$(mn ${imp_list[@]})%"
[ $fails -eq 0 ]

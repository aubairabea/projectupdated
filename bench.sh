#!/bin/bash
# Paired benchmark (rules O1-O6 via verify_lib.sh): each round = native run (/usr/bin/time -v, separate output
# file) then tool run (taskset -c 0, separate output file); outputs deleted before the round, checked non-empty,
# md5 + cmp + exit codes, no ERROR line, TC2 committed. Raw log per round in docs/verify_raw/bench_<bench>_<n>.txt
# usage: ./bench.sh <bzip2|cc1|sgcc_base|sgcc_peak|cpugcc> [N] [extra tool args...]
#   env: PROF_TIME (default 2), TOOL (.so), SETARCH=1, TAG (raw-file prefix, default bench)
# improvement = (native user+sys - tool "honest end-to-end total") / native user+sys * 100
cd "$(dirname "$0")"; source ./verify_lib.sh
B=$1; N=${2:-3}; shift; [ $# -gt 0 ] && shift; EXTRA="$*"
TOOL=${TOOL:-obj-intel64/project.so}; PT=${PROF_TIME:-2}; T=${TAG:-bench}
imps=(); fails=0; ok=0
for r in $(seq 1 $N); do
  line=$(pair ${T}_${B}_$r $B $PT "$EXTRA" $TOOL ${SETARCH:-0})
  IFS=$'\t' read -r tag b ns nm ts tm c nrc trc errs tc2 u s cpu wall win bld com exe e2e compat <<< "$line"
  imp=$(awk -v n=$cpu -v t=$e2e 'BEGIN{ if (t=="" || n==0) print "NA"; else printf "%.2f", (n-t)/n*100 }')
  st=OK; if [ "$c" != IDENTICAL ] || [ "$nrc" != "$trc" ] || [ "$errs" != 0 ] || [ "$tc2" = NO ]; then st=FAIL; fails=$((fails+1)); else ok=$((ok+1)); imps+=($imp); fi
  echo "round $r: native_cpu=$cpu (user $u + sys $s) tool_e2e=$e2e compat=$compat improvement=$imp% cmp=$c exit=$nrc/$trc errors=$errs tc2=$tc2 $st"
done
stats() { printf "%s\n" "$@" | sort -g | awk '{a[NR]=$1; s+=$1} END{ if(!NR){print "NA NA NA NA"; exit} m=(NR%2)?a[(NR+1)/2]:(a[NR/2]+a[NR/2+1])/2; printf "median=%.2f%% mean=%.2f%% min=%.2f%% max=%.2f%%", m, s/NR, a[1], a[NR] }'; }
echo "SUMMARY $B rounds=$N ok=$ok fails=$fails extra='$EXTRA' improvement: $(stats ${imps[@]})"
[ $fails -eq 0 ]

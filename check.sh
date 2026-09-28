#!/bin/bash
# Sequential correctness suite, rules O1-O6 (see verify_lib.sh). usage: ./check.sh [quick|full] [extra tool args]
cd "$(dirname "$0")"; source ./verify_lib.sh
MODE=${1:-quick}; [ $# -gt 0 ] && shift; EXTRA="$*"
pass=0; fail=0
one() { # name bench setarch
  local line=$(pair check_$1 $2 2 "$EXTRA" obj-intel64/project.so $3)
  IFS=$'\t' read -r tag b ns nm ts tm c nrc trc errs tc2 rest <<< "$line"
  if [ "$c" = IDENTICAL ] && [ "$nrc" = "$trc" ] && [ "$errs" = 0 ] && [ "$tc2" != NO ]; then pass=$((pass+1)); echo "PASS $1 (size $ns md5 $nm, exit $nrc, tc2 $tc2)"
  else fail=$((fail+1)); echo "FAIL $1: cmp=$c exit=$nrc/$trc errors=$errs tc2=$tc2"; fi
}
one bzip2 bzip2 0; one cc1 cc1 0; one sgcc_peak sgcc_peak 0; one sgcc_peak_R sgcc_peak 1
if [ "$MODE" = full ]; then one sgcc_base sgcc_base 0; one cpugcc cpugcc 0; fi
echo "CHECK: pass=$pass fail=$fail"
[ $fail -eq 0 ]

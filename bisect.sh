#!/bin/bash
# bisect.sh <bench> <lo> <hi> [args]: find a minimal routine range [lo,hi) whose translation breaks the run
B=$1; lo=$2; hi=$3; shift 3
while [ $((hi-lo)) -gt 1 ]; do
  mid=$(( (lo+hi)/2 ))
  r=$(./run1.sh $B -dbg_first $lo -dbg_count $((mid-lo)) "$@" | head -1)
  if echo "$r" | grep -q 'rc=0 SAME'; then
     lo=$mid   # first half fine -> assume culprit in second half
     echo "ok   [$lo..) : $r"
  else
     hi=$mid
     echo "bad  [..$hi) : $r"
  fi
done
echo "CULPRIT index $lo"

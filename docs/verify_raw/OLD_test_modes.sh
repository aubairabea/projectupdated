#!/bin/bash
# bzip2 extra modes + edge cases, each compared with the native run (output bytes and exit code).
cd "$(dirname "$0")"
PIN=${PIN:-$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin}
T="$PIN -t obj-intel64/project.so -prof_time ${PROF_TIME:-2} $*"
mkdir -p tm; : > tm/empty; head -c 1024 input-long.txt > tm/small1k; head -c 3000000 /dev/urandom > tm/rand3m 2>/dev/null
[ -f tm/long.bz2 ] || ./bzip2 -c input-long.txt > tm/long.bz2
pass=0; fail=0
t() { # name, args, stdin
  local n=$1 a=$2 in=${3:-/dev/null}
  timeout 300 taskset -c 0 ./bzip2 $a < $in > tm/n.out 2> tm/n.err; local r1=$?
  timeout 900 taskset -c 0 $T -- ./bzip2 $a < $in > tm/t.out 2> tm/t.err; local r2=$?
  if [ $r1 = $r2 ] && cmp -s tm/n.out tm/t.out && ! grep -q ERROR tm/t.err; then pass=$((pass+1)); echo "PASS $n"; else fail=$((fail+1)); echo "FAIL $n (rc $r1/$r2)"; fi
}
t compress-1 "-1 -c input-long.txt"
t compress-9 "-9 -c input-long.txt"
t decompress "-d -c tm/long.bz2"
t test "-t tm/long.bz2"
t empty "-c tm/empty"
t small1k "-c tm/small1k"
t rand3m "-c tm/rand3m"
t stdin "-c" input-long.txt
t decompress-stdin "-dc" tm/long.bz2
echo "MODES: pass=$pass fail=$fail"
[ $fail = 0 ]

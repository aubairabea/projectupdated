#!/bin/bash
# bzip2 extra modes + edge cases, each compared with the native run (output bytes and exit code).
cd "$(dirname "$0")"
PIN=${PIN:-$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin}
T="$PIN -t obj-intel64/project.so -prof_time ${PROF_TIME:-2} $*"
mkdir -p tm docs/verify_raw; : > tm/empty; head -c 1024 input-long.txt > tm/small1k; head -c 3000000 /dev/urandom > tm/rand3m 2>/dev/null
[ -f tm/long.bz2 ] || ./bzip2 -c input-long.txt > tm/long.bz2
pass=0; fail=0
t() { # name, args, stdin, [empty-ok]   (O1 separate files, O2 rm before, O3 non-empty unless empty-ok, O4 md5+exit, O6)
  local n=$1 a=$2 in=${3:-/dev/null} eok=${4:-0}
  rm -f tm/n.out tm/t.out tm/n.err tm/t.err
  timeout 300 taskset -c 0 ./bzip2 $a < $in > tm/n.out 2> tm/n.err; local r1=$?
  timeout 900 taskset -c 0 $T -- ./bzip2 $a < $in > tm/t.out 2> tm/t.err; local r2=$?
  local sn=$(stat -c %s tm/n.out) st=$(stat -c %s tm/t.out) mn=$(md5sum < tm/n.out | cut -c1-32) mt=$(md5sum < tm/t.out | cut -c1-32)
  local tc2=NO; grep -q 'TC2 commit:' tm/t.err && tc2=YES; grep -q 'exited before prof_time' tm/t.err && tc2=early
  local ok=1; [ $r1 = $r2 ] || ok=0; cmp -s tm/n.out tm/t.out || ok=0; grep -q ERROR tm/t.err && ok=0; [ $tc2 = NO ] && ok=0
  [ $eok = 0 ] && { [ $sn -gt 0 ] && [ $st -gt 0 ] || ok=0; }
  { echo "# test_modes $n: native: ./bzip2 $a < $in ; tool: $T -- ./bzip2 $a < $in"; echo "native size=$sn md5=$mn exit=$r1"; echo "tool size=$st md5=$mt exit=$r2 tc2=$tc2"; cat tm/t.err; } > docs/verify_raw/modes_$n.txt
  if [ $ok = 1 ]; then pass=$((pass+1)); echo "PASS $n (size $sn md5 $mn exit $r1 tc2 $tc2)"; else fail=$((fail+1)); echo "FAIL $n (rc $r1/$r2 size $sn/$st tc2 $tc2)"; fi
}
t compress-1 "-1 -c input-long.txt"
t compress-9 "-9 -c input-long.txt"
t decompress "-d -c tm/long.bz2"
t test "-t tm/long.bz2" /dev/null 1
t empty "-c tm/empty"
t small1k "-c tm/small1k"
t rand3m "-c tm/rand3m"
t stdin "-c" input-long.txt
t decompress-stdin "-dc" tm/long.bz2
echo "MODES: pass=$pass fail=$fail"
[ $fail = 0 ]

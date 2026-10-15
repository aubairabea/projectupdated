#!/bin/bash
# Live demo for the screen recording. Rebuilds nothing.
#   ./demo.sh            bzip2: native run, tool run, cmp, improvement
#   ./demo.sh --cc1      also: base tool on cc1 (output differs) and our tool on cc1 (identical)
# env: DEMO_PAUSE=seconds between steps (default 2), NO_COLOR=1 for plain output
cd "$(dirname "$0")/../../.." || exit 1          # project root (~/Desktop/project5)
PIN=$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin
TOOL=obj-intel64/project.so
BASE=obj-intel64/bprofile-with-gearing.so
PAUSE=${DEMO_PAUSE:-2}
if [ -z "$NO_COLOR" ]; then B=$'\e[1;37;44m'; G=$'\e[1;32m'; R=$'\e[1;31m'; Y=$'\e[1;33m'; N=$'\e[0m'; fi
head() { echo; echo "${B}  $*  ${N}"; sleep "$PAUSE"; }
show() { echo "${Y}\$ $*${N}"; }
TIMEFORMAT='real %3R s   user %3U s   sys %3S s'
OUT=/tmp/demo_$$; mkdir -p $OUT
ROOT=$(pwd)
# keep the working tree clean: bzip2 -k -f writes input-long.txt.bz2, the base tool writes bprofile.out
[ -f input-long.txt.bz2 ] && cp -p input-long.txt.bz2 $OUT/saved_input.bz2

head "1/4  Native bzip2 (input-long.txt, 20.8 MB)"
rm -f input-long.txt.bz2
show "time ./bzip2 -k -f input-long.txt"
{ time ./bzip2 -k -f input-long.txt ; } 2> $OUT/native_time.txt
cat $OUT/native_time.txt
mv input-long.txt.bz2 $OUT/native.bz2

head "2/4  The same command under our Pin tool (2 s of profiling, then TC2)"
show "time pin -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -k -f input-long.txt"
{ time $PIN -t $TOOL -prof_time 2 -- ./bzip2 -k -f input-long.txt ; } 2> $OUT/tool_time.txt
cat $OUT/tool_time.txt
mv input-long.txt.bz2 $OUT/tool.bz2

head "3/4  Is the output byte-identical?"
show "cmp native.bz2 tool.bz2"
ls -l $OUT/native.bz2 $OUT/tool.bz2 | awk '{print $5, $9}'
md5sum $OUT/native.bz2 $OUT/tool.bz2
if cmp $OUT/native.bz2 $OUT/tool.bz2; then echo "${G}IDENTICAL${N}"; else echo "${R}DIFFERENT${N}"; fi
echo "ERROR lines in the tool output: $(grep -c ERROR $OUT/tool_time.txt)"

head "4/4  Improvement vs native"
read nu ns < <(awk '/^real/{print $5, $8}' $OUT/native_time.txt)
nr=$(awk '/^real/{print $2}' $OUT/native_time.txt)
e2e=$(awk '/honest end-to-end total/{print $5}' $OUT/tool_time.txt)
pr=$(awk '/Translated code run/{print $7}' $OUT/tool_time.txt)
awk -v nu=$nu -v ns=$ns -v nr=$nr -v e=$e2e -v p=$pr -v g="$G" -v n="$N" 'BEGIN{
  c = nu + ns
  printf "native CPU (user+sys) = %.3f + %.3f = %.3f s\n", nu, ns, c
  printf "tool end-to-end       = %.3f s   ->  (%.3f - %.3f) / %.3f = %s%.2f%%%s\n", e, c, e, c, g, (c-e)/c*100, n
  printf "tool printed line     = %.3f s   ->  vs native real %.3f s: %s%.2f%%%s\n", p, nr, g, (nr-p)/nr*100, n }'

if [ "$1" = "--cc1" ]; then
  head "Extra: the base tool on cc1 (its indirect-jump stub changes the flags)"
  rm -f $OUT/cc1_native.s $OUT/cc1_base.s $OUT/cc1_ours.s
  show "./cc1 200.i -o cc1_native.s"
  ./cc1 200.i -o $OUT/cc1_native.s > /dev/null 2>&1
  show "pin -t obj-intel64/bprofile-with-gearing.so -prof_time 2 -- ./cc1 200.i -o cc1_base.s"
  (cd $OUT && $PIN -t $ROOT/$BASE -prof_time 2 -- $ROOT/cc1 $ROOT/200.i -o $OUT/cc1_base.s > /dev/null 2> $OUT/cc1_base_err.txt)
  grep 'Translated code run' $OUT/cc1_base_err.txt
  show "cmp cc1_native.s cc1_base.s"
  if cmp $OUT/cc1_native.s $OUT/cc1_base.s; then echo "${G}IDENTICAL${N}"; else echo "${R}base tool: DIFFERENT output${N}"; fi
  head "Extra: our tool on cc1"
  show "pin -t obj-intel64/project.so -prof_time 2 -- ./cc1 200.i -o cc1_ours.s"
  $PIN -t $TOOL -prof_time 2 -- ./cc1 200.i -o $OUT/cc1_ours.s > /dev/null 2> $OUT/cc1_ours_err.txt
  grep -E 'honest|Translated code run' $OUT/cc1_ours_err.txt
  show "cmp cc1_native.s cc1_ours.s"
  if cmp $OUT/cc1_native.s $OUT/cc1_ours.s; then echo "${G}our tool: IDENTICAL${N}"; else echo "${R}DIFFERENT${N}"; fi
fi
[ -f $OUT/saved_input.bz2 ] && mv $OUT/saved_input.bz2 input-long.txt.bz2
rm -rf $OUT
echo

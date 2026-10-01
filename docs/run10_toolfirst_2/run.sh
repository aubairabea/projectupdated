#!/bin/bash
# 10 paired rounds, TOOL FIRST then NATIVE, exact command forms, no taskset.
cd ~/Desktop/project5
D=docs/run10_toolfirst_2
PIN=$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin
for N in $(seq 1 10); do
  rm -f input-long.txt.bz2
  { time $HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -k -f input-long.txt ; } 2> $D/r${N}_tool.txt
  mv input-long.txt.bz2 $D/r${N}_tool.bz2
  rm -f input-long.txt.bz2
  { time ./bzip2 -k -f input-long.txt ; } 2> $D/r${N}_native.txt
  mv input-long.txt.bz2 $D/r${N}_native.bz2
  c=DIFFERENT; cmp $D/r${N}_native.bz2 $D/r${N}_tool.bz2 && c=IDENTICAL
  [ -s $D/r${N}_native.bz2 ] && [ -s $D/r${N}_tool.bz2 ] || c="$c-EMPTY"
  echo "round $N: $c  $(md5sum $D/r${N}_tool.bz2 $D/r${N}_native.bz2 | tr '\n' ' ') ERROR=$(grep -c ERROR $D/r${N}_tool.txt) TC2commit=$(grep -c '\[time\] TC2 commit' $D/r${N}_tool.txt)"
done
echo RUN10-DONE

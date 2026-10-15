#!/bin/bash
# Runs for STORY/HOTSPOT/FOLD_DECISIONS/DEVIRT_PRACTICE/PROFTIME. Sequential; every output compared with native.
cd "$(dirname "$0")/../../.."
D=docs/presentation/data
PIN=$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin
TMP=$(mktemp -d)
./bzip2 -c input-long.txt > $TMP/native.bz2
./cc1 200.i -o $TMP/native.s > /dev/null 2>&1
one() { # name, prof_time, extra, bench
  local n=$1 pt=$2 ex=$3 b=$4
  ( cd $TMP && rm -f edge-profile.csv
    if [ $b = bzip2 ]; then
      echo "# command: \$PIN -t obj-intel64/project.so -prof_time $pt $ex -- ./bzip2 -c input-long.txt > tool.bz2"
      $PIN -t ~/Desktop/project5/obj-intel64/project.so -prof_time $pt $ex -- ~/Desktop/project5/bzip2 -c ~/Desktop/project5/input-long.txt 2>&1 > tool.bz2
      echo "# exit code: $?"; cmp native.bz2 tool.bz2 && echo "# output IDENTICAL to native (md5 $(md5sum < tool.bz2 | cut -c1-32))"
    else
      echo "# command: \$PIN -t obj-intel64/project.so -prof_time $pt $ex -- ./cc1 200.i -o tool.s"
      $PIN -t ~/Desktop/project5/obj-intel64/project.so -prof_time $pt $ex -- ~/Desktop/project5/cc1 ~/Desktop/project5/200.i -o tool.s 2>&1 > /dev/null
      echo "# exit code: $?"; cmp native.s tool.s && echo "# output IDENTICAL to native (md5 $(md5sum < tool.s | cut -c1-32))"
    fi ) > $D/$n.txt
  [ -f $TMP/edge-profile.csv ] && mv $TMP/edge-profile.csv $D/$n.edge-profile.csv
  echo "$n: $(grep -E 'IDENTICAL|^ERROR' $D/$n.txt | head -2 | tr '\n' ' ')"
}
one bzip2_profile 2 "-verbose -dump_prof" bzip2
for pt in 1 2 3 4; do one bzip2_prof_time_$pt $pt "-verbose" bzip2; done
one cc1_profile 2 "-verbose -dump_prof" cc1
rm -rf $TMP

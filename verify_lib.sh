#!/bin/bash
# verify_lib.sh - one paired run (native then tool) that enforces rules O1-O6 and saves a raw log.
#
#   pair <tag> <bench> <prof_time> "<tool args>" [so] [setarch]
#     bench   : bzip2 | cc1 | sgcc_base | sgcc_peak | cpugcc
#     so      : tool .so (default obj-intel64/project.so)
#     setarch : 1 -> both native and tool run under "setarch x86_64 -R"
# Prints one TSV line:
#   tag bench nat_size nat_md5 tool_size tool_md5 cmp nrc trc errors tc2 user sys cpu wall window build commit exec e2e compat
# and writes docs/verify_raw/<tag>.txt with the exact command lines at the top and all raw output.
#
# O1 native and tool write DIFFERENT files: out_native.{bz2,s} vs out_tool.{bz2,s}
# O2 both files (and the stderr files) are deleted before the round
# O3 each output must exist and be non-empty (size + md5 recorded)
# O4 cmp + both md5 + both exit codes
# O6 no ERROR line in tool stderr; TC2 committed (or a clean early exit, reported as "early")
cd "$(dirname "${BASH_SOURCE[0]}")"
PIN=${PIN:-$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin}
RAW=docs/verify_raw
mkdir -p $RAW

wall2s() { awk -F: '{ if (NF==3) print $1*3600+$2*60+$3; else print $1*60+$2 }' <<< "$1"; }
field() { awk -v k="$2" 'index($0,k){ for(i=1;i<=NF;i++) if ($i ~ /^[0-9]+(\.[0-9]+)?$/) { print $i; exit } }' "$1"; }

pair() {
  local tag=$1 bench=$2 pt=$3 targs=$4 so=${5:-obj-intel64/project.so} sa=${6:-0}
  local ext nin ncmd tcmd natf toolf
  case $bench in
    bzip2) ext=bz2 ;;
    cc1) nin=./cc1 ;;
    sgcc_base) nin=./sgcc_base.mytest-m64 ;;
    sgcc_peak) nin=./sgcc_peak.mytest-m64 ;;
    cpugcc) nin=./cpugcc_r_base.mytest-m64 ;;
  esac
  local SA=""; [ "$sa" = 1 ] && SA="setarch x86_64 -R "
  natf=out_native.${ext:-s}; toolf=out_tool.${ext:-s}
  rm -f out_native.bz2 out_tool.bz2 out_native.s out_tool.s native_time.txt tool_time.txt native_stdout.txt tool_stdout.txt   # O2
  if [ $bench = bzip2 ]; then
    ncmd="${SA}/usr/bin/time -v ./bzip2 -c input-long.txt > out_native.bz2 2> native_time.txt"
    tcmd="${SA}taskset -c 0 $PIN -t $so -prof_time $pt $targs -- ./bzip2 -c input-long.txt > out_tool.bz2 2> tool_time.txt"
  else
    ncmd="${SA}/usr/bin/time -v $nin 200.i -o out_native.s > native_stdout.txt 2> native_time.txt"
    tcmd="${SA}taskset -c 0 $PIN -t $so -prof_time $pt $targs -- $nin 200.i -o out_tool.s > tool_stdout.txt 2> tool_time.txt"
  fi
  local rawf=$RAW/$tag.txt
  { echo "# tag: $tag   date: $(date -Is)   commit: $(git rev-parse --short HEAD)"
    echo "# native command: $ncmd"
    echo "# tool   command: $tcmd"; } > $rawf
  timeout 900 bash -c "$ncmd"; local nrc=$?
  timeout 1800 bash -c "$tcmd"; local trc=$?
  # /usr/bin/time reports the program's exit status in "Exit status:"; prefer it for native
  local nexit=$(awk -F': ' '/Exit status/{print $2}' native_time.txt); [ -n "$nexit" ] && nrc=$nexit
  local ns=0 ts=0 nm=MISSING tm=MISSING
  [ -f $natf ] && { ns=$(stat -c %s $natf); nm=$(md5sum < $natf | cut -c1-32); }
  [ -f $toolf ] && { ts=$(stat -c %s $toolf); tm=$(md5sum < $toolf | cut -c1-32); }
  local c=DIFF; cmp -s $natf $toolf && c=IDENTICAL
  [ "$ns" -eq 0 ] || [ "$ts" -eq 0 ] && c="EMPTY/MISSING"                                     # O3
  if [ $bench != bzip2 ] && ! cmp -s native_stdout.txt tool_stdout.txt; then c="$c+STDOUT-DIFF"; fi
  local errs=$(grep -c ERROR tool_time.txt)
  local tc2=NO; grep -q '\[time\] TC2 commit:' tool_time.txt && tc2=YES
  grep -q 'exited before prof_time' tool_time.txt && tc2=early
  local user=$(field native_time.txt "User time") sys=$(field native_time.txt "System time")
  local wall=$(wall2s "$(awk -F': ' '/Elapsed \(wall clock\)/{print $2}' native_time.txt)")
  local cpu=$(awk -v u=$user -v s=$sys 'BEGIN{printf "%.2f", u+s}')
  local win=$(field tool_time.txt "profiling window:") bld=$(field tool_time.txt "TC2 build:") com=$(field tool_time.txt "TC2 commit:")
  local exe=$(field tool_time.txt "TC2 execution:") e2e=$(field tool_time.txt "honest end-to-end total:") compat=$(field tool_time.txt "Translated code run")
  { echo "## native /usr/bin/time -v output (native_time.txt):"; cat native_time.txt
    echo "## native output: $natf size=$ns md5=$nm exit=$nrc"
    echo "## tool stderr (tool_time.txt):"; cat tool_time.txt
    echo "## tool output: $toolf size=$ts md5=$tm exit=$trc"
    echo "## cmp $natf $toolf: $c ; ERROR lines: $errs ; TC2 committed: $tc2"
    echo "## RESULT	$tag	$bench	$ns	$nm	$ts	$tm	$c	$nrc	$trc	$errs	$tc2	$user	$sys	$cpu	$wall	$win	$bld	$com	$exe	$e2e	$compat"
  } >> $rawf
  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    $tag $bench $ns $nm $ts $tm "$c" $nrc $trc $errs $tc2 "$user" "$sys" $cpu "$wall" "$win" "$bld" "$com" "$exe" "$e2e" "$compat"
}

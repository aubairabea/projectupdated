#!/bin/bash
# STEP 3: correctness of every benchmark with rules O1-O6, plus the O5 proof. Sequential.
cd "$(dirname "$0")"; source ./verify_lib.sh
chmod u+x bzip2 cc1 sgcc_base.mytest-m64 sgcc_peak.mytest-m64 cpugcc_r_base.mytest-m64
S=docs/verify_raw/step3_summary.tsv
echo -e "tag\tbench\tnat_size\tnat_md5\ttool_size\ttool_md5\tcmp\tnat_exit\ttool_exit\tERROR\tTC2\tuser\tsys\tcpu\twall\twindow\tbuild\tcommit\texec\te2e\tcompat" > $S
printf 'int f(int x) { return x + 1; }\n' > tm_tiny.i
o5() { # O5 proof for the current out_tool.* file and a second native input
  local b=$1 f=$2 bin=$3 raw=docs/verify_raw/step3_O5_$1.txt
  { echo "# O5 proof for $b: flip one byte in a copy of $f, cmp must report a difference"
    cp $f o5_copy; printf '\xff' | dd of=o5_copy bs=1 seek=100 count=1 conv=notrunc 2>/dev/null
    echo "\$ cmp $f o5_copy"; cmp $f o5_copy; echo "cmp exit code: $? (1 = difference detected)"
    md5sum $f o5_copy
    if [ $b = bzip2 ]; then
      echo "\$ ./bzip2 -c input-long.txt > o5_a ; ./bzip2 -c tm/small1k > o5_b ; cmp o5_a o5_b"
      ./bzip2 -c input-long.txt > o5_a; ./bzip2 -c tm/small1k > o5_b
    else
      echo "\$ $bin 200.i -o o5_a ; $bin tm_tiny.i -o o5_b ; cmp o5_a o5_b"
      $bin 200.i -o o5_a > /dev/null 2>&1; $bin tm_tiny.i -o o5_b > /dev/null 2>&1
    fi
    cmp o5_a o5_b; echo "cmp exit code: $? (1 = the two native outputs differ)"; ls -l o5_a o5_b; md5sum o5_a o5_b
  } > $raw 2>&1; rm -f o5_copy o5_a o5_b
  grep 'cmp exit code' $raw | sed "s/^/O5 $b: /"
}
mkdir -p tm; head -c 1024 input-long.txt > tm/small1k
for b in bzip2 cc1 sgcc_base sgcc_peak cpugcc; do
  case $b in bzip2) bin=./bzip2; f=out_tool.bz2;; cc1) bin=./cc1; f=out_tool.s;; sgcc_base) bin=./sgcc_base.mytest-m64; f=out_tool.s;; sgcc_peak) bin=./sgcc_peak.mytest-m64; f=out_tool.s;; cpugcc) bin=./cpugcc_r_base.mytest-m64; f=out_tool.s;; esac
  for r in 1 2; do pair s3_${b}_r$r $b 2 "" | tee -a $S | cut -f1,3-11; [ $r = 1 ] && o5 $b $f $bin; done
done
for r in 1 2; do pair s3_sgcc_peak_setarchR_r$r sgcc_peak 2 "" obj-intel64/project.so 1 | tee -a $S | cut -f1,3-11; done
pair s3_bzip2_prof1 bzip2 1 "" | tee -a $S | cut -f1,3-11
pair s3_bzip2_prof4 bzip2 4 "" | tee -a $S | cut -f1,3-11
for b in bzip2 cc1 sgcc_peak; do pair s3_stress_$b $b 2 "-devirt_min_count 1 -devirt_min_pct 50 -verbose" | tee -a $S | cut -f1,3-11; grep '^TC2:' docs/verify_raw/s3_stress_$b.txt; done
echo "=== bzip2 modes / empty / tiny (test_modes.sh) ==="; ./test_modes.sh
echo "=== round trip ==="
{ echo "# round trip 1: taskset -c 0 \$PIN -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -c input-long.txt > rt_tool.bz2 ; ./bzip2 -dc rt_tool.bz2 > rt_back1.txt ; cmp rt_back1.txt input-long.txt"
  rm -f rt_tool.bz2 rt_back1.txt rt_nat.bz2 rt_back2.txt
  taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -c input-long.txt > rt_tool.bz2 2> rt_err1.txt; echo "tool exit $?"; cat rt_err1.txt
  ./bzip2 -dc rt_tool.bz2 > rt_back1.txt; echo "native decompress exit $?"; ls -l rt_tool.bz2 rt_back1.txt; md5sum rt_back1.txt input-long.txt
  cmp rt_back1.txt input-long.txt && echo "ROUNDTRIP1 IDENTICAL" || echo "ROUNDTRIP1 DIFFERENT"
  echo "# round trip 2: ./bzip2 -c input-long.txt > rt_nat.bz2 ; taskset -c 0 \$PIN -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -dc rt_nat.bz2 > rt_back2.txt ; cmp rt_back2.txt input-long.txt"
  ./bzip2 -c input-long.txt > rt_nat.bz2; echo "native compress exit $?"
  taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -dc rt_nat.bz2 > rt_back2.txt 2> rt_err2.txt; echo "tool decompress exit $?"; cat rt_err2.txt
  ls -l rt_nat.bz2 rt_back2.txt; md5sum rt_back2.txt input-long.txt
  cmp rt_back2.txt input-long.txt && echo "ROUNDTRIP2 IDENTICAL" || echo "ROUNDTRIP2 DIFFERENT"
} > docs/verify_raw/step3_roundtrip.txt 2>&1
grep -E 'ROUNDTRIP|exit|ERROR' docs/verify_raw/step3_roundtrip.txt
rm -f rt_*.bz2 rt_back*.txt rt_err*.txt
echo STEP3-DONE

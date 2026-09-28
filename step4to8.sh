#!/bin/bash
# STEPS 4-8 of the re-verification. Sequential; every run logged in docs/verify_raw/.
cd "$(dirname "$0")"; source ./verify_lib.sh
BK=$HOME/Desktop/project4/obj-intel64/project.so     # backup build (project4_BACKUP_working does not exist)
H="tag\tbench\tnat_size\tnat_md5\ttool_size\ttool_md5\tcmp\tnat_exit\ttool_exit\tERROR\tTC2\tuser\tsys\tcpu\twall\twindow\tbuild\tcommit\texec\te2e\tcompat"
echo "=== STEP 4: 10 rounds, the manual procedure ==="
echo -e "$H" > docs/verify_raw/step4_summary.tsv
for r in $(seq 1 10); do pair s4_r$r bzip2 2 "" | tee -a docs/verify_raw/step4_summary.tsv | cut -f1,7,14,16-21; done
echo "=== STEP 5: outside clock, 5 rounds ==="
echo -e "round\tnat_user\tnat_sys\tnat_cpu\tnat_wall\tinternal_e2e\text_wall\text_user\text_sys\tcmp\ttool_exit\terrors" > docs/verify_raw/step5_summary.tsv
for r in 1 2 3 4 5; do
  raw=docs/verify_raw/s5_r$r.txt
  rm -f out_native.bz2 out_tool.bz2 native_time.txt tool_full.txt
  n="/usr/bin/time -v ./bzip2 -c input-long.txt > out_native.bz2 2> native_time.txt"
  t="/usr/bin/time -v taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -c input-long.txt > out_tool.bz2 2> tool_full.txt"
  echo -e "# native command: $n\n# tool command: $t" > $raw
  timeout 300 bash -c "$n"; timeout 900 bash -c "$t"; trc=$(awk -F': ' '/Exit status/{print $2}' tool_full.txt)
  c=DIFF; cmp -s out_native.bz2 out_tool.bz2 && c=IDENTICAL; [ -s out_native.bz2 ] && [ -s out_tool.bz2 ] || c=EMPTY
  nu=$(field native_time.txt "User time"); ns=$(field native_time.txt "System time"); nw=$(wall2s "$(awk -F': ' '/Elapsed \(wall/{print $2}' native_time.txt)")
  e2e=$(field tool_full.txt "honest end-to-end total:"); eu=$(field tool_full.txt "User time"); es=$(field tool_full.txt "System time")
  ew=$(wall2s "$(awk -F': ' '/Elapsed \(wall/{print $2}' tool_full.txt)")
  { echo "## native_time.txt"; cat native_time.txt; echo "## tool_full.txt (tool stderr + /usr/bin/time of the whole pin run)"; cat tool_full.txt
    echo "## sizes/md5"; ls -l out_native.bz2 out_tool.bz2; md5sum out_native.bz2 out_tool.bz2; echo "## cmp: $c"; } >> $raw
  line="$r\t$nu\t$ns\t$(awk -v a=$nu -v b=$ns 'BEGIN{printf "%.2f",a+b}')\t$nw\t$e2e\t$ew\t$eu\t$es\t$c\t$trc\t$(grep -c ERROR tool_full.txt)"
  echo -e "$line" | tee -a docs/verify_raw/step5_summary.tsv
done
echo "=== STEP 6: bench.sh bzip2 5 ==="
TAG=s6 ./bench.sh bzip2 5 | tee docs/verify_raw/step6_bench_console.txt
echo "=== STEP 7: backup vs new, 5 rounds (native, new, backup per round) ==="
echo -e "round\tnat_cpu\tnew_e2e\tnew_compat\tbackup_e2e\tbackup_compat\tnew_cmp\tbackup_cmp\tnew_err\tbackup_err\tnew_exit\tbackup_exit" > docs/verify_raw/step7_summary.tsv
for r in 1 2 3 4 5; do
  raw=docs/verify_raw/s7_r$r.txt
  rm -f out_native.bz2 out_tool.bz2 out_backup.bz2 native_time.txt tool_time.txt backup_time.txt
  n="/usr/bin/time -v ./bzip2 -c input-long.txt > out_native.bz2 2> native_time.txt"
  t="taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -c input-long.txt > out_tool.bz2 2> tool_time.txt"
  b="taskset -c 0 $PIN -t $BK -prof_time 2 -- ./bzip2 -c input-long.txt > out_backup.bz2 2> backup_time.txt"
  echo -e "# native: $n\n# new: $t\n# backup: $b" > $raw
  timeout 300 bash -c "$n"; timeout 900 bash -c "$t"; tr=$?; timeout 900 bash -c "$b"; br=$?
  c1=DIFF; cmp -s out_native.bz2 out_tool.bz2 && c1=IDENTICAL; c2=DIFF; cmp -s out_native.bz2 out_backup.bz2 && c2=IDENTICAL
  [ -s out_tool.bz2 ] || c1=EMPTY; [ -s out_backup.bz2 ] || c2=EMPTY
  { echo "## native"; cat native_time.txt; echo "## new"; cat tool_time.txt; echo "## backup"; cat backup_time.txt; ls -l out_*.bz2; md5sum out_*.bz2; } >> $raw
  cpu=$(awk -v a=$(field native_time.txt "User time") -v b=$(field native_time.txt "System time") 'BEGIN{printf "%.2f",a+b}')
  bk_e2e=$(field backup_time.txt "end-to-end"); bk_c=$(field backup_time.txt "Translated code run")
  echo -e "$r\t$cpu\t$(field tool_time.txt 'honest end-to-end total:')\t$(field tool_time.txt 'Translated code run')\t$bk_e2e\t$bk_c\t$c1\t$c2\t$(grep -c ERROR tool_time.txt)\t$(grep -c ERROR backup_time.txt)\t$tr\t$br" | tee -a docs/verify_raw/step7_summary.tsv
done
for b in cc1 sgcc_peak; do pair s7_backup_$b $b 2 "" $BK | cut -f1,3-11,20,21 | tee -a docs/verify_raw/step7_summary.tsv; done
echo "=== STEP 8: knob evidence (tool only, -verbose, bzip2) ==="
for k in "" -no_stackfold -no_addrfold "-no_cheap_prof -no_sparse_prof" -no_devirt -no_reorder "-no_stackfold -no_addrfold -no_cheap_prof -no_sparse_prof -no_devirt -no_reorder"; do
  f=docs/verify_raw/s8_evidence_$(echo "default$k" | tr -d ' -').txt
  echo "# command: taskset -c 0 \$PIN -t obj-intel64/project.so -prof_time 2 -verbose $k -- ./bzip2 -c input-long.txt > /dev/null" > $f
  timeout 900 taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -verbose $k -- ./bzip2 -c input-long.txt 2>> $f > /dev/null
  echo "[$k]"; grep -E '^translated|^stack fold|^TC2:' $f
done
echo "=== STEP 8: ablation, 4 paired rounds each ==="
echo -e "$H\tconfig" > docs/verify_raw/step8_summary.tsv
i=0
for k in "" -no_stackfold -no_addrfold "-no_cheap_prof -no_sparse_prof" -no_devirt -no_reorder "-no_stackfold -no_addrfold -no_cheap_prof -no_sparse_prof -no_devirt -no_reorder"; do
  i=$((i+1))
  for r in 1 2 3 4; do pair s8_c${i}_r$r bzip2 2 "$k" | sed "s/\$/\t[$k]/" | tee -a docs/verify_raw/step8_summary.tsv | cut -f1,7,14,20,22; done
done
echo STEP4TO8-DONE

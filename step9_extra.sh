#!/bin/bash
cd "$(dirname "$0")"; source ./verify_lib.sh
echo -e "tag\tbench\tnat_size\tnat_md5\ttool_size\ttool_md5\tcmp\tnat_exit\ttool_exit\tERROR\tTC2\tuser\tsys\tcpu\twall\twindow\tbuild\tcommit\texec\te2e\tcompat\tconfig" > docs/verify_raw/cc1_ablation_summary.tsv
for k in "" -no_devirt -no_reorder "-no_devirt -no_reorder"; do
  for r in 1 2; do pair "x_cc1_$(echo "c$k" | tr -d ' -')_r$r" cc1 2 "$k" | sed "s/\$/\t[$k]/" | tee -a docs/verify_raw/cc1_ablation_summary.tsv | cut -f1,7,14,20,22; done
done
f=docs/verify_raw/x_dump_prof_bzip2.txt
echo "# command: taskset -c 0 \$PIN -t obj-intel64/project.so -prof_time 2 -dump_prof -- ./bzip2 -c input-long.txt > out_tool.bz2 ; awk sum of column 2 of edge-profile.csv" > $f
rm -f out_tool.bz2 edge-profile.csv
taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -dump_prof -- ./bzip2 -c input-long.txt > out_tool.bz2 2>> $f
echo "cmp vs out_native.bz2 from a fresh native run:" >> $f; ./bzip2 -c input-long.txt > out_native.bz2; cmp out_native.bz2 out_tool.bz2 && echo IDENTICAL >> $f
echo "csv rows: $(wc -l < edge-profile.csv)  sum of block counts: $(awk -F', ' '{s+=$2} END{printf "%.4g", s}' edge-profile.csv)" >> $f
tail -3 $f
echo EXTRA-DONE

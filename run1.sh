#!/bin/bash
# Debug helper: one paired run (fresh native run every time, rules O1-O6), prints the result line.
# usage: ./run1.sh <bzip2|cc1|sgcc_base|sgcc_peak|cpugcc> [tool args...]   (env SETARCH=1, PROF_TIME, TOOL)
cd "$(dirname "$0")"; source ./verify_lib.sh
B=$1; shift
pair run1_$B $B ${PROF_TIME:-2} "$*" ${TOOL:-obj-intel64/project.so} ${SETARCH:-0} | cut -f1,3-11,20

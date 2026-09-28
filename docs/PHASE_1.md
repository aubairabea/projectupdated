# Phase 1 — Baseline and measurement harness

## Baseline (unchanged base tool = bprofile-with-gearing.cpp built as project.so, taskset -c 0)
| benchmark | native CPU (user+sys) | base tool "Translated code run" | output |
|---|---|---|---|
| bzip2 -c input-long.txt | 4.48 s (4.37 s in a 2nd run) | 6.46 s / 6.27 s (−43%) | identical |
| cc1 200.i | 22.99 s | 48.1 s (create_tc alone 8.5 s) | **WRONG** (differs at byte 6669460) |

cc1 goes wrong as predicted by pitfall P1: the base tool's indirect-jump profiling stub executes `AND RAX,3`,
which overwrites the flags, and cc1 has flags live across indirect jumps. The base tool also printed
10 "RTN_ReplaceProbed failed" lines on cc1.

## Harness
- `bench.sh <bzip2|cc1|sgcc_base|sgcc_peak|cpugcc> [N] [extra tool args]`: N paired rounds, native run
  (`/usr/bin/time -v`, taskset -c 0) directly followed by the tool run (taskset -c 0). Checks exit codes,
  stdout and output file with cmp, rejects a run with any "ERROR" line, parses
  "[time] honest end-to-end total" (falls back to the compat line only for the base tool, flagged),
  prints each round and a SUMMARY line with medians and the worst round. `SETARCH=1` runs under
  `setarch -R`; `PROF_TIME` sets -prof_time; `TOOL` selects another .so for A/B.
- `check.sh [quick|full] [extra args]`: sequential correctness suite (bzip2, cc1, sgcc_peak, sgcc_peak under
  setarch -R; `full` adds sgcc_base and cpugcc_r_base).

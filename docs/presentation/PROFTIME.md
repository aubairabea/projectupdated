# bzip2 with -prof_time 1, 2, 3, 4

One run each: `$PIN -t obj-intel64/project.so -prof_time N -verbose -- ./bzip2 -c input-long.txt` (no taskset).
Sources: `data/bzip2_prof_time_1.txt` ... `data/bzip2_prof_time_4.txt` (tool `-verbose` statistics and timing
lines; each output compared with a native run). These four runs were not paired with native runs.

| prof_time | window (s) | hot routines in TC2 | TC2 bytes | folded routines | loads replaced | address folds | jumps inverted | jumps added | jumps removed | cold blocks | devirt calls/jumps | TC2 build (s) | TC2 execution (s) | end-to-end (s) | printed line (s) | output |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1.000 | 46 | 48003 | 77 | 1914 | 130 | 118 | 97 | 44 | 758 | 0/0 | 0.006 | 3.732 | 4.738 | 4.732 | IDENTICAL |
| 2 | 2.002 | 46 | 48003 | 77 | 1914 | 130 | 118 | 97 | 44 | 758 | 0/0 | 0.010 | 2.604 | 4.617 | 4.604 | IDENTICAL |
| 3 | 3.011 | 46 | 48003 | 77 | 1914 | 130 | 118 | 97 | 44 | 758 | 0/0 | 0.005 | 1.479 | 4.494 | 4.479 | IDENTICAL |
| 4 | 4.002 | 46 | 48003 | 77 | 1914 | 130 | 118 | 97 | 44 | 758 | 0/0 | 0.009 | 1.762 | 5.774 | 5.762 | IDENTICAL |

Notes on the columns (what the tool reports, not interpretation):
- "folded routines", "loads replaced" and "address folds" come from the stack-fold analysis, which runs once
  before the program starts (it does not use the profile).
- The tool does not report a count of reordered blocks; the reordering columns are its jump statistics and
  the number of blocks placed in the cold region.


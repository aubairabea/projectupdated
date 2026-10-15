# De-virtualization in practice: cc1, one run

Run: `$PIN -t obj-intel64/project.so -prof_time 2 -verbose -dump_prof -- ./cc1 200.i -o tool.s`
(`data/cc1_profile.txt`, output identical to native, md5 290a33647ad8fe87f5776a7ff7aee8cd).

## What the tool itself reports (`data/cc1_profile.txt`)

- indirect jump/call sites profiled in TC: **935**
- de-virtualized in TC2: **81 indirect calls + 170 indirect jumps** (251 sites, 449 guarded targets in total)

The tool does not print why a site was not guarded, so we re-applied its rule (project.cpp:1749-1768: at least
100 executions, at most 3 targets each with at least 10%, together at least 80%, target must have a TC2 copy,
call targets must be routine entries) to the profile the tool dumped (`data/cc1_profile.edge-profile.csv`) with
`data/devirt_sites.py` (result: `data/devirt_sites_cc1.json`). The dump stores the targets per basic block, so a
block with several indirect sites cannot be split; those are counted separately.

## Breakdown of the 935 sites

| category | sites | how determined |
|---|---|---|
| never executed during the window | 556 | in no executed block of the profile |
| executed, ran fewer than 100 times | 103 | rule, single-site blocks |
| executed, chosen targets would cover less than 80% | 18 | rule, single-site blocks |
| executed, no eligible target (every target below 10% or without a TC2 copy) | 2 | rule, single-site blocks |
| executed, in a block with several indirect sites (not attributable) | 7 (in 3 blocks) | targets mixed in the dump |
| **qualified and guarded** (reconstruction) | **249**: 79 calls + 170 jumps | rule, single-site blocks |

Cross-check: the reconstruction finds 79 calls + 170 jumps in single-site blocks; the tool guarded 81 + 170. The difference of 2 calls lies within the 7 sites that cannot be attributed.

Graph: `graphs/devirt_sites.png`.

Effect on time (from docs/VERIFICATION.md, cc1 ablation, mean of 2 paired rounds): default -13.57% vs native,
with `-no_devirt` -22.81% (`docs/verify_raw/cc1_ablation_summary.tsv`).


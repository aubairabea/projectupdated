# Audit (re-verified)

This file was rewritten during the independent re-verification. The earlier audit numbers came from scripts
that compared against a cached native reference and did not check empty files, md5 or TC2 commits. Every
number below comes from `docs/verify_raw/`, measured at commit 58df1e1. Details, script fixes and the list of
changed numbers: docs/VERIFICATION.md.

## Correctness (separate native/tool output files, fresh native run each round, O1–O6)
All IDENTICAL, same exit codes, 0 ERROR lines, TC2 committed:
- bzip2 ×2, plus -prof_time 1 and -prof_time 4;
- cc1 ×2, sgcc_base ×2, sgcc_peak ×2, cpugcc_r_base ×2;
- sgcc_peak under setarch -R ×2;
- devirt stress (`-devirt_min_count 1 -devirt_min_pct 50`) on bzip2, cc1 and sgcc_peak.

Also PASS:
- bzip2 -1, -9, -d, -t, stdin, -dc stdin, an empty file, a 1 KB file, 3 MB of random data;
- both round trips;
- O5: a one-byte flip is detected for all 5 benchmarks.

Debug knobs: each of `-dump_tc`, `-dump_tc2`, `-dump_orig_code`, `-dump_prof`, `-verbose` was re-run on the full bzip2 input with O1–O6 (`x_knob-*.txt`): IDENTICAL output, 0 ERROR lines, TC2 committed. The dumps printed 35982, 12344, 24106, 39 and 68 raw-log lines respectively.

### STEP 3 correctness table

| run | native size / md5 | tool size / md5 | cmp | exit native/tool | ERROR lines | TC2 committed |
|---|---|---|---|---|---|---|
| s3_bzip2_r1 | 444576 / 8722c10f2ac1dfde915e79b1943a27d5 | 444576 / 8722c10f2ac1dfde915e79b1943a27d5 | IDENTICAL | 0/0 | 0 | YES |
| s3_bzip2_r2 | 444576 / 8722c10f2ac1dfde915e79b1943a27d5 | 444576 / 8722c10f2ac1dfde915e79b1943a27d5 | IDENTICAL | 0/0 | 0 | YES |
| s3_cc1_r1 | 6783961 / 290a33647ad8fe87f5776a7ff7aee8cd | 6783961 / 290a33647ad8fe87f5776a7ff7aee8cd | IDENTICAL | 0/0 | 0 | YES |
| s3_cc1_r2 | 6783961 / 290a33647ad8fe87f5776a7ff7aee8cd | 6783961 / 290a33647ad8fe87f5776a7ff7aee8cd | IDENTICAL | 0/0 | 0 | YES |
| s3_sgcc_base_r1 | 3873835 / 4c7942fda4169bc38439005ea539304a | 3873835 / 4c7942fda4169bc38439005ea539304a | IDENTICAL | 0/0 | 0 | YES |
| s3_sgcc_base_r2 | 3873835 / 4c7942fda4169bc38439005ea539304a | 3873835 / 4c7942fda4169bc38439005ea539304a | IDENTICAL | 0/0 | 0 | YES |
| s3_sgcc_peak_r1 | 3873835 / 4c7942fda4169bc38439005ea539304a | 3873835 / 4c7942fda4169bc38439005ea539304a | IDENTICAL | 0/0 | 0 | YES |
| s3_sgcc_peak_r2 | 3873835 / 4c7942fda4169bc38439005ea539304a | 3873835 / 4c7942fda4169bc38439005ea539304a | IDENTICAL | 0/0 | 0 | YES |
| s3_cpugcc_r1 | 3873835 / 4c7942fda4169bc38439005ea539304a | 3873835 / 4c7942fda4169bc38439005ea539304a | IDENTICAL | 0/0 | 0 | YES |
| s3_cpugcc_r2 | 3873835 / 4c7942fda4169bc38439005ea539304a | 3873835 / 4c7942fda4169bc38439005ea539304a | IDENTICAL | 0/0 | 0 | YES |
| s3_sgcc_peak_setarchR_r1 | 3873835 / 4c7942fda4169bc38439005ea539304a | 3873835 / 4c7942fda4169bc38439005ea539304a | IDENTICAL | 0/0 | 0 | YES |
| s3_sgcc_peak_setarchR_r2 | 3873835 / 4c7942fda4169bc38439005ea539304a | 3873835 / 4c7942fda4169bc38439005ea539304a | IDENTICAL | 0/0 | 0 | YES |
| s3_bzip2_prof1 | 444576 / 8722c10f2ac1dfde915e79b1943a27d5 | 444576 / 8722c10f2ac1dfde915e79b1943a27d5 | IDENTICAL | 0/0 | 0 | YES |
| s3_bzip2_prof4 | 444576 / 8722c10f2ac1dfde915e79b1943a27d5 | 444576 / 8722c10f2ac1dfde915e79b1943a27d5 | IDENTICAL | 0/0 | 0 | YES |
| s3_stress_bzip2 | 444576 / 8722c10f2ac1dfde915e79b1943a27d5 | 444576 / 8722c10f2ac1dfde915e79b1943a27d5 | IDENTICAL | 0/0 | 0 | YES |
| s3_stress_cc1 | 6783961 / 290a33647ad8fe87f5776a7ff7aee8cd | 6783961 / 290a33647ad8fe87f5776a7ff7aee8cd | IDENTICAL | 0/0 | 0 | YES |
| s3_stress_sgcc_peak | 3873835 / 4c7942fda4169bc38439005ea539304a | 3873835 / 4c7942fda4169bc38439005ea539304a | IDENTICAL | 0/0 | 0 | YES |

### STEP 4: bzip2, 10 rounds (manual procedure)

| round | native user | native sys | native CPU | native wall | window | TC2 build | TC2 commit | TC2 exec | end-to-end | printed line | improvement | cmp | ERROR | sum check | e2e>=printed | printed=2+exec | window~2 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 4.84 | 0.03 | 4.87 | 4.90 | 2.001 | 0.008 | 0.000 | 2.046 | 4.055 | 4.046 | 16.74% | IDENTICAL | 0 | ok (4.055) | ok | ok (4.046) | ok |
| 2 | 4.83 | 0.05 | 4.88 | 4.89 | 2.002 | 0.005 | 0.000 | 2.166 | 4.174 | 4.166 | 14.47% | IDENTICAL | 0 | ok (4.173) | ok | ok (4.166) | ok |
| 3 | 4.69 | 0.04 | 4.73 | 4.74 | 2.001 | 0.004 | 0.000 | 1.959 | 3.964 | 3.959 | 16.19% | IDENTICAL | 0 | ok (3.964) | ok | ok (3.959) | ok |
| 4 | 4.91 | 0.01 | 4.92 | 4.93 | 2.002 | 0.005 | 0.000 | 1.935 | 3.942 | 3.935 | 19.88% | IDENTICAL | 0 | ok (3.942) | ok | ok (3.935) | ok |
| 5 | 4.65 | 0.01 | 4.66 | 4.68 | 2.001 | 0.009 | 0.000 | 2.417 | 4.426 | 4.417 | 5.02% | IDENTICAL | 0 | ok (4.427) | ok | ok (4.417) | ok |
| 6 | 4.64 | 0.02 | 4.66 | 4.67 | 2.002 | 0.005 | 0.000 | 2.049 | 4.056 | 4.049 | 12.96% | IDENTICAL | 0 | ok (4.056) | ok | ok (4.049) | ok |
| 7 | 4.73 | 0.02 | 4.75 | 4.75 | 2.001 | 0.004 | 0.000 | 1.980 | 3.984 | 3.980 | 16.13% | IDENTICAL | 0 | ok (3.985) | ok | ok (3.980) | ok |
| 8 | 4.77 | 0.05 | 4.82 | 4.85 | 2.009 | 0.006 | 0.000 | 2.037 | 4.052 | 4.037 | 15.93% | IDENTICAL | 0 | ok (4.052) | ok | ok (4.037) | ok |
| 9 | 4.72 | 0.03 | 4.75 | 4.77 | 2.001 | 0.007 | 0.000 | 2.252 | 4.259 | 4.252 | 10.34% | IDENTICAL | 0 | ok (4.260) | ok | ok (4.252) | ok |
| 10 | 4.71 | 0.03 | 4.74 | 4.75 | 2.002 | 0.005 | 0.000 | 2.044 | 4.051 | 4.044 | 14.54% | IDENTICAL | 0 | ok (4.051) | ok | ok (4.044) | ok |

Improvement over the 10 rounds: median 15.23%, mean 14.22%, min 5.02%, max 19.88%; rounds above 5%: 10/10; cmp IDENTICAL: 10/10.
Arithmetic check failures: none

Hand arithmetic, first 3 rows:
- round 1: native CPU = 4.84 + 0.03 = 4.87 s; (4.87 - 4.055) / 4.87 = 0.815 / 4.87 = 16.74%
- round 2: native CPU = 4.83 + 0.05 = 4.88 s; (4.88 - 4.174) / 4.88 = 0.706 / 4.88 = 14.47%
- round 3: native CPU = 4.69 + 0.04 = 4.73 s; (4.73 - 3.964) / 4.73 = 0.766 / 4.73 = 16.19%

### STEP 5: outside clock (whole Pin run measured by /usr/bin/time), 5 rounds

| round | native CPU | native wall | internal end-to-end | external wall | external user+sys | external wall - internal | improvement (internal) | improvement (external wall) | improvement (external user+sys) | cmp |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 4.81 | 4.84 | 4.026 | 4.43 | 4.25 | 0.404 | 16.30% | 7.90% | 11.64% | IDENTICAL |
| 2 | 4.70 | 4.73 | 4.114 | 4.49 | 4.32 | 0.376 | 12.47% | 4.47% | 8.09% | IDENTICAL |
| 3 | 4.68 | 4.70 | 4.153 | 4.53 | 4.34 | 0.377 | 11.26% | 3.21% | 7.26% | IDENTICAL |
| 4 | 4.71 | 4.75 | 3.954 | 4.32 | 4.15 | 0.366 | 16.05% | 8.28% | 11.89% | IDENTICAL |
| 5 | 4.71 | 4.72 | 4.007 | 4.41 | 4.26 | 0.403 | 14.93% | 6.37% | 9.55% | IDENTICAL |

Internal: median 14.93%, mean 14.20%, min 11.26%, max 16.30%

External wall: median 6.37%, mean 6.04%, min 3.21%, max 8.28%; rounds above 5%: 3/5

External user+sys: median 9.55%, mean 9.69%, min 7.26%, max 11.89%

### STEP 6: ./bench.sh bzip2 5 recomputed from its raw files

- round 1: native CPU 4.65, e2e 3.885 -> 16.45%  (cmp IDENTICAL)
- round 2: native CPU 4.64, e2e 3.962 -> 14.61%  (cmp IDENTICAL)
- round 3: native CPU 4.54, e2e 3.919 -> 13.68%  (cmp IDENTICAL)
- round 4: native CPU 4.60, e2e 4.073 -> 11.46%  (cmp IDENTICAL)
- round 5: native CPU 4.72, e2e 4.068 -> 13.81%  (cmp IDENTICAL)

median 13.81%, mean 14.00%, min 11.46%, max 16.45%

### STEP 7: new vs backup (~/Desktop/project4 build), 5 rounds, same native run per round

| round | native CPU | new e2e | new improvement | backup e2e | backup improvement | new cmp | backup cmp |
|---|---|---|---|---|---|---|---|
| 1 | 4.69 | 3.983 | 15.07% | 4.0093 | 14.51% | IDENTICAL | IDENTICAL |
| 2 | 4.73 | 4.075 | 13.85% | 3.9938 | 15.56% | IDENTICAL | IDENTICAL |
| 3 | 4.84 | 4.068 | 15.95% | 3.9270 | 18.86% | IDENTICAL | IDENTICAL |
| 4 | 4.81 | 4.105 | 14.66% | 4.0789 | 15.20% | IDENTICAL | IDENTICAL |
| 5 | 4.77 | 4.086 | 14.34% | 4.0552 | 14.99% | IDENTICAL | IDENTICAL |

New: median e2e 4.075 s, improvement median 14.66%, mean 14.77%, min 13.85%, max 15.95%

Backup: median e2e 4.0093 s, improvement median 15.20%, mean 15.83%, min 14.51%, max 18.86%

Paired difference new - backup (s): -0.026, +0.081, +0.141, +0.026, +0.031; mean +0.051 s

### STEP 8: ablation, bzip2, 4 paired rounds each

| config | median native CPU | median end-to-end | median paired improvement | min | max | cmp |
|---|---|---|---|---|---|---|
| default | 4.665 | 3.941 | 15.86% | 12.84% | 17.21% | 4/4 IDENTICAL |
| -no_stackfold | 4.620 | 4.987 | -9.03% | -13.30% | -6.11% | 4/4 IDENTICAL |
| -no_addrfold | 4.620 | 4.489 | 2.73% | 1.52% | 5.03% | 4/4 IDENTICAL |
| -no_cheap_prof -no_sparse_prof | 4.580 | 4.268 | 7.61% | 4.81% | 8.17% | 4/4 IDENTICAL |
| -no_devirt | 4.675 | 4.018 | 14.33% | 13.46% | 15.91% | 4/4 IDENTICAL |
| -no_reorder | 4.640 | 4.038 | 13.22% | 4.15% | 15.16% | 4/4 IDENTICAL |
| -no_stackfold -no_addrfold -no_cheap_prof -no_sparse_prof -no_devirt -no_reorder | 4.715 | 5.286 | -14.49% | -15.41% | -8.90% | 4/4 IDENTICAL |

### gcc family timings (from the STEP 3 correctness pairs, -prof_time 2, all optimizations on)

| run | native CPU | tool end-to-end | improvement | cmp |
|---|---|---|---|---|
| s3_cc1_r1 | 21.40 | 31.221 | -45.89% | IDENTICAL |
| s3_cc1_r2 | 22.67 | 26.114 | -15.19% | IDENTICAL |
| s3_sgcc_base_r1 | 10.04 | 13.866 | -38.11% | IDENTICAL |
| s3_sgcc_base_r2 | 9.35 | 12.192 | -30.40% | IDENTICAL |
| s3_sgcc_peak_r1 | 8.92 | 14.672 | -64.48% | IDENTICAL |
| s3_sgcc_peak_r2 | 8.71 | 12.692 | -45.72% | IDENTICAL |
| s3_cpugcc_r1 | 9.93 | 12.280 | -23.67% | IDENTICAL |
| s3_cpugcc_r2 | 10.34 | 12.344 | -19.38% | IDENTICAL |
| s3_sgcc_peak_setarchR_r1 | 8.84 | 13.250 | -49.89% | IDENTICAL |
| s3_sgcc_peak_setarchR_r2 | 9.36 | 12.744 | -36.15% | IDENTICAL |

### cc1 ablation (2 paired rounds each)

| config | native CPU (r1, r2) | end-to-end (r1, r2) | improvement (r1, r2) | mean improvement | cmp |
|---|---|---|---|---|---|
| default | 22.89, 23.67 | 26.597, 26.261 | -16.19%, -10.95% | -13.57% | IDENTICAL, IDENTICAL |
| -no_devirt | 22.70, 23.31 | 28.159, 28.339 | -24.05%, -21.57% | -22.81% | IDENTICAL, IDENTICAL |
| -no_reorder | 23.99, 23.35 | 28.529, 28.380 | -18.92%, -21.54% | -20.23% | IDENTICAL, IDENTICAL |
| -no_devirt -no_reorder | 22.82, 23.59 | 28.089, 29.382 | -23.09%, -24.55% | -23.82% | IDENTICAL, IDENTICAL |

### base tool (bprofile-with-gearing.cpp built unchanged), 1 paired round each; it has no end-to-end line, its printed line is used

- x_base_bzip2: native CPU 5.86, printed 'Translated code run' 9.218 -> -57.30%, cmp IDENTICAL
- x_base_cc1: native CPU 30.91, printed 'Translated code run' 69.769 -> -125.72%, cmp DIFF

## Requirements checklist (docs/REQUIREMENTS.md)
| item | status | evidence |
|---|---|---|
| R1 probe mode, project.so | PASS | PIN_StartProgramProbed; build target obj-intel64/project.so |
| R2 based on exercise 4 + base tool | PASS | project.cpp header mapping, README "BASED ON" |
| R3 thread builds TC2, execution continues there | PASS | create_tc2_thread_func, commit_tc2, "[time] TC2 execution" |
| R4 -prof_time knob, default 2 | PASS | KnobNumSecsDuringProfile; runs with 1, 2, 4 |
| R5 counts every block and jump target | PASS | emit_block_counter, emit_indirect_profile, -dump_prof CSV |
| R6 profile indirect jumps | PASS | emit_indirect_profile |
| R7 profile indirect calls | PASS | same, IF_IND_CALL sites |
| R8 de-virtualization | PASS | emit_devirt_call/jmp; cc1: 81 calls + 171 jumps guarded in the verified run (s8_evidence_cc1_devirt.txt) |
| R9 another optimization | PASS | code reordering + stack fold + address fold |
| R10 prints elapsed time with profiling | PASS | "[time] honest end-to-end total" + compat line |
| R11 >5% on one program | PASS | bzip2 median +15.23%, worst +5.02%, 10/10 rounds > 5% (VERIFICATION.md STEP 4) |
| R12 print-outs may be disabled | PASS | default output is only the timing lines |
| R13 output correct | PASS | VERIFICATION.md STEP 3 |
| R14 dump_instr_from_xedd / _from_mem / dump_entire_instr_map | PASS | present in project.cpp |
| R15 -dump_tc / -dump_tc2 | PASS | knob tests |
| R16 -dump_orig_code | PASS | knob test |
| R17 dump_prof | PASS | -dump_prof → edge-profile.csv |
| R18 project.zip with project.so | PASS | Phase 10 |
| R19 src/ with sources, makefiles, README.txt | PASS | Phase 10 |
| R20 names + IDs | TODO (placeholders for the students) | README (a) |
| R21 compilation command | PASS | README (b) |
| R22 how to run | PASS | README (c) |
| R23 threshold | PASS | README (d) |
| R24 pairs, deadline | n/a (students) | |
| R25/R26 video and defense | material in docs/FINAL_EXPLANATION.md, SUMMARY.md | |
| H1 makefiles unchanged | PASS | md5sum (Phase 10) |
| H2 zero warnings | PASS | build log |
| H3 probe only | PASS | |
| H4 byte-identical on all 5 | PASS | VERIFICATION.md STEP 3 (fresh native, separate files, O1-O6) |
| H5 no benchmark-specific hacks | PASS | no names/addresses of benchmarks in project.cpp |
| H6 git commit per phase | PASS | git log |

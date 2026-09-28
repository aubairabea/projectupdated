# Independent re-verification

Everything below was re-measured from scratch; nothing from earlier documents was reused.
Raw output of every run: `docs/verify_raw/` (one file per run, exact command lines at the top).
All tables were computed by `compute_tables.py` from the raw `*.tsv` files; nothing was typed in by hand.

- **Tool measured:** commit `58df1e1`. `project.cpp` is byte-identical to the Phase 10 commit `3f966f0`; only
  the test scripts changed. **No bug was found in the tool**, so the tool was not changed.
- **Build:** `rm -rf obj-intel64; mkdir -p obj-intel64; make PIN_ROOT=... obj-intel64/project.so` → exit 0, no
  warning or error line (`step2_build.txt`, project.so md5 `ca18d4244b72e1627224fd66868c7ac7`). Without the
  `mkdir -p obj-intel64`, the exact command fails right after `rm -rf obj-intel64` ("can't create
  obj-intel64/project.o"): the course makefile does not create the directory for an explicit target
  (`step2_build_FAILED_without_mkdir.txt`).
- **Makefiles:** md5 `f2d63a76ce3a7eac9e4807abd539f88b` (makefile) and `8bd75ba99855633c12796a74e048a387`
  (makefile.rules), equal to the copies in `~/Desktop/project4`, and git shows them only in the initial
  commit (`step2_makefiles_md5.txt`). Note: `~/Desktop/project4_BACKUP_working` **does not exist** on this
  machine. `~/Desktop/project4` (its `obj-intel64/project.so` was built 2026-09-27) was used instead for the
  makefile comparison and for STEP 7. Its code was not read.
- **Machine:** Intel i7-12700H VM. Natives are not pinned; the tool runs `taskset -c 0` (exactly the manual
  procedure).

## STEP 1 — Script audit (the "same output file" bug)

Result of a line-by-line audit of every script (old versions saved as `docs/verify_raw/OLD_*.sh`):
**no script ever made the native run and the tool run write to the same file.**
- bench.sh used `out_native_<b>.s` / `out_tool_<b>.s`.
- check.sh used `c_nat.s` / `c_tool.s`.
- run1.sh used `ref_native_<b>.s` / `r1_tool.s`.
- test_modes.sh used `tm/n.out` / `tm/t.out`.

The earlier gcc-family results were therefore not comparisons of a file with itself. The scripts did have
real weaknesses, though, so the earlier claims counted as unproven until STEP 3 below.

| script | problem (before) | fix (after) |
|---|---|---|
| run1.sh (used for the old Phase 8 correctness claims) | compared against a native reference **cached once** (`ref_native_<b>.s`, `out_native.bz2`, made hours earlier and never regenerated, exit code never checked) — O2/O4 | rewritten on `verify_lib.sh`: a fresh native run every time, separate files, all checks |
| bench.sh | no empty/missing-file check (two empty files compare equal), no md5, no TC2-committed check; native was pinned (`taskset -c 0`) unlike the manual procedure | rewritten on `verify_lib.sh` (O1–O6); native now unpinned as in the manual procedure; raw file per round |
| check.sh | same gaps as bench.sh (no O3, no md5, no O6) | rewritten on `verify_lib.sh` |
| test_modes.sh | no `rm -f` before a round, no size/md5 record, no TC2/early-exit check, no empty check | all added; `-t` mode (legitimately empty stdout) is the only case allowed to be empty |
| phase8.sh | relied on the old run1.sh | marked superseded; not used for any number below |

`verify_lib.sh` (function `pair`) is the single implementation of O1–O6 used by all new runs:
- O1: separate `out_native.*` and `out_tool.*` files.
- O2: `rm -f` of both outputs and both stderr files before every round.
- O3: size > 0 and md5 recorded.
- O4: `cmp`, both md5, both exit codes (native exit taken from `/usr/bin/time`'s "Exit status").
- O6: ERROR count, and "TC2 committed" = the `[time] TC2 commit:` line is present (or "early" for a clean
  exit before prof_time).

## O5 — proof that the check detects differences

For each of the 5 benchmarks (`step3_O5_<bench>.txt`):
- a copy of the tool output with one byte flipped at offset 100 gives `cmp` exit code 1;
- native outputs of two different inputs also differ (bzip2: input-long.txt vs a 1 KB file; gcc family:
  200.i vs a one-line `tm_tiny.i`).

5/5 benchmarks detected the flipped byte, and 5/5 input pairs gave different outputs. Example for cc1:
```
$ cmp out_tool.s o5_copy
out_tool.s o5_copy differ: byte 101, line 9          (exit 1)
290a33647ad8fe87f5776a7ff7aee8cd  out_tool.s
fd93e2a5257dbfcc3fbfa7da2ec10bbc  o5_copy
$ ./cc1 200.i -o o5_a ; ./cc1 tm_tiny.i -o o5_b ; cmp o5_a o5_b
o5_a o5_b differ: byte 9, line 1                      (exit 1)
```
The same "one-byte flip → detected" behaviour is what the harness relies on for every run below.

## STEP 3 — Correctness (fresh native run in a separate file, every round)
Also in this step (all PASS, raw files `modes_*.txt`, `step3_roundtrip.txt`):
- `test_modes.sh`: bzip2 -1, -9, -d, -t, stdin, -dc stdin, empty file, 1 KB file, 3 MB random data.
- Round trips: the tool's compressed file decompressed natively gives input-long.txt exactly, and the native
  compressed file decompressed with the tool gives it exactly.

Debug knobs `-dump_tc -dump_tc2 -dump_orig_code -dump_prof -verbose` on the full bzip2 input: all IDENTICAL, no ERROR, TC2 committed (`x_knob-*.txt`).

Short runs (decompression, small inputs) exit before prof_time; they print
"program exited before prof_time ended" and "TC2 was not committed before exit", with a sane time.

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


## Arithmetic checks (STEP 4)
All four checks pass on all 10 rows:
- window + build + commit + exec = end-to-end (±0.01 s);
- end-to-end ≥ printed line;
- printed line = 2 + exec (±0.01 s);
- window within 0.05 s of 2.0 s.

The printed compatibility line is always 0.004–0.015 s smaller than end-to-end: it counts exactly 2.000 s
for the window instead of the measured window, and it excludes the TC2 build.

Grep caveat: the manual procedure greps for "execution in TC2", but the tool prints "[time] TC2 execution:
X s". None of the grep alternatives match that line, so the manual grep does not show it. It is present in
every tool_time.txt, and the tables use it.

## STEP 5 — What the outside clock adds
External wall − internal end-to-end = 0.37–0.40 s per round. This is time the internal clock deliberately
excludes:
- Pin's own start-up (injection, loading the tool);
- `create_tc`: decoding the image, the stack-fold analysis, building and committing TC;
- process exit after `_exit`.

The internal clock starts at `project.cpp:2173` (`now(&t_app_start)`), right after `commit_tc()` inside the
main-image load callback `create_tc`. Pin runs this callback before the application executes its first
instruction (that is why `_start` itself runs in TC), so **no application work is outside the internal
clock**. It stops in `Fini` (`project.cpp:2110`), which runs when the application calls `_exit`.

Measured with the outside wall clock, the whole Pin run beats native CPU time by a **median of 6.37%**, but
**only 3 of 5 rounds are above 5%** (3.21% and 4.47% in the other two). Measured by the whole run's
user+sys CPU time, the median is 9.55% and all 5 rounds are above 5%.

## STEP 6 — bench.sh against the manual numbers
bench.sh now calls the same `pair` function, with the same two command lines as the manual procedure.
Native = user+sys, tool = "honest end-to-end total", improvement = (native − tool) / native × 100.
Its 5 rounds (median 13.81%, range 11.46–16.45%) lie inside the range of the 10 manual rounds (5.02–19.88%,
median 15.23%). The difference between the two medians (1.4 points) is run-to-run noise: different rounds,
not a formula difference. The recomputation of each round from its raw file matches bench.sh's own output
digit for digit.

## STEP 7 — Backup build (~/Desktop/project4) vs new
Both produce identical output on bzip2 (5/5 each). The backup is also identical on cc1 and sgcc_peak
(1 round each, separate files).

The backup was faster in 4 of 5 paired rounds, by a mean of 0.051 s (≈1.2%). This is below the run-to-run
spread of either version (0.12 s for new, 0.15 s for backup), so the difference is **not above the noise**.

Also note: the backup prints its own "end-to-end" line, and without reading its code we cannot confirm it
measures the same interval.

**Submit the new version.** It is the one fully verified here:
- all 5 benchmarks, setarch -R, devirt stress, bzip2 modes, round trips;
- every number has a raw file;
- its source is in the package.

## Discrepancies: old claim → verified value → reason
| where | old | verified | reason |
|---|---|---|---|
| AUDIT/SUMMARY/README: bzip2 10-round median | +13.5% | +15.23% (STEP 4) | new measurement; same conclusion |
| same, worst round | +5.1% | +5.02% | new measurement (round 5 of STEP 4); still above 5%, but only just |
| same, best round | +23.0% | +19.88% | old round 8 had an unusually slow native (5.96 s) |
| ablation, default | +16.3% | +15.86% | noise |
| ablation, -no_stackfold | −9.4% | −9.03% | noise |
| ablation, -no_addrfold | +0.7% | +2.73% | noise |
| ablation, cheap profiling off | +6.9% | +7.61% | noise |
| ablation, -no_devirt | +26.5% (noisy) | +14.33% | old set had one very slow native; on bzip2 the knob changes nothing (0 sites qualify, identical TC2: `s8_evidence_*`) |
| ablation, -no_reorder | +16.0% (one −60.8% round) | +13.22% | noise |
| ablation, all off | −7.0% | −14.49% | old value measured on a slow-native session |
| cc1 default | −24.7% | −13.57% (2 rounds; STEP 3 rounds: −45.89%, −15.19%) | the old set had native 34 s instead of 23 s; the gcc timings vary a lot run to run |
| cc1: devirt and reorder "are a loss" | −24.7% with vs −3.6% without | **wrong: they help** — default −13.57%, -no_devirt −22.81%, -no_reorder −20.23%, both off −23.82% | the old conclusion came from one noisy 3-round set; the claim is withdrawn |
| sgcc_base / sgcc_peak / cpugcc | −19.9% / −41.0% / −23.2% (1 round, old harness) | −38.11%, −30.40% / −64.48%, −45.72% / −23.67%, −19.38% | new measurement, 2 rounds, unpinned native |
| README: block executions in 2 s | ~1.85e9, threshold ~1850 | 1.613e9 in the verified run (`x_dump_prof_bzip2.txt`), threshold ~1600 | the count depends on how fast the machine runs during the window |
| cc1 devirt counts | 76 calls + 160 jumps | 81 calls + 171 jumps in the verified run | profile-dependent (window content differs per run) |
| gcc-family correctness (old AUDIT, from run1.sh) | "identical" vs a cached reference | identical vs a fresh native run, 2 rounds each + setarch -R + stress | now proven with O1–O6 |
| base tool | bzip2 −43% correct; cc1 wrong at byte 6669460 | bzip2 −57.30% correct (slower session: native 5.86 s); cc1 wrong, first difference at byte 6669460 | reproduced |

Numbers in docs/PHASE_*.md and docs/JOURNAL.md are development-time measurements made with the old scripts.
They are kept as history, and each of those files now says so at the top. The verified numbers are only the
ones in this file, in SUMMARY.md, AUDIT.md and README.txt.

## How to check this yourself
bzip2 (about 10 s):
```
cd ~/Desktop/project5
PIN=$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin
rm -f out_native.bz2 out_tool.bz2
/usr/bin/time -v ./bzip2 -c input-long.txt > out_native.bz2 2> native_time.txt
grep -E "User|System|wall clock" native_time.txt
taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -c input-long.txt > out_tool.bz2 2> tool_time.txt
grep -E "\[time\]|Translated code run" tool_time.txt
ls -l out_native.bz2 out_tool.bz2; md5sum out_native.bz2 out_tool.bz2
cmp out_native.bz2 out_tool.bz2 && echo IDENTICAL
grep -c ERROR tool_time.txt          # must print 0
# improvement = (User + System - "honest end-to-end total") / (User + System) * 100
```
cc1 (about 1 minute), with separate output files:
```
rm -f out_native.s out_tool.s
/usr/bin/time -v ./cc1 200.i -o out_native.s 2> native_time.txt
taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./cc1 200.i -o out_tool.s 2> tool_time.txt
ls -l out_native.s out_tool.s; md5sum out_native.s out_tool.s
cmp out_native.s out_tool.s && echo IDENTICAL
grep -E "ERROR|TC2 commit|honest" tool_time.txt
```
Check that cmp can fail: `cp out_tool.s x; printf '\xff' | dd of=x bs=1 seek=100 count=1 conv=notrunc; cmp out_tool.s x`.
Automated: `./check.sh full`, `./bench.sh bzip2 5`, `./test_modes.sh` (all use O1–O6).

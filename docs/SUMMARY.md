# Summary — verified final numbers

All numbers come from the re-verification (docs/VERIFICATION.md, raw files in docs/verify_raw/), measured at
commit 58df1e1. The tool source is identical to the Phase 10 version.

Metric: improvement = (native user+sys − tool "honest end-to-end total") / (native user+sys) × 100.
Each tool run is paired with a native run taken right before it. Native unpinned, tool `taskset -c 0`.

## bzip2: does it beat native by more than 5%?
| measurement | median | min | max | rounds > 5% |
|---|---|---|---|---|
| STEP 4, internal end-to-end, 10 rounds | **+15.23%** | +5.02% | +19.88% | **10/10** |
| STEP 6, bench.sh, 5 rounds | +13.81% | +11.46% | +16.45% | 5/5 |
| STEP 5, internal end-to-end, 5 rounds | +14.93% | +11.26% | +16.30% | 5/5 |
| STEP 5, outside clock, whole Pin run (wall) | **+6.37%** | +3.21% | +8.28% | **3/5** |
| STEP 5, outside clock, whole Pin run (user+sys) | +9.55% | +7.26% | +11.89% | 5/5 |

The outside wall clock also contains Pin start-up, TC construction and process exit: 0.37–0.40 s per run
that the internal clock excludes by design. The internal clock starts before the first application
instruction.

## Ablation (bzip2, 4 paired rounds each, median paired improvement)
| config | median | min | max |
|---|---|---|---|
| default | +15.86% | +12.84% | +17.21% |
| -no_stackfold | −9.03% | −13.30% | −6.11% |
| -no_addrfold | +2.73% | +1.52% | +5.03% |
| -no_cheap_prof -no_sparse_prof | +7.61% | +4.81% | +8.17% |
| -no_devirt (bzip2: 0 sites qualify, TC2 identical to default) | +14.33% | +13.46% | +15.91% |
| -no_reorder | +13.22% | +4.15% | +15.16% |
| all off | −14.49% | −15.41% | −8.90% |

## gcc family: correct everywhere, slower than native
- **Correctness:** byte-identical to a fresh native run with separate output files. Covered: 2 rounds each
  for cc1, sgcc_base, sgcc_peak and cpugcc_r_base; sgcc_peak under `setarch -R` ×2; devirt stress on cc1
  and sgcc_peak.
- **Timings (STEP 3 pairs):**
  - cc1: −45.89%, −15.19%
  - sgcc_base: −38.11%, −30.40%
  - sgcc_peak: −64.48%, −45.72%
  - cpugcc_r_base: −23.67%, −19.38%
- **cc1 ablation (2 rounds each, mean):** default −13.57%, -no_devirt −22.81%, -no_reorder −20.23%, both off
  −23.82%. De-virtualization and reordering help cc1, but not enough to beat native.

## Base tool (unchanged bprofile-with-gearing.cpp)
bzip2 correct, printed time −57.30% vs native. cc1 output wrong: first difference at byte 6669460.

# The turning point: from "slower than native" to "faster than native"

bzip2, improvement vs native CPU time (positive = faster than native, negative = slower). Development
measurements are marked *(dev)*: they were made with our first scripts (paired rounds, native and tool pinned to
CPU 0) and were not re-measured; the final numbers are from docs/VERIFICATION.md.

## The path

| stage | what was on | bzip2 vs native | rounds | source |
|---|---|---|---|---|
| provided base tool | profiling, plain TC2 | about -43% (6.46 s / 6.27 s vs 4.48 s / 4.37 s) | 2 *(dev)* | docs/PHASE_1.md |
| Phases 2+3: our TC/TC2, flag-free profiling | profiling, plain TC2 | -13.3% | 3 *(dev)* | docs/PHASE_3.md |
| Phase 5: **required optimizations** (de-virtualization + reordering) | + devirt, + reorder | -12.0% (without reordering: -10.6%) | 3 + 3 *(dev)* | docs/PHASE_5.md |
| Phase 6: cheaper profiling | + cheap and sparse counters | -12.4% (same session without them: -16.9%) | 3 *(dev)* | docs/PHASE_6.md |
| Phase 7a: stack fold, spares r10/r11 only | + stack fold | +4.9% | 3 *(dev)* | docs/PHASE_7.md |
| Phase 7b: liveness-based spare registers | + more spares | +5.0% | 3 *(dev)* | docs/PHASE_7.md |
| Phase 7c: address fold | + address fold | **+17.0%** (worst round +12.3%) | 3 *(dev)* | docs/PHASE_7.md |
| Phase 8: audit | all on | +13.5% (worst +5.1%) | 10 *(dev)* | docs/JOURNAL.md (Phase 8 line) |
| **final, verified** | all on | **+15.23% median, worst +5.02%, 10/10 rounds > 5%** | 10 | docs/VERIFICATION.md (STEP 4) |

## What it shows
- With only the required optimizations (de-virtualization and code reordering, Phase 5) bzip2 under our tool was
  still about 12% slower than native *(dev)*. bzip2 has no hot indirect site (0 guarded in every bzip2 run, e.g.
  docs/verify_raw/s8_evidence_default.txt), and reordering was within noise (docs/PHASE_5.md).
- The sign changed with the stack fold (Phase 7a, -12.4% -> +4.9% *(dev)*), and the large step came with the address
  fold (+5.0% -> +17.0% *(dev)*).
- The final ablation confirms the same picture with the verified procedure (docs/VERIFICATION.md, STEP 8, 4 paired
  rounds each, medians):

| configuration (final, verified) | bzip2 vs native |
|---|---|
| everything on | +15.86% |
| without the stack fold (devirt, reordering, cheap profiling still on) | -9.03% |
| without the address fold | +2.73% |
| without cheap/sparse profiling | +7.61% |
| everything off | -14.49% |

Note: the Phase 5 configuration (required optimizations without cheap profiling and without the stack fold) was not
re-measured in the final verification; the closest verified configuration is "without the stack fold" (-9.03%),
which still includes cheap/sparse profiling.

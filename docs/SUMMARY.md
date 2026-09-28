# Summary — final numbers

Metric: improvement = (native CPU − tool honest end-to-end) / native CPU; paired runs, taskset -c 0, medians.

## bzip2 (the target): requirement met
10 paired rounds, -prof_time 2: median native 4.97 s, median tool 4.29 s → **median +13.5%, worst +5.1%,
best +23.0%**. Every round above +5%.

## Progress by phase (bzip2, paired medians)
| state | vs native |
|---|---|
| base tool (bprofile-with-gearing) | −43% (and cc1 output wrong) |
| Phase 3: fresh TC/TC2, flag-free profiling | −13.3% |
| Phase 5: + devirt + reorder | −12.0% |
| Phase 6: + cheap/sparse profiling | −12.4% (vs −16.9% without it in the same session) |
| Phase 7: + stack fold | +4.9% |
| + liveness-based spares | +5.0% |
| + address fold | +17.0% (3 rounds) / +13.5% (10 rounds, Phase 8) |

## Ablation (bzip2, 4 rounds, median)
all on +16.3% · no stack fold −9.4% · no address fold +0.7% · no cheap/sparse profiling +6.9% ·
no devirt (no hot sites in bzip2) · no reorder +16.0% · all off −7.0%.

## Other programs (correct everywhere; slower than native)
cc1 −24.7% (3 rounds; −3.6% with devirt+reorder off), sgcc_base −19.9%, sgcc_peak −41.0%, cpugcc_r_base −23.2%.

## Correctness
All five programs byte-identical in every run, incl. setarch -R, devirt stress mode, bzip2 modes and edge
cases, round trips, all debug knobs (docs/AUDIT.md).

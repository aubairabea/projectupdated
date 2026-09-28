# Journal

- 2026-09-28 Phase 0: read the assignment and references, wrote REQUIREMENTS/REFERENCES, confirmed bzip2 is -O0 (r10/r11 unused). git initialised.
- Phase 1: base tool baseline: bzip2 −43% vs native (correct), cc1 wrong output (P1). bench.sh/check.sh written.
- Phases 2+3: new project.cpp (flag-free profiling, patch pads, fresh TC2 list, region placement). Fixed two sgcc_peak crashes found by bisection (probes overwriting bytes entered by other code). All 4 checks pass. bzip2 -13% vs native (no optimizations yet).

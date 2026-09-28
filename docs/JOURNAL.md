# Journal

- 2026-09-28 Phase 0: read the assignment and references, wrote REQUIREMENTS/REFERENCES, confirmed bzip2 is -O0 (r10/r11 unused). git initialised.
- Phase 1: base tool baseline: bzip2 −43% vs native (correct), cc1 wrong output (P1). bench.sh/check.sh written.
- Phases 2+3: new project.cpp (flag-free profiling, patch pads, fresh TC2 list, region placement). Fixed two sgcc_peak crashes found by bisection (probes overwriting bytes entered by other code). All 4 checks pass. bzip2 -13% vs native (no optimizations yet).
- Phase 4: de-virtualization of indirect calls (cmp guard) and jumps (flag-free lea/jrcxz guard); stress tests pass.
- Phase 5: Pettis-Hansen reordering with link1 inversion, cold region, hot routine order; correct on all; neutral on bzip2.
- Phase 6: cheap add-counter at a flags-dead point + sparse derived counters; bzip2 -16.9% -> -12.4%.
- Phase 7: stack fold (liveness-based spares, must-dataflow) + address fold, TC and TC2. bzip2 +17.0% median / +12.3% worst vs native. All correctness tests pass.
- Phase 8: full matrix all correct; bzip2 10 rounds median +13.5%, worst +5.1%; ablation + cc1 tables in AUDIT.md. cc1 is slower than native, devirt/reorder look negative there (noisy).
- Phase 9: README.txt, FINAL_EXPLANATION.md, SUMMARY.md written.
- Phase 10: project.zip built and tested from a clean unzip (README now says mkdir -p obj-intel64 before make).

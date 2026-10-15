# What we tried that did not help, or dropped

Numbers marked *(dev)* are development measurements (first scripts, not re-measured); others are final.

| idea / approach | what happened | decision | source |
|---|---|---|---|
| Stack fold with only the never-used registers r10/r11 as spares | +4.9% *(dev)*: too few spares; most hot slots of mainGtU stayed in memory | replaced by liveness-based spares (r11, r10, r9, r8, rdi, rsi, rcx usable where the program's value is dead) | docs/PHASE_7.md |
| Liveness-based spares alone (no address fold) | +5.0% *(dev)*, barely better although mainGtU then folded 207 loads in 7 slots | kept, and added the address fold, which gave the large step (+17.0% *(dev)*; final: without it +2.73%, with it +15.86%) | docs/PHASE_7.md; docs/VERIFICATION.md (ablation) |
| Address fold, first version | produced 0 address folds: the check for "the operation overwrites P" used a register mask that excludes base registers | fixed by checking the destination operand directly | presentation/PROJECT_LOG.md (bug table); commit 4054a0a |
| Code reordering on bzip2 | -12.0% vs -10.6% without it *(dev)*, within noise; final: +13.22% without vs +15.86% with (4 rounds each) | kept (required; helps cc1: -13.57% with devirt+reorder vs -20.23% without reorder) | docs/PHASE_5.md; docs/VERIFICATION.md |
| De-virtualization on bzip2 | 0 sites qualify, TC2 identical with and without it | kept (required; helps cc1) | docs/PHASE_4.md; docs/verify_raw/s8_evidence_*.txt |
| Sparse profiling on bzip2 | cheap only -12.1%, cheap + sparse -12.4% *(dev)*: neutral | kept (removes most fall-through stubs, costs nothing) | docs/PHASE_6.md |
| ExtTSP block layout (the paper's main contribution) | the paper reports about 0.5-1% over the Pettis-Hansen/TSP approach, below our run-to-run noise | not implemented | docs/PHASE_5.md; docs/REFERENCES.md |
| Code alignment (padding hot code to cache-line boundaries) | not found in our development notes | not tried | (none) |
| Timing with native pinned to CPU 0 (first scripts) | development numbers came from pinned native runs; the course's manual procedure runs native unpinned | final measurements: native unpinned, tool `taskset -c 0` | docs/PHASE_1.md; docs/VERIFICATION.md (STEP 1, machine note) |
| Conclusions from a single noisy session | an early note said de-virtualization and reordering made cc1 slower (-24.7% with vs -3.6% without, native varied 27-34 s) | withdrawn; re-measurement: they help (-13.57% vs -23.82%) | docs/VERIFICATION.md (discrepancies) |
| Comparing against a cached native output (one test helper) | weak evidence for the gcc binaries | replaced by a fresh native run per round in a separate file, with md5 and empty-file checks | docs/VERIFICATION.md (STEP 1) |
| Waiting for the window with one `sleep(prof_time)` | could overshoot on our VM | short sleeps with a clock check and an overshoot warning | docs/PHASE_2.md |
| Probing every translated routine | sgcc_peak crashed (other code jumps into the overwritten first bytes) | routines entered near their start, or falling off their end, are translated but not probed | docs/PHASE_2.md |
| Reporting Pin's probe refusals as errors | Pin refuses probes for some tiny routines; harmless | turned into a verbose note | docs/PHASE_2.md |
| Running the tool first instead of native first | 10 rounds: end-to-end median +17.89%, all 10 above 5% (but no taskset, output to a file, faster machine that day) | no change to the procedure; order alone cannot be credited | docs/run10_toolfirst/RESULTS.md |
| Reading live patch-pad addresses from a `-dump_orig_code` run | that option lays TC out slightly differently, so its addresses belong to another stub | for live reads we use the addresses of the matching run | presentation/BEFORE_AFTER.md (section on the switched-off profiling pad) |

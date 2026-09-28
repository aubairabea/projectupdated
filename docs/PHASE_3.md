# Phase 3 — Fresh TC2 list

> **Historical note (re-verification):** the numbers in this file are development-time measurements made with the old test scripts (cached native reference in run1.sh, native pinned to CPU 0, no empty-file/md5/TC2 checks). They were not re-verified. Verified numbers: docs/VERIFICATION.md, docs/SUMMARY.md, docs/AUDIT.md.


- TC2 is built by `build_tc2()` from the ORIGINAL instruction records (`ins_v`), never by editing the TC list.
  Every entry keeps its true original address (`ent_t::orig`); TC landing addresses are kept separately
  (`tc_land`). TC2 contains no profiling.
- The emitter (`emitter_t`, `layout_and_copy`) encodes all direct branches as rel32 with a target kind
  (original address / label / absolute) and re-derives every RIP-relative displacement from the stored absolute
  address (P6). Targets resolve to the TC2 copy, else the TC copy, else the original address.
- `verify_branches()` decodes every branch in memory after copying and checks its target (self-check).
- Only routines that ran during the window get a TC2 copy; the others keep running in TC.
- Switch to TC2 (`commit_tc2`): each TC routine starts with a 5-byte trampoline + an 8-byte pad. The trampoline
  (never executed yet) gets `jmp rel32 <TC2>`, then one aligned 4-byte store turns the pad into
  `jmp short trampoline` (P4, no torn instruction).
- P2: one region (data | TC | TC2) ~1 GB above the END of the whole image (IMG_HighAddress), reduced to stay
  within 2 GB of the code, plain mmap hint + result check; fallback to the base tool's search with a warning.
- P3: direct branches go straight to original code with rel32 (always reachable by construction); the
  jump-to-original table is only a fallback for targets outside the image and uses a real map lookup.
- P10: every failure prints an `ERROR:` line and a final error count; bench.sh/check.sh reject such runs.

## Result (paired, taskset -c 0, 3 rounds, later optimizations off)
bzip2 median native 6.29 s (the VM was slow during this run), tool end-to-end 7.13 s → −13.3%.
Correct on bzip2, cc1, sgcc_peak, sgcc_peak under setarch -R.

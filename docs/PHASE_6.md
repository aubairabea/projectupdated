# Phase 6 — Cheaper profiling in TC

Functions: `emit_block_counter`, `flags_dead_from`, `emit_tc_routine`, the sparse part of `analyze_image`,
`derive_profile`.

1. **Cheap counter** (`-no_cheap_prof` disables): where the status flags are provably dead (a later
   instruction on the straight-line path writes all six status flags before any flag read, or a call comes
   first — the ABI does not keep flags across a call), the counter is one `add qword [rip+ctr], 1`.
   The counter no longer has to sit at the block start: any point of the straight-line block runs once per
   execution, so it is placed at the first point where the flags are dead — in -O0 code typically right
   before the block's `cmp`. Otherwise: a dead register (`mov/lea/mov`), otherwise save RAX in the region.
2. **Sparse profiling** (`-no_sparse_prof` disables): a block with exactly one predecessor in its routine
   (routines without indirect jumps, never the entry block, no branch from another routine) has no counter:
   - reached only by a jcc fall-through → it keeps its counter and that counter IS the jcc's fall-through
     counter (no separate fall-through stub any more);
   - reached only by a jcc taken edge → count = exec(pred) − fallthru(pred);
   - reached only by a jmp → count = exec(pred).
   Derived after the window, iterating until all derivable counts are known.
3. All counters are in one dense array inside the region.

## Correctness
Counters only write tool memory; `add` is only used where the flags are dead; the other forms are flag-free.
Correct on bzip2, cc1, sgcc_peak (+ setarch -R). The derived profile is consistent (-dump_prof: e.g. block
0x402408's count equals the taken count of block 0x4023d0).

## Measured (bzip2, 3 paired rounds each, stack fold off)
| config | median vs native |
|---|---|
| neither (Phase 5 state) | −16.9% |
| cheap only | −12.1% |
| cheap + sparse | −12.4% |
Cheap counters give ~4–5 points; sparse is neutral within noise on bzip2 (kept: it removes most fall-through
stubs and costs nothing).

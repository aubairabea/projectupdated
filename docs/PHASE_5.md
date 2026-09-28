# Phase 5 — Code reordering (link1.txt + Newell & Pupyrev)

Functions: `reorder_routine_blocks`, `emit_tc2_block`, `invert_jcc`, `build_tc2`.

## Algorithm (per routine with a TC2 copy)
1. Blocks and profiled edges: jcc taken = exec − fallthru, jcc fall-through = fallthru, jmp = exec,
   plain fall-through = exec.
2. **Hot/cold threshold ("frequently vs rarely executed")**: a block is cold if its count is 0 or below
   `-cold_ppm` parts-per-million of the TOTAL block count of the whole profile (default **1 ppm**, i.e. for a
   2-second bzip2 window with ~1e9 block executions a block must run ≥ ~1000 times to count as hot). A relative
   threshold scales with the length of the window and the speed of the program; an absolute count would not.
   The routine entry block is always kept in the hot part.
3. **Pettis–Hansen chain merging**: edges between hot blocks sorted by weight (heaviest first); edge s→t merges
   the chain ending in s with the chain starting at t (t never the entry block). Result: the hottest successor
   falls through.
4. Layout: the entry chain first, then the other chains by decreasing hottest block. Cold blocks of all
   routines go to ONE cold region at the end of TC2 (hot/cold splitting, paper §3.3). Hot routines are placed
   in order of decreasing hotness.
5. Emission of a block knowing its new successor:
   - jcc whose fall-through block follows: unchanged;
   - jcc whose TAKEN block follows: inverted with the link1.txt table (JB↔JNB, JBE↔JNBE, JL↔JNL, JLE↔JNLE,
     JO↔JNO, JP↔JNP, JS↔JNS, JZ↔JNZ) to target the old fall-through; JRCXZ/LOOP never appear (such routines are
     not translated);
   - otherwise jcc + an added `jmp` to the old fall-through;
   - a `jmp` whose target now follows is dropped;
   - a block whose fall-through successor no longer follows gets an explicit `jmp`.
   All labels/targets are original addresses, remapped after layout to the new TC2 addresses.
6. ExtTSP (the paper's main contribution) is **not** implemented: the paper reports only about 0.5–1% gain over
   the Pettis–Hansen/TSP approach, not worth the complexity here. Knob `-no_reorder`.

## Why it cannot change the output
Only the placement of blocks changes. Each conditional branch keeps its condition or is replaced by its exact
logical inverse with swapped targets; every broken fall-through gets an unconditional jump; flags and registers
are untouched.

## Results
Correct on bzip2 (117 inverted, 98 jmps added, 44 removed, 761 cold blocks), cc1, sgcc_peak (+setarch -R),
cc1 stress (`-devirt_min_count 1 -devirt_min_pct 50 -cold_ppm 1000`).
bzip2 3 paired rounds: reorder −12.0% vs native, no reorder −10.6% → neutral within noise (bzip2 is small and
fits the caches). The benefit is expected on the large gcc binaries (Phase 8 ablation).

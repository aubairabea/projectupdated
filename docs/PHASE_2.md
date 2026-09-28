# Phase 2 — Profiling foundation (based on exercise 4)

> **Historical note (re-verification):** the numbers in this file are development-time measurements made with the old test scripts (cached native reference in run1.sh, native pinned to CPU 0, no empty-file/md5/TC2 checks). They were not re-verified. Verified numbers: docs/VERIFICATION.md, docs/SUMMARY.md, docs/AUDIT.md.


project.cpp was rewritten from scratch (it no longer edits the base tool in place), keeping the base tool's
flow and exercise 4's profiling features. The header comment of project.cpp maps each hw4 feature to its
function here.

## What the TC profiles
- **Every basic block** (leaders: routine head, any direct branch target in the image, the instruction after
  a jcc/jmp/ret/indirect jmp): a 64-bit counter (`emit_block_counter`).
- **Fall-through count** after every conditional branch (hw4's `fallthru_counter`; taken = exec − fallthru in
  `derive_profile`).
- **Every indirect jump AND indirect call**: 16-slot target table per site (`emit_indirect_profile`).
  The target operand is taken from the FIRST EXPLICIT operand (P5: `call reg` also has a hidden memory
  operand). Hash = `(T >> 4) & 15` (P9), computed flag-free with a 256-byte lookup table.

## Flag-free, push-free stubs (P1, P13)
Only MOV, MOVZX and LEA are used. A counter uses a register proven dead on the straight-line path
(`find_dead_reg`, hw4's dead-RAX idea generalised to all GPRs), otherwise RAX is saved to a slot inside the
region. Nothing is pushed: -O0 leaf functions keep data in the red zone below RSP.
All tool data used by generated code (counters, site tables, save slots) lives in the region, within ±2 GB,
so every access is RIP-relative.

## Patch pads (P4)
Every stub starts with an 8-byte pad = NOP(k)+NOP(8−k) where pad+k is 4-byte aligned. Disabling the stub
(`disable_profiling`) is ONE aligned 4-byte store of `jmp short <after stub>` at pad+k, so no thread can
fetch a half-written instruction. The NOP encodings are checked with XED at start-up (`check_nops`); if they
are not as assumed the tool warns and never patches.

## Other pitfalls handled here
- P7: explicit `IF_HAS_TARG` / `IF_HAS_RIP` flags; an address 0 is a legal value.
- P8: routines with JRCXZ/LOOP/XBEGIN are skipped; a routine that is the target of a branch into the middle
  of one of its instructions is left untranslated (only that routine).
- P11: the TC2 thread waits for the window with a monotonic clock and 10 ms sleeps, and warns if the measured
  window overshoots.
- P12: Fini prints sane times when the program exits before TC2 (window/TC2 lines say so).

## Bugs found while testing (sgcc_peak crashed, found by bisection over routines with -dbg_first/-dbg_count)
1. `htab_expand.lto_priv.0.cold` is entered by its parent 2 bytes after its start (`pop rbx`). Pin's probe
   overwrites the first 5 bytes, and Pin's safety check only analyses the routine itself. Fix: decode the whole
   image, collect all direct branch targets; a routine with a target in its first 16 bytes is translated but
   not probed (`no_probe`).
2. `uid_decl_map_hash` has symbol size 3 while its `ret` is the 4th byte. The TC copy falls off the end and
   continues at the original `ret`, which the probe had overwritten. Fix: a routine that falls off its end
   into bytes that are not the next routine is not probed.
3. Pin refuses probes for some tiny routines; that is harmless (they run from TC when called from TC code,
   the original otherwise), so it is no longer reported as an ERROR (verbose note only).

## Result
check.sh (with the later optimizations switched off): bzip2, cc1, sgcc_peak, sgcc_peak under setarch -R all
byte-identical. **The base tool's cc1 bug is fixed.**

# Reference files and how the project uses them

## project-2026.pdf — the assignment
Probe-mode pintool `project.so`, based on exercise 4 + bprofile-with-gearing.cpp. A thread builds TC2 after
`-prof_time` seconds of profiling in TC. Required: de-virtualization of indirect jumps **and** calls using the
TC profile, plus at least one more optimization (we do code reordering, and also redundant stack-load
elimination). The printed elapsed time must be >5% better on one of the 5 benchmarks. README must give names/IDs,
the compile command, how to run, and the frequent/rare threshold. Full checklist: docs/REQUIREMENTS.md.

## bprofile-with-gearing.cpp — the provided base tool
Flow we keep:
1. `create_tc` (IMG load callback) → `allocate_and_init_memory` → `find_candidate_rtns_for_tc` (decode every
   routine into `instr_map`, insert profiling stubs, one `bbl_map` record per block) →
   `chain_all_direct_jmp_and_call_target_entries` → `set_initial_estimated_new_ins_addrs_in_tc` →
   `fix_instructions_displacements` (RIP-relative + branch displacements, iterate until sizes are stable) →
   `copy_instrs_to_tc` → `commit_translated_rtns_to_tc` (RTN_ReplaceProbed: original routine → TC).
2. `create_tc2_thread_func` (internal thread): sleep prof_time → `disable_profiling_in_tc` (patch a jump over each
   profiling stub) → rebuild the list for TC2 → same fix/copy steps → `commit_translated_rtns_to_tc2` (patch each
   TC routine head, a NOP7, into a jump to TC2).
3. `Fini` via a probe on `_exit`: prints "Translated code run (including profiling) took: X seconds".

Known defects we fix (see the brief, P1–P12): `AND RAX,3` in the indirect-jump stub clobbers flags (cc1 breaks);
TC is mapped right where the heap grows (static sgcc binaries); the jump-to-orig table index bug; torn multi-byte
patching of live code; address-0 target/RIP operands treated as "none"; branch into the middle of an instruction
aborts everything; `target & 3` hash; silent fallback on failure; `sleep` overshoot; garbage time when TC2 never
ran; TC2 built by editing the TC list in place (orig addresses lost).

## hw4.cpp — my exercise 4 (the project MUST be based on it)
Feature → where it lives in project.cpp:
| hw4 feature | project.cpp |
|---|---|
| per-BBL execution counter (`bbl_map[].counter`) | `bbl_t::counter` slot in the dense counter array, incremented by the block-head stub (`emit_block_counter`) |
| fall-through counter after a conditional branch (`fallthru_counter`) | `emit_fallthru_counter`, a stub placed on the fall-through path |
| taken count derived as `exec - fallthru` | `derive_edge_counts()` after the window |
| indirect-jump target table (`targ_addr[]/targ_count[]`) | `emit_indirect_target_profile` for **jumps and calls**, flag-free, hashed `(t>>4)&15`, 16 slots |
| dead-RAX analysis (`insKillsFullReg`, `isRegDeadOnFallthruPath`) | `reg_dead_after()` / `flags_dead_after()` liveness scans used to pick the cheapest counter form |
| skip untranslatable routines (targets outside instruction starts, RIP target 0) | `check_rtn_translatable()` (mid-instruction branch, JRCXZ/LOOP, explicit has_rip flags) |
| atomic stub disabling (2-byte short jmp first) | patch pads: 8-byte NOP pad, one aligned 4-byte store (`patch_pad_to_jump`) |
| `edge-profile.csv` sorted hottest first | `dump_profile()` (knob -dump_prof) writes the same CSV format |
| elapsed-time print in Fini | timing breakdown + "[time] honest end-to-end total" |

## link1.txt — course example of reversing a conditional jump
Table used by `invert_jcc()` in code reordering:
JB↔JNB, JBE↔JNBE, JL↔JNL, JLE↔JNLE, JO↔JNO, JP↔JNP, JS↔JNS, JZ↔JNZ. JRCXZ (and LOOP*) are never inverted.
Technique: `xed_encoder_request_init_from_decode` + `xed_encoder_request_set_iclass(inverted)`, then add a direct
`JMP rel32` to the old target when a fall-through is broken.

## Improved_Basic_Block_Reordering.pdf — Newell & Pupyrev, IEEE Trans. Computers 69(12), 2020
- The classical goal (Pettis–Hansen 1990): maximize fall-through edge weight (a TSP-like objective); greedy chain
  merging along heaviest edges. We implement this per routine (`reorder_routine_blocks`).
- ExtTSP: also rewards short forward/backward jumps (weights 0.1·(1−len/1024) forward, 0.1·(1−len/640) backward);
  greedy chain merging with chain splitting. The paper reports ~0.5–1% over the TSP-based approach. **Not
  implemented** — documented as a possible extension.
- Section 3.2/3.3: cold blocks can be split away from hot code (hot/cold splitting) — we move cold blocks to one
  cold region at the end of TC2, and sort hot routines by hotness.

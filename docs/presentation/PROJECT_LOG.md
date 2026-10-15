# Project log: what we did, in order

Sources: `git log --reverse --date=short --stat`, docs/JOURNAL.md, docs/PHASE_*.md, docs/AUDIT.md,
docs/VERIFICATION.md. Numbers marked *(historical)* were measured during development with our first test scripts
(native pinned to CPU 0, a cached native reference for some checks); they show the trend but were not re-measured.
**Final numbers come only from docs/VERIFICATION.md** (commit 58df1e1) and are marked *(final)*.

## Timeline

| date | commit | step |
|---|---|---|
| 2026-09-28 | 99a7b80 | Phase 0: start from the base tool; requirements and references |
| 2026-09-28 | 2aa6632 | Phase 1: baseline measurements, benchmark and correctness scripts |
| 2026-09-28 | 9e3d5c5 | Phases 2+3: new project.cpp: exercise-4 profiling, patch pads, fresh TC2 list, region placement, probe-safety fixes |
| 2026-09-28 | c337ac8 | Phase 4: de-virtualization enabled and tested |
| 2026-09-28 | c8dcbc8 | Phase 5: code reordering enabled and tested |
| 2026-09-28 | 56bdbb9 | Phase 6: cheap and sparse profiling enabled and tested |
| 2026-09-28 | 4054a0a | Phase 7: stack fold with liveness-based spare registers, address fold |
| 2026-09-28 | a262948 | Phase 8: audit, full test matrix, ablation |
| 2026-09-28 | 5e09a18 | Phase 9: documentation |
| 2026-09-28 | 3f966f0 | Phase 10: submission package |
| 2026-09-28 | 58df1e1, 620d2e4 | Independent re-verification: stricter test scripts, everything re-measured, documents corrected |
| 2026-10-01 | e30e86a | Extra check: 10 paired rounds with the tool run first |
| 2026-10-01 | bfcfb45 ... 318906a | Final package: names, folder rename, README wording, all copies identical |
| 2026-10-13 | a8c6a2f | All documents, results, logs and outputs committed |

Note on the code history: the de-virtualization, reordering and cheap/sparse profiling code was written together
with the Phase 2+3 rewrite (commit 9e3d5c5), each behind its own knob (`-no_devirt`, `-no_reorder`,
`-no_cheap_prof`, `-no_sparse_prof`). Phases 4-6 switched them on one at a time, tested and measured them; their
commits contain the tests and documents. Phase 7 (4054a0a) added the stack fold and the address fold to project.cpp.

## Phase 0: understanding the task (99a7b80)
- Read the assignment, the base tool (bprofile-with-gearing.cpp), our exercise 4 (hw4.cpp), the course's link1.txt
  (conditional-jump inversion) and the Newell & Pupyrev paper; wrote docs/REQUIREMENTS.md and docs/REFERENCES.md.
- Disassembled bzip2 and confirmed it is compiled -O0: e.g. mainGtU has 564 instructions with 175 loads from and
  107 stores to `[rbp-X]`; r10/r11 never appear in the binary (docs/PHASE_0.md). This decided our main optimization.

## Phase 1: baseline (2aa6632)
- Ran the unchanged base tool: bzip2 correct but about 43% slower than native *(historical)*; **cc1 output wrong**
  (differs at byte 6669460). Re-measured later: bzip2 -57.30% in that run, cc1 wrong at the same byte *(final)*.
- Wrote bench.sh (paired native/tool rounds) and check.sh (correctness suite).

## Phases 2+3: profiling foundation and a fresh TC2 (9e3d5c5)
Built project.cpp anew on the base tool's TC/TC2 flow and exercise 4's profiling.
- **Decoding and blocks** (`analyze_image`, `analyze_ins`): flags read/written, registers read/killed, branch targets,
  RIP-relative addresses, all with explicit "has target"/"has RIP" flags (an address of 0 is legal).
- **Flag-free profiling** (`emit_block_counter`, `emit_indirect_profile`): counters with a dead register or a
  saved RAX, never push/pop; indirect jump AND call targets with the hash (target>>4)&15 computed via MOVZX + table.
- **Patch pads** (`fill_pad`, `patch_pad_to_jump`): 8-byte NOP pads switched by one aligned 4-byte store.
- **Region placement** (`allocate_region`): ~1 GB above the image, within 2 GB of the code; ERROR line on failure.
- **Fresh TC2** (`build_tc2`, `layout_and_copy`, `verify_branches`): built from the original instructions, every
  branch decoded again after layout.
- **Clock-checked window** (`create_tc2_thread_func`): short sleeps instead of one long sleep that could overshoot.
- Result: cc1 correct for the first time; bzip2 about -13% vs native at this point *(historical)*.

## Phase 4: de-virtualization (c337ac8)
- `devirt_targets`, `emit_devirt_call` (cmp/jnz/direct call, original call as fallback), `emit_devirt_jmp`
  (save RCX, `lea rcx,[rcx-T]`, `jrcxz`, original jump as fallback: no flag is touched).
- Stress test with `-devirt_min_count 1 -devirt_min_pct 50`: identical output on bzip2, cc1, sgcc_peak.
  Final: 552 calls + 490 jumps guarded on sgcc_peak, identical *(final)*.

## Phase 5: code reordering (c8dcbc8)
- `reorder_routine_blocks` (Pettis-Hansen), `emit_tc2_block` (keep / invert with `invert_jcc` / add jmp / drop jmp),
  cold region at the end of TC2, hot routines by hotness. Correct everywhere; little effect on bzip2.

## Phase 6: cheaper profiling (56bdbb9)
- Cheap `add [rip+ctr],1` at the first flags-dead point of a block; sparse counters derived from a single
  predecessor (`derive_profile`). bzip2 improved from about -16.9% to -12.4% *(historical)*; final ablation: without
  cheap/sparse profiling +7.61% instead of +15.86% *(final)*.

## Phase 7: stack fold and address fold (4054a0a)
- `compute_stack_folds_rtn`: spare caller-saved registers hold the most-read `[rbp-X]` slots; backward liveness says
  where a spare may be written; a forward must-dataflow says where it equals its slot on every path. Applied in TC
  and TC2 (`emit_ins`).
- First version used only r10/r11: bzip2 about +4.9% *(historical)*. With liveness-based spares (r11, r10, r9, r8,
  rdi, rsi, rcx): about +5.0% *(historical)*. Adding `address_fold`: about +17% over 3 rounds *(historical)*.
- Final: 77 routines, 1914 loads replaced, 775 register syncs, 130 address folds; ablation: without the stack fold
  -9.03%, without the address fold +2.73% *(final)*.

## Phases 8-10: audit, documentation, package (a262948, 5e09a18, 3f966f0)
- Full correctness matrix and ablation; README, explanation, summary; project.zip built and tested from a clean
  unzip (we found that a fresh `src/` needs an `obj-intel64/` directory before `make`).

## Re-verification (58df1e1, 620d2e4)
- Audited every test script: native and tool never wrote to the same file, but one helper compared against a native
  reference made once and never refreshed, and no script checked for empty outputs, recorded md5 sums or checked that
  TC2 was committed. All scripts now use one function (`verify_lib.sh`) that does all of this.
- Re-measured everything at commit 58df1e1 (tool source unchanged): all 5 programs byte-identical against fresh
  native runs, bzip2 median **+15.23%**, worst round +5.02%, 10/10 rounds above 5%; outside clock median +6.37%
  wall (3/5 above 5%) and +9.55% CPU *(final)*.
- Corrected the documents. The biggest correction: an early note said de-virtualization and reordering made cc1
  slower; the re-measurement shows they help (-13.57% with both, -23.82% without) *(final)*.
- Extra checks afterwards: two sets of 10 rounds with the tool run first (docs/run10_toolfirst*/RESULTS.md) gave the
  same picture; a live read of the running process showed the patch pads switching (presentation/dumps/).

## Bugs we found and fixed

| bug | where | symptom | how we found it | root cause | fix |
|---|---|---|---|---|---|
| flags clobbered by profiling | base tool | cc1 output differs at byte 6669460 | ran the base tool, cmp vs native | `AND RAX,3` in the indirect-jump stub; cc1 uses flags across switch jumps | all stubs and jump guards flag-free (MOV, LEA, MOVZX, JRCXZ) |
| hash puts aligned targets in one slot | base tool | useless indirect profile | code review | `target & 3` | `(target >> 4) & 15` |
| caches where the heap grows | base tool | known risk for statically linked programs | code review | first free address above the code | region ~1 GB above the image |
| jump-to-original table never finds entries | base tool | table fills up | code review | index taken after incrementing the counter | direct rel32 to original code; map lookup in the fallback table |
| torn patches | base tool | possible half-written instruction | code review | multi-byte writes to live code | patch pads, one aligned 4-byte store |
| `call reg` misread | base tool | wrong operand for the profile | code review | hidden memory operand (return-address push) | use the first explicit operand |
| silent fallback | base tool | "no speed-up" instead of an error | code review | failures ignored | ERROR line + error count |
| probe overwrites bytes others jump into | our tool | sgcc_peak crashed | translating halves of the routine list (`-dbg_first/-dbg_count`) | htab_expand.lto_priv.0.cold entered 2 bytes after its start | decode the whole image; no probe for such routines |
| symbol too small | our tool | sgcc_peak crashed | same bisection | uid_decl_map_hash: size 3, ret is the 4th byte | no probe when a routine falls off its end |
| address-fold never fired | our tool | 0 address folds | -verbose statistics | the destination check used a mask that excludes base registers | check the destination operand directly |
| stale test reference | our tests | weak correctness evidence | script audit | cached native output | fresh native run, separate files, md5 + empty checks |

## Design decisions and why

| decision | why |
|---|---|
| Flag-free jump guards (LEA + JRCXZ) | flags can be live across indirect jumps (cc1); CMP is used only at calls, where flags are dead by convention |
| Never push/pop in stubs | -O0 leaf functions keep locals in the red zone below RSP |
| Stack fold in both TC and TC2 | about half of a bzip2 run is the profiling window; running frames finish in TC |
| Liveness-based spare registers | r10/r11 alone gave too few spares; the program's own registers can be used where its value is dead |
| Relative cold threshold (1 ppm) | the total count scales with window length and machine speed |
| De-virtualization 100 / 10% / 80% / 3 | a guard must pay for its compares; low coverage means the fallback runs too often |
| Fresh TC2 list from the original instructions | true original addresses for every entry and RIP-relative operand |
| No ExtTSP | the paper reports about 0.5-1% over Pettis-Hansen, below our measurement noise |
| Report end-to-end time, not only the printed line | the printed line assumes a perfect window and omits the TC2 build; end-to-end is stricter |
| One region ~1 GB above the image, within 2 GB | leaves room for the heap; rel32 and RIP-relative accesses still reach |

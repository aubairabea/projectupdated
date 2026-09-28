# project.so — study guide and defense notes

## 1. The problem
We must write a probe-mode Pin tool that runs a program in a translation cache (TC) with profiling for
`-prof_time` seconds, then builds a second, optimized cache (TC2) from the profile and continues there. The
printed time (including the profiling) must beat native execution by more than 5% on one of five programs.
De-virtualization is mandatory, plus at least one more optimization.

Why this is hard: native never pays for profiling, and translated code has overheads (jumps over disabled
stubs, redirections). For bzip2 (~4.5–6 s native) the 2-second window is 40% of the run. Classic TC2
optimizations (reordering, de-virtualization) only speed up well-compiled code by a little. So the plan was:
make profiling nearly free, and exploit that bzip2 is compiled -O0 — it reloads every local variable from the
stack all the time. Removing those reloads, in TC **and** TC2, makes the translated code faster than the
original.

## 2. The flow, in execution order
1. `main` → `PIN_Init`, `PIN_InitSymbols`, registers `create_tc` (image load), spawns the TC2 thread
   (`create_tc2_thread_func`), `PIN_StartProgramProbed`.
2. `create_tc` (main image loaded, program not started yet):
   - probes `_exit` → `ExitInProbeMode` → `Fini` (prints the times);
   - `check_nops` (patch pads can be used);
   - `analyze_image`: collects all routines of executable sections, decodes the whole image to know every
     direct branch target, decodes each routine (`analyze_ins`: flags, register read/kill masks, targets,
     RIP-relative addresses), skips unsafe routines (JRCXZ/LOOP/XBEGIN, branch into the middle of an
     instruction, overlapping symbols), marks routines that must not be probed, forms basic blocks, assigns
     counters (sparse) and indirect-site tables;
   - `compute_stack_folds` (+ `address_fold`): the -O0 rewrite, computed once for TC and TC2;
   - `allocate_region`: data | TC | TC2, ~1 GB above the image end, within 2 GB of the code;
   - `build_tc` → `emit_tc_routine` per routine: trampoline + head pad, counters (`emit_block_counter`),
     indirect profiles (`emit_indirect_profile`), instructions (`emit_ins`/`emit_orig`); then
     `layout_and_copy` (addresses, RIP and branch displacements) and `verify_branches` (self-check);
   - `commit_tc`: `RTN_ReplaceProbed` original → TC copy. The clock `t_app_start` starts here.
3. The program runs in TC, counting.
4. `create_tc2_thread_func`: waits for the window with short, clock-checked sleeps; `disable_profiling`
   (one 4-byte store per stub pad); `derive_profile` (counts, derived counts, fall-through); `build_tc2`:
   `reorder_routine_blocks` (Pettis–Hansen) → `emit_tc2_block` (jcc inversion / added and removed jmps) →
   `emit_tc2_ins` (de-virtualization: `devirt_targets`, `emit_devirt_call`, `emit_devirt_jmp`; stack fold via
   `emit_ins`) → `layout_and_copy` → `verify_branches`; `commit_tc2`: trampoline := `jmp TC2`, then the head
   pad := `jmp short trampoline` (one aligned 4-byte store).
5. `_exit` → `Fini`: prints TC build, window, TC2 build, commit, execution, honest end-to-end total, and the
   base tool's compatibility line.

## 3. The optimizations (what / why / why the output cannot change)
### De-virtualization
What: hot indirect calls/jumps get compare-and-direct-branch guards for their 1–3 most frequent targets.
Why: a direct call/jump to the TC2 copy is predicted better and keeps execution in optimized code (in TC an
indirect jump to a switch case lands in ORIGINAL code).
Safe: equality is checked on the real runtime target; otherwise the original branch runs. Jump guards are
flag-free (MOV/LEA/JRCXZ) and restore RCX on every path; call guards use CMP only at a call (flags are dead by
the ABI).

### Code reordering
What: Pettis–Hansen chains so the hottest successor falls through; jcc inversion (link1 table); cold blocks to
one region at the end of TC2; hot routines by hotness.
Why: fewer taken branches, denser hot code in the I-cache/iTLB (matters for the big gcc binaries).
Safe: only placement changes; inverted conditions are exact logical inverses with swapped targets; every broken
fall-through gets a jmp.

### Stack fold + address fold (the bzip2 win)
What: keep copies of the hottest `[rbp-X]` slots in spare registers; replace reloads by register moves; fold the
-O0 `mov/add/[reg]` address computation into one addressing mode.
Why: in -O0 code the critical path goes through store→load pairs on the stack (store forwarding latency) and
through redundant address arithmetic.
Safe: stores all remain; a register replaces a read only when a must-dataflow proves it equals the slot on every
path; spares are written only where the application's register is dead (liveness); address-taken slots
excluded; cache cleared at calls/pushes/pops/syscalls/RSP stores/RBP writes; flags untouched or identical.

### Cheap profiling
`add qword [rip+ctr],1` at the first flags-dead point in the block; no counter where the count is derivable
from a single predecessor; counters in a dense array. The window then costs little.

## 4. Bugs fixed (see README "BUGS" for the full list)
Flags clobbered by `AND RAX,3` (cc1 wrong), heap collision (static sgcc), jump table index, torn patching,
`call reg` operand, RIP from TC bytes, address-0 operands, mid-instruction branches, `&3` hash, silent
fallback, sleep overshoot, garbage time. Found by bisection on sgcc_peak (`-dbg_first/-dbg_count`): probes
overwriting bytes that other code enters (`.cold` parts; symbol size too small).

## 5. Timing model
Native = user+sys of `/usr/bin/time -v`. Tool = monotonic clock from the program's first TC instruction to
`_exit` (window + TC2 build + commit + TC2 execution; the TC2 build overlaps with the program running in TC).
Each tool run is paired with a native run right before it, both on CPU 0; medians. The base tool's line
("prof_time + time after commit") is printed but never used: it hides the real window and the build.

## 6. Results
See docs/SUMMARY.md and docs/AUDIT.md.

## 7. Defense Q&A
**Why must the profiling code not change the flags?** Because the flags can be live at that point: cc1 computes
a condition, jumps through a switch table, and tests the flags in the target. The base tool's `AND RAX,3`
broke cc1. All our stubs at arbitrary points use only MOV/MOVZX/LEA; `add` is used only where an analysis
proves the flags are dead (a later instruction writes all six status flags before any read, or a call comes).

**Why never push/pop in stubs?** -O0 leaf functions keep locals in the 128-byte red zone below RSP; a push
would overwrite them. We save registers to slots in our region (RIP-relative).

**Why can de-virtualization never break correctness?** The guard compares the actual target value with the
profiled T; only if equal it jumps/calls T's TC2 copy, which is a translation of the same code. Otherwise the
unchanged original indirect branch runs. Registers and flags are preserved as explained above.

**Why is the region 1 GB above the image?** The heap (brk) starts right after the image; the base tool mapped TC
exactly there and the static gcc binaries failed in glibc start-up. It must stay within ±2 GB so rel32 branches
and RIP-relative accesses reach both the code and the region.

**How do you switch a running routine from TC to TC2 safely?** The trampoline is written while nothing can
execute it; then a single aligned 4-byte store turns the NOP pad at the routine head into `jmp short
trampoline`. A thread fetching the pad sees either the whole old NOP or the whole new jump.

**Why are the gcc binaries slower than native?** They are compiled with optimization, so the stack fold has
almost nothing to do. Reordering and de-virtualization did not gain in our cc1 measurements — the ablation even
shows them as a loss (−24.7% with both, −3.6% without; native varied 27–34 s, so this is noisy). Likely causes:
the bigger TC2 (guards, added jumps, a TC2 build of ~2100–3600 routines inside the measured time) and switch-table
jumps in TC that land in original code. They are required, so they stay on by default; knobs switch them off.

**What is the frequent/rare threshold?** Blocks: cold if count is 0 or below 1 ppm of all block executions
(`-cold_ppm`). Devirt: ≥100 executions, ≤3 targets each ≥10%, covering ≥80%. (README section (d).)

**How is it based on exercise 4?** Same profile (BBL counters, fall-through counters, indirect target tables,
dead-register analysis, atomic stub disabling, sorted CSV dump) — mapping in the README and in project.cpp's
header.

**What did you take from link1?** The inversion table of conditional jumps and the technique (re-encode with
`xed_encoder_request_set_iclass`, add an unconditional jmp) — used by `invert_jcc`/`emit_tc2_block`.

**What did you take from the paper?** The problem formulation (maximize fall-through weight, the TSP view),
greedy chain merging (Pettis–Hansen, which the paper uses as the baseline), hot/cold splitting of cold blocks.
ExtTSP (their contribution, also rewarding short jumps) was not implemented: they report only ~0.5–1% gain.

**Is the stack fold "allowed"?** The assignment allows "any other optimization from class"; it is redundant
load elimination / register promotion of stack slots (register allocation), applied at the binary level.

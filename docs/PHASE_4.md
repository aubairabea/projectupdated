# Phase 4 — De-virtualization (required)

Functions: `devirt_targets`, `emit_devirt_call`, `emit_devirt_jmp`, `emit_tc2_ins` (project.cpp).

## Which sites qualify ("frequently executed" threshold for devirt)
A site's executions = sum of its 16 profiled target counters. It is de-virtualized if
- it ran at least `-devirt_min_count` times in the window (default **100**: a site that ran fewer than ~50
  times per second of profiling cannot pay back even the few bytes of a guard), and
- its guarded targets (at most `-devirt_max_targets`, default **3**, each with at least `-devirt_min_share`,
  default **10%** of the executions) together cover at least `-devirt_min_pct` (default **80%**). Below that
  the fallback indirect branch is taken too often and the compares are mostly wasted.
- A target is skipped if it does not fit a sign-extended imm32 or has no TC2 copy; call targets must be routine
  entries.

## Code
Indirect CALL (flags are dead at a call by the ABI, so CMP is allowed):
```
cmp <operand>, T1 ; jne L1 ; call T1' ; jmp done
L1: cmp <operand>, T2 ; jne L2 ; call T2' ; jmp done
L2: call <operand>            ; original indirect call (RIP operand re-derived)
done:
```
Indirect JMP (flags may be live — P1 — so the guard is flag-free):
```
mov [slot], rcx ; mov rcx, <operand>
lea rcx, [rcx - T1] ; jrcxz hit1
lea rcx, [rcx + T1 - T2] ; jrcxz hit2 ...
mov rcx, [slot] ; jmp <operand>         ; original
hit1: mov rcx, [slot] ; jmp T1'  ...
```
T' = the target's TC2 copy. Guard-internal branches use labels resolved after layout; jrcxz is rel8 and its
range is checked at layout. `verify_branches` decodes every guard branch after copying (self-check). If a
guard cannot be built the site is copied unchanged. Knob `-no_devirt`.

## Why it cannot change the output
The guard compares the real runtime target with T; only on equality does it go to T's TC2 copy, which is a
translation of exactly the code the indirect branch would have reached. Otherwise the original indirect branch
executes. RCX is restored on every path; the jump guard touches no flags; the call guard only touches flags at
a call, where the ABI already treats them as clobbered.

## Tests
Correct on bzip2, cc1, sgcc_peak (also setarch -R). Stress `-devirt_min_count 1 -devirt_min_pct 50`: correct on
bzip2, cc1 (143 calls + 227 jumps guarded), sgcc_peak (518 calls + 468 jumps). bzip2 has no hot indirect
sites (0 guarded). cc1: 76 calls + 160 jumps guarded by default.
cc1 single-round A/B was inconclusive (VM noise: native varied 23–33 s); see the Phase 8 ablation.

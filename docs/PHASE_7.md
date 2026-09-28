# Phase 7 — Redundant stack-load elimination (stack fold + address fold), in TC and TC2

> **Historical note (re-verification):** the numbers in this file are development-time measurements made with the old test scripts (cached native reference in run1.sh, native pinned to CPU 0, no empty-file/md5/TC2 checks). They were not re-verified. Verified numbers: docs/VERIFICATION.md, docs/SUMMARY.md, docs/AUDIT.md.


Functions: `classify_fold`, `compute_stack_folds_rtn`, `compute_stack_folds`, `address_fold`, `emit_ins`.
The rewrite is computed once from the original code (`fold_of`) and applied when emitting both TC and TC2.

## Stack fold
bzip2 is -O0: every local lives in `[rbp-X]`, is stored after every change and reloaded for every use.
Per routine (only if it has a standard `mov rbp,rsp` frame and no indirect jump):
1. Classify every `[rbp+d]` access: store reg / store imm / load / movzx-movsx load / ALU reg,[m] /
   cmp [m],imm / cmp [m],reg / add-sub-inc-dec [m] / other.
2. Unfoldable slots: any `lea [rbp+d]` or indexed `[rbp+d+idx]` makes slots at and above d unfoldable (arrays
   and structs whose address escapes); slots with d ≥ 0; slots accessed with more than one width/offset.
3. The most-read slots get one spare register each. Candidates: r11, r10, r9, r8, rdi, rsi, rcx
   (caller-saved). A spare can be WRITTEN only where the application's value of that register is dead —
   **register liveness** is computed backwards over the routine's CFG (ret: return and callee-saved registers
   live; call: argument registers live, caller-saved killed; jumps out of the routine: everything live).
4. Forward "must" dataflow per spare: valid = the spare equals the slot on EVERY path. Entry block, blocks
   entered from other routines and unreachable blocks start invalid; merge = AND. The cache is cleared at every
   call, push/pop, syscall, RSP-based store and RBP write; an application write to the spare register or any
   other write overlapping the slot invalidates it.
5. Rewrites: every store stays; after it `mov spare, src` (or `mov spare, imm`) keeps the copy in sync. A load /
   compare / ALU read of a valid slot uses the spare instead of memory (`mov eax,[rbp-0x14]` → `mov eax,r8d`,
   `cmp dword [rbp-0x14],3` → `cmp r8d,3`). `add/sub/inc/dec [slot]` keeps the memory op and syncs the spare with
   a flag-free `lea spare,[spare+imm]` (or `+reg`). A load of an invalid slot syncs the spare after it.
   TC counter stubs never use a register that the fold uses in that routine. If a sync that the dataflow relied
   on cannot be encoded, the whole routine is left unfolded.

## Address fold
`mov P, spare` (a folded pointer load) ; `add P, X` ; `op dst, [P+d]` where the op fully overwrites P (P is
its destination) and the add's flags are dead → `op dst, [spare + X + d]`, dropping two instructions.
(-O0 array indexing: `block[i1]` is exactly this pattern in mainGtU.)

## Why the output cannot change
All stores stay, so memory is always exact; the spare only replaces a read when the dataflow proves it holds the
same bits (same width) on every path; spares are only written where the application's value is dead; lea/mov do
not touch flags, and replaced compares/ALU ops set the same flags; the address fold only drops an add whose flags
are dead and a register value that is overwritten before any use.

## Results
bzip2: 77 routines folded, 1914 loads replaced, 775 syncs, 130 address folds (mainGtU: 7 slots, 207 loads).
Correct: check.sh quick (bzip2, cc1, sgcc_peak, sgcc_peak -R) and test_modes.sh (bzip2 -1, -9, -d, -t, stdin,
empty file, 1 KB, 3 MB random): all pass.
bzip2, 3 paired rounds:
| config | median vs native | worst |
|---|---|---|
| stack fold with r10/r11 only (first version) | +4.9% | +2.7% |
| + liveness spares | +5.0% | +4.8% |
| + address fold (default) | **+17.0%** | **+12.3%** |
| -no_addrfold | +3.3% | +2.7% |

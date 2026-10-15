# Defense Q&A (backup slides)

Short, precise answers. Numbers: see KEY_NUMBERS.md (sources in docs/VERIFICATION.md and docs/verify_raw/).

## The task and the measurement

**1. What exactly does "faster than native" mean in your numbers?**
Per round we run native bzip2 under `/usr/bin/time -v` and then our tool. Improvement = (native user+sys CPU -
tool end-to-end time) / native user+sys. The end-to-end time runs from the program's first instruction in TC to
`_exit` and includes the whole profiling window, the TC2 build, commit and TC2 execution. Median over 10 rounds:
15.23%, all 10 rounds above 5%.

**2. The assignment asks for the printed time. Why do you report a different number?**
We print the required line "Translated code run (including profiling) took" (prof_time + TC2 execution). It assumes
the window lasted exactly prof_time and leaves out the TC2 build, so it is always slightly smaller than our
end-to-end time. It therefore beats native by at least as much; we report the stricter end-to-end number.

**3. What does your clock not include?**
Pin's start-up and building TC (0.261 s in our sample run), because the program has not started yet; native has
neither. Measured from outside around the whole pin command, the bzip2 gain is 6.37% median wall clock (3 of 5
rounds above 5%) and 9.55% in CPU time. The difference is 0.37-0.40 s of start-up and exit per run.

**4. Where does the clock start, exactly?**
In `create_tc()` right after `commit_tc()` (project.cpp:2173). Pin calls `create_tc()` for the main executable
before the program executes its first instruction, so no application work is outside the clock.

**5. How noisy are your measurements?**
Single bzip2 rounds vary by several percent on our virtual machine (our 10-round bzip2 measurement ranges from 5.02% to 19.88%), so we pair every
tool run with a native run made right before it and report medians. The gcc binaries vary even more (cc1: -45.89%
and -15.19% in two consecutive rounds).

**6. How do you know the output is correct?**
Every run is compared with a fresh native run written to a separate file: sizes and md5 recorded, `cmp`, exit codes,
no ERROR line, TC2 committed. All 5 programs identical (2 rounds each), plus setarch -R, a de-virtualization stress
test, 9 bzip2 modes and both round trips. We also checked that cmp catches a single flipped byte.

## Profiling

**7. Why must profiling code not change the flags?**
It is inserted between two program instructions, where the program may still need the flags. cc1 computes a
condition, jumps through a switch table and tests the flags in the target. The base tool's `AND RAX,3` changed them
and cc1's output was wrong (first difference at byte 6669460).

**8. When is the cheap `add [rip+ctr],1` allowed?**
Only when `flags_dead_from()` proves that, along the straight-line path, an instruction overwrites all six status
flags before any instruction reads one, or a call comes first. The counter is placed at the first such point in the
block; any point of a straight-line block runs once per execution.

**9. Why not just push/pop a register around a counter?**
-O0 leaf functions keep their locals in the 128-byte red zone below RSP without moving RSP. A push would overwrite
them. We use a dead register, or save RAX to a slot inside our region.

**10. What is sparse profiling?**
A block with exactly one predecessor in its routine gets no counter: its count is derived after the window
(executions minus fall-through for a taken edge, the predecessor's count for a jmp), and a fall-through-only block's
counter doubles as the jump's fall-through counter. Only in routines without indirect jumps and never for entries.

**11. How do you profile indirect targets?**
16 slots of {target, count} per site, slot = (target >> 4) & 15, computed with MOVZX and a 256-byte table, so no
flags change. The base tool's `target & 3` put every 16-byte-aligned target into one slot.

## De-virtualization

**12. Why can de-virtualization never break correctness?**
The guard compares the real runtime target with T. Only on equality does it go to T's TC2 copy, a translation of the
same code; otherwise the original indirect branch runs. RCX is restored on every path; the jump guard changes no
flags; the call guard uses CMP only at a call, where flags are not preserved by the calling convention.

**13. Why JRCXZ and LEA in the jump guard?**
They are the way to test "target == T" without touching the flags: `lea rcx,[rcx-T]` subtracts without setting
flags and `jrcxz` jumps when RCX is zero. JRCXZ has only an 8-bit displacement, which our layout checks.

**14. What is the de-virtualization threshold and why?**
At least 100 executions in the window; at most 3 targets, each at least 10% of the site; together at least 80%.
Below that, the compares in front of the original branch cost more than the direct calls save.

**15. Did de-virtualization actually do anything?**
On cc1: 81 calls and 171 jumps guarded, and removing it costs about 9 points (-13.57% vs -22.81%). On bzip2 no site
qualifies, so TC2 is identical with and without it. The stress test guarded 552 calls + 490 jumps on sgcc_peak with
identical output.

## Code reordering

**16. How does your code reordering work?**
Pettis-Hansen chain merging per routine: edges weighted with the profile, heaviest first; the chain ending in s is
joined with the chain starting in t, so t falls through after s. The entry chain stays first; other chains by their
hottest block; hot routines by hotness; cold blocks to one cold region at the end of TC2.

**17. What did you take from link1.txt?**
The inversion table for conditional jumps (JZ<->JNZ, JB<->JNB, JL<->JNL, ...) and the technique of re-encoding the
jump with the inverted iclass plus an unconditional jump where needed (`invert_jcc`, `emit_tc2_block`).

**18. What is the frequent/rare threshold for reordering?**
A block is cold if its count is below 1 part per million of all block executions in the window (and always if 0).
For bzip2: 1.613e9 block executions, so a block needs at least 1613 executions to be hot. Relative, because the
total grows with the window length and the machine speed.

**19. What did you take from the paper, and why no ExtTSP?**
Newell & Pupyrev, "Improved Basic Block Reordering", IEEE Trans. Computers 2020: the fall-through objective,
Pettis-Hansen chain merging as the baseline, and hot/cold splitting. Their ExtTSP adds about 0.5-1%, which is below
our run-to-run noise.

## Stack fold

**20. Why is the stack fold safe?**
All stores stay, so memory is always exact. A register replaces a read only where a must-dataflow proves it holds
the slot's value on every path; spares are written only where liveness proves the program's value is dead; only
slots whose address is never taken; everything invalidated at calls, push/pop, syscalls, RSP stores, RBP writes and
program writes to the spare; routines with indirect jumps are skipped. When unsure, we do not fold.

**21. Why apply it in TC too?**
About half of a bzip2 run is the 2-second profiling window, and frames already running at the commit finish in TC.
Folding TC makes the window itself faster.

**22. Is it a legitimate "optimization taught in class"?**
Yes: it is redundant load elimination / register promotion of stack slots, plus address-mode folding, done at the
binary level. Without it bzip2 is 9.03% slower than native; with it 15.86% faster (ablation, median of 4 rounds).

**23. What is the address fold?**
`mov P, spare; add P, X; op dst, [P+d]` becomes `op dst, [spare+X+d]` when the operation overwrites P and the add's
flags are dead. In mainGtU it turns `mov rax,[rbp-0x20]; add rax,rdx; movzx eax,[rax]` into
`movzx eax, byte ptr [r11+rdx*1]`.

## Robustness and limits

**24. Why are the gcc binaries slower than native?**
They are compiled with optimization, so the stack fold finds almost nothing (30 routines on cc1). What remains are the
costs: the profiling window, jumps over disabled stubs in TC, switch jumps in TC that land in original code, and a
TC2 of 2164 routines for cc1. De-virtualization and reordering help cc1 but not enough.

**25. How do you switch running code without crashing another thread?**
Patch pads: an 8-byte NOP pad split so that the second NOP starts 4-byte aligned; one aligned 4-byte atomic store turns
it into `jmp short`. An aligned 4-byte store never crosses a cache line, so a CPU sees either the old NOP or the new
jump. For TC2 we write the trampoline first, then flip the pad.

**26. Why is your region 1 GB above the program?**
The heap grows right after the image, which is where the base tool put its caches. We stay about 1 GB above the end
of the image but within 2 GB, so rel32 branches and RIP-relative accesses still reach.

**27. What bugs did you find in your own tool?**
sgcc_peak crashed. By translating halves of the routine list we found that Pin's probe overwrote bytes that other
code jumps into: htab_expand.lto_priv.0.cold is entered 2 bytes after its start, and uid_decl_map_hash has symbol
size 3 with its ret at the 4th byte. Fix: decode the whole image first and do not probe such routines.

**28. Does the tool work for multi-threaded programs?**
Not guaranteed: the save slots used by stubs and guards are shared. The five benchmarks are single-threaded.
Per-thread slots (e.g. via the FS segment) would be the fix.

**29. How is the project based on exercise 4?**
The profile is exercise 4's: block counters, fall-through counters, indirect target tables, dead-register analysis,
atomic stub disabling, the sorted CSV dump (`-dump_prof`). The mapping table is in README section 2.

**30. What would you do with more time?**
ExtTSP layout, per-thread save slots, more spare registers (callee-saved ones with a save in the prologue),
re-translating jump tables so switch jumps in TC stay in translated code, and redirecting TC frames that are still
running when TC2 is committed.

# Talk script: "Profile-guided binary optimization with Pin: beating native bzip2"

Speakers: **Aubai Rabea** and **Yazan Rabea**.
Target: about 22 minutes (22:00 planned) at about 130 spoken words per minute.
Visual references: `graphs/NN_*.png`, `DIAGRAMS.md` (a-e), `CODE_WALKTHROUGH.md` (sections 1-13),
`BEFORE_AFTER.md` (a-f). Every number is listed in `KEY_NUMBERS.md` with its source file.
Slides marked **[8-MIN]** are also used in the 8-minute video cut at the end of this file.

| section | slides | time |
|---|---|---|
| 1. Introduction | 1-3 | 1:50 |
| 2. Background | 4-8 | 2:15 |
| 3. Architecture | 9-10 | 1:45 |
| 4. Code walk-through | 11-23 | 9:25 |
| 5. Bugs we found and fixed | 24-26 | 1:55 |
| 6. Results | 27-31 | 2:50 |
| 7. Live demo | 32 | 1:30 |
| 8. Conclusions | 33-34 | 0:30 |
| **total** | **34 slides** | **22:00** |

---

## Section 1: Introduction (1:50)

### Slide 1: Title [8-MIN] (0:25)
- Profile-guided binary optimization with Pin
- From a run-time profile to faster-than-native machine code
- Aubai Rabea, Yazan Rabea
- Visual: title, course name, headline "bzip2: 15.23% faster than native (median of 10 rounds)"

**Aubai:** Hi, I'm Aubai Rabea, and this is Yazan Rabea. Our project is a Pin tool that runs a program, profiles it
while it runs, rewrites its hot code, and switches the running program over to the rewritten code. On bzip2 the
result is faster than the original binary, even though we pay for the profiling.

### Slide 2: Agenda (0:15)
- The problem and the requirement
- Background: probe mode, TC and TC2, how we measure time
- Architecture and a code walk-through
- Bugs we found, results, a live demo, conclusions
- Visual: agenda list

**Yazan:** First the problem, then the background, the architecture and a code walk-through with real examples from
our dumps, then the bugs we found, our results, a live demo and our conclusions.

### Slide 3: The problem [8-MIN] (1:10)
- Run the program in TC with profiling for -prof_time seconds (default 2)
- A thread builds an optimized TC2; the program continues there
- Required: de-virtualization + at least one more optimization
- Goal: the printed time, including profiling, more than 5% better than native
- Output must stay byte-identical
- Visual: requirement box + the five benchmarks (bzip2, cc1, sgcc_base, sgcc_peak, cpugcc_r_base)

**Aubai:** The assignment: write a probe-mode Pin tool, based on our exercise 4, that runs a program in a first
translation cache, TC, with profiling code, for a fixed time, two seconds by default. Then a separate thread uses
the profile to build a second, optimized cache, TC2, and the program continues there. De-virtualization is required,
plus at least one more optimization. And the hard part: the printed time, including the profiling, must be more than
five percent better than native on at least one of five programs: bzip2, cc1, and three builds of gcc.

**Yazan:** Native never pays for profiling, and translated code has overheads of its own. So "faster than native"
does not come for free. And there is one rule we never broke: whatever we do, the output must stay byte for byte the
same as the native run, on every program and in every run.

---

## Section 2: Background (2:15)

### Slide 4: Pin probe mode [8-MIN] (0:25)
- Pin overwrites the first bytes of a routine with a jump (a probe)
- No JIT: the program runs natively
- We use probes to send every routine to our own copy
- Visual: DIAGRAMS a (left half: original routine -> probe -> TC)

**Yazan:** In probe mode Pin does not interpret the program and does not JIT-compile it. It only lets us overwrite
the first bytes of a routine with a jump, a probe. We use that to redirect every routine of the executable into code
we wrote ourselves. Everything else is our job: our copy must behave exactly like the original.

### Slide 5: TC to TC2 gearing [8-MIN] (0:25)
- TC: translated code + counters, runs for prof_time seconds
- Our thread: stop profiling, read counters, build TC2
- Switch each routine head from TC to TC2 while the program runs
- Visual: DIAGRAMS a (full)

**Aubai:** First the program runs in TC, a translated copy with counters. After two seconds our thread switches the
profiling off, reads the counters, builds TC2 with all the optimizations, and then switches every routine head from
its TC copy to its TC2 copy, while the program keeps running. From then on, every new call enters optimized code.

### Slide 6: Timing model (0:45)
- Clock starts when the program's first instruction runs in TC
- End-to-end = window + TC2 build + commit + TC2 execution
- Printed line "Translated code run" = prof_time + TC2 execution
- Native = user + system CPU time
- Visual: graphs/08_run_timeline.png

**Yazan:** This is one real bzip2 run. Our clock starts when the program executes its first instruction, and stops
when it calls exit. Building TC takes a quarter of a second, but before the program starts, so native does not have
it either. Then two seconds of profiling, eight milliseconds to build TC2, and two seconds in TC2: four point zero
five five seconds end to end. The line the assignment asks us to print is prof time plus TC2 execution. It is always
a little smaller than our end-to-end time, so we report the stricter end-to-end number.

### Slide 7: Why beating native is hard (0:25)
- Almost half of bzip2's run happens inside the profiling window
- Classic optimizations give a few percent on cache-friendly code
- Overheads: counters, jumps over switched-off stubs
- Visual: graphs/01_bzip2_native_vs_tool.png (native bar only)

**Aubai:** Native bzip2 takes about four point seven five seconds of CPU, and two of those seconds are our profiling
window. Code reordering and de-virtualization alone give a few percent at most on a small, cache-friendly program.
That cannot pay for profiling. So we needed something that makes the code itself faster than the original binary.

### Slide 8: The opportunity: bzip2 is -O0 [8-MIN] (0:15)
- Every local lives in [rbp-X] and is reloaded for every use
- Visual: BEFORE_AFTER e (original listing only)

**Yazan:** But bzip2 is compiled without optimization. Every local variable lives on the stack and is reloaded for
every single use. Here in mainGtU: store i1, reload i1, reload block, add, load. That is our opportunity.

---

## Section 3: Architecture (1:45)

### Slide 9: Architecture overview (0:55)
- create_tc: analyze, stack-fold analysis, place region, build TC, probe
- Our thread: wait, disable profiling, derive profile, build TC2, commit
- Fini at _exit: the timing report
- Visual: DIAGRAMS a; CODE_WALKTHROUGH 1

**Aubai:** Here is the whole flow in our code. When Pin loads the main executable, it calls create_tc, before the
program runs a single instruction. create_tc decodes every routine, computes the stack-fold rewrite once for both
caches, places our memory region, builds TC and probes every routine. Only then does the program start, and only
then does our clock start. Our thread waits for the window with short sleeps that check the clock, because on our
virtual machine a single long sleep sometimes overshot. Then it switches the profiling off, derives the profile,
builds TC2 and commits it. When the program calls exit, our replacement of underscore exit calls Fini, which prints
the timing report.

### Slide 10: Memory layout (0:50)
- One region: data | TC | TC2
- About 1 GB above the image: room for the heap
- Within 2 GB of the code: rel32 and RIP-relative accesses reach
- Visual: DIAGRAMS b; CODE_WALKTHROUGH 3

**Yazan:** All our code and data live in one memory region: first the data, meaning counters, target tables and save
slots, then TC, then TC2. The base tool mapped its caches right after the program, which is exactly where the heap
grows. We place the region about one gigabyte above the end of the whole image, but still within two gigabytes, so
every relative branch and every RIP-relative access still reaches both the program and our region. For bzip2 it sits
at address 0x40600000. And if placement ever fails, we print an ERROR line, instead of silently running the original
code and making it look as if our optimizations just did nothing.

---

## Section 4: Code walk-through (9:25)

### Slide 11: Profiling: what we count (0:30)
- Every basic block (as in exercise 4)
- Fall-through after every conditional jump
- Targets of every indirect jump AND indirect call
- Visual: CODE_WALKTHROUGH 2 (block formation lines)

**Aubai:** The profiling is based on our exercise 4. A basic block starts at a routine entry, at every branch
target, and after every jump. We count every block, and the fall-through path of every conditional jump, so the
taken count is executions minus fall-through. And we record the targets of every indirect jump and every indirect
call, which is exactly what de-virtualization needs.

### Slide 12: Counters that never change the flags [8-MIN] (0:55)
- Flags dead? one `add qword [rip+ctr], 1`
- Otherwise a dead register: mov / lea / mov
- Otherwise save RAX in our region, never push (red zone)
- Visual: CODE_WALKTHROUGH 4; BEFORE_AFTER a (cheap form + save/restore form)

**Yazan:** A counter is inserted between two instructions of the program, so it must not change anything the program
still needs, including the flags. If our analysis proves the flags are dead, meaning the next instructions overwrite
all six status flags before anyone reads them, the counter is a single add, placed right before the block's compare.
Otherwise we borrow a register that is dead, with mov, lea, mov, which never touch the flags. As a last resort we
save RAX in our region. We never push, because minus O zero leaf functions keep local variables below the stack
pointer, where a push would overwrite them. On the slide you see both forms in the real TC of bzip2.

### Slide 13: Sparse profiling (0:35)
- One predecessor -> no counter of its own
- A fall-through block's counter = the jump's fall-through counter
- Derived after the window
- Visual: CODE_WALKTHROUGH 6

**Aubai:** A block with exactly one predecessor does not need its own counter: we derive its count after the window
from the predecessor. And when that block is the fall-through of a conditional jump, its counter doubles as that
jump's fall-through counter. This removed most of the fall-through stubs from TC. We only do this in routines
without indirect jumps, and never for a routine entry, because there we cannot know all the predecessors.

### Slide 14: Indirect targets without touching the flags (0:30)
- 16 slots per site, slot = (target >> 4) & 15
- Computed with MOVZX and a 256-byte table
- The base tool used AND RAX,3: wrong flags, one slot
- Visual: CODE_WALKTHROUGH 5; BEFORE_AFTER a (indirect profile)

**Yazan:** For indirect branches we keep sixteen slots per site. The slot index uses bits four to seven of the
target, because many targets are sixteen-byte aligned and their low bits are always zero. We compute it with MOVZX
and a small lookup table, so no instruction changes the flags. The base tool used AND RAX, 3 here, which changes the
flags and puts all aligned targets into the same slot.

### Slide 15: Switching live code safely [8-MIN] (0:55)
- 8-byte pad = two NOPs, split so pad+k is 4-byte aligned
- One aligned 4-byte store -> "jmp short"
- Trampoline written first, then the head pad flips
- Visual: CODE_WALKTHROUGH 7; BEFORE_AFTER f (live memory before/after)

**Aubai:** The program keeps running while we change its code, so no CPU may ever see half an instruction. Every
stub and every routine head starts with an eight-byte pad made of two NOPs, split so that the second one starts on a
four-byte boundary. Switching the pad is one aligned four-byte store of a short jump, and an aligned store can never
be seen half-written. For the switch to TC2, we first write a full jump into a small trampoline in front of the
head, which nothing executes yet, and then flip the pad. These bytes were read from the running bzip2 process before
and after the switch: the trampoline now jumps to 0x40f13000, exactly mainGtU's first instruction in TC2.

### Slide 16: A fresh TC2 (0:35)
- Built from the original instructions, not from TC
- Only routines that ran; hot ones first; one cold region
- Every branch decoded again and checked
- Visual: CODE_WALKTHROUGH 8 and 12

**Yazan:** TC2 is built from the original instructions, not by editing TC, so every entry knows its true original
address, and every RIP-relative operand is recomputed from it. Only routines that ran during the window get a copy.
After layout we decode every branch again from memory and check that it reaches its intended target, a cheap guard
against our own mistakes. Hot routines are placed first, ordered by how hot they were in the profile.

### Slide 17: De-virtualization: the rule (0:30)
- Site ran at least 100 times
- At most 3 targets, each at least 10%
- Together at least 80% of the executions
- Visual: CODE_WALKTHROUGH 9 (devirt_targets lines)

**Aubai:** De-virtualization replaces hot indirect branches with direct ones. A site qualifies if it ran at least a
hundred times during the window, and its top targets, at most three, each with at least ten percent, together cover
at least eighty percent of its executions. Otherwise the compares would cost more than they save. With these
defaults, cc1 gets eighty-one guarded calls and one hundred seventy-one guarded jumps.

### Slide 18: The call guard [8-MIN] (0:40)
- cmp target, T ; jnz next ; call T' ; jmp done
- Original indirect call stays as the fallback
- CMP allowed: flags are dead across a call
- Visual: BEFORE_AFTER b (htab_traverse in cc1)

**Yazan:** Here is a real call site from cc1: htab_traverse calls its callback through register r13. In TC2 we
compare r13 with the two hot callbacks from the profile and call their TC2 copies directly. If neither matches, the
original indirect call runs, so the result is always the same. Using CMP is fine here, because the calling
convention does not keep flags across a call anyway. Because the original call is always kept as the last step, even
a completely wrong profile can only cost a few compares, never a wrong result.

### Slide 19: The jump guard without flags [8-MIN] (0:50)
- Flags can be live across a switch jump (cc1)
- lea rcx,[rcx-T] + jrcxz: an equality test without flags
- RCX saved in our region on every path
- Visual: BEFORE_AFTER c; DIAGRAMS c

**Aubai:** Jumps are harder. In cc1 the flags can be live across a switch jump, so the guard may not change them.
Our trick: save RCX in our region, load the real target into RCX, subtract the expected target with LEA, which
leaves the flags alone, and use JRCXZ, which jumps if RCX is zero, again without reading or writing flags. A hit
restores RCX and jumps straight to the TC2 copy. A miss restores RCX and takes the original jump. This is the real
guard in cc1's ggc_mark_rtx_children_1, and in a stress test, where we guarded hundreds of sites, every output
stayed identical.

### Slide 20: Code reordering: Pettis-Hansen (0:30)
- Edges weighted by the profile, heaviest first
- Join chains so the hot successor falls through
- Entry chain first; hot routines ordered by hotness
- Visual: DIAGRAMS d; CODE_WALKTHROUGH 10

**Yazan:** Our second required optimization is code reordering, following the recommended paper by Newell and
Pupyrev. We weight every edge with the profile and merge chains, heaviest edge first, so that the hottest successor
of every block falls through. In this example, A to C and C to D join into one chain, B ends up after them, and the
hot path A, C, D has no taken jump at all.

### Slide 21: Inversion, cold region, the paper (0:50)
- Taken target follows -> invert the jcc (link1.txt table)
- Broken fall-through -> add jmp; jmp to the next block -> drop it
- Cold blocks (< 1 ppm of all executions) to one region
- ExtTSP not implemented: about 0.5-1% in the paper
- Visual: BEFORE_AFTER d (mainGtU jz -> jnz); graphs/10_reorder_stats.png

**Aubai:** When the taken successor now comes right after the branch, we invert the condition, using the table from
the course's link1 example. Here, mainGtU's jz became jnz in TC2. Where a fall-through was broken we add a jump, and
a jump to the very next block is removed. On bzip2 we inverted one hundred eighteen branches and moved seven hundred
fifty-eight cold blocks into a single cold region. Cold means below one part per million of all block executions in
the window. The paper's ExtTSP refinement reports about half a percent to one percent more, which is below our
measurement noise, so we did not implement it.

### Slide 22: Stack fold [8-MIN] (1:05)
- Keep hot stack slots in spare registers
- Liveness: write a spare only where the program's value is dead
- Must-dataflow: replace a load only if valid on every path
- Stores stay; lea keeps the spare in sync
- Visual: DIAGRAMS e; CODE_WALKTHROUGH 11

**Yazan:** Now the optimization that makes bzip2 faster than native. For each minus O zero routine we keep the most
read stack slots in spare caller-saved registers. A backward liveness analysis tells us where a spare may be written
without destroying a value the program still needs. A forward dataflow analysis tells us where the spare provably
equals its slot on every path into that point. Then a reload becomes a register move, every store stays and also
updates the spare, and an add to memory is followed by a flag-free LEA on the spare. Calls, push and pop, stores
through the stack pointer, and any slot whose address is taken all invalidate the spare. When we are unsure, we do
not fold. We apply this in both TC and TC2, because almost half of bzip2's run happens inside the profiling window.

### Slide 23: Stack fold + address fold in mainGtU [8-MIN] (1:00)
- Reloads of i1, i2, c1, c2 -> register moves
- mov / add / load -> one load [r11+rdx]
- 77 routines, 1914 loads replaced, 130 address folds
- Visual: BEFORE_AFTER e; graphs/09_stackfold_routines.png

**Aubai:** This is mainGtU before and after, taken from our TC2 dump. The reload of i1 is now a register move.
Loading the pointer, adding the index and reading the byte became one instruction, a movzx from r11 plus rdx: that
is the address fold. And the compare of c1 and c2 now uses registers instead of memory. Over the whole program:
seventy-seven routines, one thousand nine hundred fourteen loads replaced and one hundred thirty address folds. All
the stores to the stack are still there, so memory is always exactly as in the original program; only reads that we
can prove are redundant are gone. The chart shows the routines with the most folded loads: mainGtU alone has two
hundred seven, kept in seven registers.

---

## Section 5: Bugs we found and fixed (1:55)

### Slide 24: The base tool breaks cc1 (0:45)
- Base tool on cc1: output differs at byte 6669460
- Cause: AND RAX,3 in the indirect-jump stub changes the flags
- Fix: all stubs and jump guards are flag-free
- Visual: graphs/06_base_vs_ours.png

**Yazan:** The first thing we did was run the base tool as it was given. On bzip2 it is correct but fifty-seven
percent slower than native. On cc1 the output is simply wrong: the first difference is at byte six million six
hundred sixty-nine thousand four hundred sixty. The cause is the AND in its indirect-jump stub, which changes the
flags while cc1 still needs them. That is why everything we insert is flag-free. We confirmed this again in our
final measurements, and in the demo you can see the same difference at the same byte.

### Slide 25: Probes overwriting bytes others jump into (0:50)
- sgcc_peak crashed; we found the routine by translating halves of the routine list
- htab_expand's .cold part is entered 2 bytes after its start
- uid_decl_map_hash: symbol size 3, its ret is the 4th byte
- Fix: decode the whole image; such routines are translated but not probed
- Visual: CODE_WALKTHROUGH 2 (lines 601-640, 669-675)

**Aubai:** Our own first version crashed on sgcc_peak. We found the culprit by translating only half of the
routines, then half of that half, and so on, until one routine was left. Pin's probe overwrites the first bytes of a
routine, but the cold part of htab_expand is entered by its parent two bytes after its start, right inside the
overwritten bytes. A second case: a symbol of size three whose return instruction is the fourth byte. The fix:
decode the whole image first, and never probe a routine that some code jumps into near its start. It is still
translated and still reached from our own code.

### Slide 26: Heap placement and safe patching (0:20)
- Base tool: caches where the heap grows; we go 1 GB higher
- Multi-byte patches could tear; we use one 4-byte store
- Failures print ERROR, never fail silently
- Visual: DIAGRAMS b

**Yazan:** Two more design fixes from reading the base tool: its caches sit where the heap grows, and it patches
live code with multi-byte writes that another CPU could see half-done. We moved the region and use single aligned
stores.

---

## Section 6: Results (2:50)

### Slide 27: bzip2 vs native [8-MIN] (0:40)
- Median native CPU 4.75 s, median tool 4.05 s end-to-end
- Median improvement 15.23%
- Visual: graphs/01_bzip2_native_vs_tool.png

**Aubai:** Now the results. We measured ten paired rounds: each round runs native bzip2 and then our tool. Native
bzip2 took a median of four point seven five seconds of CPU. Under our tool, end to end, including the two-second
profiling window, it took four point zero five seconds. That is a median improvement of fifteen point two three
percent. The improvement is computed per round, as native CPU time minus our end-to-end time, divided by native CPU
time, and then we take the median over the rounds.

### Slide 28: Every round counts [8-MIN] (0:35)
- 10 of 10 rounds above 5%
- Worst round 5.02%, best 19.88%
- All 10 outputs byte-identical
- Visual: graphs/02_bzip2_rounds.png

**Yazan:** And it is not only the median: every one of the ten rounds is above five percent. The worst round is five
point zero two percent, the best is nineteen point eight eight, and all ten outputs are byte-identical to native.
The weakest round had a longer TC2 execution than the others, and even it stays above the target. An automated
script with five more rounds gave a median of thirteen point eight one percent.

### Slide 29: Where the speed comes from [8-MIN] (0:35)
- Without stack fold: -9.03%
- Without address fold: +2.73%
- Without cheap profiling: +7.61%
- Everything off: -14.49%
- Visual: graphs/03_bzip2_ablation.png

**Aubai:** Turning optimizations off one at a time shows where the speed comes from. Without the stack fold we are
nine percent slower than native. Without the address fold we barely beat native. Cheap profiling adds about eight
points. With everything off we are fourteen and a half percent slower. Reordering and de-virtualization change
little on bzip2, which has no hot indirect branch at all. The stack fold together with the address fold is the core
of our result.

### Slide 30: Measured honestly, three ways [8-MIN] (0:35)
- Tool clock: 14.93%
- Whole pin run, CPU: 9.55%
- Whole pin run, wall clock: 6.37% (3 of 5 rounds above 5%)
- Visual: graphs/05_bzip2_three_clocks.png

**Yazan:** We also measured from outside, with /usr/bin/time around the whole pin command. That includes Pin's own
start-up and building TC, about four tenths of a second per run. Measured that way, the median gain is still six
point three seven percent in wall-clock time and nine point five five percent in CPU time. In wall-clock time two of
the five rounds were below five percent, and we want to be upfront about that.

### Slide 31: cc1 and the gcc family (0:25)
- De-virtualization and reordering help cc1 (-13.57% vs -23.82%)
- Still slower than native: already optimized code
- Correct everywhere
- Visual: graphs/04_cc1_devirt_reorder.png, graphs/07_gcc_family.png, graphs/11_correctness_matrix.png

**Aubai:** On cc1, de-virtualization and reordering do help: minus thirteen point six percent with them, minus
twenty-three point eight without. But the gcc binaries are already optimized, so there is little to fold, and they
stay slower than native. What matters to us: all five programs produce byte-identical output in every run.

---

## Section 7: Live demo (1:30)

### Slide 32: Demo [8-MIN] (1:30)
- `docs/presentation/demo/demo.sh` (native, tool, cmp, improvement)
- Visual: terminal (screen recording)

**Yazan:** Let's see it live. The script runs exactly the commands from our README. First, native bzip2 compresses
the twenty-megabyte input. It takes a bit over four seconds.

**Aubai:** Now the same command under our tool. Watch the timing lines: the TC build before the program starts, two
seconds of profiling, a TC2 built in a few milliseconds, and then the rest of the run in TC2. It finishes well
before native.

**Yazan:** Next, cmp: the two compressed files are byte-identical, same size and same md5. Finally, the script
computes the improvement from the two measured times, both against native CPU time and for the printed line. Every
single run varies a little on this machine; the numbers on our slides are the medians of ten paired rounds. If time
allows, the second part of the script runs cc1 under the base tool, whose output differs, and under our tool, whose
output is identical.

**Aubai:** While it runs, notice that the profiling window ends after almost exactly two seconds, because our thread
checks the clock in short sleeps. The script is in our repository under docs, presentation, demo, if you want to run
it yourself.

---

## Section 8: Conclusions (0:30)

### Slide 33: Conclusions and limitations [8-MIN] (0:20)
- bzip2: 15.23% faster than native, output always identical
- Required de-virtualization + reordering work, and help cc1
- Limits: single-threaded save slots; gcc binaries slower than native
- Visual: summary bullets

**Aubai:** To sum up: bzip2 runs fifteen percent faster than native including profiling, and every output is
identical. The required optimizations work and help cc1. Our limits: the save slots assume a single-threaded
program, and the gcc binaries stay slower than native.

### Slide 34: Future work and thanks (0:10)
- ExtTSP, per-thread save slots, more spare registers
- Thank you, questions
- Visual: thank-you slide

**Yazan:** With more time we would add ExtTSP and per-thread save slots. Thank you, we are happy to take questions.

---

## Backup slides (not timed)
- **B1. Q&A**: QA.md (questions with short answers).
- **B2. Correctness matrix**: graphs/11_correctness_matrix.png and KEY_NUMBERS "Correctness".
- **B3. Knob table**: README.txt section 11 (all knobs with defaults).
- **B4. gcc family per round**: graphs/07_gcc_family.png.
- **B5. One run, second by second**: graphs/08_run_timeline.png.
- **B6. All profiling stub forms**: BEFORE_AFTER a.
- **B7. Exercise 4 mapping**: README.txt section 2 table.

---

# 8-MINUTE CUT (the course video)

Covers (1) the problem, (2) the solution with a code walk-through, (3) measurement results as graphs.
13 slides built from the full-talk slides; the spoken text below is the shortened version.

| # | video slide (from full-talk slide) | visual | time |
|---|---|---|---|
| V1 | Title (1) | title slide | 0:20 |
| V2 | The problem (3) | requirement box | 0:40 |
| V3 | Probe mode + gearing (4+5) | DIAGRAMS a | 0:40 |
| V4 | The opportunity: bzip2 is -O0 (8) | BEFORE_AFTER e (original) | 0:20 |
| V5 | Counters that never change the flags (12) | CODE_WALKTHROUGH 4, BEFORE_AFTER a | 0:40 |
| V6 | Switching live code safely (15) | BEFORE_AFTER f | 0:35 |
| V7 | De-virtualization: call + jump guards (18+19) | BEFORE_AFTER b, c | 0:55 |
| V8 | Code reordering + inversion (20+21) | DIAGRAMS d, BEFORE_AFTER d | 0:40 |
| V9 | Stack fold + address fold (22+23) | DIAGRAMS e, BEFORE_AFTER e | 0:50 |
| V10 | bzip2 vs native + every round (27+28) | graphs/01, graphs/02 | 0:40 |
| V11 | Where the speed comes from + three clocks (29+30) | graphs/03, graphs/05 | 0:35 |
| V12 | Demo, shortened (32) | terminal | 0:45 |
| V13 | Conclusions (33) | summary | 0:20 |
| | **total** | | **8:00** |

### V1. Title (0:20)
**Aubai:** Hi, we are Aubai Rabea and Yazan Rabea. Our Pin tool profiles a running program, rewrites its hot code,
and switches the program over to it while it runs. On bzip2 the result is faster than the original binary, even
including the time spent profiling.

### V2. The problem (0:40)
**Aubai:** The task: a probe-mode Pin tool, based on our exercise 4, runs the program in a translation cache, TC,
with profiling for two seconds. Then a thread builds an optimized cache, TC2, and the program continues there. De-
virtualization is required, plus at least one more optimization, and the printed time, profiling included, must beat
native by more than five percent on one of five programs.

**Yazan:** Native never pays for profiling, so this is hard. And the output must stay byte for byte identical, on
every program and in every run.

### V3. Probe mode and gearing (0:40)
**Yazan:** In probe mode, Pin only lets us overwrite the start of each routine with a jump, and we send every
routine into our copy, TC, which has counters. After two seconds our thread switches the profiling off, builds TC2
from the profile, and switches every routine head from TC to TC2 while the program keeps running. Our clock runs
from the program's first instruction to its exit. From then on, every new call enters the optimized code, and TC2
itself is built in a few milliseconds.

### V4. The opportunity (0:20)
**Aubai:** Our opportunity: bzip2 is compiled without optimization. Every local variable lives on the stack and is
reloaded for every use, like i1 and the block pointer here in mainGtU. Removing those reloads, in both caches, is
how we get below native.

### V5. Cheap, flag-safe counters (0:40)
**Yazan:** Profiling code sits between the program's instructions, so it must never change anything the program
needs, including the flags. Where our analysis proves the flags are dead, a counter is a single add. Otherwise we
use a dead register with mov and lea, which leave the flags alone, and we never push, because minus O zero code
keeps locals below the stack pointer. Blocks with a single predecessor get no counter at all. Indirect jumps and
calls get small target tables, also filled without touching the flags.

### V6. Switching live code (0:35)
**Aubai:** Every stub and routine head starts with an eight-byte NOP pad. One aligned four-byte store turns it into
a short jump, so no CPU can ever see half an instruction. These bytes were read from the running process: after the
switch, mainGtU's TC head jumps straight to its TC2 copy. We first write a full jump into a small trampoline that
nothing executes yet, and only then flip the pad, so the switch is safe while the program keeps running.

### V7. De-virtualization (0:55)
**Yazan:** For a hot indirect call, like the callback in cc1's htab_traverse, TC2 compares the target with the two
hot callbacks from the profile and calls their TC2 copies directly; otherwise the original indirect call runs.

**Aubai:** Jumps are harder, because cc1 keeps flags live across switch jumps. So the jump guard saves RCX in our
region, subtracts the expected target with LEA, and uses JRCXZ, which tests for zero without touching the flags. A
miss always falls back to the original jump, so the output can never change. A site must run at least a hundred
times, and up to three targets must cover eighty percent of it. In cc1 that gives eighty-one guarded calls and one
hundred seventy-one guarded jumps.

### V8. Code reordering (0:40)
**Yazan:** For reordering we follow the recommended paper by Newell and Pupyrev: weight every edge with the profile,
merge chains along the heaviest edges so the hot successor falls through, invert branches with the table from the
course example, and move cold blocks into one region. In mainGtU, jz became jnz, so the hot path falls through. On
bzip2 we inverted one hundred eighteen branches and moved seven hundred fifty-eight cold blocks, where cold means
below one part per million of all block executions.

### V9. Stack fold (0:50)
**Aubai:** The optimization that beats native: we keep the most-read stack slots in spare registers. Liveness tells
us where a spare may be written, and a must-dataflow tells us where it provably equals its slot on every path.
Stores stay, reloads become register moves, and mov, add, load becomes one load from r11 plus rdx. Over bzip2 that
is one thousand nine hundred fourteen loads replaced, in TC and in TC2. Every store to the stack is kept, so memory
is always exact; only reads we can prove redundant disappear. Calls, push, pop and any slot whose address is taken
invalidate the spare registers, and when we are unsure, we do not fold.

### V10. Results (0:40)
**Yazan:** Over ten paired rounds, native bzip2 took a median of four point seven five seconds of CPU and our tool
four point zero five seconds end to end, profiling included. That is a median improvement of fifteen point two three
percent. All ten rounds are above five percent, the worst is five point zero two, and every output is identical. The
improvement is computed per round against native user plus system CPU time, and the profiling window is fully
included in our time.

### V11. Why, and measured honestly (0:35)
**Aubai:** Without the stack fold we would be nine percent slower than native, so that is where the gain comes from.
Measured from outside, including Pin's own start-up, the median gain is still six point three seven percent in wall-
clock time, with two of five rounds below five percent, and nine point five five percent in CPU time. On cc1, de-
virtualization and reordering help, but the already optimized gcc binaries stay slower than native. Cheap profiling
adds about eight more points.

### V12. Demo (0:45)
**Yazan:** Live: native bzip2, then the same command under our tool. Two seconds of profiling, TC2 built in a few
milliseconds, and the run ends before native. cmp says the files are identical, and the script prints the
improvement computed from the two times. The script runs exactly the commands from our README. The TC build happens
before the program starts, the profiling window ends after two seconds, and the rest of the run is in TC2. Every run
varies a little; our slides use the median of ten paired rounds.

### V13. Conclusions (0:20)
**Aubai:** bzip2 runs fifteen percent faster than native including profiling, with identical output on all five
programs. The gcc binaries stay slower, because they are already optimized. Thank you for watching. All our raw
measurements and scripts are in our project repository.
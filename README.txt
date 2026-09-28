PROJECT: project.so - a probe-mode Pin tool that profiles in TC and re-optimizes into TC2
=====================================================================================

(a) STUDENTS
------------
  Student 1: name TODO-NAME-1     ID TODO-ID-1
  Student 2: name TODO-NAME-2     ID TODO-ID-2


(b) COMPILATION
---------------
From the src/ directory (it contains project.cpp, makefile, makefile.rules):

  make PIN_ROOT=<pin-kit-root> obj-intel64/project.so

On our machine:

  make PIN_ROOT=$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux obj-intel64/project.so

The makefiles are the unmodified course files; the build uses -Wall -Werror and produces zero warnings.


(c) HOW TO RUN
--------------
General form:

  <pin-kit-root>/pin -t <path>/project.so [-prof_time N] [other knobs] -- <program> <args>

The benchmark files may arrive without the execute bit; if so, first run:

  chmod +x bzip2 cc1 sgcc_base.mytest-m64 sgcc_peak.mytest-m64 cpugcc_r_base.mytest-m64

The five benchmarks with the default profiling time of 2 seconds:

  $PIN -t project.so -prof_time 2 -- ./bzip2 -c input-long.txt > out_tool.bz2
  $PIN -t project.so -prof_time 2 -- ./cc1 200.i -o out_tool.s
  $PIN -t project.so -prof_time 2 -- ./sgcc_base.mytest-m64 200.i -o out_tool.s
  $PIN -t project.so -prof_time 2 -- ./sgcc_peak.mytest-m64 200.i -o out_tool.s
  $PIN -t project.so -prof_time 2 -- ./cpugcc_r_base.mytest-m64 200.i -o out_tool.s

The native (reference) run and the output check, e.g. for bzip2:

  /usr/bin/time -v ./bzip2 -c input-long.txt > out_native.bz2
  cmp out_native.bz2 out_tool.bz2          (no output = identical)

(for the gcc binaries: <bin> 200.i -o out_native.s ; cmp out_native.s out_tool.s)

What the tool prints (stderr) at exit:

  [time] TC build (not counted): ...        translation before the program starts
  [time] profiling window: ...              program running in TC with profiling (= prof_time)
  [time] TC2 build: ...                     building TC2 while the program keeps running in TC
  [time] TC2 commit: ...                    switching the routines to TC2
  [time] TC2 execution: ...                 from the switch to exit
  [time] honest end-to-end total: ...       monotonic clock from the moment the program starts in TC
                                            until it exits (= window + build + commit + execution)
   Translated code run (including profiling) took: X seconds
                                            the base tool's line (prof_time + time from the TC2 commit
                                            to exit), printed for compatibility only

We compare the "honest end-to-end total" with the NATIVE CPU time (user + sys of /usr/bin/time -v).
Any failure prints a line starting with "ERROR:".


(d) THRESHOLDS: FREQUENTLY vs RARELY EXECUTED
---------------------------------------------
1. Code reordering (hot/cold blocks), knob -cold_ppm (default 1):
   A basic block is RARELY executed (cold) if its execution count during the profiling window is 0, or is
   below cold_ppm / 1,000,000 of the TOTAL number of block executions counted in the window. Otherwise it is
   FREQUENTLY executed (hot). Example: bzip2 counts about 1.85e9 block executions in 2 seconds, so a block must
   run at least ~1850 times to be hot.
   Why relative: the total count grows with the window length and with the program's speed; a fixed absolute
   count would mean something different for -prof_time 1 and -prof_time 4. One part per million keeps
   everything that matters for time in the hot area and moves the error paths and initialisation code away.
   Hot blocks are chained with Pettis-Hansen and laid out per routine; cold blocks of all routines go to one
   cold region at the end of TC2. The routine entry block always stays in the hot part.
   A routine gets a TC2 copy at all only if it executed during the window (count > 0).

2. De-virtualization, knobs -devirt_min_count (default 100), -devirt_min_pct (default 80),
   -devirt_min_share (default 10), -devirt_max_targets (default 3):
   An indirect jump or call site is FREQUENTLY executed if it ran at least 100 times in the window. Its most
   frequent targets (at most 3, each with at least 10% of the site's executions) are guarded only if together
   they cover at least 80% of its executions. Reasons: a guard costs a few instructions and bytes, so a site
   must run often to pay for it; below 80% coverage the fallback indirect branch (plus the failed compares) is
   taken too often; a target with less than 10% share mostly adds a failed compare to every other execution.


BASED ON
--------
The tool is based on our exercise 4 pintool (hw4.cpp: counts how often every basic block and every jump
target executes) and on the course's bprofile-with-gearing.cpp (TC -> TC2 gearing). project.cpp was
rewritten on that basis:
  exercise 4 feature                          -> project.cpp
  per-BBL execution counter                   -> emit_block_counter()
  fall-through counter after a cond. branch   -> emit_block_counter() on the fall-through path
  taken = exec - fallthru                     -> derive_profile()
  indirect jump target table                  -> emit_indirect_profile() (now jumps AND calls, flag-free)
  dead-register analysis (insKillsFullReg)    -> analyze_ins(), find_dead_reg(), flags_dead_from()
  skipping untranslatable routines            -> analyze_image()
  atomic disabling of profiling stubs         -> patch pads + disable_profiling()
  edge-profile.csv sorted hottest first       -> dump_profile() (-dump_prof)
  bprofile-with-gearing feature               -> project.cpp
  translate routines into an instruction list -> analyze_image(), emit_tc_routine(), emitter_t
  fix RIP-relative / branch displacements     -> layout_and_copy()
  commit with RTN_ReplaceProbed               -> commit_tc()
  TC2 thread after prof_time seconds          -> create_tc2_thread_func()
  TC -> TC2 switch at routine heads           -> commit_tc2()
  Fini on _exit, elapsed time                 -> Fini() / ExitInProbeMode()


HOW IT WORKS (TC -> TC2)
------------------------
1. When the main image is loaded (create_tc): every routine of the executable is decoded; routines that cannot
   be translated safely are left alone. Basic blocks are formed. One memory region is mapped about 1 GB above
   the end of the whole image (within 2 GB of the code): data (counters, tables) | TC | TC2.
2. TC = a translation of every routine with profiling code: a counter per block (sparse: blocks whose count can
   be derived from a single predecessor have none), a fall-through counter where needed, a target table at every
   indirect jump and call. The stack fold (below) is applied in TC as well. Every routine is redirected to its TC
   copy with RTN_ReplaceProbed. Then the program starts.
3. A tool thread waits -prof_time seconds (clock-checked), disables all profiling stubs (each starts with an
   8-byte patch pad that one aligned 4-byte store turns into "jmp over the stub"), reads the profile, and builds
   TC2 from the ORIGINAL instructions, only for routines that ran: blocks reordered, indirect branches
   de-virtualized, stack fold applied, no profiling. Every branch in TC2 is decoded again and checked.
4. Commit: each TC routine head (a 5-byte trampoline + an 8-byte pad) is switched with one aligned 4-byte
   store to jump to its TC2 copy. Calls from then on run in TC2; loops already running in TC finish in TC.
5. At _exit the times are printed.


OPTIMIZATIONS
-------------
1. De-virtualization (required). Indirect call: "cmp <operand>, T; jne next; call T' ; jmp done" per guarded
   target, then the original indirect call. Indirect jump (flags may be live, e.g. in cc1): flag-free guard
   "mov [slot],rcx; mov rcx,<operand>; lea rcx,[rcx-T1]; jrcxz hit1; lea rcx,[rcx+T1-T2]; jrcxz hit2; ...;
   mov rcx,[slot]; original jmp; hitK: mov rcx,[slot]; jmp TK'". T' = the target's TC2 copy. The original
   branch stays as the fallback, so the result is always the same. Knob -no_devirt.
2. Code reordering (Pettis-Hansen, from the recommended paper: A. Newell and S. Pupyrev, "Improved Basic
   Block Reordering", IEEE Transactions on Computers 69(12), 2020). Per routine: profiled edges, chains merged
   along the heaviest edges so the hottest successor falls through; conditional jumps inverted with the table
   of the course example (link1: JZ<->JNZ, JB<->JNB, JL<->JNL, ...; never JRCXZ/LOOP), a jmp added where a
   fall-through was broken, a jmp dropped when its target now follows; cold blocks in one cold region at the
   end of TC2; hot routines sorted by hotness. The paper's ExtTSP refinement is not implemented (the paper
   reports only about 0.5-1% over this approach). Knob -no_reorder.
3. Redundant stack-load elimination ("stack fold"), an optimization from class (register allocation /
   redundant load elimination), in TC and TC2. bzip2 is compiled -O0: every local variable lives in
   [rbp-X] and is reloaded all the time. The most-read stack slots of a routine get a spare register
   (a caller-saved register whose value is dead there, by register liveness analysis). All stores stay and
   also update the register; a later load/compare of the slot uses the register when a dataflow analysis
   proves it is up to date on every path. Only slots whose address is never taken; cleared at calls,
   push/pop, syscalls, RSP-based stores, RBP writes; routines with indirect jumps are skipped. Knob
   -no_stackfold.
4. Address fold: "mov P,spare ; add P,X ; op dst,[P+d]" -> "op dst,[spare+X+d]" when the add's flags are
   dead and P is overwritten by the op. Knob -no_addrfold.
5. Cheap profiling in TC: "add qword [rip+ctr],1" placed where the flags are dead, sparse counters.
   Knobs -no_cheap_prof, -no_sparse_prof.


BUGS OF THE BASE TOOL THAT WE FIXED
-----------------------------------
- Indirect-jump profiling used "AND RAX,3", which destroys the flags; cc1 keeps flags live across indirect
  jumps and produced wrong output. All our profiling code and jump guards are flag-free.
- TC/TC2 were mapped at the first free address above the code = where the heap grows; the static binaries
  (sgcc_*) failed in glibc start-up. Our region is ~1 GB above the end of the whole image.
- The jump-to-original table stored a new entry after incrementing its counter, so lookups never found it.
  We branch to original code directly (always reachable) and the table, used only as a fallback, uses a map.
- Multi-byte overwrites of live code (5-byte stub patch, 2-stage TC->TC2 patch) could tear. We use patch
  pads switched by one aligned 4-byte store.
- "call reg" was taken as a memory operand (it has a hidden one); we use the first explicit operand.
- RIP-relative operands in TC2 were derived from TC bytes; we always use the stored original address.
- A target or RIP address of 0 meant "none"; we use explicit flags.
- A branch into the middle of an instruction aborted all translation; we skip only that routine.
- Target hash (target & 3) put all aligned targets in one slot; we use (target >> 4) & 15.
- Failures were silent; now every failure prints ERROR.
- sleep(prof_time) can overshoot on this VM; we check the clock in short sleeps.
- Garbage time if the program exited before TC2; we print what really happened.
Found while testing (sgcc_peak crashed): Pin's probe overwrites the first bytes of a routine, but some code
jumps into those bytes from OUTSIDE the routine (a function's ".cold" part entered 2 bytes after its start;
a 3-byte symbol whose "ret" lies outside the symbol). Such routines are still translated but not probed.


HOW TIME IS COUNTED AND RESULTS
-------------------------------
Improvement = (native CPU time - honest end-to-end time) / native CPU time, each tool run paired with a
native run taken right before it, both pinned to CPU 0 (taskset -c 0), medians reported.
The profiling window IS included (native never pays for it); TC build before the program starts is not.
Measured on our VM (Intel i7-12700H guest, 4 vCPUs; timing noise of several percent, so paired medians):
  bzip2 -c input-long.txt, 10 paired rounds, -prof_time 2 (all optimizations on):
      median native CPU 4.97 s, median tool end-to-end 4.29 s
      median improvement +13.5%, worst round +5.1%, best +23.0%  -> the >5% requirement is met on bzip2.
  Ablation, bzip2, 4 paired rounds each (median improvement vs native):
      all on                        +16.3%
      -no_stackfold                  -9.4%
      -no_addrfold                   +0.7%
      -no_cheap_prof -no_sparse_prof +6.9%
      -no_devirt                     (no hot indirect sites in bzip2; +26.5% median, one very noisy native)
      -no_reorder                   +16.0%
      everything off                 -7.0%
  cc1 (3 paired rounds): all on -24.7%, -no_devirt -17.0%, -no_reorder -16.1%, both off -3.6%
      (native itself varied 27-34 s during these runs).
  sgcc_base -19.9%, sgcc_peak -41.0%, cpugcc_r_base -23.2% (1 round each).
  All five programs produce byte-identical output in every run (see docs/AUDIT.md).


KNOBS
-----
  -prof_time N            seconds of profiling in TC before TC2 is built (default 2)
  -no_devirt              disable de-virtualization
  -devirt_min_count N     minimum executions of a site in the window (default 100)
  -devirt_min_pct N       minimum coverage of the guarded targets, percent (default 80)
  -devirt_min_share N     minimum share of one guarded target, percent (default 10)
  -devirt_max_targets N   maximum guarded targets per site (default 3)
  -no_reorder             disable code reordering
  -cold_ppm N             cold-block threshold, parts per million of all block executions (default 1)
  -no_stackfold           disable the stack fold (TC and TC2)
  -no_addrfold            disable the address fold
  -no_cheap_prof          counters always in the flag-free register form
  -no_sparse_prof         a counter on every block and every fall-through
  -no_prof                no profiling at all (TC2 then contains nothing)
  -no_tc2                 do not build TC2 (stay in TC)
  -no_tc_commit           build TC but do not redirect the program to it
  -dump_tc / -dump_tc2    disassemble TC / TC2 to stderr
  -dump_orig_code         disassemble the original routines
  -dump_prof              write the profile to edge-profile.csv (exercise-4 format)
  -verbose                statistics and details
  -dbg_first / -dbg_count debugging: translate only a range of routines


KNOWN LIMITATIONS
-----------------
- The gcc binaries run slower than native: the profiling window, the TC2 build and the jumps into original
  code (switch tables target original addresses in TC) cost more than anything we gain on code that is
  already compiled with optimization. On cc1 our measurements even show de-virtualization and reordering
  as a loss (-24.7% with both vs -3.6% without, noisy native 27-34 s); they are kept on by default because
  they are the required optimizations, and can be switched off with -no_devirt / -no_reorder.
  The >5% target is met on bzip2.
- Save slots used by the profiling stubs and guards are shared by all threads; the benchmarks are
  single-threaded (a multi-threaded program would need per-thread slots).
- Return addresses on the stack point into TC/TC2 (as in the base tool): code that inspects its own return
  addresses (e.g. C++ exception unwinding through translated frames) is not supported.
- Loops that are already running in TC when TC2 is committed finish in TC.
- ExtTSP is not implemented.


FILES
-----
  project.so                    the compiled tool (top level of project.zip)
  src/project.cpp               the tool's source
  src/makefile, makefile.rules  the unmodified course makefiles
  src/README.txt                this file

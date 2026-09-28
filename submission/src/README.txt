==========================================================================================
PROJECT: project.so
A probe-mode Pin tool that profiles a program in a first translation cache (TC) and then
re-optimizes the hot code into a second translation cache (TC2) while the program runs.
==========================================================================================

Every number in this file comes from the independent re-verification (commit 58df1e1).
The raw output of every run is in docs/verify_raw/ (not part of the submission package).
The full verification report is docs/VERIFICATION.md.


==========================================================================================
(a) NAMES AND ID NUMBERS
==========================================================================================

  Student 1:  name TODO-NAME-1        ID number TODO-ID-1
  Student 2:  name TODO-NAME-2        ID number TODO-ID-2


==========================================================================================
(b) COMPILATION COMMAND
==========================================================================================

The submission contains a directory src/ with:
  project.cpp        the whole tool (one source file)
  makefile           the course makefile, unmodified
  makefile.rules     the course makefile rules, unmodified
  README.txt         this file

The build uses only the course makefiles. PIN_ROOT is the root directory of the Pin kit, i.e. the
directory that contains the "pin" launcher, source/, intel64/ and extras/. On our machine that is
$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux.

From inside src/:

  mkdir -p obj-intel64
  make PIN_ROOT=$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux obj-intel64/project.so

The "mkdir -p obj-intel64" is needed once in a fresh directory. The course makefile does not create
the output directory when the target obj-intel64/project.so is named explicitly. Without it the
compiler stops with "can't create obj-intel64/project.o: No such file or directory". We verified this
both ways (docs/verify_raw/step2_build_FAILED_without_mkdir.txt and step2_build.txt).

What the command does:
  - compiles project.cpp with the Pin tool compiler flags from the course makefile, which include
    -Wall -Werror (every warning would be an error); the build prints no warning;
  - links the tool with the Pin and XED libraries;
  - produces obj-intel64/project.so, the tool that is given to pin with "-t".

The makefiles are the unmodified course files (md5 f2d63a76ce3a7eac9e4807abd539f88b for makefile and
8bd75ba99855633c12796a74e048a387 for makefile.rules).


==========================================================================================
(c) HOW TO RUN THE TOOL
==========================================================================================

General form:

  <PIN_ROOT>/pin -t <path>/project.so [-prof_time N] [other knobs] -- <program> <program arguments>

Everything before "--" is for Pin and the tool; everything after it is the program and its arguments.
-prof_time N is the number of seconds the program runs in TC with profiling before TC2 is built. The
default is 2.

The benchmark files may arrive without the execute permission. If so, run once:

  chmod u+x bzip2 cc1 sgcc_base.mytest-m64 sgcc_peak.mytest-m64 cpugcc_r_base.mytest-m64

In the commands below PIN stands for the pin launcher:

  PIN=$HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin

The five benchmarks, each with -prof_time 2. The native run and the tool run ALWAYS write to DIFFERENT
output files, so that cmp really compares two separate results:

  bzip2:
    rm -f out_native.bz2 out_tool.bz2
    /usr/bin/time -v ./bzip2 -c input-long.txt > out_native.bz2 2> native_time.txt
    taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -c input-long.txt > out_tool.bz2 2> tool_time.txt
    cmp out_native.bz2 out_tool.bz2 && echo IDENTICAL

  cc1:
    rm -f out_native.s out_tool.s
    /usr/bin/time -v ./cc1 200.i -o out_native.s 2> native_time.txt
    taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./cc1 200.i -o out_tool.s 2> tool_time.txt
    cmp out_native.s out_tool.s && echo IDENTICAL

  sgcc_base:
    rm -f out_native.s out_tool.s
    /usr/bin/time -v ./sgcc_base.mytest-m64 200.i -o out_native.s 2> native_time.txt
    taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./sgcc_base.mytest-m64 200.i -o out_tool.s 2> tool_time.txt
    cmp out_native.s out_tool.s && echo IDENTICAL

  sgcc_peak:
    rm -f out_native.s out_tool.s
    /usr/bin/time -v ./sgcc_peak.mytest-m64 200.i -o out_native.s 2> native_time.txt
    taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./sgcc_peak.mytest-m64 200.i -o out_tool.s 2> tool_time.txt
    cmp out_native.s out_tool.s && echo IDENTICAL

  cpugcc_r_base:
    rm -f out_native.s out_tool.s
    /usr/bin/time -v ./cpugcc_r_base.mytest-m64 200.i -o out_native.s 2> native_time.txt
    taskset -c 0 $PIN -t obj-intel64/project.so -prof_time 2 -- ./cpugcc_r_base.mytest-m64 200.i -o out_tool.s 2> tool_time.txt
    cmp out_native.s out_tool.s && echo IDENTICAL

"taskset -c 0" pins the tool run to CPU 0. It is optional; it makes timings more stable.

What to check after a tool run:
  - cmp prints nothing and "IDENTICAL" is echoed (the outputs are byte-identical);
  - both output files exist and are not empty (ls -l);
  - grep -c ERROR tool_time.txt prints 0 (any failure inside the tool prints a line starting with
    "ERROR:");
  - the native CPU time is "User time" + "System time" in native_time.txt.

Example of what the tool prints on stderr (real output, bzip2, re-verification STEP 4 round 1):

  [time] TC build (not counted): 0.261 s
  [time] profiling window: 2.001 s (prof_time 2)
  [time] TC2 build: 0.008 s
  [time] TC2 commit: 0.000 s
  [time] TC2 execution: 2.046 s
  [time] honest end-to-end total: 4.055 s
   Translated code run (including profiling) took: 4.046 seconds

Line by line:
  TC build (not counted)   time spent before the program starts: decoding the executable, analysing it,
                           writing TC and redirecting the routines to it. The program has not executed a
                           single instruction yet, so this is not part of the program's run time.
  profiling window         from the program's first instruction (in TC) until the tool thread switched
                           profiling off. It should be close to -prof_time.
  TC2 build                time to build TC2 from the profile. The program keeps running in TC meanwhile.
  TC2 commit               time to switch every hot routine from its TC copy to its TC2 copy.
  TC2 execution            from the switch until the program called _exit.
  honest end-to-end total  from the program's first instruction until _exit, measured with one monotonic
                           clock = window + TC2 build + TC2 commit + TC2 execution. THIS is the number we
                           compare with native.
  Translated code run ...  the base tool's line, kept for compatibility: prof_time + TC2 execution.
                           It assumes the window was exactly prof_time and leaves out the TC2 build, so it
                           is always slightly smaller than the end-to-end total. We never use it for the
                           comparison with native.

If the program exits before -prof_time seconds (a short run), the tool prints
"program exited before prof_time ended (X s)" and "TC2 was not committed before exit", and the end-to-end
total is simply the time until exit.


==========================================================================================
(d) THRESHOLD FOR FREQUENTLY VS RARELY EXECUTED CODE
==========================================================================================

There are two decisions that separate frequently executed ("hot") from rarely executed ("cold") code.

1. Code reordering: which basic blocks are hot.

   Rule (function build_tc2 / reorder_routine_blocks in project.cpp):
     total      = sum of the execution counts of ALL basic blocks during the profiling window
     threshold  = total * cold_ppm / 1,000,000        (at least 1)
     a block is HOT  if its count >= threshold,
     a block is COLD otherwise (in particular every block with count 0).
     The entry block of a routine is always treated as hot.
   Default: -cold_ppm 1 (one part per million of all block executions).

   Worked example (bzip2, verified run docs/verify_raw/x_dump_prof_bzip2.txt): the profile of the 2-second
   window contains 1.613e9 block executions in total, so
     threshold = 1.613e9 * 1 / 1,000,000 = 1613.
   A block that ran 1613 times or more during the window is hot and is laid out with the Pettis-Hansen
   chains. A block that ran 1612 times or less goes to the cold region at the end of TC2.

   Why a RELATIVE threshold: the total count grows with the window length (-prof_time 1 vs 4) and with how
   fast the machine runs. A fixed absolute count (say "100 executions") would mean something different in
   each case. One part per million keeps in the hot area everything that can matter for the run time and
   moves out initialisation, error handling and rarely used paths. Those would otherwise sit between hot
   blocks and waste instruction-cache lines.

   A routine gets a TC2 copy at all only if at least one of its blocks ran during the window. Routines that
   did not run stay in TC.

2. De-virtualization: which indirect jumps and calls are hot enough to guard.

   Rule (function devirt_targets in project.cpp). For one indirect jump or call site:
     executions = the sum of the counts of its profiled targets during the window
     - the site qualifies only if executions >= devirt_min_count (default 100);
     - its targets are taken from the most frequent down; a target is taken only while it has at least
       devirt_min_share percent of the executions (default 10%), and at most devirt_max_targets targets
       are taken (default 3);
     - a target is skipped if it does not fit a sign-extended 32-bit immediate or has no TC2 copy; for
       calls it must also be the entry of a routine;
     - the chosen targets must together cover at least devirt_min_pct percent (default 80%) of the site's
       executions, otherwise the site is not changed at all.

   Worked examples:
     - A call site ran 1000 times: target A 700, B 200, C 60, D 40.
       A has 70% (>= 10%): taken. B has 20%: taken. C has 6% (< 10%): stop.
       A + B cover 900/1000 = 90% >= 80%: the site gets guards for A and B.
     - A site ran 50 times: below 100, nothing is done.
     - A site ran 10,000 times, spread over 5 targets with 2000 each: the top 3 cover 60% < 80%, nothing
       is done.

   Why these values: a guard costs a compare and a branch per guarded target, and some code bytes. A site
   that runs fewer than 100 times in 2 seconds cannot pay that back. Below 80% coverage the original
   indirect branch, plus the failed compares in front of it, is still taken too often. A target with less
   than 10% of the executions adds a failed compare to 90% of the executions for very little gain.
   Three targets keep the guard short (one compare and one branch each).


==========================================================================================
1. OVERVIEW
==========================================================================================

The problem. We run an unmodified x86-64 Linux program under Pin in PROBE mode. In probe mode Pin does
not interpret or JIT-compile the program. The program runs natively, and Pin only lets a tool overwrite the
beginning of routines with jumps ("probes"). The tool uses this to redirect every routine of the program
into code that the tool wrote itself:

  - TC (first translation cache): a copy of every routine of the executable with profiling code inserted.
    The program runs here for -prof_time seconds while the profile is collected.
  - TC2 (second translation cache): after the window, a tool thread builds a new, optimized copy of the
    routines that actually ran, using the profile, and switches each routine from TC to TC2.

The goal of the assignment is that the time printed by the tool, INCLUDING the profiling period, beats
native execution by more than 5% on at least one of the five given programs (bzip2, cc1, sgcc_base,
sgcc_peak, cpugcc_r_base). The output of the program must stay exactly the same.

Why that is hard. Native execution never pays for profiling. With -prof_time 2 and bzip2 running about
4.7 s natively, almost half of the run happens in TC with profiling. Translated code also has overheads of
its own: jumps over switched-off profiling code, redirections through routine heads. Well-known TC2
optimizations such as code reordering and de-virtualization gain little on small, cache-friendly programs.

How this tool gets there on bzip2:
  (1) profiling is made very cheap (one "add" per block in most blocks, many blocks without a counter);
  (2) bzip2 is compiled without optimization (-O0). It stores every local variable on the stack and reloads
      it for every use. The tool removes most of these redundant reloads (and redundant address arithmetic)
      in BOTH TC and TC2, so the translated code is faster than the original machine code.
  (3) the required de-virtualization and code reordering are fully implemented. They matter mostly for the
      big gcc binaries (on cc1 they measurably help, see section 10).

Result (verified, section 10): on bzip2 the honest end-to-end time is 15.23% below the native CPU time
(median of 10 paired rounds; worst round 5.02%; all 10 rounds above 5%). All five programs produce
byte-identical output.


==========================================================================================
2. WHAT THE TOOL IS BASED ON
==========================================================================================

The tool is based on two earlier pieces of code, as the assignment requires.

Exercise 4 (our hw4.cpp): a probe-mode tool that counts how many times every basic block and every jump
target runs. From it the project takes:

  hw4.cpp feature                                   where it is in project.cpp
  ----------------------------------------------    -------------------------------------------------
  execution counter per basic block                 emit_block_counter()
  fall-through counter after a conditional branch   emit_block_counter() placed on the fall-through path
  taken count = executions - fall-through           derive_profile()
  target table per indirect jump                    emit_indirect_profile() (now for jumps AND calls)
  dead-register analysis (insKillsFullReg,          analyze_ins(), find_dead_reg(), flags_dead_from()
    isRegDeadOnFallthruPath)
  skipping routines that cannot be translated       analyze_image()
  switching profiling stubs off atomically          patch pads: fill_pad(), patch_pad_to_jump(),
                                                    disable_profiling()
  edge-profile.csv sorted hottest first             dump_profile() (knob -dump_prof)

bprofile-with-gearing.cpp (the course's base tool): the TC/TC2 framework. From it the project takes:

  base tool feature                                 where it is in project.cpp
  ----------------------------------------------    -------------------------------------------------
  translate each routine into an instruction list   analyze_image(), emit_tc_routine(), emitter_t
  fix RIP-relative and branch displacements         layout_and_copy()
  copy into the cache                               layout_and_copy()
  redirect routines with RTN_ReplaceProbed          commit_tc()
  a tool thread that waits prof_time seconds        create_tc2_thread_func()
  switch TC routine heads to TC2                    commit_tc2()
  Fini on _exit printing the elapsed time           ExitInProbeMode(), Fini()
  debug dumps (-dump_tc, -dump_tc2,                 dump_tc(), dump_image_instrs(), dump_instr_from_mem(),
    -dump_orig_code, dump functions)                  dump_instr_from_xedd(), dump_entire_instr_map()

What was added on top of these two:
  - TC2 is built from a fresh instruction list made from the ORIGINAL instructions, not by editing the TC
    list;
  - flag-free, push-free profiling stubs, and a cheaper counter where the flags are provably dead;
  - sparse profiling (counts derived from a single predecessor);
  - patch pads for safe code patching while the program runs;
  - region placement away from the heap;
  - de-virtualization of indirect jumps and indirect calls;
  - Pettis-Hansen code reordering with branch inversion, a cold region and hot routine ordering;
  - redundant stack-load elimination ("stack fold") and address folding, in TC and TC2;
  - a self-check that decodes every branch after it has been written;
  - an honest timing report.


==========================================================================================
3. STEP-BY-STEP FLOW OF A RUN
==========================================================================================

 1. main()
      PIN_Init and PIN_InitSymbols; registers create_tc() as the image-load callback; starts the tool thread
      create_tc2_thread_func() with PIN_SpawnInternalThread; calls PIN_StartProgramProbed(). From here Pin
      loads the program.

 2. create_tc() - called by Pin for each loaded image, before the program executes anything.
      a. For every image: if it contains _exit, _exit is replaced (RTN_ReplaceProbed) by ExitInProbeMode(),
         which calls Fini() and then the real _exit.
      b. Only for the main executable:
         - check_nops(): checks with the XED decoder that the multi-byte NOP encodings used for patch pads
           decode as expected.
         - analyze_image(): collects all routines of the executable's code sections (except .plt) and sorts
           them by address. It first decodes all of them to collect every direct branch target in the whole
           image. Then it decodes each routine instruction by instruction; analyze_ins() records branch
           targets, RIP-relative addresses, flag reads and writes, and register reads and writes. Routines
           that cannot be translated safely are left untranslated (section 8). It marks routines that must
           not be probed, forms basic blocks, and assigns counters, fall-through counters and
           indirect-target sites (with sparse profiling).
         - compute_stack_folds() and address_fold(): compute the -O0 load elimination once; it is used by
           both TC and TC2.
         - allocate_region(): one memory region = data | TC | TC2, placed about 1 GB above the end of the
           image.
         - build_tc(): emit_tc_routine() for every translated routine, then layout_and_copy() (addresses,
           displacements, copy into memory) and verify_branches() (decode every branch again and check its
           target).
         - commit_tc(): RTN_ReplaceProbed() from each original routine entry to its TC copy.
         - the end-to-end clock starts here (t_app_start). The program has not run yet.

 3. The program runs in TC. Every block counter, fall-through counter and indirect-target table fills up.

 4. create_tc2_thread_func() - the tool thread.
      a. waits until TC is running, then waits for the window in short sleeps (10 ms, 1 ms near the end),
         checking the monotonic clock each time. It never uses one long sleep.
      b. disable_profiling(): switches every profiling stub off with one 4-byte store each.
      c. derive_profile(): reads the counters and derives the counts of blocks without a counter.
      d. build_tc2(): picks the routines that ran; reorder_routine_blocks() chooses a block order per
         routine; emit_tc2_block() emits the blocks (branch inversion, added/removed jumps);
         emit_tc2_ins() emits each instruction, with devirt_targets(), emit_devirt_call() and
         emit_devirt_jmp() for hot indirect branches and emit_ins() for the stack fold; then
         layout_and_copy() and verify_branches().
      e. commit_tc2(): switches each TC routine head to its TC2 copy (section 4, "patch pads").

 5. The program continues. Every new call of a hot routine enters its TC2 copy. Loops that are already
    running inside a TC frame finish there (with profiling off).

 6. The program calls _exit. ExitInProbeMode() calls Fini(), which prints the timing lines (and, with
    -dump_prof, writes edge-profile.csv). Then the real _exit runs.


==========================================================================================
4. PROFILING IN TC
==========================================================================================

What is counted:
  - every basic block: how many times it ran. Block leaders are the routine entry, every direct branch
    target in the image, and the instruction after a conditional jump, an unconditional jump, a return or
    an indirect jump;
  - after every conditional jump: how many times the fall-through path was taken, so that
    taken = executions - fall-through;
  - every indirect jump AND every indirect call: which target addresses it went to, and how often.

Counter forms (emit_block_counter):
  - Cheap form: add qword ptr [rip+counter], 1
    Used only where the flags are provably dead. flags_dead_from() scans forward along the straight-line
    path (at most 24 instructions). The flags are dead if an instruction writes all six status flags
    (CF, PF, AF, ZF, SF, OF) before any instruction reads one, or if a call comes first (the calling
    convention does not preserve flags across a call). The counter may be placed anywhere inside the
    block, because every instruction of a straight-line block runs once per execution of the block. It
    goes to the first point where the flags are dead, in -O0 code typically right before the block's
    compare.
  - Dead-register form: mov r,[rip+counter] ; lea r,[r+1] ; mov [rip+counter],r
    Used where the flags may be live but some register r is dead (find_dead_reg(): it is overwritten
    before it is read on the straight-line path). MOV and LEA do not change the flags.
  - Save/restore form: mov [rip+save],rax ; mov rax,[rip+counter] ; lea rax,[rax+1] ;
    mov [rip+counter],rax ; mov rax,[rip+save]
    The last resort, also flag-free.

Every stub starts with an 8-byte patch pad (see below), so it can be switched off.

Sparse profiling. A block that has exactly one predecessor in its routine does not need its own counter.
This applies only in routines without indirect jumps, never to a routine entry, and never to a block that
another routine jumps into.
  - Reached only by a conditional jump's fall-through: the block keeps its counter, and that counter also
    serves as the jump's fall-through counter. The separate fall-through stub disappears.
  - Reached only by a conditional jump's taken edge: count = executions(pred) - fall-through(pred).
  - Reached only by an unconditional jump: count = executions(pred).
derive_profile() fills these in after the window, repeating until nothing changes.

Dense counters. All counters are 64-bit values in one array inside the region, next to TC. Every counter
access is a short RIP-relative instruction.

Indirect-target tables (emit_indirect_profile). Each indirect site has 16 slots of {target, count}. The
slot is chosen by the hash (target >> 4) & 15. Shifting first matters because many targets are 16-byte
aligned: "target & 3", as in the base tool, puts all of them into the same slot. The hash is computed
without changing the flags: the target is stored to a scratch slot, its low byte is loaded with MOVZX, and
a 256-entry table translates the byte into the slot offset. The target operand is taken from the FIRST
EXPLICIT operand of the jump or call. A "call reg" also has a hidden memory operand (the return-address
push), so counting memory operands is wrong for it.

Why the stubs must be flag-free. A stub is inserted between two instructions of the program, and at that
point the program may still need the flags. cc1 computes a condition, jumps through a switch table and
tests the flags in the case it lands on. The base tool's indirect-jump stub uses AND RAX,3 (which changes
the flags), and with the base tool cc1's output is wrong (verified: first difference at byte 6669460).
With our flag-free stubs cc1's output is identical.

Why nothing is pushed. Leaf functions compiled with -O0 keep their local variables in the 128-byte "red
zone" below the stack pointer, without moving the stack pointer. A push would overwrite them. Every
register the stubs need is saved in a slot inside the region instead.

Patch pads. Code that the program may be executing must be changed without letting another CPU see a
half-written instruction. Every stub, and every routine head in TC, starts with an 8-byte pad made of two
NOP instructions: NOP(k) + NOP(8-k), with k chosen so that pad+k is 4-byte aligned (fill_pad).
patch_pad_to_jump() turns the pad into a jump with ONE aligned 4-byte store at pad+k: "EB disp8" plus two
filler bytes. An aligned 4-byte store never crosses a cache line, so a CPU fetching the pad sees either the
complete old NOP or the complete new jump.
  - Switching a profiling stub off: the pad becomes "jmp short <end of the stub>".
  - Switching a routine to TC2: in front of each TC routine head there are 5 spare bytes (the trampoline).
    commit_tc2() first writes "jmp rel32 <TC2 copy>" into the trampoline, which nothing executes yet, and
    then turns the head pad into "jmp short trampoline".


==========================================================================================
5. DE-VIRTUALIZATION (REQUIRED OPTIMIZATION)
==========================================================================================

Selection: section (d) above (devirt_targets).

Indirect CALL (emit_devirt_call). Before, in TC2:

    call qword ptr [rbx+0x18]

After (two guarded targets T1, T2; T1' and T2' are their TC2 copies):

    cmp qword ptr [rbx+0x18], T1
    jnz L1
    call T1'
    jmp done
  L1:
    cmp qword ptr [rbx+0x18], T2
    jnz L2
    call T2'
    jmp done
  L2:
    call qword ptr [rbx+0x18]          ; the original indirect call as fallback
  done:

CMP changes the flags. That is allowed only at a call, because the calling convention does not keep flags
across a call.

Indirect JUMP (emit_devirt_jmp). The flags can be live across an indirect jump (the cc1 case above), so
this guard uses only MOV, LEA and JRCXZ, none of which changes the flags. Before:

    jmp qword ptr [rax*8+0x6a1b20]

After:

    mov [rip+slot], rcx                 ; save RCX in the region (never push: red zone)
    mov rcx, qword ptr [rax*8+0x6a1b20] ; rcx = the real target
    lea rcx, [rcx-T1]                   ; rcx = target - T1, flags unchanged
    jrcxz hit1                          ; jump if rcx == 0, i.e. target == T1
    lea rcx, [rcx+T1-T2]                ; rcx = target - T2
    jrcxz hit2
    mov rcx, [rip+slot]                 ; restore RCX
    jmp qword ptr [rax*8+0x6a1b20]      ; the original indirect jump as fallback
  hit1:
    mov rcx, [rip+slot]
    jmp T1'
  hit2:
    mov rcx, [rip+slot]
    jmp T2'

JRCXZ only has an 8-bit displacement, so the layout checks that every hit label is within range.

Why it can never change the output: the guard compares the real runtime target with T. Only if they are
equal does it continue at T's TC2 copy, which is a translation of exactly the code the indirect branch would
have reached. Otherwise the unchanged original branch runs. RCX is restored on every path. The jump guard
changes no flags; the call guard changes them only where they are dead by convention.

Checks:
  - verify_branches() decodes every branch of TC2 (including the guard branches) after it has been
    written, and compares its target with the intended one.
  - Stress test: with -devirt_min_count 1 -devirt_min_pct 50, far more sites are guarded (cc1 144 calls +
    228 jumps, sgcc_peak 552 calls + 490 jumps in the verified runs). The output of bzip2, cc1 and
    sgcc_peak stays byte-identical.
  - In the default configuration on cc1, 81 calls and 171 jumps are guarded (verified run). On bzip2 no
    site qualifies, so -no_devirt produces an identical TC2 there.


==========================================================================================
6. CODE REORDERING
==========================================================================================

Blocks and edges. For each routine with a TC2 copy, the profile gives edge weights:
  - conditional jump: taken edge = executions - fall-through, fall-through edge = fall-through count;
  - unconditional jump: executions;
  - block that just falls into the next one: executions.

Pettis-Hansen chain merging (reorder_routine_blocks):
  1. Every hot block starts as a chain of one block.
  2. Edges between hot blocks are sorted by weight, heaviest first.
  3. For each edge s -> t with weight > 0: if s is the LAST block of its chain, t is the FIRST block of a
     different chain, and t is not the routine entry, the two chains are joined (t's chain after s's chain).
     Now t directly follows s, so this edge needs no jump.
  4. The chain containing the entry block is placed first, then the other chains ordered by their hottest
     block.

Small example (traced by hand against the algorithm). Original order A, B, C, D. Block A ends with
"jl C": taken 990 times, fall-through to B 10 times. B ends with "jmp D" (10 times). C falls through into D
(990 times). Edges, heaviest first: A->C 990, C->D 990, A->B 10, B->D 10.
  - A->C: A is the end of [A], C is the start of [C]  -> chain [A C]
  - C->D: C is the end of [A C], D is the start of [D] -> chain [A C D]
  - A->B: A is not the end of its chain any more     -> skipped
  - B->D: D is not the start of its chain any more   -> skipped
Layout: [A C D] (the entry chain) then [B]. Now the TAKEN successor C follows A, so A's jump is inverted
(next paragraph). C falls into D without a jump, B keeps its "jmp D", and the hot path A-C-D contains no
taken jump at all.

Branch inversion (the technique of the course's link1.txt, function invert_jcc). If after reordering the
TAKEN successor of a conditional jump comes right after it, the condition is inverted and the jump goes to
the old fall-through instead. For the example above, before (original order A, B, C):

    A:  cmp eax, ebx
        jl  C                ; taken 990 times
    B:  ...                  ; fall-through, 10 times

After (C placed right after A):

    A:  cmp eax, ebx
        jnl B                ; inverted: go to B when NOT less
    C:  ...                  ; falls through, the hot case

The inversion table (from link1.txt): JB<->JNB, JBE<->JNBE, JL<->JNL, JLE<->JNLE, JO<->JNO, JP<->JNP,
JS<->JNS, JZ<->JNZ. JRCXZ and LOOP are never inverted (routines containing them are not translated).

Added and removed jumps (emit_tc2_block):
  - a conditional jump whose neither successor follows it keeps its condition, and an unconditional jump to
    the old fall-through is added;
  - a block whose fall-through successor no longer follows it gets an explicit jump;
  - an unconditional jump whose target now follows it is removed.
In the verified bzip2 run: 118 jumps inverted, 97 jumps added, 44 jumps removed, 758 cold blocks.

The cold region. Cold blocks of all routines go into ONE region at the end of TC2, in their original order.
Hot code then sits together, and fewer instruction-cache lines and TLB pages are needed.

Routine order. Hot routines are placed in order of decreasing hotness (sum of their block counts).

Relation to the paper. A. Newell and S. Pupyrev, "Improved Basic Block Reordering", IEEE Transactions on
Computers 69(12), 2020. The paper describes the classical goal (maximize fall-through weight, a TSP-like
problem), the Pettis-Hansen greedy chain merging as the baseline, and hot/cold splitting of cold blocks.
Its own contribution, ExtTSP, also rewards short (not only zero-length) jumps and splits chains while
merging. The paper reports that ExtTSP gains about 0.5-1% over the TSP-based approach. That is too small to
measure on this machine (run-to-run noise is several percent), so ExtTSP was not implemented.


==========================================================================================
7. STACK FOLD AND ADDRESS FOLD
==========================================================================================

What -O0 code looks like (bzip2, routine mainGtU):

    mov   dword ptr [rbp-0x14], edi     ; store argument i1 on the stack
    ...
    mov   edx, dword ptr [rbp-0x14]     ; reload i1
    mov   rax, qword ptr [rbp-0x20]     ; reload the pointer "block"
    add   rax, rdx
    movzx eax, byte ptr [rax]           ; block[i1]
    mov   byte ptr [rbp-0x5], al        ; store c1
    ...
    add   dword ptr [rbp-0x14], 1       ; i1++
    mov   edx, dword ptr [rbp-0x14]     ; reload i1 again

Every value goes through memory. The reloads sit on the critical path of the computation.

Stack fold (compute_stack_folds_rtn). For each routine that has a standard frame (mov rbp,rsp) and no
indirect jumps:
  1. Every access to a stack slot [rbp+d] is classified: store of a register or a constant, load,
     zero/sign-extending load, arithmetic with a memory source, compare with memory, add/sub/inc/dec of
     memory, or anything else.
  2. The most-read slots each get a spare register. Candidates are the caller-saved registers r11, r10, r9,
     r8, rdi, rsi, rcx.
  3. Register liveness is computed backwards over the routine's control-flow graph. A spare may only be
     WRITTEN where the program's own value in that register is dead.
  4. A forward dataflow analysis computes, for every instruction, whether the spare is known to equal its
     slot on EVERY path to that point. At a merge of paths both must agree. The routine entry, and blocks
     entered from other routines, start as "unknown".
  5. Rewrites:
       store to the slot           -> the store stays, followed by "mov spare, value"
       load of a valid slot        -> "mov reg, spare" instead of the memory load
       compare/arithmetic with it  -> the register form (e.g. cmp dword [rbp-0x14],3 -> cmp r8d,3)
       add/sub/inc/dec of the slot -> the memory instruction stays, followed by "lea spare,[spare+imm]"
                                      (LEA does not change the flags)
       load of a slot not known valid -> the load stays, followed by "mov spare, reg" (now it is valid)
The example above becomes (i1 in r8, block in r11):

    mov   dword ptr [rbp-0x14], edi
    mov   r8d, edi                      ; keep the copy in sync
    ...
    mov   edx, r8d                      ; was a memory load
    movzx eax, byte ptr [r11+rdx]       ; address fold, see below
    mov   byte ptr [rbp-0x5], al
    movzx r9d, al
    ...
    add   dword ptr [rbp-0x14], 1
    lea   r8, [r8+1]
    mov   edx, r8d

Address fold (address_fold). The pattern "mov P, spare ; add P, X ; op dst, [P+d]", where the operation
overwrites P and the flags of the add are dead, becomes "op dst, [spare+X+d]". Two instructions disappear.
In the verified bzip2 run: 77 routines folded, 1914 loads replaced, 775 register syncs, 130 address folds.
mainGtU alone: 7 slots, 207 loads.

Safety rules and why each is needed:
  - Every store stays. Memory is always exact, so any code that reads the variable through memory (another
    function, a debugger, a signal handler) still sees the right value.
  - Only slots whose address is never taken. A "lea reg,[rbp+d]" or an indexed access [rbp+d+index]
    means a pointer to that area exists, and a write through the pointer could change the slot behind our
    back. Every slot at or above d is excluded (arrays and structures grow upwards from their base). Slots
    at positive offsets (arguments) and slots accessed with several different widths are excluded too.
  - Spares only where the program's value is dead (liveness). We never overwrite a value the program still
    needs, including argument registers that are passed on.
  - Everything is invalidated at calls (the callee may change caller-saved registers), at push/pop, at
    system calls, at stores through RSP (they can reach the frame), at writes to RBP, and when the program
    itself writes the spare register.
  - Routines with indirect jumps are skipped, because not all of their control flow is known.
  - Profiling stubs in TC never use a register that the fold uses in that routine.
  - If a register copy that the analysis relies on cannot be encoded, the whole routine is left unchanged.
  - Rule of thumb: when unsure, do not fold.

Why the output cannot change: memory holds exactly the same values as in the original program. A register
replaces a memory read only where it provably holds the same bits. The flags are either not touched (MOV,
LEA) or set exactly as before (the register form of a compare or arithmetic instruction sets the same flags
as the memory form). The address fold only removes an add whose flags are dead and a register value that is
overwritten before anyone reads it.

Why in both TC and TC2: almost half of a bzip2 run happens in TC (the 2-second window). Frames that are
already running when TC2 is committed also finish in TC. Folding TC as well makes the profiling window
itself faster than native.


==========================================================================================
8. BUGS FOUND IN THE BASE TOOL AND FIXED
==========================================================================================

Observed directly:

  cc1 output is wrong with the base tool.
    How it showed up: with bprofile-with-gearing.cpp built unchanged, cc1's output differs from native
    (re-verified: first difference at byte 6669460, docs/verify_raw/x_base_cc1.txt). bzip2 is correct but
    slower (printed time 9.22 s against 5.86 s native CPU in that run).
    Cause: the indirect-jump profiling stub executes AND RAX,3, which changes the flags, and cc1 uses flags
    across indirect jumps.
    Fix: all profiling code and jump guards are flag-free (section 4, 5).

Found while testing our own tool on sgcc_peak (it crashed; the crashing routine was found by translating
only halves of the routine list, knobs -dbg_first/-dbg_count):

  A probe overwrote bytes that other code jumps into.
    Pin's RTN_ReplaceProbed overwrites the first bytes of a routine, and Pin's safety check only looks inside
    that routine. htab_expand.lto_priv.0.cold is entered by its parent routine 2 bytes after its start.
    Fix: the whole image is decoded first, and a routine with any branch target in its first 16 bytes is
    translated but not probed (it is still reached from TC/TC2 code).

  A symbol whose size is too small.
    uid_decl_map_hash has symbol size 3, but its ret is the 4th byte. The TC copy continued at the
    original ret, which the probe had overwritten.
    Fix: a routine that runs off its end into bytes that are not the next routine is not probed.

Found by reading the base tool's code (design fixes; not all of them were observed failing here):

  - TC/TC2 are placed at the first free address above the code, which is where the heap grows. The task
    brief reports start-up failures of the static gcc binaries from this; we did not reproduce it
    ourselves. Fix: the region is placed about 1 GB above the end of the whole image (IMG_HighAddress),
    reduced if needed to stay within 2 GB of the code, with the old search as a fallback. sgcc_peak runs
    correctly, also under setarch -R.
  - The jump-to-original table stores a new entry at the index after incrementing its counter, so lookups
    never find an existing entry and the table fills up. Fix: direct rel32 branches to the original code
    (always reachable by construction); the table is only a fallback for targets outside the image and uses
    a real map lookup.
  - Code is patched with multi-byte writes while it may be executing (a torn instruction is possible).
    Fix: patch pads, one aligned 4-byte store.
  - "call reg" is classified by counting memory operands, which is wrong because it has a hidden one.
    Fix: the first explicit operand decides.
  - In TC2, RIP-relative operands are derived from the TC bytes. Fix: always from the stored original
    absolute address.
  - An address or branch target of 0 means "no operand". Fix: explicit has-target / has-RIP flags.
  - A branch into the middle of an instruction aborts all translation. Fix: only that routine is skipped.
    Routines with JRCXZ, LOOP or XBEGIN are skipped too (8-bit or special displacements).
  - The indirect-target hash is target & 3. Fix: (target >> 4) & 15.
  - Failures are silent (the program then just runs original code). Fix: every failure prints a line
    starting with "ERROR:", plus a final error count.
  - sleep(prof_time) can overshoot on this VM. Fix: short sleeps with a clock check, and a warning if the
    window overshoots by more than 0.25 s.
  - If the program exits before TC2, garbage times are printed. Fix: the report says what really happened.


==========================================================================================
9. HOW TIME IS MEASURED
==========================================================================================

Three measurements exist, and they include different things:

  1. The printed compatibility line "Translated code run (including profiling) took: X seconds"
       = prof_time + (time from the TC2 commit until _exit).
     It assumes the window lasted exactly prof_time and leaves out the TC2 build and commit. It is printed
     because the base tool printed it. We do not use it for the comparison.

  2. The internal end-to-end time "[time] honest end-to-end total: X s"
       = one monotonic clock from the program's first instruction until the program calls _exit
       = profiling window + TC2 build + TC2 commit + TC2 execution.
     The clock starts in create_tc(), right after commit_tc() (project.cpp line 2173). Pin calls create_tc()
     for the main executable before the program executes anything, so no application work is outside
     this clock. It EXCLUDES Pin's own start-up and the building of TC. The native run has neither, and
     the program has not started yet at that point. It also excludes process teardown after _exit.
     This is the number used for the >5% question.

  3. The external whole-run time: /usr/bin/time -v around the complete pin command. It INCLUDES Pin's
     start-up, TC construction and teardown, which is about 0.37-0.40 s per bzip2 run in our
     measurements.

Improvement = (native user + system CPU time - tool time) / (native user + system CPU time) * 100.
Each tool run is paired with a native run made right before it. Medians over several rounds are reported,
because run-to-run noise on this VM is several percent.


==========================================================================================
10. RESULTS (VERIFIED)
==========================================================================================

All results: commit 58df1e1, docs/VERIFICATION.md, raw files in docs/verify_raw/.
Native runs are not pinned; tool runs use taskset -c 0 (exactly the commands in section (c)).

bzip2, 10 paired rounds, internal end-to-end time:

  round  native CPU  end-to-end  improvement
    1      4.87        4.055       16.74%
    2      4.88        4.174       14.47%
    3      4.73        3.964       16.19%
    4      4.92        3.942       19.88%
    5      4.66        4.426        5.02%
    6      4.66        4.056       12.96%
    7      4.75        3.984       16.13%
    8      4.82        4.052       15.93%
    9      4.75        4.259       10.34%
   10      4.74        4.051       14.54%

  median 15.23%, mean 14.22%, minimum 5.02%, maximum 19.88%, 10 of 10 rounds above 5%, all 10 outputs
  byte-identical.
  Arithmetic checked on every row: window + build + commit + execution = end-to-end (within 0.01 s), the
  compatibility line = 2 + execution, the window is 2.001-2.009 s.

bzip2 with the outside clock (whole pin run under /usr/bin/time), 5 paired rounds:
  internal end-to-end improvement: median 14.93% (11.26% ... 16.30%)
  external wall-clock improvement: median 6.37% (3.21% ... 8.28%), 3 of 5 rounds above 5%
  external user+sys improvement:   median 9.55% (7.26% ... 11.89%), 5 of 5 rounds above 5%
  The difference between external wall time and internal end-to-end is 0.37-0.40 s per run: Pin start-up,
  TC construction and teardown.

bench.sh (the automated version of the same procedure), 5 rounds: median 13.81% (11.46% ... 16.45%).

Ablation on bzip2 (4 paired rounds each, median paired improvement):
  all optimizations on                          +15.86%
  -no_stackfold                                  -9.03%
  -no_addrfold                                   +2.73%
  -no_cheap_prof -no_sparse_prof                 +7.61%
  -no_devirt                                    +14.33%  (no site qualifies on bzip2; identical TC2)
  -no_reorder                                   +13.22%
  everything off                                -14.49%
  The stack fold with the address fold is what makes bzip2 faster than native. Cheap profiling adds
  about 8 points.

Correctness (every run compared with a fresh native run written to a SEPARATE file; sizes and md5 recorded;
exit codes compared; no ERROR line; TC2 committed):
  bzip2 (2 rounds, -prof_time 1, -prof_time 4)          identical
  cc1 (2 rounds)                                          identical
  sgcc_base (2 rounds)                                    identical
  sgcc_peak (2 rounds, and 2 rounds under setarch -R)     identical
  cpugcc_r_base (2 rounds)                                identical
  de-virtualization stress on bzip2, cc1, sgcc_peak      identical
  bzip2 -1, -9, -d, -t, stdin, -dc, empty file, 1 KB file, 3 MB random data: identical
  round trips (tool compress / native decompress and the reverse): identical to input-long.txt
  debug knobs -dump_tc -dump_tc2 -dump_orig_code -dump_prof -verbose: identical output
  The check itself was tested: a copy of the tool output with one flipped byte is reported as different
  by cmp for all 5 benchmarks.

The gcc family (correct, but slower than native), improvement per paired round:
  cc1              -45.89%, -15.19%
  sgcc_base        -38.11%, -30.40%
  sgcc_peak        -64.48%, -45.72%   (under setarch -R: -49.89%, -36.15%)
  cpugcc_r_base    -23.67%, -19.38%
  cc1 ablation (2 rounds each, mean): all on -13.57%, -no_devirt -22.81%, -no_reorder -20.23%,
  both off -23.82%. De-virtualization and reordering help cc1, but not enough to beat native.
Why the gcc binaries are slower: they are compiled with optimization, so the stack fold finds almost
nothing (on cc1 only 30 routines with a -O0 style frame). What remains are the costs: the 2-second window
with profiling, jumps over the disabled stubs in TC, switch-table jumps in TC that land in original code,
and a large TC2 (2164 routines for cc1 in the verified run) built during the run.
The timings of these large programs also vary a lot from run to run on this VM (cc1: -46% and
-15% in two consecutive rounds).


==========================================================================================
11. ALL KNOBS
==========================================================================================

  knob                    default  meaning
  ----------------------  -------  ---------------------------------------------------------------
  -prof_time N            2        seconds of profiling in TC before TC2 is built
  -no_devirt              0        disable de-virtualization in TC2
  -devirt_min_count N     100      minimum executions of a site during the window
  -devirt_min_pct N       80       minimum percent of a site's executions covered by the guards
  -devirt_min_share N     10       minimum percent of a site's executions for one guarded target
  -devirt_max_targets N   3        maximum guarded targets per site
  -no_reorder             0        disable code reordering (original block and routine order)
  -cold_ppm N             1        cold-block threshold in parts per million of all block executions
  -no_stackfold           0        disable the stack fold (in TC and TC2; also disables the address fold)
  -no_addrfold            0        disable only the address fold
  -no_cheap_prof          0        never use "add [rip+ctr],1"; always the flag-free register forms
  -no_sparse_prof         0        a counter on every block and every fall-through
  -no_prof                0        no profiling at all (then no routine ran "during the window" and TC2
                                   stays empty)
  -no_tc2                 0        do not build TC2 (the program stays in TC)
  -no_tc_commit           0        build TC but do not redirect the program to it
  -dump_tc                0        disassemble TC to stderr
  -dump_tc2               0        disassemble TC2 to stderr
  -dump_orig_code         0        disassemble the original routines to stderr
  -dump_prof              0        write the profile to edge-profile.csv (exercise-4 format:
                                   address, executions, taken, fall-through, then target/count pairs)
  -verbose                0        statistics (routines, blocks, sites, fold counts, TC2 contents)
  -dbg_first N            0        debugging: translate only candidate routines with index >= N
  -dbg_count N            -1       debugging: translate at most N candidate routines (-1 = all)


==========================================================================================
12. KNOWN LIMITATIONS
==========================================================================================

  - The gcc binaries run slower than native (section 10). The >5% target is met on bzip2.
  - Measured with an outside clock that also contains Pin start-up and TC construction, the bzip2 gain is
    smaller: median 6.37%, and 2 of 5 rounds were below 5% (section 10).
  - The save slots used by the profiling stubs and jump guards are shared by all threads. The benchmarks
    are single-threaded; a multi-threaded program would need per-thread slots.
  - Return addresses on the stack point into TC/TC2, as in the base tool. Code that inspects its own
    return addresses (for example C++ exception unwinding through translated frames) is not supported.
  - Loops that are already running inside a TC frame when TC2 is committed finish in TC.
  - Indirect jumps in TC go to the original code (the targets in jump tables are original addresses). The
    rest of that routine invocation then runs untranslated; only de-virtualized targets in TC2 stay in
    translated code.
  - ExtTSP from the paper is not implemented.
  - Routines that cannot be translated safely (JRCXZ, LOOP, XBEGIN, a branch into the middle of one of
    their instructions, overlapping symbols) run as original code.


==========================================================================================
13. FILES IN THE SUBMISSION
==========================================================================================

  project.zip
    project.so                 the compiled tool (built with the command in section (b))
    src/project.cpp            the complete source of the tool
    src/makefile               the unmodified course makefile
    src/makefile.rules         the unmodified course makefile rules
    src/README.txt             this file

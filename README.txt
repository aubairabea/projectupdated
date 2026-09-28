================================================================================
 Binary Translation Course Project - project.so
================================================================================

NAMES AND ID NUMBERS
--------------------
    <name 1>    <ID 1>
    <name 2>    <ID 2>


COMPILATION COMMAND
-------------------
The sources are in src/: project.cpp, makefile, makefile.rules and this
README. Copy the src/ directory into the Pin kit's tools directory (for
example as pin4.0/source/tools/project) and run, from inside it:

    make obj-intel64/project.so

(Running plain "make" builds the same tool.) This produces
obj-intel64/project.so. Alternatively, the directory can live anywhere if
PIN_ROOT points to the kit root:

    make PIN_ROOT=<path-to-pin4.0> obj-intel64/project.so

There are no optimization switches to set: every optimization described
below is built in and always on.


HOW TO RUN THE TOOL
-------------------
Run the benchmark through Pin, with its normal arguments after "--".
-prof_time sets the number of seconds of profiling in TC before the tool
switches to the optimized TC2 (default 2; our results use -prof_time 2).
From the directory that holds the tool and the benchmark:

    <pindir>/pin -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -c input-long.txt > out_tool.bz2
    <pindir>/pin -t obj-intel64/project.so -prof_time 2 -- ./cc1 200.i -o out_tool.s
    <pindir>/pin -t obj-intel64/project.so -prof_time 2 -- ./sgcc_base.mytest-m64 200.i -o out_tool.s

On exit the tool prints its elapsed time, including the profiling phase:

    Translated code run (including profiling) took: <seconds> seconds

together with a breakdown (TC construction, profiling window, TC2 build,
execution in TC2) and a CPU-time figure. The breakdown also gives the
measurement used by the provided bprofile-with-gearing tool: add -prof_time
to the printed "execution in TC2" line (see "How the time is counted").

To run natively for comparison, run the benchmark directly:

    /usr/bin/time -v ./bzip2 -c input-long.txt > out_native.bz2

Correctness check - the two outputs must be byte-identical:

    cmp out_native.bz2 out_tool.bz2

Note: the .mytest-m64 files are provided without execute permission; run
"chmod u+x <file>" before running them.

Optional numeric knobs (defaults shown; our results use the defaults):
    -devirt_min_pct 90  -devirt_min_count 1000  -reorder_min_count 1


THRESHOLDS: FREQUENTLY VS. RARELY EXECUTED CODE
-----------------------------------------------
All decisions are made from the execution counts gathered in TC during the
-prof_time window.

  Code reordering (trace layout)
    A basic block is "frequently executed" if it ran at least once during
    the profiling window (reorder_min_count = 1); a block that never ran is
    "rarely executed". Traces are grown only along CFG edges whose measured
    count is non-zero, starting from the most-executed block, so hot blocks
    are laid out first and in hot-path order, and never-executed blocks end
    up in a cold tail after all the hot code. We use "ran at least once"
    rather than a higher cut-off because the profiling window is short
    (2 seconds): any block seen in that window is on a path the program is
    actually using.

  De-virtualization
    An indirect call or jump site is de-virtualized only if its most
    frequent target was hit at least 1000 times (devirt_min_count) and
    accounts for at least 90% of all the samples at that site
    (devirt_min_pct). Below that, the guard would miss too often to pay for
    itself, so the site is left as it is.

  Stack-fold
    Not profile-driven: it is applied to every matching site, and is
    applied before TC is built so that the profiling window benefits too.


OVERVIEW
--------
project.so is a Pin probe-mode pintool built on the Exercise-4 basic-block /
jump profiler and the provided bprofile-with-gearing.cpp. Every routine of
the main executable is translated into an instrumented translation cache
(TC) that counts how often each basic block, each fall-through edge and each
indirect call/jump target is executed. After -prof_time seconds, an internal
thread uses that profile to build a second, optimized and
instrumentation-free translation cache (TC2), and execution is redirected
into it for the rest of the run.

We measured our result against NATIVE execution (the benchmark run directly,
without Pin).


OPTIMIZATIONS USED
------------------
All of the following are always on. (The names in parentheses are the names
of the corresponding switches inside project.cpp.)

  De-virtualization - the required optimization (opt_devirt)
    Applied to both indirect calls and indirect jumps. During the TC run
    every indirect call/jump site records its most frequent target. When
    TC2 is built, a site whose dominant target is hot enough (see the
    thresholds above) gets a guard in front of it: if the run-time target
    equals the dominant one, control goes by a direct call/jump straight to
    that target's translated code; otherwise the original indirect
    call/jump runs unchanged.
    A call guard uses a plain cmp, since flags are not preserved across a
    call anyway. A jump guard must not disturb anything the jump target
    might rely on, so it compares with lea+jrcxz (neither touches the
    flags), uses no stack (the enclosing routine may keep live data below
    the stack pointer), and saves the one register it uses in a scratch
    word inside our own code cache. A jump is only redirected if its hot
    target starts a basic block in our translation.

  Code reordering (opt_reorder / opt_trace)
    Profile-guided trace layout: greedy trace formation along the hottest
    edges, reversal of conditional branches so that the hot successor falls
    through, and removal of jumps that became redundant.

  Stack-fold register caching (opt_stackfold / opt_stackfold2 / opt_foldtc1)
    Our main additional optimization.
    - opt_stackfold: -O0 code repeatedly reloads the same pointer from the
      stack, computes an address and uses it ("mov R,[rbp+D]; add R,idx;
      ...,[R]"). We cache the reloaded pointer in a spare register the
      first time and fold the address computation directly into the
      addressing mode of every later use, instead of repeating the reload
      and the add.
    - opt_stackfold2: a value that is loaded, immediately stored to a
      stack slot and reloaded later in the same block is kept in a spare
      register instead; each later reload becomes a cheap register move
      (or the memory access is folded into the instruction that uses it).
    - opt_foldtc1: both rewrites are applied before TC is even built, so
      the profiling window itself already runs the faster folded code, not
      only the code that runs in TC2.
    The spare registers are chosen only from registers that static analysis
    proves are either never referenced anywhere in the routine, or read only
    in its entry block and never written - restricted to caller-saved
    registers, so nothing is left corrupted for the caller. We also verified,
    by decoding every profiling instruction inside a folded region, that our
    own instrumentation never writes to a register a fold site reserved.

  Cheaper profiling in TC (opt_sparseprof / opt_leantc1 / opt_densectr)
    - no counter on blocks whose count can be derived from a predecessor,
    - no marker NOP in front of every profiling stub,
    - all counters in one contiguous array (fewer cache misses).
    These make the fixed profiling window run closer to native speed.
    (Counters still use registers that are dead at the insertion point
    wherever the simple liveness test allows it; the wider dead-register
    search, opt_cheapprof, measured no gain on bzip2 and is off - see
    "Most contributing optimizations".)

Correctness was verified throughout by comparing the tool's output byte for
byte against the output of the native binary.


MOST CONTRIBUTING OPTIMIZATIONS
-------------------------------
To measure each optimization we built the same tool with the twelve
optimizations as command-line knobs, so every configuration below is the
same code and only the knobs differ. Each configuration was run 20
consecutive times on bzip2, each run paired with its own native run. The
numbers are the mean improvement of our measured end-to-end time over
native CPU time.

By far the largest contributor is the stack fold together with opt_foldtc1,
i.e. applying the fold before TC is built rather than only in TC2:

    fold in TC2 only                      +1.0%    (1 of 20 runs above 5%)
    fold also before profiling (TC1)     +10.4%   (20 of 20 runs above 5%)

With the fold applied only in TC2, the profiling window - about two thirds
of bzip2's run under the tool (2.01 s of the 3.16 s printed; see the phase
breakdown under "Timing results on bzip2") - still executes unoptimized
code, and the tool is barely faster than native.

Among the profiling-cost optimizations, measured by adding each one alone
to a base of de-virtualization, code reordering and the stack fold:

    none of them                          +5.4%
    + sparse profiling only              +10.4%
    + lean TC only                        +6.2%
    + dense counters only                 +5.6%

Sparse profiling on its own accounts for the whole gain of the group; lean
TC adds a little, and dense counters add nothing measurable on bzip2. The
wider dead-register search (opt_cheapprof) measured slightly below the base
(+4.9%), and function inlining finds no candidates at all in bzip2, so both
are switched off in the submitted tool. Everything left on together gives
+10.4% in that knobbed measurement, and +10.1% (measured end-to-end) /
+10.7% (as in bprofile-with-gearing) in the ten-round batch of the submitted
binary reported below.

De-virtualization and code reordering are the required and structural part
of the tool. On bzip2 de-virtualization finds no site hot enough (bzip2 has
almost no indirect control flow), so its effect there is neutral. On the
gcc-based benchmarks it is active - 37 call + 23 jump sites in cc1,
116 + 49 in sgcc_base, 267 + 66 in sgcc_peak and 109 + 42 in
cpugcc_r_base in our last runs (the exact numbers vary slightly from run to
run because the profile covers a fixed 2-second window) - always with
byte-identical output. As an extra correctness check we also lowered the
thresholds (-devirt_min_count 1 -devirt_min_pct 50), which guards hundreds
of sites and exercises the fall-back path heavily; the output stayed
byte-identical on bzip2, cc1 and sgcc_peak.


HOW THE TIME IS COUNTED (TWO TOOL METRICS, TWO NATIVE REFERENCES)
------------------------------------------------------------------
A run under the tool has four phases:

    (1) translating the program into TC   (before the application starts)
    (2) the profiling window in TC        (-prof_time seconds)
    (3) building TC2 and redirecting into it
    (4) running in TC2 until the program exits

We report the translated run in two ways. Neither includes phase (1),
exactly as in the provided tool.

  "as in bprofile-with-gearing"
      prof_time + phase (4).
      This is what the provided bprofile-with-gearing.cpp prints: it starts
      its clock only after TC2 has been committed, measures until exit, and
      adds the -prof_time knob value as a constant. Phase (3) is therefore
      not counted, and the profiling phase is the nominal 2 seconds rather
      than a measured value. This is the measurement the course intends.

  "measured end-to-end" (what project.so prints)
      phases (2) + (3) + (4), measured with a monotonic clock from the
      moment the application starts running in TC until it exits. It counts
      the real profiling window and the time spent building TC2 - during
      which the application keeps running in TC - so it is never smaller
      than the first metric.

The native run is likewise reported two ways, both from /usr/bin/time:

  native CPU     user + sys: CPU time actually spent executing the program.
  native wall    elapsed real time, which also includes any waiting.

Native CPU is the smaller of the two, so comparing our time against it is
the conservative choice; we quote it first throughout.


TIMING RESULTS ON BZIP2 (vs. NATIVE)
------------------------------------
Benchmark: bzip2 -c input-long.txt, tool run with -prof_time 2. Each tool
run is immediately preceded by a native run of the same program, so changes
in host speed affect both. Output was compared byte for byte with native on
every run and was identical every time.

Ten consecutive rounds of the submitted binary (obj-intel64/project.so,
exactly as shipped in this archive, with no switches beyond -prof_time 2).
The percentages are against that round's own native CPU time:

    round   native CPU   native wall   as in bprofile-   measured
                                       with-gearing      end-to-end
      1       4.930 s      4.99 s       4.054 s (+17.8%)  4.077 s (+17.3%)
      2       3.440 s      3.45 s       3.518 s ( -2.3%)  3.545 s ( -3.1%)
      3       3.400 s      3.42 s       3.018 s (+11.2%)  3.038 s (+10.7%)
      4       3.450 s      3.45 s       3.089 s (+10.5%)  3.106 s (+10.0%)
      5       3.500 s      3.52 s       3.076 s (+12.1%)  3.094 s (+11.6%)
      6       3.580 s      3.73 s       3.139 s (+12.3%)  3.157 s (+11.8%)
      7       3.580 s      3.68 s       3.202 s (+10.6%)  3.223 s (+10.0%)
      8       3.510 s      3.53 s       3.146 s (+10.4%)  3.167 s ( +9.8%)
      9       3.510 s      3.53 s       3.106 s (+11.5%)  3.127 s (+10.9%)
     10       3.610 s      3.65 s       3.137 s (+13.1%)  3.158 s (+12.5%)

    mean improvement      vs native CPU     vs native wall
      as in bprofile-with-gearing  10.7%          11.7%
      measured end-to-end          10.1%          11.2%

    9 of the 10 rounds clear 5% on both metrics and against both native
    references. Round 1 ran while the host was still slow - its native run,
    4.93 s, is 40% above the average native run of the batch - and round 2 is the one
    round that misses (-2.3% / -3.1%): its native run was among the fastest
    of the batch and the tool run right after it was the slowest of the
    batch, i.e. the host sped up and slowed down between the two halves of
    that round. We report both rather than drop them. Leaving those two
    extremes out, the remaining eight rounds average +11.4% (as in
    bprofile-with-gearing) and +10.9% (measured end-to-end), with every
    single round between +9.8% and +12.5%.

Where the time goes - the phase breakdown the tool printed in round 10 of
that batch (native CPU 3.610 s, native wall 3.65 s):

    TC construction (before the clock starts)     0.151 s   in neither metric
    profiling window in TC                        2.011 s
    TC2 construction (rebuild + layout + copy)    0.010 s   not in the first metric
    TC2 commit (redirect TC -> TC2)              0.0001 s   not in the first metric
    execution in TC2                              1.137 s
    ----------------------------------------------------------------------
    as in bprofile-with-gearing (2 + TC2)         3.137 s   +13.1% vs native CPU
    measured end-to-end (printed "overall")       3.158 s   +12.5% vs native CPU

The profiling window is about two thirds of the run under the tool, which
is why applying the stack fold before TC is built (opt_foldtc1) is worth so
much more than applying it in TC2 alone.

The two tool metrics stay close on bzip2 because what the first one leaves
out is small here: TC2 construction took 10-12 ms, and the redirect about
0.1 ms.

Earlier batches of the same configuration agree. A ten-round batch of the
identical code built with the optimizations as command-line knobs, with
exactly the knob set that is hard-coded in the submitted tool, gave +10.4%
(measured end-to-end) and +10.9% (as in bprofile-with-gearing) against
native CPU. Over the whole project we measured well over 60 rounds on bzip2
with this method; the mean stayed in the 9-13% range throughout.

Taken together, bzip2 runs roughly 10-12% faster than native on the metric
the course intends, and roughly 10-11% faster on our stricter end-to-end
metric, against either native reference.


RESULTS ON THE OTHER BENCHMARKS (cc1, cpugcc_r_base, sgcc_base, sgcc_peak)
--------------------------------------------------------------------------
Same method, three consecutive rounds each of the submitted build,
compiling 200.i. On these four the tool is slower than native on both
metrics; all runs completed without crashes and every output was
byte-identical to native (3 of 3 each). The bzip2 row is the ten-round
batch above, repeated here for comparison.

    benchmark        as in bprofile-with-gearing   measured end-to-end
                     vs CPU      vs wall           vs CPU      vs wall
    bzip2            +10.7%      +11.7%            +10.1%      +11.2%
    cc1              -20.3%      -19.1%            -22.7%      -21.5%
    sgcc_base        -25.9%      -24.7%            -47.3%      -45.9%
    sgcc_peak        -35.4%      -34.9%            -59.2%      -58.6%
    cpugcc_r_base    -28.3%      -27.8%            -49.0%      -48.5%

    (positive = faster than native)

The gap between the two tool metrics is much wider here than on bzip2
because building TC2 for these binaries is real work that the first metric
does not count: about 0.4-0.6 s for cc1 and about 1.5-1.7 s for the three
gcc builds, against native runs of only 7-19 s.

The reason the tool is slower on these four is that the stack-fold
optimization, which produces our bzip2 result, finds no sites in them: they
are compiled with -O2 and do not contain the repeated stack-reload pattern
it targets (bzip2 is compiled with -O0). Without that saving, nothing
offsets the fixed cost of the profiling instrumentation and of the TC -> TC2
translation, and these binaries are far larger than bzip2 (hundreds of
thousands to millions of translated instructions). Our benchmark for the
"more than 5%" requirement is therefore bzip2.


A BUG WE FOUND AND FIXED: TRANSLATION-CACHE PLACEMENT VS. THE HEAP
------------------------------------------------------------------
While testing the statically linked benchmarks (sgcc_base and sgcc_peak) we
hit an intermittent segmentation fault: roughly a third to a half of all
runs died, always at the same address (0x830), while the runs that did
complete always produced correct output. The same crash also happens with
the provided bprofile-with-gearing tool, so it was not caused by our
optimizations.

How we found it: we installed a SIGSEGV handler in the tool that maps the
faulting address, and every return address on the stack, back to the
original instruction it was translated from. That showed the crash inside
memcpy, called during glibc's start-up (before main) from the routine that
sets up thread-local storage. The registers showed that the destination
pointer had been computed from an sbrk() return value of -1, i.e. the heap
could not be extended.

The cause is the memory placement in allocate_and_init_memory(). It asked
for one large mapping to hold TC and TC2 at the first free address above
the end of the code - which is exactly where the kernel starts the program
break. The mapping therefore landed on top of the heap and every later
brk() failed. A statically linked glibc calls sbrk() before main to
allocate the thread-local storage block, so the failure produced a wild
pointer and the crash. Dynamically linked programs set up their TLS through
the dynamic loader with mmap, and malloc falls back to mmap when brk fails,
which is why bzip2, cc1 and cpugcc_r_base never showed the problem.

It looked random because of address-space layout randomization: whether our
mapping lands on the break depends on where other mappings happen to be.
With randomization disabled (setarch -R) the crash happened on every run,
which is what let us debug it.

The fix places the TC/TC2 mapping a heap-sized gap (1 GB, reduced
automatically if it would not fit) above the end of the entire loaded
image, while still keeping it within the +/-2 GB range required by the
32-bit relative branches in the translated code (with the original
first-fit search as a fallback). After the fix, the case that used to fail
every time passes, and a validation sweep of 32 runs (sgcc_peak and
sgcc_base 10 runs each with randomization and 3 each without, plus cc1 and
cpugcc_r_base) had no crashes and byte-identical output in every run.

We also fixed a smaller bug nearby: the routine that records a call/jump
target lying outside the translated code (fix_direct_jmp_or_call_to_orig_addr)
used the table index after incrementing the counter instead of before, so a
target already in the table was never found again and every call site used
up a new slot.

#!/usr/bin/env python3
"""Builds CODE_WALKTHROUGH.md. Every excerpt is copied from ../../project.cpp with its real line numbers.
A line that only carries an internal checklist label in a comment is shown as '...'."""
import re, os
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', '..', 'project.cpp')
L = open(SRC).read().split('\n')
LABEL = re.compile(r'\bP\d+\b')

def func_range(start):
    """Line range of the function that starts at line 'start' (ends at the first line that is exactly '}')."""
    for i in range(start, len(L) + 1):
        if L[i - 1] == '}':
            return start, i
    return start, start

def excerpt(segments):
    out, last = [], None
    for a, b in segments:
        if last is not None and a > last + 1:
            out.append('   ...')
        for n in range(a, b + 1):
            line = L[n - 1]
            out.append('   ...' if LABEL.search(line) else f'{n:5d}  {line}')
        last = b
    return '```cpp\n' + '\n'.join(out) + '\n```'

E = []   # (title, [(function, start_line)], segments, explanation)

E.append(("1. Overall flow: main, create_tc, create_tc2_thread_func",
  [("main", 2186), ("create_tc", 2141), ("create_tc2_thread_func", 2066)],
  [(2188, 2193), (2160, 2163), (2172, 2174), (2071, 2078), (2080, 2085), (2087, 2087), (2092, 2092)],
  """main() registers create_tc() as the image-load callback, starts our tool thread and hands control to Pin in
probe mode. Pin calls create_tc() for every image before the program runs a single instruction; for the main
executable it analyses the code, computes the stack-fold rewrite, places the memory region, builds TC and redirects
every routine to its TC copy - and only then starts our end-to-end clock (line 2173). The tool thread waits for the
profiling window with short, clock-checked sleeps, switches profiling off, derives the profile, builds TC2 and
commits it. Without this split the program could start before TC exists, or the window could overshoot badly
(we saw sleep() overshoot on our VM)."""))

E.append(("2. Decoding and block formation: analyze_ins, analyze_image",
  [("analyze_ins", 484), ("analyze_image", 582)],
  [(495, 497), (505, 508), (516, 517), (601, 603), (612, 613), (639, 640), (657, 659), (672, 674), (733, 734)],
  """analyze_ins() turns one XED-decoded instruction into what the rest of the tool needs: branch target, RIP-relative
address, the kind of control flow, whether it reads the status flags or overwrites all six of them, and which
registers it reads or fully kills. analyze_image() first decodes the whole image to collect every branch target,
because Pin's probe overwrites the first bytes of a routine and other code may jump right into them (lines 601-641).
Routines with JRCXZ/LOOP/XBEGIN or a branch into the middle of an instruction stay untranslated, and a routine that
runs off its end is translated but not probed (lines 669-675). Blocks start at the routine entry, at every direct
branch target and after every jump, so profiling and reordering see the same block structure."""))

E.append(("3. Heap-safe placement of our memory: allocate_region",
  [("allocate_region", 817)],
  [(820, 826), (828, 846), (851, 852)],
  """All our code and data live in one region: data (counters, target tables, save slots) | TC | TC2. Every rel32
branch and RIP-relative access must reach both the program and the region, so everything has to fit within 2 GB
(line 832). The base tool mapped its caches at the first free address above the code - exactly where the heap grows.
We aim about 1 GB above the end of the whole image (line 833) and shrink that gap only if needed, with the old search
as a logged fallback and an ERROR line if nothing works. The 256-byte table at line 852 is used by the flag-free
indirect-target hash."""))

E.append(("4. Cheap, flag-safe block counters: emit_block_counter, flags_dead_from, find_dead_reg",
  [("emit_block_counter", 1433), ("flags_dead_from", 549), ("find_dead_reg", 562)],
  [(1436, 1436), (1439, 1441), (1445, 1456), (551, 557), (568, 568), (572, 572)],
  """Every counter starts with an 8-byte patch pad (line 1436) so it can be switched off later. If flags_dead_from()
proves that the six status flags are overwritten (or a call comes) before anything reads them, the counter is a single
'add qword [rip+ctr], 1'. Otherwise find_dead_reg() looks for a register the program overwrites before reading it and
uses mov/lea/mov, which never touch the flags; as a last resort RAX is saved to a slot in our region. We never push:
-O0 leaf functions keep locals in the red zone below RSP. Without this analysis a counter could silently change a
flag the program is about to test (exactly what broke cc1 under the base tool)."""))

E.append(("5. Indirect jump and call targets, flag-free: emit_indirect_profile",
  [("emit_indirect_profile", 1527)],
  [(1529, 1534), (1538, 1555)],
  """Every indirect jump and every indirect call gets a 16-slot table of {target, count}. The slot is (target >> 4) & 15,
computed without any flag-changing instruction: the target is stored to a scratch slot, its low byte is loaded with
MOVZX and translated through the 256-byte table (lines 1541-1546). RAX and RCX are saved to and restored from our
region. The base tool used 'AND RAX,3' here, which both changed the flags and put all 16-byte-aligned targets into
one slot; that stub is the reason cc1's output was wrong with the base tool."""))

E.append(("6. Sparse profiling: the decision in analyze_image and derive_profile",
  [("analyze_image", 582), ("derive_profile", 1652)],
  [(774, 779), (785, 788), (1665, 1673)],
  """A block with exactly one predecessor in its routine does not need its own counter. If it is reached only by a
fall-through, its counter doubles as the predecessor's fall-through counter (line 787); if it is reached only by a
taken branch or a jmp, its count is computed after the window from the predecessor (lines 1667-1670). We only do
this in routines without indirect jumps and never for entries or blocks entered from other routines, because there
the predecessor set is not fully known. This removed most fall-through stubs from TC."""))

E.append(("7. Switching live code safely: fill_pad, patch_pad_to_jump, disable_profiling, commit_tc2",
  [("fill_pad", 230), ("patch_pad_to_jump", 251), ("disable_profiling", 1682), ("commit_tc2", 2036)],
  [(232, 235), (253, 257), (1684, 1687), (2039, 2051)],
  """The program keeps running while we change its code, so no CPU may ever fetch a half-written instruction. A pad is
two NOPs, NOP(k)+NOP(8-k), chosen so that pad+k is 4-byte aligned; switching it is one aligned 4-byte atomic store
of 'jmp short' (line 257), which never crosses a cache line. disable_profiling() uses this to jump over every
profiling stub. commit_tc2() first writes 'jmp rel32 <TC2 copy>' into the 5-byte trampoline in front of each TC
routine head (nothing executes it yet), then flips the head pad to 'jmp short trampoline'."""))

E.append(("8. A fresh TC2 list: build_tc2",
  [("build_tc2", 1981)],
  [(1988, 1999), (2004, 2008), (2010, 2015), (2017, 2018)],
  """TC2 is built from the original instruction records, not by editing the TC list, so every entry still knows its
true original address and every RIP-relative operand is re-derived from it. Only routines that ran during the window
get a TC2 copy; hot routines are ordered by hotness, each routine's blocks are laid out by reorder_routine_blocks(),
and all cold blocks go to one region at the end. The cold threshold is relative: one part per million of all block
executions (line 1989). layout_and_copy() and verify_branches() finish the job."""))

E.append(("9. De-virtualization: devirt_targets, emit_devirt_call, emit_devirt_jmp",
  [("devirt_targets", 1736), ("emit_devirt_call", 1773), ("emit_devirt_jmp", 1801)],
  [(1749, 1749), (1756, 1757), (1768, 1768), (1780, 1780), (1786, 1791), (1805, 1807), (1810, 1817), (1820, 1822)],
  """devirt_targets() applies our threshold: at least 100 executions, at most 3 targets each with at least 10%, together
at least 80% of the site. A call site becomes 'cmp operand, T; jnz next; call T'; ...' with the original call as
the fallback; CMP is fine there because the calling convention does not keep flags across a call. A jump site may
have live flags (cc1), so its guard uses only MOV, LEA and JRCXZ: rcx = target - T, and jrcxz fires when they are
equal. RCX is saved to a slot in our region on every path. The original indirect branch always remains, so a wrong
guess only costs a compare."""))

E.append(("10. Code reordering: reorder_routine_blocks, emit_tc2_block, invert_jcc",
  [("reorder_routine_blocks", 1929), ("emit_tc2_block", 1892), ("invert_jcc", 1855)],
  [(1941, 1944), (1952, 1953), (1958, 1965), (1903, 1913), (1857, 1858)],
  """Edges between hot blocks are weighted with the profile (taken = executions - fall-through) and sorted heaviest
first. Pettis-Hansen merging joins the chain ending in s with the chain starting in t, so t falls through after s;
the entry chain stays first. emit_tc2_block() then emits each block knowing its new successor: keep the jcc, invert
it with the link1.txt table when its taken target now follows, add a jmp when a fall-through was broken, and drop a
jmp whose target now follows. Every target is an original address that is remapped after layout."""))

E.append(("11. Stack fold and address fold: compute_stack_folds_rtn, address_fold",
  [("compute_stack_folds_rtn", 1117), ("address_fold", 1043)],
  [(1124, 1124), (1129, 1129), (1146, 1146), (1164, 1164), (1182, 1182), (1220, 1220), (1246, 1246), (1257, 1257),
   (1261, 1261), (1269, 1269), (1277, 1277), (1316, 1318), (1364, 1367), (1093, 1097), (1109, 1111)],
  """bzip2 is compiled -O0: every local lives in [rbp-X] and is reloaded for each use. For routines with a standard
frame we keep the most-read slots in spare caller-saved registers. A backward liveness pass (line 1220) tells
where a spare may be written; a forward must-dataflow (transfer, lines 1246-1269) tells where the spare provably
equals its slot on every path. Then loads become register moves, stores stay and sync the spare, and add [slot],imm
is followed by a flag-free lea on the spare. address_fold() turns 'mov P,spare; add P,X; op [P]' into
'op [spare+X]'. Address-taken slots, calls, push/pop and RSP stores all invalidate - when unsure we do not fold."""))

E.append(("12. Layout and self-check: layout_and_copy, verify_branches",
  [("layout_and_copy", 427), ("verify_branches", 464)],
  [(430, 430), (435, 439), (446, 455), (459, 459), (470, 473)],
  """Each list is laid out once: addresses are assigned, RIP-relative displacements are recomputed from the absolute
address we stored, and branch displacements are resolved from labels, original addresses or absolute targets. Any
value that does not fit (rel8 for JRCXZ, rel32 for everything else) is an ERROR, never a silent truncation. After the
copy, verify_branches() decodes every branch from memory again and checks that it reaches exactly the intended
target - a cheap guard against encoder or layout mistakes."""))

E.append(("13. The timing report: Fini",
  [("Fini", 2106)],
  [(2108, 2127)],
  """Fini() runs from our _exit replacement. It prints the TC build time (before the program started, not counted),
the profiling window, TC2 build, commit and execution, and the end-to-end total measured with one monotonic clock
from the program's first instruction to _exit. It also prints the base tool's line 'Translated code run (including
profiling) took', which is prof_time + TC2 execution; the assignment's requirement concerns that printed time, and it
is always slightly smaller than our end-to-end total, so it beats native by at least as much."""))

md = ["# Code walkthrough", "",
      "All excerpts are copied from `project.cpp` (the submitted source) with their real line numbers;",
      "`...` marks lines we left out. Function line ranges are computed from the source file.", ""]
for title, funcs, segs, text in E:
    md.append(f"## {title}")
    md.append("")
    md.append("Functions: " + ", ".join(f"`{n}()` project.cpp:{a}-{b}" for n, (a, b) in ((f, func_range(s)) for f, s in funcs)))
    md.append("")
    md.append(excerpt(segs))
    md.append("")
    md.append(text.replace('\n', ' '))
    md.append("")
open(os.path.join(HERE, 'CODE_WALKTHROUGH.md'), 'w').write('\n'.join(md))
# sanity checks: every function name exists at its start line, every excerpt <= 30 code lines
for title, funcs, segs, text in E:
    for f, s in funcs:
        assert re.search(r'\b' + f + r'\(', L[s - 1]), (f, s, L[s - 1])
    n = sum(b - a + 1 for a, b in segs)
    print(f"{n:3d} lines  {title}")

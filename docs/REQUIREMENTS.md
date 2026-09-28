# Requirements checklist (from project-2026.pdf)

Each item is ticked in Phase 8 (docs/AUDIT.md) with evidence.

## Tool
- [x] R1. Pintool in **probe mode** (PIN_StartProgramProbed), named **project.so**.
- [x] R2. Based on the exercise-4 pintool (hw4.cpp: counts executions of every basic block and every jump target)
      **and** on the provided bprofile-with-gearing.cpp.
- [x] R3. A spawned thread builds a second translation cache **TC2** from the gathered profile; execution continues there.
- [x] R4. Knob **-prof_time** (seconds of profiling in TC, default 2).
- [x] R5. TC profiling counts the execution frequency of every basic block and every jump target.

## Optimizations
- [x] R6. Profile **every indirect jump** target (collected in TC).
- [x] R7. Profile **every indirect call** target as well.
- [x] R8. **De-virtualization**: the most frequent indirect jump/call targets are replaced by direct jumps/calls (guarded).
- [x] R9. At least one more optimization from the list: **code reordering** (using link1's condition inversion and the
      recommended Newell & Pupyrev paper). (Extra: redundant stack-load elimination, an "other optimization taught in class".)

## Output / performance
- [x] R10. The tool prints the elapsed time (with profiling) of the translated code.
- [x] R11. That printed time improves by **more than 5%** on at least one of: bzip2, cc1, sgcc_base.mytest-m64,
       sgcc_peak.mytest-m64, cpugcc_r_base.mytest-m64 (we compare against native CPU time).
- [x] R12. (Allowed) print-outs, including profile dumps, may be disabled for performance.
- [x] R13. Program output must stay correct (byte-identical to native) — implied, and our hard rule.

## Debug facilities (programming tips)
- [x] R14. dump_instr_from_xedd(), dump_instr_from_mem(), dump_entire_instr_map() available.
- [x] R15. dump_tc() for TC and TC2 via **-dump_tc** and **-dump_tc2**.
- [x] R16. dump_orig_code via **-dump_orig_code**.
- [x] R17. dump_prof() (profile dump, knob -dump_prof).

## Submission
- [x] R18. One file **project.zip** containing **project.so** (compiled and tested).
- [x] R19. Directory **src/** with all sources + the make files + **README.txt**.
- [ ] R20. README: (a) names + ID numbers (both students).
- [x] R21. README: (b) compilation command.
- [x] R22. README: (c) how to run the tool.
- [x] R23. README: (d) threshold between frequently and rarely executed instructions.
- [ ] R24. Pairs only; deadline midnight Thursday October 1st, 2026.

## Defense (not part of the zip; prepared from docs/)
- [ ] R25. 8-minute video: the problem, a code walk-through, measurement results as graph(s).
- [ ] R26. Be able to answer questions (docs/FINAL_EXPLANATION.md, Q&A section).

## Our own hard rules (from the task brief)
- [x] H1. makefile / makefile.rules unchanged (md5sum).
- [x] H2. Builds with the exact course command, zero warnings (-Wall -Werror).
- [x] H3. Probe mode only, never JIT.
- [x] H4. Byte-identical output on all 5 benchmarks, every run.
- [x] H5. No benchmark-specific hacks (no program names, inputs or hard-coded addresses).
- [x] H6. git commit after every phase.

Status after Phase 8 (evidence in docs/AUDIT.md). R18/R19 are ticked in Phase 10; R20 (names/IDs) is a TODO for the students; R24–R26 are the students' part.

Re-verification (docs/VERIFICATION.md, commit 58df1e1): R11 = bzip2 median +15.23%, worst +5.02%, 10/10 rounds > 5% (internal clock); outside clock median +6.37%. R13/H4 = all 5 benchmarks byte-identical vs a fresh native run with separate output files.

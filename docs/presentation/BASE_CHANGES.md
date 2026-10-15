# What we changed in the provided tool

Line numbers: `bprofile-with-gearing.cpp` (the provided tool, unchanged in our repository) and `project.cpp`
(our tool). "Observed" means we saw the problem happen; otherwise the change comes from reading the code.

| feature | provided tool (bprofile-with-gearing.cpp) | our tool (project.cpp) | why it mattered |
|---|---|---|---|
| Indirect-jump profiling stub | `AND RAX, MAX_TARG_ADDRS` changes the flags, lines 850-853 | flag-free hash with MOVZX + 256-byte table, `emit_indirect_profile()` 1527-1560 (hash 1541-1546) | **observed**: base tool's cc1 output differs at byte 6669460 (docs/verify_raw/x_base_cc1.txt); ours identical |
| Target hash | `target & 3` (4 slots), lines 175, 850-853 | `(target >> 4) & 15` (16 slots), table filled in `allocate_region()` 851-852 | aligned targets all landed in one slot |
| Indirect calls profiled? | only jumps: `!INS_IsCall(ins)`, line 727 | jumps and calls, sites created in `analyze_image()` 790-799 | de-virtualization of calls needs call targets |
| Target operand of `jmp/call reg` | "no memory operand" test, lines 746-748 | first explicit operand, `get_target_op()` 1475-1505 (1483-1484) | `call reg` has a hidden memory operand |
| Block counter | always saves RAX through a static variable, counter at block end, lines 710, 946-972, 1470-1474 | `add [rip+ctr],1` where flags are dead, else a dead register, else RAX saved; placed at a flags-dead point; `emit_block_counter()` 1433-1463, `flags_dead_from()` 549, `find_dead_reg()` 562 | cheaper profiling: +7.61% vs native with it, see ablation (docs/VERIFICATION.md) |
| Fall-through / sparse counters | a fall-through counter after every jcc, lines 1503-1508 | counters derived from a single predecessor, `analyze_image()` 771-788, `derive_profile()` 1652-1680 | fewer stubs in TC |
| Switching profiling off | NOP4 + `memcpy` of a 5-byte jmp over live code, lines 714, 582 | 8-byte pad, one aligned 4-byte atomic store, `fill_pad()` 230, `patch_pad_to_jump()` 251-259, `disable_profiling()` 1682 | no torn instruction while the program runs |
| TC -> TC2 switch | two `memcpy`s into the live NOP7 head, lines 1659-1665 | trampoline written first, then one 4-byte store on the head pad, `commit_tc2()` 2036-2053 | same; verified live (dumps/live_pad_before_after.txt) |
| Building TC2 | edits the TC list in place: `orig_ins_addr = new_ins_addr`, lines 1700-1720 | fresh list from the original instructions, `build_tc2()` 1981-2032 | true original address in every entry; reordering and devirt need it |
| Region placement | first free address above the highest routine, lines 1840-1886 | ~1 GB above the image end, within 2 GB, fallback with a warning, `allocate_region()` 817-857 (gap 833) | the heap grows right after the image |
| Jump-to-original table | new index after incrementing the counter (lookups never hit), lines 1157-1171 | direct rel32 branch to the original address; map lookup in `jslot_for()` 860 for the rare fallback | table overflow |
| "No operand" encoding | target/RIP address 0 means "none", lines 997-998, 1039-1040, 1233-1234 | explicit `IF_HAS_TARG` / `IF_HAS_RIP` flags, lines 111-112, `analyze_ins()` 495-503 | address 0 is a legal value |
| Untranslatable code | a decode failure aborts all translation (`return -1`), lines 1480-1484 | skip only that routine: JRCXZ/LOOP/XBEGIN 655-659, branch into the middle of an instruction 703-708 | sgcc binaries could not be translated at all otherwise |
| Probe safety | none beyond Pin's own check | no probe for routines entered near their start or that fall off their end, `analyze_image()` 638-641, 669-675 | **observed**: sgcc_peak crashed (docs/PHASE_2.md) |
| Waiting for the window | `sleep(prof_time)`, line 1687 | clock-checked short sleeps, `create_tc2_thread_func()` 2071-2078 | sleep overshoot on our VM (docs/PHASE_2.md) |
| Error reporting | messages, then continues on the original code (e.g. line 1993) | `TOOL_ERROR` prints `ERROR:` and counts, line 93; final count in `Fini()` 2129 | a silent fallback looks like "no speed-up" |
| Timing output | `elapsed + prof_time` from the TC2 commit, lines 1943-1946 | same line kept, plus window / build / commit / execution / end-to-end, `Fini()` 2106-2130 | an honest end-to-end number |
| Self-check | none | `verify_branches()` 464-477 decodes every branch after layout | catches encoder/layout mistakes |
| De-virtualization | not present | `devirt_targets()` 1736, `emit_devirt_call()` 1773, `emit_devirt_jmp()` 1801 | required by the assignment; cc1 -13.57% vs -22.81% without (docs/VERIFICATION.md) |
| Code reordering | not present | `reorder_routine_blocks()` 1929, `emit_tc2_block()` 1892, `invert_jcc()` 1855 | the additional optimization from the list |
| Stack fold + address fold | not present | `compute_stack_folds_rtn()` 1117, `address_fold()` 1043, applied by `emit_ins()` 1411 in TC and TC2 | bzip2 -9.03% -> +15.86% (ablation, docs/VERIFICATION.md) |

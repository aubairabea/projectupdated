# Phase 8 — Audit and test matrix

Raw log: bench_logs/phase8.txt (script phase8.sh). Build: exact course command, zero warnings.
All runs sequential, taskset -c 0, compared with cmp against a native run, exit codes compared, runs with an
`ERROR` line rejected.

## Correctness
| test | runs | result |
|---|---|---|
| bzip2 -prof_time 2 | 3 | identical |
| bzip2 -prof_time 1 | 1 | identical |
| bzip2 -prof_time 4 | 1 | identical |
| cc1 | 3 | identical (base tool: WRONG) |
| sgcc_base | 2 | identical |
| sgcc_peak | 2 | identical |
| cpugcc_r_base | 2 | identical |
| sgcc_peak under setarch -R | 2 | identical |
| stress -devirt_min_count 1 -devirt_min_pct 50: bzip2, cc1, sgcc_peak | 1 each | identical |
| bzip2 -1, -9, -d, -t, stdin, -dc stdin | 1 each | identical, same exit codes |
| empty file, 1 KB file (exit before prof_time), 3 MB random | 1 each | identical |
| round trip: tool compress → native decompress; native compress → tool decompress | 1 each | PASS |
| -dump_tc, -dump_tc2, -dump_orig_code, -dump_prof, -verbose (1 KB input) | 1 each | identical, no crash |
| -dump_tc2 -dump_prof on the long input | 1 | identical, 12312 dump lines, 749 CSV rows |

## Performance — bzip2, 10 paired rounds (all optimizations, -prof_time 2)
| round | native CPU (s) | tool end-to-end (s) | improvement |
|---|---|---|---|
| 1 | 4.920 | 4.669 | +5.10% |
| 2 | 5.160 | 4.222 | +18.18% |
| 3 | 4.980 | 4.268 | +14.30% |
| 4 | 5.010 | 4.375 | +12.67% |
| 5 | 4.790 | 4.198 | +12.36% |
| 6 | 4.850 | 4.055 | +16.39% |
| 7 | 4.800 | 4.300 | +10.42% |
| 8 | 5.960 | 4.592 | +22.95% |
| 9 | 5.210 | 4.274 | +17.97% |
| 10 | 4.960 | 4.489 | +9.50% |
| **median** | 4.970 | 4.287 | **+13.5%** |
| **worst** | | | **+5.1%** |

**The >5% requirement is met** (median +13.5%; every one of the 10 rounds is above +5%).

## Ablation — bzip2, 4 paired rounds each (median / worst improvement vs native)
| config | median | worst |
|---|---|---|
| default (all on) | +16.3% | +14.3% |
| -no_stackfold | −9.4% | −10.8% |
| -no_addrfold | +0.7% | −2.7% |
| cheaper profiling off (-no_cheap_prof -no_sparse_prof) | +6.9% | −1.2% |
| -no_devirt | +26.5% | −8.3% (native very noisy in this set; bzip2 has no hot indirect site, 0 guards) |
| -no_reorder | +16.0% | −60.8% (one outlier round) |
| all off | −7.0% | −15.0% |

Reading: the stack fold + address fold give ~+23 points, cheap profiling ~+9 points; devirt and reorder are
neutral on bzip2 (no hot indirect sites; small code).

## cc1, 3 paired rounds (median / worst)
| config | median | worst |
|---|---|---|
| default | −24.7% | −26.1% |
| -no_devirt | −17.0% | −22.0% |
| -no_reorder | −16.1% | −22.1% |
| -no_devirt -no_reorder | −3.6% | −35.1% |
Native cc1 varied 27–34 s between these sets, so the differences are only indicative; on cc1 the required
optimizations do not pay back their cost.

## Other gcc binaries (1 paired round)
sgcc_base −19.9%, sgcc_peak −41.0%, cpugcc_r_base −23.2%.

## Requirements checklist (docs/REQUIREMENTS.md)
| item | status | evidence |
|---|---|---|
| R1 probe mode, project.so | PASS | PIN_StartProgramProbed; build target obj-intel64/project.so |
| R2 based on exercise 4 + base tool | PASS | project.cpp header mapping, README "BASED ON" |
| R3 thread builds TC2, execution continues there | PASS | create_tc2_thread_func, commit_tc2, "[time] TC2 execution" |
| R4 -prof_time knob, default 2 | PASS | KnobNumSecsDuringProfile; runs with 1, 2, 4 |
| R5 counts every block and jump target | PASS | emit_block_counter, emit_indirect_profile, -dump_prof CSV |
| R6 profile indirect jumps | PASS | emit_indirect_profile |
| R7 profile indirect calls | PASS | same, IF_IND_CALL sites |
| R8 de-virtualization | PASS | emit_devirt_call/jmp; cc1: 76 calls + 160 jumps guarded |
| R9 another optimization | PASS | code reordering + stack fold + address fold |
| R10 prints elapsed time with profiling | PASS | "[time] honest end-to-end total" + compat line |
| R11 >5% on one program | PASS | bzip2 median +13.5%, worst +5.1% (10 rounds) |
| R12 print-outs may be disabled | PASS | default output is only the timing lines |
| R13 output correct | PASS | correctness table above |
| R14 dump_instr_from_xedd / _from_mem / dump_entire_instr_map | PASS | present in project.cpp |
| R15 -dump_tc / -dump_tc2 | PASS | knob tests |
| R16 -dump_orig_code | PASS | knob test |
| R17 dump_prof | PASS | -dump_prof → edge-profile.csv |
| R18 project.zip with project.so | PASS | Phase 10 |
| R19 src/ with sources, makefiles, README.txt | PASS | Phase 10 |
| R20 names + IDs | TODO (placeholders for the students) | README (a) |
| R21 compilation command | PASS | README (b) |
| R22 how to run | PASS | README (c) |
| R23 threshold | PASS | README (d) |
| R24 pairs, deadline | n/a (students) | |
| R25/R26 video and defense | material in docs/FINAL_EXPLANATION.md, SUMMARY.md | |
| H1 makefiles unchanged | PASS | md5sum (Phase 10) |
| H2 zero warnings | PASS | build log |
| H3 probe only | PASS | |
| H4 byte-identical on all 5 | PASS | table above |
| H5 no benchmark-specific hacks | PASS | no names/addresses of benchmarks in project.cpp |
| H6 git commit per phase | PASS | git log |

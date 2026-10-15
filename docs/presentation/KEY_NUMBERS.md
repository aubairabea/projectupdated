# Key numbers (every number we say out loud, with its source)

All sources are in our repository: `docs/VERIFICATION.md` and the raw run logs in `docs/verify_raw/`
(measured at commit 58df1e1, tool source unchanged since). "VER" = docs/VERIFICATION.md, "raw/" = docs/verify_raw/.

## The headline (bzip2, 10 paired rounds: native run, then tool run)
| what | value | source |
|---|---|---|
| median improvement vs native CPU (user+sys), tool end-to-end clock | **15.23%** | VER (STEP 4 table), raw/step4_summary.tsv |
| mean / minimum / maximum improvement | 14.22% / 5.02% / 19.88% | VER, raw/step4_summary.tsv |
| rounds above 5% | **10 of 10** | VER, raw/step4_summary.tsv |
| outputs byte-identical | 10 of 10 | VER, raw/step4_summary.tsv |
| median native CPU time | 4.75 s | raw/step4_summary.tsv |
| median tool end-to-end time | 4.05 s (4.0535) | raw/step4_summary.tsv |
| profiling window (all rounds) | 2.001-2.009 s | VER, raw/step4_summary.tsv |
| median TC2 build / commit | 0.005 s / 0.000 s | raw/step4_summary.tsv |
| median TC2 execution | 2.045 s | raw/step4_summary.tsv |
| bench.sh, 5 more rounds: median | 13.81% (11.46%-16.45%) | VER (STEP 6) |

## One run, second by second (round 1)
| TC build (before the program starts) | profiling window | TC2 build | TC2 commit | TC2 execution | end-to-end | printed "Translated code run" | source |
|---|---|---|---|---|---|---|---|
| 0.261 s | 2.001 s | 0.008 s | 0.000 s | 2.046 s | 4.055 s | 4.046 s | raw/s4_r1.txt |
Round 1 arithmetic: native CPU 4.84 + 0.03 = 4.87 s; (4.87 - 4.055) / 4.87 = 16.74% (VER).

## The same bzip2 runs measured three ways (5 paired rounds)
| clock | median improvement | rounds > 5% | source |
|---|---|---|---|
| tool's own end-to-end clock | 14.93% | 5/5 | VER (STEP 5), raw/step5_summary.tsv |
| whole pin command, user+sys CPU | 9.55% | 5/5 | same |
| whole pin command, wall clock | **6.37%** | **3/5** (3.21% and 4.47% in the other two) | same |
| what the outside clock adds (Pin start-up, building TC, exit) | 0.37-0.40 s per run | | same |

## Ablation on bzip2 (4 paired rounds each, median)
| config | median improvement | source |
|---|---|---|
| all optimizations on | +15.86% | VER (STEP 8), raw/step8_summary.tsv |
| without stack fold | -9.03% | same |
| without address fold | +2.73% | same |
| without cheap/sparse profiling | +7.61% | same |
| without de-virtualization (no hot indirect site in bzip2: identical TC2) | +14.33% | same; raw/s8_evidence_*.txt |
| without reordering | +13.22% | same |
| everything off | -14.49% | same |

## What the optimizations did on bzip2 (one -verbose run)
| item | value | source |
|---|---|---|
| translated | 118/118 routines, 23786 instructions, 2895 blocks, 28 indirect sites, TC 165634 bytes | raw/s8_evidence_default.txt |
| TC2 | 46 routines, 48003 bytes | same |
| stack fold | 77 routines, 1914 loads replaced, 775 register syncs, 130 address folds | same |
| mainGtU | 207 loads, 7 slots kept in registers | same |
| reordering | 118 jumps inverted, 97 jumps added, 44 jumps removed, 758 cold blocks | same |
| our memory region | at 0x40600000, size 0x1159000; image 0x400000-0x41b944 | same |
| hot/cold threshold | 1.613e9 block executions in the window -> threshold 1613 | raw/x_dump_prof_bzip2.txt |

## cc1 and the gcc family
| item | value | source |
|---|---|---|
| cc1 de-virtualized sites (default) | 81 indirect calls + 171 indirect jumps | raw/s8_evidence_cc1_devirt.txt |
| cc1 TC2 | 2164 routines | same |
| stress test (-devirt_min_count 1 -devirt_min_pct 50) | cc1 144 calls + 228 jumps; sgcc_peak 552 calls + 490 jumps; output identical | raw/s3_stress_cc1.txt, raw/s3_stress_sgcc_peak.txt |
| cc1, mean of 2 rounds: default / no devirt / no reorder / both off | -13.57% / -22.81% / -20.23% / -23.82% | VER, raw/cc1_ablation_summary.tsv |
| cc1 per round (correctness pairs) | -45.89%, -15.19% | VER, raw/step3_summary.tsv |
| sgcc_base | -38.11%, -30.40% | same |
| sgcc_peak | -64.48%, -45.72% (setarch -R: -49.89%, -36.15%) | same |
| cpugcc_r_base | -23.67%, -19.38% | same |

## The base tool (bprofile-with-gearing.cpp built unchanged)
| item | value | source |
|---|---|---|
| bzip2 | correct, printed time 9.22 s vs native 5.86 s (-57.30%) | VER, raw/x_base_bzip2.txt |
| cc1 | **wrong output**, first difference at byte 6669460 | VER, raw/x_base_cc1.txt |

## Correctness
| item | value | source |
|---|---|---|
| benchmarks byte-identical (fresh native run, separate output file) | 5 of 5, 2 rounds each | VER (STEP 3), raw/step3_summary.tsv |
| sgcc_peak under setarch -R | identical, 2 rounds | same |
| bzip2 extra modes (-1, -9, -d, -t, stdin, -dc, empty, 1 KB, 3 MB random) | 9 of 9 identical | raw/modes_*.txt |
| round trips (both directions) | identical | raw/step3_roundtrip.txt |
| a flipped byte is detected by cmp | 5 of 5 benchmarks | raw/step3_O5_*.txt |

## Thresholds (defaults in project.cpp)
| rule | value | source |
|---|---|---|
| cold block | count < 1 ppm of all block executions (and every count 0) | project.cpp:1989 (`-cold_ppm`, default 1) |
| devirt site | >= 100 executions; <= 3 targets, each >= 10%; together >= 80% | project.cpp:1749-1768 (knob defaults at the top of project.cpp) |

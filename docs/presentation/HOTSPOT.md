# Hotspot: where bzip2 executes its instructions

One bzip2 run under our tool with `-prof_time 2 -verbose -dump_prof` (`data/bzip2_profile.txt`, output identical
to native). Our tool's `-dump_prof` writes the execution count of every basic block during the profiling window
(`data/bzip2_profile.edge-profile.csv`; the tool has no separate per-routine instruction report).
`data/hotspot.py` turns this into executed instructions: each block's instructions are read from `objdump -d bzip2`
(from the block start to its first jump/return, or to the next profiled block start), multiplied by the block's
count, and summed per routine (`nm bzip2`). This covers the 2-second profiling window only.

Total: 2.126e+10 instructions executed in the window.

| rank | routine | executed instructions | share |
|---|---|---|---|
| 1 | mainGtU | 16,413,819,317 | 77.21% |
| 2 | mainQSort3 | 1,831,026,515 | 8.61% |
| 3 | mainSimpleSort | 947,982,845 | 4.46% |
| 4 | mainSort | 869,473,591 | 4.09% |
| 5 | copy_input_until_stop | 648,191,778 | 3.05% |
| 6 | generateMTFValues | 207,212,861 | 0.97% |
| 7 | sendMTFValues | 143,301,658 | 0.67% |
| 8 | add_pair_to_block | 110,147,500 | 0.52% |
| 9 | BZ2_blockSort | 43,972,217 | 0.21% |
| 10 | bsW | 21,580,431 | 0.10% |
| 11 | BZ2_hbMakeCodeLengths | 9,632,773 | 0.05% |
| 12 | copy_output_until_stop | 7,687,470 | 0.04% |
| 13 | mmed3 | 3,865,208 | 0.02% |
| 14 | BZ2_hbAssignCodes | 319,972 | 0.00% |
| 15 | BZ2_bzWrite | 128,819 | 0.00% |
| | all other 31 routines | 256,900 | 0.00% |

Graph: `graphs/hotspot_bzip2.png` (top 8 routines + the rest).


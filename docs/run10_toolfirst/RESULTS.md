# bzip2, 10 paired rounds, TOOL FIRST then NATIVE

Commands (from ~/Desktop/project5, no taskset, sequential; output moved to r<N>_*.bz2 after each run):

    tool:   time $HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -k -f input-long.txt
    native: time ./bzip2 -k -f input-long.txt

project.so md5 ca18d4244b72e1627224fd66868c7ac7 (same build as docs/VERIFICATION.md); every tool run: 0 ERROR lines, '[time] TC2 commit' present (console.txt).

A = (native real - tool printed line) / native real; B = (native user+sys - tool end-to-end) / (native user+sys); C = (native real - tool real) / native real.

| round | native real | native user | native sys | tool real (whole pin) | tool printed line | tool end-to-end | cmp | A | B | C |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 4.166 | 4.152 | 0.009 | 3.908 | 3.439 | 3.445 | IDENTICAL | 17.45% | 17.21% | 6.19% |
| 2 | 4.302 | 4.282 | 0.014 | 4.751 | 3.154 | 3.325 | IDENTICAL | 26.69% | 22.60% | -10.44% |
| 3 | 4.405 | 4.373 | 0.025 | 3.763 | 3.482 | 3.489 | IDENTICAL | 20.95% | 20.67% | 14.57% |
| 4 | 4.155 | 4.125 | 0.026 | 4.349 | 3.414 | 3.484 | IDENTICAL | 17.83% | 16.07% | -4.67% |
| 5 | 4.178 | 4.150 | 0.020 | 3.687 | 3.418 | 3.426 | IDENTICAL | 18.19% | 17.84% | 11.75% |
| 6 | 4.154 | 4.096 | 0.047 | 3.663 | 3.379 | 3.383 | IDENTICAL | 18.66% | 18.34% | 11.82% |
| 7 | 4.213 | 4.129 | 0.055 | 3.659 | 3.412 | 3.417 | IDENTICAL | 19.01% | 18.33% | 13.15% |
| 8 | 4.197 | 4.122 | 0.060 | 3.685 | 3.426 | 3.432 | IDENTICAL | 18.37% | 17.93% | 12.20% |
| 9 | 4.189 | 4.020 | 0.164 | 3.759 | 3.496 | 3.501 | IDENTICAL | 16.54% | 16.32% | 10.26% |
| 10 | 4.235 | 4.194 | 0.027 | 3.800 | 3.531 | 3.539 | IDENTICAL | 16.62% | 16.16% | 10.27% |

Arithmetic, round 1:
- A = (4.166 - 3.439) / 4.166 * 100 = 0.727 / 4.166 * 100 = 17.45%
- B = ((4.152 + 0.009) - 3.445) / 4.161 * 100 = 0.716 / 4.161 * 100 = 17.21%
- C = (4.166 - 3.908) / 4.166 * 100 = 0.258 / 4.166 * 100 = 6.19%

| metric | median | mean | min | max | rounds > 5% |
|---|---|---|---|---|---|
| A (printed line vs native real) | 18.28% | 19.03% | 16.54% | 26.69% | 10/10 |
| B (end-to-end vs native user+sys) | 17.89% | 18.15% | 16.07% | 22.60% | 10/10 |
| C (whole pin run vs native real) | 11.01% | 7.51% | -10.44% | 14.57% | 8/10 |

Output: 10/10 IDENTICAL (and md5 8722c10f2ac1dfde915e79b1943a27d5 for every file, console.txt).

## Notes on rounds 2 and 4
- **Round 2:** the whole pin command spent 3.753 s in the kernel (sys), against 0.35–0.47 s in normal rounds
  (r1, r3). Its real time exceeds the end-to-end time by 1.426 s (normally ~0.25–0.46 s). That time is outside
  the program's run (Pin start-up / teardown), so it hurts C but not B. The profiling window also overshot to
  2.158 s. The printed line assumes exactly 2.000 s, so in this round it is 0.171 s below the real end-to-end
  time, and A (26.69%) is inflated.
- **Round 4:** the same effect, smaller: sys 1.257 s, window 2.065 s.
- **Cause:** not determined beyond "kernel time during Pin start-up/teardown". The tool and its output were
  unchanged; both rounds were IDENTICAL with 0 ERROR lines.
- **Effect on C:** these are the only two rounds where C is below 5% (−10.44%, −4.67%).

## Tool first vs native first (compared with docs/VERIFICATION.md)
Running the tool first did not make it worse; every metric is at least as good as before:
- B here: median 17.89%, min 16.07%. VERIFICATION STEP 4 (native first): median 15.23%, min 5.02%.
- C here: median 11.01%, 8/10 above 5%. The closest earlier measurement is VERIFICATION STEP 5 (whole run vs
  native CPU): median 6.37%, 3/5 above 5%.

The setups also differ in other ways, so the order alone cannot be credited with the gain:
- no taskset here;
- the output goes to a file (`-k -f`) instead of stdout;
- native real was used here for A and C;
- the machine ran faster today (native ≈ 4.2 s vs ≈ 4.7 s).

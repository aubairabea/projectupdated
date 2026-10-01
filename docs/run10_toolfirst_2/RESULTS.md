# bzip2, 10 paired rounds, TOOL FIRST then NATIVE

Commands (from ~/Desktop/project5, no taskset, sequential; output moved to r<N>_*.bz2 after each run):

    tool:   time $HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -k -f input-long.txt
    native: time ./bzip2 -k -f input-long.txt

project.so md5 ca18d4244b72e1627224fd66868c7ac7 (same build as docs/VERIFICATION.md); every tool run: 0 ERROR lines, '[time] TC2 commit' present (console.txt).

A = (native real - tool printed line) / native real; B = (native user+sys - tool end-to-end) / (native user+sys); C = (native real - tool real) / native real.

| round | native real | native user | native sys | tool real (whole pin) | tool printed line | tool end-to-end | cmp | A | B | C |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3.977 | 2.602 | 1.373 | 6.054 | 3.115 | 3.165 | IDENTICAL | 21.67% | 20.38% | -52.23% |
| 2 | 4.115 | 3.998 | 0.111 | 3.663 | 3.392 | 3.398 | IDENTICAL | 17.57% | 17.30% | 10.98% |
| 3 | 4.085 | 2.630 | 1.454 | 3.734 | 3.445 | 3.450 | IDENTICAL | 15.67% | 15.52% | 8.59% |
| 4 | 4.135 | 3.655 | 0.475 | 3.717 | 3.448 | 3.453 | IDENTICAL | 16.61% | 16.39% | 10.11% |
| 5 | 4.724 | 4.437 | 0.187 | 3.783 | 3.516 | 3.522 | IDENTICAL | 25.57% | 23.83% | 19.92% |
| 6 | 4.418 | 4.388 | 0.021 | 4.057 | 3.753 | 3.760 | IDENTICAL | 15.05% | 14.72% | 8.17% |
| 7 | 4.544 | 4.435 | 0.037 | 3.870 | 3.598 | 3.605 | IDENTICAL | 20.82% | 19.39% | 14.83% |
| 8 | 4.266 | 4.152 | 0.097 | 3.883 | 3.611 | 3.617 | IDENTICAL | 15.35% | 14.87% | 8.98% |
| 9 | 4.213 | 4.180 | 0.011 | 3.752 | 3.501 | 3.506 | IDENTICAL | 16.90% | 16.34% | 10.94% |
| 10 | 4.259 | 4.206 | 0.044 | 4.596 | 3.389 | 3.393 | IDENTICAL | 20.43% | 20.16% | -7.91% |

Arithmetic, round 1:
- A = (3.977 - 3.115) / 3.977 * 100 = 0.862 / 3.977 * 100 = 21.67%
- B = ((2.602 + 1.373) - 3.165) / 3.975 * 100 = 0.810 / 3.975 * 100 = 20.38%
- C = (3.977 - 6.054) / 3.977 * 100 = -2.077 / 3.977 * 100 = -52.23%

| metric | median | mean | min | max | rounds > 5% |
|---|---|---|---|---|---|
| A (printed line vs native real) | 17.23% | 18.56% | 15.05% | 25.57% | 10/10 |
| B (end-to-end vs native user+sys) | 16.85% | 17.89% | 14.72% | 23.83% | 10/10 |
| C (whole pin run vs native real) | 9.54% | 3.24% | -52.23% | 19.92% | 8/10 |

Output: 10/10 IDENTICAL (and md5 8722c10f2ac1dfde915e79b1943a27d5 for every file, console.txt).

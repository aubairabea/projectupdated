# Phase 0 — Understand and plan

## What was read
- project-2026.pdf → docs/REQUIREMENTS.md (checklist).
- bprofile-with-gearing.cpp, hw4.cpp, link1.txt, the Newell & Pupyrev paper → docs/REFERENCES.md.

## Benchmarks
All five are non-PIE ELF x86-64 executables (code below 2 GB, so every code address fits a sign-extended imm32):
bzip2, cc1, cpugcc_r_base are dynamically linked; sgcc_base/sgcc_peak are statically linked.
The benchmark files were not executable in the directory (mode 644) → `chmod +x` is needed (README notes it).

## bzip2 is compiled -O0 (evidence from objdump -d)
| routine | instructions | loads from [rbp-X] | stores to [rbp-X] |
|---|---|---|---|
| mainGtU | 564 | 175 | 107 |
| mainSort | 938 | 234 | 101 |
| fallbackSort | 550 | 115 | 65 |
| BZ2_compressBlock | 192 | 51 | 3 |
| generateMTFValues | 242 | 57 | 37 |
| sendMTFValues | 5398 | 1138 | 192 |
| BZ2_blockSort | 155 | 41 | 22 |
| copy_input_until_stop | 264 | 73 | 8 |

Typical code (mainGtU): arguments are spilled to `[rbp-0x14]`… then reloaded right away
(`mov %edi,-0x14(%rbp)` … `mov -0x14(%rbp),%edx`), `addl $1,-0x14(%rbp)` followed by a reload of the same slot, etc.
Registers r10 and r11 never appear in the whole bzip2 binary; r8/r9 only for arguments; r12–r15 in a few routines.
So r10/r11 are free spare registers for the stack-load elimination of Phase 7.

## Plan
Phases 1–10 as in the brief. Key design decisions taken now:
- One mmap region (TC | TC2 | data) placed ~1 GB above the image end (P2), all tool data used by generated code
  inside it (P13).
- All branches encoded rel32; RIP-relative operands always re-derived from their absolute address (P6).
- Branches to code that is not translated go directly (rel32) to the original address — the region is within ±2 GB.
- Patch pads (8-byte NOP, one aligned 4-byte store) for disabling profiling and for the TC→TC2 switch (P4).
- The TC2 list is built fresh from the original-instruction records (not from TC).

# Fold decisions: which stack slots got a register

Sources:
- The slot/size/spare-register decisions are the tool's own `-verbose` lines
  (`fold <routine>: N loads; [rbp-X]/size->rK ...`) from `dumps/bzip2_tc_only.txt.gz` (run with `-verbose -dump_tc`).
- Loads replaced per slot = reads of the slot in the original code (objdump of `bzip2`) minus reads of the slot
  still present in that routine's TC copy (same dump). Computed by `data/fold_slots.py`, result in
  `data/fold_slots_bzip2.json`. **Check:** the tool prints a `fold` line for the 23 routines with at least 20 replaced loads (77 routines
  were folded in total); for all 23 the per-slot sum equals the tool's own number N (e.g. mainGtU 207 = 207).
- Share of executed instructions: `data/hotspot_bzip2.json` (see HOTSPOT below).
- bzip2's C source is not in our repository, so variables are described only by what the machine code shows
  (which argument register is stored into the slot, how the value is used); otherwise the role is left blank.
- A slot can be chosen and still replace 0 loads: the dataflow analysis found no read where the register is
  provably up to date on every path (for example reads after a call, which invalidates all spares).

## mainGtU (77.21% of executed instructions; tool: 207 loads replaced)

| slot | size (bytes) | spare register | reads in original | reads left in TC | loads replaced | role (from the code) |
|---|---|---|---|---|---|---|
| [rbp-0x28] | 8 | rsi | 16 | 0 | 16 | 4th argument (stored from rcx); pointer to an array of 16-bit values |
| [rbp-0x20] | 8 | r11 | 40 | 0 | 40 | 3rd argument (stored from rdx); pointer to a byte array |
| [rbp-0x18] | 4 | r8 | 29 | 2 | 27 | 2nd argument (stored from esi); used as an index into the same byte array |
| [rbp-0x14] | 4 | rdi | 29 | 1 | 28 | 1st argument (stored from edi); used as an index into the 3rd argument's byte array |
| [rbp-0xa] | 2 | rcx | 16 | 0 | 16 | 16-bit value loaded from the 4th argument's array |
| [rbp-0x6] | 1 | r10 | 40 | 0 | 40 | byte loaded from the byte array at the 2nd-argument index |
| [rbp-0x5] | 1 | r9 | 40 | 0 | 40 | byte loaded from the byte array at the 1st-argument index |
| **total** | | | 210 | 3 | **207** | |

## mainQSort3 (8.61% of executed instructions; tool: 31 loads replaced)

| slot | size (bytes) | spare register | reads in original | reads left in TC | loads replaced | role (from the code) |
|---|---|---|---|---|---|---|
| [rbp-0x560] | 8 | r8 | 6 | 6 | 0 | 2nd argument (stored from rsi) |
| [rbp-0x558] | 8 | r11 | 26 | 7 | 19 | 1st argument (stored from rdi) |
| [rbp-0x54c] | 4 | rcx | 4 | 4 | 0 |  |
| [rbp-0x540] | 4 | rdi | 5 | 0 | 5 |  |
| [rbp-0x53c] | 4 | r10 | 7 | 0 | 7 |  |
| [rbp-0x530] | 4 | rsi | 5 | 5 | 0 |  |
| [rbp-0x52c] | 4 | r9 | 7 | 7 | 0 |  |
| **total** | | | 60 | 29 | **31** | |

## mainSimpleSort (4.46% of executed instructions; tool: 27 loads replaced)

| slot | size (bytes) | spare register | reads in original | reads left in TC | loads replaced | role (from the code) |
|---|---|---|---|---|---|---|
| [rbp-0x44] | 4 | rcx | 4 | 3 | 1 | 6th argument (stored from r9d) |
| [rbp-0x40] | 4 | rsi | 5 | 4 | 1 | 5th argument (stored from r8d) |
| [rbp-0x28] | 8 | r11 | 15 | 6 | 9 | 1st argument (stored from rdi) |
| [rbp-0x18] | 4 | rdi | 6 | 6 | 0 |  |
| [rbp-0x14] | 4 | r9 | 13 | 6 | 7 |  |
| [rbp-0x8] | 4 | r10 | 15 | 12 | 3 |  |
| [rbp-0x4] | 4 | r8 | 9 | 3 | 6 |  |
| **total** | | | 67 | 40 | **27** | |

## mainSort (4.09% of executed instructions; tool: 39 loads replaced)

| slot | size (bytes) | spare register | reads in original | reads left in TC | loads replaced | role (from the code) |
|---|---|---|---|---|---|---|
| [rbp-0xd78] | 4 | rsi | 4 | 4 | 0 | 6th argument (stored from r9d) |
| [rbp-0xd74] | 4 | r9 | 11 | 11 | 0 | 5th argument (stored from r8d) |
| [rbp-0xd70] | 8 | r11 | 36 | 13 | 23 | 4th argument (stored from rcx) |
| [rbp-0xd68] | 8 | rdi | 9 | 5 | 4 | 3rd argument (stored from rdx) |
| [rbp-0xd60] | 8 | r10 | 17 | 5 | 12 | 2nd argument (stored from rsi) |
| [rbp-0xd58] | 8 | r8 | 11 | 11 | 0 | 1st argument (stored from rdi) |
| **total** | | | 88 | 49 | **39** | |

## All routines with a `fold` line (tool total vs per-slot sum)

| routine | tool: loads replaced | per-slot sum | slots |
|---|---|---|---|
| fallbackSimpleSort | 42 | 42 | 7 |
| fallbackQSort3 | 24 | 24 | 2 |
| mainGtU | 207 | 207 | 7 |
| mainSimpleSort | 27 | 27 | 7 |
| mainQSort3 | 31 | 31 | 7 |
| mainSort | 39 | 39 | 6 |
| BZ2_bzCompressInit | 47 | 47 | 5 |
| add_pair_to_block | 66 | 66 | 2 |
| copy_input_until_stop | 67 | 67 | 5 |
| BZ2_bzCompress | 26 | 26 | 4 |
| BZ2_bzDecompressInit | 23 | 23 | 4 |
| unRLE_obuf_to_output_FAST | 196 | 196 | 7 |
| unRLE_obuf_to_output_SMALL | 237 | 237 | 2 |
| BZ2_bzDecompress | 29 | 29 | 3 |
| BZ2_bzWriteOpen | 25 | 25 | 7 |
| BZ2_bzWrite | 27 | 27 | 6 |
| BZ2_bzReadOpen | 32 | 32 | 7 |
| BZ2_bzRead | 34 | 34 | 5 |
| BZ2_bzReadGetUnused | 24 | 24 | 4 |
| generateMTFValues | 22 | 22 | 1 |
| sendMTFValues | 307 | 307 | 1 |
| BZ2_compressBlock | 25 | 25 | 1 |
| BZ2_hbCreateDecodeTables | 43 | 43 | 7 |

Whole program (tool, same run): `stack fold: 77 routines, 1914 loads replaced, 775 register syncs, 130 address folds`.

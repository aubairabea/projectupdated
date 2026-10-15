#!/usr/bin/env python3
"""Builds BEFORE_AFTER.md. Every assembly line is copied verbatim from a tool dump in dumps/ (by line number);
only the trailing '; ...' comments are ours."""
import gzip, os
HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, 'dumps')
B = gzip.open(os.path.join(D, 'bzip2_dumps.txt.gz'), 'rt').read().split('\n')
C = gzip.open(os.path.join(D, 'cc1_dumps.txt.gz'), 'rt').read().split('\n')
LIVE = open(os.path.join(D, 'live_pad_before_after.txt')).read().split('\n')
LIVE2 = open(os.path.join(D, 'live_pad_profiling_stub.txt')).read().split('\n')

def block(lines, a, b, notes, src):
    out = []
    for n in range(a, b + 1):
        t = lines[n - 1]
        addr = t.split(':')[0].strip()
        note = notes.get(addr, '')
        out.append(f'{t:<52}' + (f'; {note}' if note else '') if note else t)
    return f'{src}, lines {a}-{b}:\n```asm\n' + '\n'.join(out) + '\n```'

def live(lines, labels, notes, src):
    out = []
    for t in lines:
        if t.startswith('=== tool stderr'): break
        if t.startswith('==='): out.append('; ' + t.strip('= ')); continue
        if not t.strip(): continue
        addr = t.strip().split(':')[0]
        note = notes.get(addr, '')
        out.append(f'{t.strip():<70}' + (f'; {note}' if note else '') if note else t.strip())
    return f'{src} (memory of the running bzip2 process, read twice and disassembled with objdump):\n```asm\n' + '\n'.join(out) + '\n```'

md = ["# Before / after: real assembly from our tool's dumps", "",
      "Every instruction below is copied from a dump produced by our tool; only the `; comments` are added.",
      "Sources (in `dumps/`): `bzip2_dumps.txt.gz` = `-verbose -dump_orig_code -dump_tc -dump_tc2` on",
      "`./bzip2 -c input-long.txt` (output identical to native); `cc1_dumps.txt.gz` = `-verbose -dump_orig_code",
      "-dump_tc2` on `./cc1 200.i` (output identical); `live_pad_*.txt` = bytes read from the running process by",
      "`read_live_pad.py`. Line numbers refer to the uncompressed dump files.", ""]

# a. profiling stubs
md += ["## a. Profiling stubs in TC", "",
       "### Cheap form: one `add` where the flags are dead (mainGtU entry in TC)", "",
       block(B, 1432, 1435, {'0x4020ad': 'original entry of mainGtU'}, 'Original (`bzip2_dumps.txt`)'), "",
       block(B, 26061, 26070, {
           '0x40689f8a': 'trampoline (5 bytes; becomes "jmp TC2" at commit)',
           '0x40689f8f': 'head pad, part 1 (NOP(1))',
           '0x40689f90': 'head pad, part 2 (NOP(7)); the probe from 0x4020ad lands here',
           '0x40689f97': 'counter pad, part 1',
           '0x40689f98': 'counter pad, part 2',
           '0x40689f9f': 'block counter: flags are dead here -> a single add',
           '0x40689fa7': 'original code continues'}, 'TC (`bzip2_dumps.txt`)'), "",
       "### Save/restore form: RAX is live (return value) and the flags are not provably dead (fallbackQSort3)", "",
       block(B, 872, 878, {'0x401892': 'i++', '0x401896': 'loop test', '0x4018a0': 'block that only returns'},
             'Original (`bzip2_dumps.txt`)'), "",
       block(B, 25215, 25229, {
           '0x4068906d': 'i++ (unchanged)',
           '0x40689079': 'counter of the loop-test block: cheap form, the cmp right after overwrites the flags',
           '0x40689085': 'loop back-edge',
           '0x4068908b': 'pad of the next counter',
           '0x40689093': 'save RAX to a slot in our region (never push: red zone)',
           '0x4068909a': 'load the counter',
           '0x406890a1': 'increment with LEA (does not touch the flags)',
           '0x406890a5': 'store the counter',
           '0x406890ac': 'restore RAX (the function result)'}, 'TC (`bzip2_dumps.txt`)'), "",
       "### Dead-register form: the counter borrows RBP right before `pop rbp` overwrites it (deregister_tm_clones)", "",
       block(B, 232, 247, {'0x400fe5': 'block: pop rbp; ret', '0x400ff7': 'indirect jump (profiled, see below)'},
             'Original (`bzip2_dumps.txt`)'), "",
       block(B, 24304, 24318, {
           '0x406880eb': 'RBP is dead: the next original instruction is "pop rbp"',
           '0x406880f2': 'LEA: flags untouched',
           '0x40688105': 'second counter (block "pop rbp; ret"), again with RBP',
           '0x40688117': 'original pop rbp overwrites our temporary value'}, 'TC (`bzip2_dumps.txt`)'), "",
       "### Indirect-jump target profile (flag-free hash), same routine", "",
       block(B, 24335, 24351, {
           '0x40688174': 'pad (switched off after the window)',
           '0x40688179': 'save RAX',
           '0x40688180': 'save RCX',
           '0x40688187': 'store the target T (the jump is "jmp rax")',
           '0x4068818e': 'low byte of T',
           '0x40688195': '256-byte table: byte -> ((byte>>4)&15)*16',
           '0x4068819c': 'slot offset, no flag-changing instruction',
           '0x406881a1': "this site's 16-slot table",
           '0x406881ad': 'T',
           '0x406881b4': 'slot.target = T',
           '0x406881bc': 'slot.count++ with LEA',
           '0x406881c4': 'restore RCX',
           '0x406881cb': 'restore RAX',
           '0x406881d2': 'the original indirect jump'}, 'TC (`bzip2_dumps.txt`)'), ""]

# b. call guard
md += ["## b. Indirect CALL guard in cc1 (htab_traverse calls its callback)", "",
       block(C, 558377, 558384, {'0x62c0d5': 'indirect call of the callback in r13'}, 'Original (`cc1_dumps.txt`)'), "",
       block(C, 561754, 561768, {
           '0x41efa03e': 'hottest target: cselib_invalidate_mem_1 (0x477ef0)',
           '0x41efa045': 'not equal -> try the next target',
           '0x41efa04b': 'direct call to its TC2 copy',
           '0x41efa055': 'second target: ggc_htab_delete (0x4e6700)',
           '0x41efa062': 'direct call to its TC2 copy',
           '0x41efa06c': 'fallback: the original indirect call',
           '0x41efa06f': 'all paths continue here'}, 'TC2 (`cc1_dumps.txt`)'), "",
       "CMP is allowed here because flags are not preserved across a call. Target names from `nm cc1`.", ""]

# c. jump guard
md += ["## c. Indirect JUMP guard in cc1 (switch in ggc_mark_rtx_children_1)", "",
       block(C, 232563, 232567, {'0x4e5390': 'out of range -> 0x4e5408', '0x4e5395': 'switch jump through a table'},
             'Original (`cc1_dumps.txt`)'), "",
       block(C, 564391, 564398, {
           '0x41efce95': 'unchanged',
           '0x41efce98': 'save RCX in our region (no push)',
           '0x41efce9f': 'rcx = the real target, read from the same table',
           '0x41efcea7': 'rcx = target - 0x4e5408; LEA keeps the flags',
           '0x41efceae': 'rcx == 0 -> the profiled hot target',
           '0x41efceb0': 'miss: restore RCX ...',
           '0x41efceb7': '... and take the original indirect jump',
           '0x41efcebe': 'hit: restore RCX ...'}, 'TC2 (`cc1_dumps.txt`)'),
       "```asm\n" + C[564398] + "                                ; ... and jump directly to the TC2 copy of 0x4e5408\n```", "",
       "No instruction in the guard writes the flags, because cc1 can still need them after an indirect jump.", ""]

# d. inverted branch
md += ["## d. Inverted conditional branch after reordering (mainGtU)", "",
       block(B, 1451, 1459, {'0x4020ee': 'equal (taken) -> continue comparing at 0x4020ff',
                             '0x4020f0': 'not equal (fall-through): compute the result and leave',
                             '0x4020ff': 'the hot path: i1++'}, 'Original (`bzip2_dumps.txt`)'), "",
       block(B, 60189, 60193, {'0x40f13046': 'same compare (folded to registers, see e.)',
                               '0x40f13049': 'INVERTED: jz -> jnz, the not-equal block is now placed elsewhere',
                               '0x40f1304f': 'the hot block (i1++) now falls through'}, 'TC2 (`bzip2_dumps.txt`)'), "",
       "In TC (original order) the same branch is still `jz` (TC, line 26089: `0x40689ff0: jz 0x4068a015`).", ""]

# e. stack fold + address fold
md += ["## e. Stack fold + address fold (mainGtU)", "",
       "The -verbose line for this run: `" + [l for l in B if l.startswith('fold mainGtU')][0] + "`", "",
       block(B, 1433, 1446, {'0x4020b1': 'store i1', '0x4020c7': 'reload i1', '0x4020ca': 'reload block',
                             '0x4020ce': 'address arithmetic', '0x4020d1': 'block[i1]', '0x4020d4': 'store c1',
                             '0x4020d7': 'reload i2'}, 'Original (`bzip2_dumps.txt`)'), "",
       block(B, 60172, 60191, {
           '0x40f13004': 'store i1 stays ...',
           '0x40f13007': '... and syncs the spare (here the slot lives in RDI)',
           '0x40f13010': 'block also kept in R11',
           '0x40f13022': 'reload of i1 replaced by a register move',
           '0x40f13024': 'address fold: mov rax,[rbp-0x20]; add rax,rdx; movzx eax,[rax] -> one instruction',
           '0x40f1302c': 'sync c1 into R9',
           '0x40f13033': 'first load of i2 syncs R8',
           '0x40f1303e': 'sync c2 into R10',
           '0x40f13042': 'reload of c1 replaced (was movzx eax, byte ptr [rbp-0x5])',
           '0x40f13046': 'compare with c2 from a register (was cmp al, byte ptr [rbp-0x6])'}, 'TC2 (`bzip2_dumps.txt`)'), "",
       block(B, 60193, 60198, {'0x40f1304f': 'i1++ in memory stays ...', '0x40f13053': '... the spare follows with LEA (flags kept)',
                               '0x40f1305a': 'i2++', '0x40f1305e': 'spare of i2 follows'}, 'TC2, next block'), ""]

# f. patch pads
md += ["## f. Patch pads before and after switching", "",
       "### mainGtU's routine head in TC: switched to TC2", "",
       live(LIVE, None, {
           '40689fbb:': 'trampoline: 5-byte NOP while profiling',
           '40689fc0:': 'head pad: 8-byte NOP',
           '40689fc8:': 'counter pad: 8-byte NOP',
           '40689fd0:': 'the block counter'}, '`dumps/live_pad_before_after.txt`'), "",
       "After the commit: the trampoline holds `jmp 0x40f13000` (mainGtU's first instruction in TC2, see e.), the head",
       "pad is `jmp short` back to the trampoline (`eb f9`), and the counter pad jumps over the counter (`eb 0e`).",
       "Each pad changed with one aligned 4-byte store; the `cc cc` filler bytes and the old NOP tail are never",
       "executed.", "",
       "### A profiling stub pad switched off (NOP(3) + NOP(5) layout)", "",
       live(LIVE2, None, {}, '`dumps/live_pad_profiling_stub.txt` (a `-dump_orig_code` run lays TC out slightly differently, so these addresses are another stub)'), "",
       "The pad's second NOP started at the 4-byte aligned address 0x40689f94; the store wrote `eb 23 cc cc` there,",
       "turning it into `jmp 0x40689fb9` over the save/restore counter that follows.", ""]
open(os.path.join(HERE, 'BEFORE_AFTER.md'), 'w').write('\n'.join(md))
print("ok", sum(1 for l in md for _ in l.split('\n')))

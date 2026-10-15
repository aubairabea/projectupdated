#!/usr/bin/env python3
"""Share of executed instructions per routine for bzip2 during the profiling window.
Input: data/bzip2_profile.edge-profile.csv (our tool's -dump_prof: block start address, execution count, ...),
objdump -d of ../../bzip2 (instruction addresses and mnemonics), nm (routine names).
A block runs from its start address to its first jump/return (inclusive) or to the next profiled block start."""
import subprocess, re, bisect, os, csv, json
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
dis = subprocess.run(['objdump', '-d', '--no-show-raw-insn', os.path.join(ROOT, 'bzip2')], capture_output=True, text=True).stdout
ins = []
for l in dis.splitlines():
    m = re.match(r'\s+([0-9a-f]+):\s+(\S+)', l)
    if m: ins.append((int(m.group(1), 16), m.group(2)))
addr = [a for a, _ in ins]
syms = sorted((int(p[0], 16), p[2]) for p in (l.split() for l in subprocess.run(['nm', os.path.join(ROOT, 'bzip2')], capture_output=True, text=True).stdout.splitlines()) if len(p) == 3 and p[1] in 'tT')
sa = [s[0] for s in syms]
def rtn(a): return syms[bisect.bisect_right(sa, a) - 1][1]
blocks = []
for row in csv.reader(open(os.path.join(HERE, 'bzip2_profile.edge-profile.csv'))):
    blocks.append((int(row[0], 16), int(row[1])))
starts = set(a for a, _ in blocks)
term = lambda m: m.startswith('j') or m.startswith('ret') or m.startswith('repz') and False
tot = {}; ninsn = 0
for a, c in blocks:
    i = bisect.bisect_left(addr, a); n = 0; r = rtn(a)
    while i < len(ins):
        n += 1; m = ins[i][1]
        if m.startswith('j') or m.startswith('ret') or m in ('bnd',) : break
        i += 1
        if i < len(ins) and (ins[i][0] in starts or rtn(ins[i][0]) != r): break
    tot[r] = tot.get(r, 0) + n * c; ninsn += n * c
rows = sorted(tot.items(), key=lambda kv: -kv[1])
json.dump({'total_instructions': ninsn, 'routines': rows}, open(os.path.join(HERE, 'hotspot_bzip2.json'), 'w'), indent=1)
print(f'total executed instructions in the window (reconstructed): {ninsn:.4g}')
for r, v in rows[:12]: print(f'{100*v/ninsn:6.2f}%  {v:14d}  {r}')

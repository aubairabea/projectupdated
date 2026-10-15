#!/usr/bin/env python3
"""Per-slot fold decisions for bzip2 from the tool's own output (dumps/bzip2_tc_only.txt.gz: '-verbose -dump_tc').
For each routine in the tool's 'fold <routine>: N loads; [rbp-X]/size->rK ...' lines we count, per slot, the reads
of [rbp-X] in the original code (objdump -M intel) and the reads still present in the routine's TC copy;
replaced = original reads - remaining reads. Check: the per-slot sum must equal the tool's N for that routine."""
import gzip, re, subprocess, os, bisect, json
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
dump = gzip.open(os.path.join(HERE, '..', 'dumps', 'bzip2_tc_only.txt.gz'), 'rt').read().split('\n')
folds = {}
for l in dump:
    m = re.match(r'fold (\S+): (\d+) loads;(.*)', l)
    if m: folds[m.group(1)] = (int(m.group(2)), re.findall(r'\[rbp(-\d+)\]/(\d+)->r(\d+)', m.group(3)))
REG = {1: 'rcx', 6: 'rsi', 7: 'rdi', 8: 'r8', 9: 'r9', 10: 'r10', 11: 'r11'}
# TC listing
i0 = dump.index('Translation Cache dump:') + 1
tc = []
for l in dump[i0:]:
    m = re.match(r'0x([0-9a-f]+): (.*)', l)
    if not m: break
    tc.append((int(m.group(1), 16), m.group(2).strip()))
isnop = lambda t: t.startswith('nop') or t.startswith('data16 nop')
starts = [k for k in range(1, len(tc) - 1) if isnop(tc[k][1]) and tc[k + 1][0] - tc[k][0] == 5 and not isnop(tc[k - 1][1])]
starts = [0] + starts if isnop(tc[0][1]) and tc[1][0] - tc[0][0] == 5 else starts
# original routines in address order (same order as the tool's TC)
dis = subprocess.run(['objdump', '-d', '-M', 'intel', '--no-show-raw-insn', os.path.join(ROOT, 'bzip2')], capture_output=True, text=True).stdout
orig = {}; cur = None; order = []
for l in dis.splitlines():
    m = re.match(r'([0-9a-f]+) <(.+)>:', l)
    if m and not m.group(2).endswith('@plt') and m.group(2) not in ('.plt', '.plt.got'):
        cur = m.group(2); orig[cur] = []; order.append((int(m.group(1), 16), cur)); continue
    m = re.match(r'\s+[0-9a-f]+:\s+(.*)', l)
    if m and cur: orig[cur].append(m.group(1).split('#')[0].strip())
sec = subprocess.run(['objdump', '-h', os.path.join(ROOT, 'bzip2')], capture_output=True, text=True).stdout
print('TC routine copies found:', len(starts))
def reads(lines, disp, size):
    """instructions that read [rbp-disp] (memory as a source operand, or the first operand of cmp)"""
    pat = re.compile(r'(byte|word|dword|qword) ptr \[rbp-0x%x\]' % disp, re.I)
    want = {1: 'byte', 2: 'word', 4: 'dword', 8: 'qword'}[size]
    n = 0
    for t in lines:
        mm = pat.search(t)
        if not mm or mm.group(1).lower() != want: continue
        mnem, _, ops = t.partition(' ')
        ops = ops.strip(); first = ops.split(',')[0]
        if 'rbp-0x%x' % disp in first and not mnem.lower().startswith('cmp'): continue   # destination: store / RMW
        n += 1
    return n
res = {}
names = [n for _, n in sorted(order)]
for rn, (total, slots) in folds.items():
    k = names.index(rn) if rn in names else -1
    # the k-th TC copy belongs to the k-th routine with code; check the copy really holds this routine
    a = starts[k]; b = starts[k + 1] if k + 1 < len(starts) else len(tc)
    tcl = [t for _, t in tc[a:b]]
    rows = []
    for d, sz, r in slots:
        d = -int(d); sz = int(sz)
        o = reads(orig[rn], d, sz); t = reads(tcl, d, sz)
        rows.append({'slot': f'[rbp-0x{d:x}]', 'size': sz, 'spare': REG[int(r)], 'orig_reads': o, 'tc_reads_left': t, 'replaced': o - t})
    res[rn] = {'tool_loads_replaced': total, 'sum_per_slot': sum(x['replaced'] for x in rows), 'slots': rows}
    print(rn, 'tool:', total, 'per-slot sum:', res[rn]['sum_per_slot'])
json.dump(res, open(os.path.join(HERE, 'fold_slots_bzip2.json'), 'w'), indent=1)

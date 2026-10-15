#!/usr/bin/env python3
"""Why cc1's indirect sites were (not) de-virtualized. Inputs: data/cc1_profile.edge-profile.csv (our tool's
-dump_prof: per block start, count, then the profiled targets of the indirect sites in that block), objdump of cc1
(where the indirect jumps/calls are), nm (routine entries). The rule applied is the tool's own (project.cpp:1749-1768,
defaults 100 executions / 10% share / 80% coverage / 3 targets; target needs a TC2 copy; calls only to routine entries).
Blocks containing more than one indirect site mix their targets in the dump, so they are reported separately."""
import subprocess, re, bisect, os, csv, json
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
dis = subprocess.run(['objdump', '-d', '--no-show-raw-insn', os.path.join(ROOT, 'cc1')], capture_output=True, text=True).stdout
ins = []
for l in dis.splitlines():
    m = re.match(r'\s+([0-9a-f]+):\s+(\S+)\s*(.*)', l)
    if m: ins.append((int(m.group(1), 16), m.group(2), m.group(3)))
addr = [a for a, _, _ in ins]; addrset = set(addr)
syms = sorted((int(p[0], 16), p[2]) for p in (l.split() for l in subprocess.run(['nm', os.path.join(ROOT, 'cc1')], capture_output=True, text=True).stdout.splitlines()) if len(p) == 3 and p[1] in 'tT')
sa = [s[0] for s in syms]; entries = set(sa)
def rtn(a):
    i = bisect.bisect_right(sa, a) - 1
    return syms[i][0] if i >= 0 else None
rows = list(csv.reader(open(os.path.join(HERE, 'cc1_profile.edge-profile.csv'))))
starts = set(int(r[0], 16) for r in rows)
hot_rtns = set(rtn(int(r[0], 16)) for r in rows if int(r[1]) > 0)
stat = {'executed_blocks_with_sites': 0, 'single_site': 0, 'multi_site_blocks': 0, 'multi_site_sites': 0}
reasons = {}; guarded = {'call': 0, 'jmp': 0}; detail = []
for r in rows:
    a, cnt = int(r[0], 16), int(r[1])
    tg = [(int(r[k], 16), int(r[k + 1])) for k in range(4, len(r) - 1, 2)]
    i = bisect.bisect_left(addr, a); sites = []
    rt = rtn(a)
    while i < len(ins):
        m, ops = ins[i][1], ins[i][2]
        if (m.startswith('jmp') or m.startswith('call')) and '*' in ops and 'notrack' not in m: sites.append('call' if m.startswith('call') else 'jmp')
        if m.startswith('j') or m.startswith('ret'): break
        i += 1
        if i < len(ins) and (ins[i][0] in starts or rtn(ins[i][0]) != rt): break
    if not sites: continue
    stat['executed_blocks_with_sites'] += 1
    if len(sites) > 1:
        stat['multi_site_blocks'] += 1; stat['multi_site_sites'] += len(sites); continue
    stat['single_site'] += 1
    kind = sites[0]
    agg = {}
    for t, c in tg: agg[t] = agg.get(t, 0) + c
    total = sum(agg.values())
    def why():
        if total == 0: return 'no target recorded'
        if total < 100: return 'ran fewer than 100 times'
        v = sorted(agg.items(), key=lambda kv: (-kv[1], kv[0]))
        out, cov = [], 0
        for t, c in v:
            if len(out) >= 3: break
            if c * 100 < total * 10: break
            if t > 0x7FFFFFFF: continue
            if t not in addrset or rtn(t) not in hot_rtns: continue
            if kind == 'call' and t not in entries: continue
            out.append(t); cov += c
        if not out: return 'no eligible target (each below 10% or no TC2 copy)'
        if cov * 100 < total * 80: return f'guarded targets would cover only {100*cov//total}% (< 80%)'
        return 'GUARDED'
    w = why()
    if w.startswith('guarded targets would cover'): w = 'targets cover less than 80% (too many different targets)'
    if w == 'GUARDED': guarded[kind] += 1
    reasons[w] = reasons.get(w, 0) + 1
tool = open(os.path.join(HERE, 'cc1_profile.txt')).read()
m = re.search(r'translated \S+ routines, \d+ instructions, \d+ blocks, (\d+) indirect sites', tool); sites_total = int(m.group(1))
m2 = re.search(r'devirt calls (\d+) jmps (\d+) targets (\d+)', tool)
res = {'sites_profiled_by_tool': sites_total, 'tool_guarded_calls': int(m2.group(1)), 'tool_guarded_jmps': int(m2.group(2)),
       'tool_guarded_targets': int(m2.group(3)), **stat, 'reasons_single_site': reasons, 'reconstructed_guarded_single_site': guarded}
json.dump(res, open(os.path.join(HERE, 'devirt_sites_cc1.json'), 'w'), indent=1)
print(json.dumps(res, indent=1))

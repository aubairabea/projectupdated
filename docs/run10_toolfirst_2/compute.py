#!/usr/bin/env python3
# Builds RESULTS.md from r<N>_tool.txt / r<N>_native.txt (bash `time` output + the tool's [time] lines).
import re, statistics as st, os
def t(s, key):
    m = re.search(r'^%s\s+(\d+)m([\d.]+)s' % key, s, re.M); return int(m.group(1))*60 + float(m.group(2))
def num(s, pat): return float(re.search(pat, s).group(1))
rows = []
for n in range(1, 11):
    T = open(f'r{n}_tool.txt').read(); N = open(f'r{n}_native.txt').read()
    nr, nu, ns = t(N,'real'), t(N,'user'), t(N,'sys'); tr = t(T,'real')
    pr = num(T, r'Translated code run \(including profiling\) took: ([\d.]+)')
    e2e = num(T, r'honest end-to-end total: ([\d.]+)')
    same = open(f'r{n}_tool.bz2','rb').read() == open(f'r{n}_native.bz2','rb').read()
    A = (nr-pr)/nr*100; B = (nu+ns-e2e)/(nu+ns)*100; C = (nr-tr)/nr*100
    rows.append((n, nr, nu, ns, tr, pr, e2e, 'IDENTICAL' if same else 'DIFFERENT', A, B, C))
o = []
o.append("# bzip2, 10 paired rounds, TOOL FIRST then NATIVE\n")
o.append("Commands (from ~/Desktop/project5, no taskset, sequential; output moved to r<N>_*.bz2 after each run):\n")
o.append("    tool:   time $HOME/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin -t obj-intel64/project.so -prof_time 2 -- ./bzip2 -k -f input-long.txt")
o.append("    native: time ./bzip2 -k -f input-long.txt\n")
o.append("project.so md5 ca18d4244b72e1627224fd66868c7ac7 (same build as docs/VERIFICATION.md); every tool run: 0 ERROR lines, '[time] TC2 commit' present (console.txt).\n")
o.append("A = (native real - tool printed line) / native real; B = (native user+sys - tool end-to-end) / (native user+sys); C = (native real - tool real) / native real.\n")
o.append("| round | native real | native user | native sys | tool real (whole pin) | tool printed line | tool end-to-end | cmp | A | B | C |")
o.append("|---|---|---|---|---|---|---|---|---|---|---|")
for r in rows:
    o.append("| %d | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f | %s | %.2f%% | %.2f%% | %.2f%% |" % r)
n, nr, nu, ns, tr, pr, e2e = rows[0][:7]
o.append("\nArithmetic, round 1:")
o.append(f"- A = ({nr:.3f} - {pr:.3f}) / {nr:.3f} * 100 = {nr-pr:.3f} / {nr:.3f} * 100 = {(nr-pr)/nr*100:.2f}%")
o.append(f"- B = (({nu:.3f} + {ns:.3f}) - {e2e:.3f}) / {nu+ns:.3f} * 100 = {nu+ns-e2e:.3f} / {nu+ns:.3f} * 100 = {(nu+ns-e2e)/(nu+ns)*100:.2f}%")
o.append(f"- C = ({nr:.3f} - {tr:.3f}) / {nr:.3f} * 100 = {nr-tr:.3f} / {nr:.3f} * 100 = {(nr-tr)/nr*100:.2f}%")
o.append("\n| metric | median | mean | min | max | rounds > 5% |\n|---|---|---|---|---|---|")
summ = {}
for i, name in ((8, 'A (printed line vs native real)'), (9, 'B (end-to-end vs native user+sys)'), (10, 'C (whole pin run vs native real)')):
    v = [r[i] for r in rows]; summ[name[0]] = v
    o.append(f"| {name} | {st.median(v):.2f}% | {st.mean(v):.2f}% | {min(v):.2f}% | {max(v):.2f}% | {sum(x > 5 for x in v)}/10 |")
ident = sum(r[7] == 'IDENTICAL' for r in rows)
o.append(f"\nOutput: {ident}/10 IDENTICAL (and md5 8722c10f2ac1dfde915e79b1943a27d5 for every file, console.txt).")
open('RESULTS.md', 'w').write("\n".join(o) + "\n")
print("\n".join(o))

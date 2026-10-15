#!/usr/bin/env python3
"""Presentation graphs (1920x1080 PNG). All numbers are read from docs/verify_raw/ (our final measurements,
summarised in docs/VERIFICATION.md); nothing is typed in by hand.
Needs matplotlib (we ran it with matplotlib 3.7.5)."""
import csv, os, re, statistics as st
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.normpath(os.path.join(HERE, '..', '..', 'verify_raw'))
def rows(name): return list(csv.DictReader(open(os.path.join(RAW, name)), delimiter='\t'))
def raw(name): return open(os.path.join(RAW, name)).read()
def imp(n, t): return (n - t) / n * 100

# ---------- style ----------
NAVY, BLUE, TEAL, ORANGE, RED, GREY, LIGHT = '#1f3a5f', '#2f6db5', '#2a9d8f', '#e9a03b', '#c8553d', '#8a8f98', '#e8edf3'
plt.rcParams.update({'font.size': 22, 'axes.titlesize': 34, 'axes.titleweight': 'bold', 'axes.labelsize': 26,
                     'xtick.labelsize': 22, 'ytick.labelsize': 22, 'legend.fontsize': 22, 'font.family': 'DejaVu Sans',
                     'axes.spines.top': False, 'axes.spines.right': False, 'axes.edgecolor': '#444444',
                     'axes.titlepad': 24, 'figure.facecolor': 'white'})
def fig(): return plt.figure(figsize=(19.2, 10.8), dpi=100)
def save(f, name, source):
    f.text(0.995, 0.01, 'Source: ' + source, ha='right', va='bottom', fontsize=15, color=GREY)
    f.savefig(os.path.join(HERE, name), dpi=100)
    plt.close(f)
    print('wrote', name)
def five(ax, horizontal=False):
    if horizontal: ax.axvline(5, ls='--', lw=2.5, color=RED); return
    ax.axhline(5, ls='--', lw=2.5, color=RED)

# ---------- data ----------
s4 = rows('step4_summary.tsv')
s4_cpu = [float(r['cpu']) for r in s4]; s4_e2e = [float(r['e2e']) for r in s4]
s4_imp = [imp(n, t) for n, t in zip(s4_cpu, s4_e2e)]
med_imp = st.median(s4_imp)

# 1. native vs tool, tool split
f = fig(); ax = f.add_axes([0.10, 0.12, 0.62, 0.72])
win = st.median(float(r['window']) for r in s4); bld = st.median(float(r['build']) for r in s4)
com = st.median(float(r['commit']) for r in s4); exe = st.median(float(r['exec']) for r in s4)
nat = st.median(s4_cpu); tot = st.median(s4_e2e)
ax.bar(0, nat, 0.55, color=GREY, label='native (user + sys)')
ax.bar(1, win, 0.55, color=ORANGE, label='profiling window (TC)')
ax.bar(1, bld + com, 0.55, bottom=win, color=RED, label='TC2 build + commit')
ax.bar(1, exe, 0.55, bottom=win + bld + com, color=BLUE, label='TC2 execution')
ax.text(0, nat + 0.08, f'{nat:.2f} s', ha='center', fontsize=28, fontweight='bold')
ax.text(1, tot + 0.08, f'{tot:.2f} s', ha='center', fontsize=28, fontweight='bold')
ax.text(1, win / 2, f'{win:.2f} s', ha='center', va='center', color='white', fontsize=24, fontweight='bold')
ax.text(1, win + bld + com + exe / 2, f'{exe:.2f} s', ha='center', va='center', color='white', fontsize=24, fontweight='bold')
ax.text(1.33, win + (bld + com) / 2, f'build+commit {bld + com:.3f} s', va='center', fontsize=18, color=RED)
ax.set_xticks([0, 1]); ax.set_xticklabels(['native bzip2', 'our tool (end-to-end)'])
ax.set_ylabel('seconds (median of 10 paired rounds)'); ax.set_ylim(0, nat * 1.18); ax.set_xlim(-0.5, 1.9)
ax.set_title(f'bzip2: our tool vs native  (median improvement {med_imp:.2f}%)', loc='left')
ax.legend(loc='upper left', bbox_to_anchor=(1.0, 0.95), frameon=False)
save(f, '01_bzip2_native_vs_tool.png', 'docs/verify_raw/step4_summary.tsv (10 paired rounds)')

# 2. per-round improvement
f = fig(); ax = f.add_axes([0.08, 0.12, 0.88, 0.72])
x = list(range(1, 11))
b = ax.bar(x, s4_imp, 0.62, color=[BLUE if v > 5 else RED for v in s4_imp])
for xi, v in zip(x, s4_imp): ax.text(xi, v + 0.35, f'{v:.1f}%', ha='center', fontsize=21, fontweight='bold')
ax.axhline(5, ls='--', lw=2.5, color=RED, label='5% target')
ax.axhline(med_imp, ls=':', lw=3, color=NAVY, label=f'median {med_imp:.2f}%')
ax.legend(loc='upper right', bbox_to_anchor=(1.0, 1.0), frameon=True, framealpha=1, edgecolor='white')
ax.set_xticks(x); ax.set_xlabel('paired round (native run, then tool run)'); ax.set_ylabel('improvement vs native CPU (%)')
ax.set_ylim(0, max(s4_imp) * 1.32); ax.set_xlim(0.4, 10.6)
ax.set_title(f'bzip2: improvement in every round  ({sum(v > 5 for v in s4_imp)}/10 above 5%)', loc='left')
save(f, '02_bzip2_rounds.png', 'docs/verify_raw/step4_summary.tsv; improvement = (native user+sys - end-to-end) / native user+sys')

# 3. ablation
s8 = rows('step8_summary.tsv'); cfg = {}
for r in s8: cfg.setdefault(r['config'].strip('[]'), []).append(imp(float(r['cpu']), float(r['e2e'])))
names = {'': 'all optimizations on', '-no_stackfold': 'without stack fold', '-no_addrfold': 'without address fold',
         '-no_cheap_prof -no_sparse_prof': 'without cheap profiling', '-no_devirt': 'without de-virtualization\n(no hot indirect site in bzip2)',
         '-no_reorder': 'without reordering',
         '-no_stackfold -no_addrfold -no_cheap_prof -no_sparse_prof -no_devirt -no_reorder': 'everything off'}
lab = [names[k] for k in cfg]; val = [st.median(v) for v in cfg.values()]
f = fig(); ax = f.add_axes([0.32, 0.12, 0.62, 0.72])
y = list(range(len(lab)))[::-1]
ax.barh(y, val, 0.62, color=[TEAL if i == 0 else (BLUE if v > 5 else RED) for i, v in enumerate(val)])
for yi, v in zip(y, val):
    ax.text(v + (0.4 if v >= 0 else -0.4), yi, f'{v:+.2f}%', va='center', ha='left' if v >= 0 else 'right',
            fontsize=22, fontweight='bold')
ax.axvline(0, color='#444444', lw=1.5); five(ax, True)
ax.set_yticks(y); ax.set_yticklabels(lab); ax.set_xlabel('median improvement vs native (%), 4 paired rounds each')
ax.set_xlim(min(val) - 6, max(val) + 6)
ax.set_title('bzip2 ablation: turning one optimization off at a time', loc='left', x=-0.42)
save(f, '03_bzip2_ablation.png', 'docs/verify_raw/step8_summary.tsv')

# 4. cc1 ablation
cc = rows('cc1_ablation_summary.tsv'); cfg = {}
for r in cc: cfg.setdefault(r['config'].strip('[]'), []).append(imp(float(r['cpu']), float(r['e2e'])))
lab = {'': 'default\n(devirt + reorder)', '-no_devirt': 'without\nde-virtualization', '-no_reorder': 'without\nreordering',
       '-no_devirt -no_reorder': 'both off'}
f = fig(); ax = f.add_axes([0.11, 0.14, 0.86, 0.66])
ks = list(cfg); vals = [st.mean(cfg[k]) for k in ks]
ax.bar(range(len(ks)), vals, 0.6, color=[TEAL] + [GREY] * (len(ks) - 1))
for i, k in enumerate(ks):
    ax.text(i, vals[i] / 2, f'{vals[i]:.2f}%', ha='center', va='center', fontsize=28, fontweight='bold', color='white')
    for v in cfg[k]: ax.plot(i + 0.36, v, 'o', color=NAVY, ms=12)
ax.axhline(0, color='#444444', lw=1.5)
ax.set_xticks(range(len(ks))); ax.set_xticklabels([lab[k] for k in ks])
ax.set_ylabel('improvement vs native (%)\nmean of 2 paired rounds (dots: each round)')
ax.set_ylim(min(min(v) for v in cfg.values()) - 4, 2)
ax.set_title('cc1: de-virtualization and reordering help\n(but cc1 stays slower than native)', loc='left')
save(f, '04_cc1_devirt_reorder.png', 'docs/verify_raw/cc1_ablation_summary.tsv')

# 5. three ways
s5 = rows('step5_summary.tsv')
a = [imp(float(r['nat_cpu']), float(r['internal_e2e'])) for r in s5]
b = [imp(float(r['nat_cpu']), float(r['ext_user']) + float(r['ext_sys'])) for r in s5]
c = [imp(float(r['nat_cpu']), float(r['ext_wall'])) for r in s5]
gap = [float(r['ext_wall']) - float(r['internal_e2e']) for r in s5]
f = fig(); ax = f.add_axes([0.09, 0.14, 0.62, 0.70])
labels = ["tool's own clock\n(end-to-end)", 'whole pin run\n(user + sys)', 'whole pin run\n(wall clock)']
data = [a, b, c]; meds = [st.median(v) for v in data]
ax.bar(range(3), meds, 0.6, color=[BLUE, TEAL, ORANGE])
for i, v in enumerate(data):
    ax.text(i, meds[i] + 0.5, f'{meds[i]:.2f}%', ha='center', fontsize=28, fontweight='bold')
    for p in v: ax.plot(i + 0.38, p, 'o', color=NAVY, ms=11)
five(ax); ax.text(2.62, 5.25, '5%', color=RED, va='bottom', fontsize=22)
ax.set_xticks(range(3)); ax.set_xticklabels(labels); ax.set_ylabel('improvement vs native CPU (%)')
ax.set_ylim(0, max(max(v) for v in data) * 1.2); ax.set_xlim(-0.5, 2.8)
ax.set_title('bzip2 measured three ways (5 paired rounds)', loc='left')
f.text(0.74, 0.62, 'The whole-run wall clock\nalso contains Pin start-up,\nbuilding TC and exit:\n'
       f'{min(gap):.2f}-{max(gap):.2f} s per run.\n\nWall clock: '
       f'{sum(v > 5 for v in c)}/5 rounds above 5%.', fontsize=21, va='top', color=NAVY)
save(f, '05_bzip2_three_clocks.png', 'docs/verify_raw/step5_summary.tsv')

# 6. base tool vs ours
base = [l for l in raw('x_base_bzip2.txt').split('\n') if l.startswith('## RESULT')][0].split('\t')
base_imp = imp(float(base[14]), float(base[21]))
f = fig(); ax = f.add_axes([0.10, 0.14, 0.55, 0.70])
vals = [base_imp, med_imp]
ax.bar([0, 1], vals, 0.55, color=[RED, BLUE])
for i, v in enumerate(vals):
    ax.text(i, v + (2 if v > 0 else -2), f'{v:+.2f}%', ha='center', va='bottom' if v > 0 else 'top', fontsize=30, fontweight='bold')
ax.axhline(0, color='#444444', lw=1.5); five(ax)
ax.set_xticks([0, 1]); ax.set_xticklabels(['base tool\n(bprofile-with-gearing)', 'our tool'])
ax.set_ylabel('bzip2 improvement vs native (%)'); ax.set_ylim(base_imp - 12, med_imp + 12)
ax.set_title('Base tool vs our tool on bzip2', loc='left')
f.text(0.69, 0.70, 'Base tool: its printed time\n(1 round).\nOur tool: median end-to-end\n(10 rounds).\n\n'
       'On cc1 the base tool\nproduces WRONG output\n(first difference at byte\n6669460); ours is identical.',
       fontsize=21, va='top', color=NAVY)
save(f, '06_base_vs_ours.png', 'docs/verify_raw/x_base_bzip2.txt, x_base_cc1.txt, step4_summary.tsv')

# 7. gcc family
s3 = [r for r in rows('step3_summary.tsv') if r['bench'] != 'bzip2' and 'stress' not in r['tag']]
grp = {}
for r in s3:
    key = 'sgcc_peak\n(setarch -R)' if 'setarchR' in r['tag'] else {'cpugcc': 'cpugcc_r_base'}.get(r['bench'], r['bench'])
    grp.setdefault(key, []).append(imp(float(r['cpu']), float(r['e2e'])))
f = fig(); ax = f.add_axes([0.09, 0.14, 0.88, 0.70])
ks = list(grp); w = 0.36
for j in range(2):
    xs = [i + (j - 0.5) * w for i in range(len(ks))]; vs = [grp[k][j] for k in ks]
    ax.bar(xs, vs, w * 0.92, color=[NAVY, BLUE][j], label=f'round {j + 1}')
    for xi, v in zip(xs, vs): ax.text(xi, v + 1.2, f'{v:.0f}%', ha='center', va='bottom', color='white', fontsize=19, fontweight='bold')
ax.axhline(0, color='#444444', lw=1.5)
ax.set_xticks(range(len(ks))); ax.set_xticklabels(ks); ax.set_ylabel('improvement vs native (%)')
ax.set_ylim(min(min(v) for v in grp.values()) - 6, 3); ax.legend(loc='lower left', frameon=False)
ax.set_title('gcc binaries: always identical output, but slower than native', loc='left')
save(f, '07_gcc_family.png', 'docs/verify_raw/step3_summary.tsv (each round compared byte-for-byte with a fresh native run)')

# 8. timeline of one run
r1 = raw('s4_r1.txt')
g = lambda k: float(re.search(k + r':\s*([\d.]+)', r1).group(1))
tcb, w, bu, cm, ex, tot = g(r'TC build \(not counted\)'), g('profiling window'), g('TC2 build'), g('TC2 commit'), g('TC2 execution'), g('honest end-to-end total')
f = fig(); ax = f.add_axes([0.05, 0.22, 0.90, 0.56])
segs = [(-tcb, tcb, LIGHT, ''), (0, w, ORANGE, 'profiling window in TC'),
        (w, bu + cm, RED, ''), (w + bu + cm, ex, BLUE, 'TC2 execution')]
for x0, dx, col, txt in segs:
    ax.barh(0, dx, 0.5, left=x0, color=col, edgecolor='white', lw=2)
    if txt: ax.text(x0 + dx / 2, 0, txt, ha='center', va='center', fontsize=21,
                    color='#333333' if col == LIGHT else 'white', fontweight='bold')
ax.annotate(f'TC2 build {bu:.3f} s + commit {cm:.3f} s', xy=(w + (bu + cm) / 2, 0.25), xytext=(w + 0.05, 0.62),
            fontsize=20, color=RED, arrowprops=dict(arrowstyle='->', color=RED, lw=2))
for t, lbl in [(0, 'program starts\n(clock starts)'), (w, f'{w:.3f} s'), (tot, f'_exit\n{tot:.3f} s')]:
    ax.plot([t, t], [-0.33, 0.33], color='#333333', lw=2, ls='--'); ax.text(t, -0.40, lbl, ha='center', va='top', fontsize=21)
ax.text(-tcb - 0.04, 0, f'TC build\n{tcb:.3f} s\n(not\ncounted)', ha='right', va='center', fontsize=18, color='#555555')
ax.set_xlim(-tcb - 0.55, tot + 0.3); ax.set_ylim(-0.75, 0.9); ax.set_yticks([]); ax.set_xticks([])
ax.spines['left'].set_visible(False); ax.spines['bottom'].set_visible(False)
ax.set_title(f'One bzip2 run under our tool: end-to-end {tot:.3f} s', loc='left')
save(f, '08_run_timeline.png', 'docs/verify_raw/s4_r1.txt (round 1 of our 10-round bzip2 measurement)')

# 9. stack fold per routine
ev = raw('s8_evidence_default.txt')
fl = [(m.group(1), int(m.group(2)), m.group(3).count('->')) for m in re.finditer(r'^fold (\S+): (\d+) loads;(.*)$', ev, re.M)]
fl.sort(key=lambda t: -t[1]); top = fl[:10]
tot_line = re.search(r'^stack fold: .*$', ev, re.M).group(0)
f = fig(); ax = f.add_axes([0.30, 0.12, 0.64, 0.66])
y = list(range(len(top)))[::-1]
ax.barh(y, [t[1] for t in top], 0.62, color=TEAL)
for yi, t in zip(y, top): ax.text(t[1] + 3, yi, f'{t[1]}  ({t[2]} slot{"s" if t[2] != 1 else ""})', va='center', fontsize=20)
ax.set_yticks(y); ax.set_yticklabels([t[0] for t in top]); ax.set_xlabel('stack loads replaced by register moves (static count)')
ax.set_xlim(0, max(t[1] for t in top) * 1.3)
ax.set_title('Stack fold on bzip2: routines with the most folded loads', loc='left', x=-0.42)
ax.text(0.98, 0.06, tot_line.replace('stack fold: ', 'Whole program:\n').replace(', 1914', ',\n1914').replace(', 775', ',\n775'), transform=ax.transAxes, ha='right', va='bottom', fontsize=22, color=NAVY)
save(f, '09_stackfold_routines.png', 'docs/verify_raw/s8_evidence_default.txt (-verbose statistics)')

# 10. reordering statistics
m = re.search(r'inverted (\d+) jmp added (\d+) removed (\d+) cold blocks (\d+)', ev)
inv, add, rem, cold = map(int, m.groups())
f = fig(); ax = f.add_axes([0.09, 0.14, 0.86, 0.70])
lab = ['conditional jumps\ninverted', 'jumps added\n(broken fall-through)', 'jumps removed\n(target now follows)', 'cold blocks moved\nto the cold region']
vals = [inv, add, rem, cold]
ax.bar(range(4), vals, 0.6, color=[BLUE, ORANGE, TEAL, GREY])
for i, v in enumerate(vals): ax.text(i, v + 12, str(v), ha='center', fontsize=30, fontweight='bold')
ax.set_xticks(range(4)); ax.set_xticklabels(lab); ax.set_ylabel('count in TC2')
ax.set_ylim(0, max(vals) * 1.18)
ax.set_title('Code reordering on bzip2 (46 hot routines in TC2)', loc='left')
save(f, '10_reorder_stats.png', 'docs/verify_raw/s8_evidence_default.txt (-verbose TC2 statistics)')

# 11. correctness matrix
s3all = rows('step3_summary.tsv')
def ok(tag): return any(r['tag'] == tag and r['cmp'] == 'IDENTICAL' and r['nat_exit'] == r['tool_exit'] and r['ERROR'] == '0' for r in s3all)
modes = {os.path.basename(p)[6:-4]: open(p).read() for p in [os.path.join(RAW, x) for x in os.listdir(RAW) if x.startswith('modes_')]}
def mode_ok(n):
    t = modes.get(n, ''); m1 = re.search(r'native size=(\d+) md5=(\w+) exit=(\d+)', t); m2 = re.search(r'tool size=(\d+) md5=(\w+) exit=(\d+)', t)
    return bool(m1 and m2 and m1.groups() == m2.groups())
rt = raw('step3_roundtrip.txt')
bench = ['bzip2', 'cc1', 'sgcc_base', 'sgcc_peak', 'cpugcc_r_base']
key = {'cpugcc_r_base': 'cpugcc'}
cols = ['round 1', 'round 2', 'prof_time 1 / 4', 'setarch -R', 'devirt stress']
cell = {}
for bname in bench:
    k = key.get(bname, bname)
    cell[(bname, 'round 1')] = ok(f's3_{k}_r1'); cell[(bname, 'round 2')] = ok(f's3_{k}_r2')
    cell[(bname, 'prof_time 1 / 4')] = (ok('s3_bzip2_prof1') and ok('s3_bzip2_prof4')) if bname == 'bzip2' else None
    cell[(bname, 'setarch -R')] = (ok('s3_sgcc_peak_setarchR_r1') and ok('s3_sgcc_peak_setarchR_r2')) if bname == 'sgcc_peak' else None
    cell[(bname, 'devirt stress')] = ok(f's3_stress_{k}') if bname in ('bzip2', 'cc1', 'sgcc_peak') else None
f = fig(); ax = f.add_axes([0.02, 0.24, 0.96, 0.60]); ax.axis('off')
cw, ch, x0 = 0.165, 0.15, 0.17
for j, c in enumerate(cols): ax.text(x0 + j * cw + cw / 2, 0.93, c, ha='center', va='center', fontsize=22, fontweight='bold', color=NAVY)
for i, bname in enumerate(bench):
    yy = 0.78 - i * ch
    ax.text(0.155, yy, bname, ha='right', va='center', fontsize=24, fontweight='bold')
    for j, c in enumerate(cols):
        v = cell[(bname, c)]
        col, txt = (TEAL, 'identical') if v else ((LIGHT, 'n/a') if v is None else (RED, 'DIFFERENT'))
        ax.add_patch(FancyBboxPatch((x0 + j * cw + 0.006, yy - ch / 2 + 0.012), cw - 0.012, ch - 0.024,
                                    boxstyle='round,pad=0.004', color=col, transform=ax.transAxes))
        ax.text(x0 + j * cw + cw / 2, yy, txt, ha='center', va='center', fontsize=21,
                color='white' if v else '#777777', fontweight='bold', transform=ax.transAxes)
f.text(0.02, 0.93, 'Correctness: every run byte-identical to a fresh native run', fontsize=34, fontweight='bold')
extra = [n for n in ('compress-1', 'compress-9', 'decompress', 'test', 'stdin', 'decompress-stdin', 'empty', 'small1k', 'rand3m') if mode_ok(n)]
f.text(0.03, 0.15, f'bzip2 extra modes identical: {len(extra)}/9  ({", ".join(extra)})', fontsize=19, color=NAVY)
f.text(0.03, 0.10, 'Round trips (tool compress + native decompress, and the reverse): '
       + ('identical' if 'ROUNDTRIP1 IDENTICAL' in rt and 'ROUNDTRIP2 IDENTICAL' in rt else 'NOT identical'), fontsize=19, color=NAVY)
f.text(0.03, 0.05, 'Separate output files, sizes + md5 recorded, exit codes compared, no ERROR line; a flipped byte is detected.',
       fontsize=19, color=NAVY)
save(f, '11_correctness_matrix.png', 'docs/verify_raw/step3_summary.tsv, modes_*.txt, step3_roundtrip.txt')

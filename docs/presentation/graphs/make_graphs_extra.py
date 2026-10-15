#!/usr/bin/env python3
"""Extra graphs (same style as make_graphs.py): hotspot_bzip2.png, devirt_sites.png, metric_timeline.png.
Data: ../data/hotspot_bzip2.json, ../data/devirt_sites_cc1.json (computed from the tool's own output by the scripts
in ../data/) and docs/verify_raw/s5_r1.txt (one bzip2 run with the tool's timing lines and /usr/bin/time around it)."""
import json, os, re
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, '..', 'data')
RAW = os.path.normpath(os.path.join(HERE, '..', '..', 'verify_raw'))
NAVY, BLUE, TEAL, ORANGE, RED, GREY, LIGHT = '#1f3a5f', '#2f6db5', '#2a9d8f', '#e9a03b', '#c8553d', '#8a8f98', '#e8edf3'
plt.rcParams.update({'font.size': 22, 'axes.titlesize': 34, 'axes.titleweight': 'bold', 'axes.labelsize': 26,
                     'xtick.labelsize': 22, 'ytick.labelsize': 22, 'legend.fontsize': 22, 'font.family': 'DejaVu Sans',
                     'axes.spines.top': False, 'axes.spines.right': False, 'axes.edgecolor': '#444444',
                     'axes.titlepad': 24, 'figure.facecolor': 'white'})
def fig(): return plt.figure(figsize=(19.2, 10.8), dpi=100)
def save(f, name, source):
    f.text(0.995, 0.01, 'Source: ' + source, ha='right', va='bottom', fontsize=15, color=GREY)
    f.savefig(os.path.join(HERE, name), dpi=100); plt.close(f); print('wrote', name)

# hotspot
h = json.load(open(os.path.join(DATA, 'hotspot_bzip2.json')))
tot = h['total_instructions']; rows = h['routines']
top = rows[:8]; other = sum(v for _, v in rows[8:])
lab = [r for r, _ in top] + ['all other routines']; val = [100 * v / tot for _, v in top] + [100 * other / tot]
f = fig(); ax = f.add_axes([0.28, 0.12, 0.66, 0.70])
y = list(range(len(lab)))[::-1]
ax.barh(y, val, 0.62, color=[RED] + [BLUE] * (len(lab) - 2) + [GREY])
for yi, v in zip(y, val): ax.text(v + 0.8, yi, f'{v:.2f}%', va='center', fontsize=22, fontweight='bold')
ax.set_yticks(y); ax.set_yticklabels(lab); ax.set_xlim(0, max(val) * 1.15)
ax.set_xlabel('share of executed instructions in the profiling window (%)')
ax.set_title('Where bzip2 spends its instructions', loc='left', x=-0.36)
ax.text(0.97, 0.30, f'{tot:.3g} instructions executed\nin the 2-second window\n(block counts x block length)', transform=ax.transAxes, ha='right', fontsize=21, color=NAVY)
save(f, 'hotspot_bzip2.png', 'data/bzip2_profile.edge-profile.csv (-dump_prof) + objdump/nm of bzip2, computed by data/hotspot.py')

# devirt sites
d = json.load(open(os.path.join(DATA, 'devirt_sites_cc1.json')))
not_run = d['sites_profiled_by_tool'] - d['single_site'] - d['multi_site_sites']
r = d['reasons_single_site']
cats = [('never ran in the window', not_run, GREY),
        ('ran fewer than 100 times', r.get('ran fewer than 100 times', 0), ORANGE),
        ('targets cover < 80%', r.get('targets cover less than 80% (too many different targets)', 0), RED),
        ('no eligible target', r.get('no eligible target (each below 10% or no TC2 copy)', 0), RED),
        ('several sites in one block\n(not attributable)', d['multi_site_sites'], LIGHT),
        ('guarded (reconstructed)', r.get('GUARDED', 0), TEAL)]
f = fig(); ax = f.add_axes([0.30, 0.12, 0.64, 0.66])
y = list(range(len(cats)))[::-1]
ax.barh(y, [c[1] for c in cats], 0.62, color=[c[2] for c in cats], edgecolor='#bbbbbb')
for yi, c in zip(y, cats): ax.text(c[1] + 6, yi, str(c[1]), va='center', fontsize=24, fontweight='bold')
ax.set_yticks(y); ax.set_yticklabels([c[0] for c in cats]); ax.set_xlim(0, max(c[1] for c in cats) * 1.15)
ax.set_xlabel('number of indirect jump/call sites')
ax.set_title(f"cc1: {d['sites_profiled_by_tool']} indirect sites profiled, what happened to them", loc='left', x=-0.42)
ax.text(0.97, 0.30, f"Tool's own count: {d['tool_guarded_calls']} calls + {d['tool_guarded_jmps']} jumps guarded\n"
       f"({d['tool_guarded_targets']} targets).\nOur re-application of the rule\n(single-site blocks): "
       f"{d['reconstructed_guarded_single_site']['call']} calls + {d['reconstructed_guarded_single_site']['jmp']} jumps.", transform=ax.transAxes, ha='right', fontsize=20, color=NAVY)
save(f, 'devirt_sites.png', 'data/cc1_profile.txt (-verbose), data/cc1_profile.edge-profile.csv (-dump_prof), data/devirt_sites.py')

# metric timeline (one run with all three clocks: verify_raw/s5_r1.txt)
t = open(os.path.join(RAW, 's5_r1.txt')).read()
g = lambda k: float(re.search(k + r':\s*([\d.]+)', t).group(1))
tcb, win, bld, com, exe, e2e, pr = (g(r'TC build \(not counted\)'), g('profiling window'), g('TC2 build'), g('TC2 commit'),
                                    g('TC2 execution'), g('honest end-to-end total'), g(r'Translated code run \(including profiling\) took'))
walls = re.findall(r'Elapsed \(wall clock\) time \(h:mm:ss or m:ss\): (\d+):([\d.]+)', t)
ext = int(walls[-1][0]) * 60 + float(walls[-1][1])
users = re.findall(r'User time \(seconds\): ([\d.]+)', t); syss = re.findall(r'System time \(seconds\): ([\d.]+)', t)
nat = float(users[0]) + float(syss[0])
pre = ext - e2e                      # Pin start-up + TC build + exit, as seen from outside
f = fig(); ax = f.add_axes([0.06, 0.10, 0.90, 0.66])
x0 = -tcb
ax.barh(4, tcb, 0.55, left=x0, color=LIGHT, edgecolor='white'); ax.text(x0 + tcb / 2, 4.42, f'TC build\n{tcb:.3f} s', ha='center', va='bottom', fontsize=17, color='#555555')
ax.barh(4, win, 0.55, left=0, color=ORANGE, edgecolor='white'); ax.text(win / 2, 4, f'profiling window {win:.3f} s', ha='center', va='center', color='white', fontsize=21, fontweight='bold')
ax.barh(4, bld + com, 0.55, left=win, color=RED, edgecolor='white')
ax.barh(4, exe, 0.55, left=win + bld + com, color=BLUE, edgecolor='white'); ax.text(win + bld + com + exe / 2, 4, f'TC2 execution {exe:.3f} s', ha='center', va='center', color='white', fontsize=21, fontweight='bold')
ax.annotate(f'TC2 build {bld:.3f} s + commit {com:.3f} s', xy=(win, 4.28), xytext=(win - 0.2, 4.95), fontsize=18, color=RED,
            arrowprops=dict(arrowstyle='->', color=RED, lw=2))
bars = [(3, 0, pr, NAVY, f'printed "Translated code run": prof_time + TC2 execution = {pr:.3f} s'),
        (2, 0, e2e, TEAL, f'end-to-end (our clock): window + build + commit + execution = {e2e:.3f} s'),
        (1, e2e - ext, ext, GREY, f'outside wall clock (whole pin command, aligned at exit) = {ext:.2f} s'),
        (0, 0, nat, '#555555', f'native bzip2 CPU (user + sys), same round = {nat:.2f} s')]
for yy, left, w, col, txt in bars:
    ax.barh(yy, w, 0.42, left=left, color=col)
    ax.text(max(left, 0) + 0.06, yy, txt, va='center', fontsize=19, color='white', fontweight='bold')
ax.axvline(0, color='#333333', ls='--', lw=1.5); ax.text(0, 5.35, 'program starts', ha='center', fontsize=17)
ax.set_yticks([]); ax.set_xlabel('seconds from the program start (negative = before the program starts)')
ax.set_xlim(e2e - ext - 0.15, max(e2e, nat) + 0.2); ax.set_ylim(-0.6, 5.7)
ax.set_title('One bzip2 run: what each metric counts', loc='left')
ax.text(0.995, 0.985, f'The outside clock also contains {pre:.2f} s\nof Pin start-up, TC build and exit.\nThe printed line counts the window\nas exactly prof_time.', transform=ax.transAxes, ha='right', va='top', fontsize=18, color=NAVY)
save(f, 'metric_timeline.png', 'docs/verify_raw/s5_r1.txt (tool timing lines + /usr/bin/time of the whole pin command; native run of the same round)')

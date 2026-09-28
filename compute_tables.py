#!/usr/bin/env python3
# Recomputes every verification table from docs/verify_raw/*.tsv (no number is typed in by hand).
import csv, statistics as st
R='docs/verify_raw/'
def rows(f): return list(csv.DictReader(open(R+f), delimiter='\t'))
def imp(n,t): return (n-t)/n*100
out=[]
p=out.append
def stats(v): return "median %.2f%%, mean %.2f%%, min %.2f%%, max %.2f%%" % (st.median(v), st.mean(v), min(v), max(v))
# STEP 3
p("### STEP 3 correctness table\n")
p("| run | native size / md5 | tool size / md5 | cmp | exit native/tool | ERROR lines | TC2 committed |\n|---|---|---|---|---|---|---|")
for r in rows('step3_summary.tsv'):
    p(f"| {r['tag']} | {r['nat_size']} / {r['nat_md5']} | {r['tool_size']} / {r['tool_md5']} | {r['cmp']} | {r['nat_exit']}/{r['tool_exit']} | {r['ERROR']} | {r['TC2']} |")
# STEP 4
s4=rows('step4_summary.tsv'); imps=[]
p("\n### STEP 4: bzip2, 10 rounds (manual procedure)\n")
p("| round | native user | native sys | native CPU | native wall | window | TC2 build | TC2 commit | TC2 exec | end-to-end | printed line | improvement | cmp | ERROR | sum check | e2e>=printed | printed=2+exec | window~2 |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
fails=[]
for i,r in enumerate(s4,1):
    u,s,w=float(r['user']),float(r['sys']),float(r['wall']); cpu=u+s
    win,b,c,e,e2e,pr=[float(r[k]) for k in ('window','build','commit','exec','e2e','compat')]
    im=imp(cpu,e2e); imps.append(im)
    c1=abs(win+b+c+e-e2e)<=0.01; c2=e2e>=pr; c3=abs(pr-(2+e))<=0.01; c4=abs(win-2.0)<=0.05
    for n,ok in (('sum',c1),('e2e>=printed',c2),('printed=2+exec',c3),('window',c4)):
        if not ok: fails.append(f"round {i}: {n}")
    p(f"| {i} | {u:.2f} | {s:.2f} | {cpu:.2f} | {w:.2f} | {win:.3f} | {b:.3f} | {c:.3f} | {e:.3f} | {e2e:.3f} | {pr:.3f} | {im:.2f}% | {r['cmp']} | {r['ERROR']} | {'ok' if c1 else 'FAIL'} ({win+b+c+e:.3f}) | {'ok' if c2 else 'FAIL'} | {'ok' if c3 else 'FAIL'} ({2+e:.3f}) | {'ok' if c4 else 'FAIL'} |")
p(f"\nImprovement over the 10 rounds: {stats(imps)}; rounds above 5%: {sum(1 for x in imps if x>5)}/10; cmp IDENTICAL: {sum(1 for r in s4 if r['cmp']=='IDENTICAL')}/10.")
p("Arithmetic check failures: " + (", ".join(fails) if fails else "none"))
p("\nHand arithmetic, first 3 rows:")
for i,r in enumerate(s4[:3],1):
    u,s,e2e=float(r['user']),float(r['sys']),float(r['e2e'])
    p(f"- round {i}: native CPU = {u:.2f} + {s:.2f} = {u+s:.2f} s; ({u+s:.2f} - {e2e:.3f}) / {u+s:.2f} = {u+s-e2e:.3f} / {u+s:.2f} = {imp(u+s,e2e):.2f}%")
# STEP 5
s5=rows('step5_summary.tsv'); a=[];b=[];cc=[]
p("\n### STEP 5: outside clock (whole Pin run measured by /usr/bin/time), 5 rounds\n")
p("| round | native CPU | native wall | internal end-to-end | external wall | external user+sys | external wall - internal | improvement (internal) | improvement (external wall) | improvement (external user+sys) | cmp |\n|---|---|---|---|---|---|---|---|---|---|---|")
for r in s5:
    n=float(r['nat_cpu']); nw=float(r['nat_wall']); ie=float(r['internal_e2e']); ew=float(r['ext_wall']); eus=float(r['ext_user'])+float(r['ext_sys'])
    a.append(imp(n,ie)); b.append(imp(n,ew)); cc.append(imp(n,eus))
    p(f"| {r['round']} | {n:.2f} | {nw:.2f} | {ie:.3f} | {ew:.2f} | {eus:.2f} | {ew-ie:.3f} | {a[-1]:.2f}% | {b[-1]:.2f}% | {cc[-1]:.2f}% | {r['cmp']} |")
p(f"\nInternal: {stats(a)}\n\nExternal wall: {stats(b)}; rounds above 5%: {sum(1 for x in b if x>5)}/5\n\nExternal user+sys: {stats(cc)}")
# STEP 6
p("\n### STEP 6: ./bench.sh bzip2 5 recomputed from its raw files\n")
v=[]
for i in range(1,6):
    line=[l for l in open(R+f's6_bzip2_{i}.txt') if l.startswith('## RESULT')][0].rstrip('\n').split('\t')
    cpu,e2e=float(line[14]),float(line[20]); v.append(imp(cpu,e2e))
    p(f"- round {i}: native CPU {cpu:.2f}, e2e {e2e:.3f} -> {v[-1]:.2f}%  (cmp {line[7]})")
p(f"\n{stats(v)}")
# STEP 7
s7=[r for r in rows('step7_summary.tsv') if r['round'].isdigit()]
p("\n### STEP 7: new vs backup (~/Desktop/project4 build), 5 rounds, same native run per round\n")
p("| round | native CPU | new e2e | new improvement | backup e2e | backup improvement | new cmp | backup cmp |\n|---|---|---|---|---|---|---|---|")
ni=[];bi=[];ne=[];be=[]
for r in s7:
    n=float(r['nat_cpu']); x=float(r['new_e2e']); y=float(r['backup_e2e']); ne.append(x); be.append(y); ni.append(imp(n,x)); bi.append(imp(n,y))
    p(f"| {r['round']} | {n:.2f} | {x:.3f} | {ni[-1]:.2f}% | {y:.4f} | {bi[-1]:.2f}% | {r['new_cmp']} | {r['backup_cmp']} |")
p(f"\nNew: median e2e {st.median(ne):.3f} s, improvement {stats(ni)}\n\nBackup: median e2e {st.median(be):.4f} s, improvement {stats(bi)}\n\nPaired difference new - backup (s): " + ", ".join(f"{x-y:+.3f}" for x,y in zip(ne,be)) + f"; mean {st.mean([x-y for x,y in zip(ne,be)]):+.3f} s")
# STEP 8
s8=rows('step8_summary.tsv'); cfg={}
for r in s8: cfg.setdefault(r['config'],[]).append(r)
p("\n### STEP 8: ablation, bzip2, 4 paired rounds each\n")
p("| config | median native CPU | median end-to-end | median paired improvement | min | max | cmp |\n|---|---|---|---|---|---|---|")
for k,rs in cfg.items():
    n=[float(r['cpu']) for r in rs]; e=[float(r['e2e']) for r in rs]; ii=[imp(x,y) for x,y in zip(n,e)]
    p(f"| {k.strip('[]') or 'default'} | {st.median(n):.3f} | {st.median(e):.3f} | {st.median(ii):.2f}% | {min(ii):.2f}% | {max(ii):.2f}% | {sum(1 for r in rs if r['cmp']=='IDENTICAL')}/4 IDENTICAL |")
open(R+'computed_tables.md','w').write("\n".join(out)+"\n")
print("\n".join(out))
# ---- extra tables ----
out2=[]; q=out2.append
q("\n### gcc family timings (from the STEP 3 correctness pairs, -prof_time 2, all optimizations on)\n")
q("| run | native CPU | tool end-to-end | improvement | cmp |\n|---|---|---|---|---|")
for r in rows('step3_summary.tsv'):
    if r['bench']=='bzip2' or 'stress' in r['tag']: continue
    n=float(r['cpu']); e=float(r['e2e']); q(f"| {r['tag']} | {n:.2f} | {e:.3f} | {imp(n,e):.2f}% | {r['cmp']} |")
q("\n### cc1 ablation (2 paired rounds each)\n")
q("| config | native CPU (r1, r2) | end-to-end (r1, r2) | improvement (r1, r2) | mean improvement | cmp |\n|---|---|---|---|---|---|")
cfg={}
for r in rows('cc1_ablation_summary.tsv'): cfg.setdefault(r['config'],[]).append(r)
for k,rs in cfg.items():
    n=[float(r['cpu']) for r in rs]; e=[float(r['e2e']) for r in rs]; ii=[imp(x,y) for x,y in zip(n,e)]
    q(f"| {k.strip('[]') or 'default'} | {n[0]:.2f}, {n[1]:.2f} | {e[0]:.3f}, {e[1]:.3f} | {ii[0]:.2f}%, {ii[1]:.2f}% | {st.mean(ii):.2f}% | {', '.join(r['cmp'] for r in rs)} |")
q("\n### base tool (bprofile-with-gearing.cpp built unchanged), 1 paired round each; it has no end-to-end line, its printed line is used\n")
for t in ('x_base_bzip2','x_base_cc1'):
    line=[l for l in open(R+t+'.txt') if l.startswith('## RESULT')][0].rstrip('\n').split('\t')
    n=float(line[14]); c=float(line[21]); q(f"- {t}: native CPU {n:.2f}, printed 'Translated code run' {c:.3f} -> {imp(n,c):.2f}%, cmp {line[7]}")
open(R+'computed_tables.md','a').write("\n".join(out2)+"\n")
print("\n".join(out2))

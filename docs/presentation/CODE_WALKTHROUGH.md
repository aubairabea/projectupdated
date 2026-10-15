# Code walkthrough

All excerpts are copied from `project.cpp` (the submitted source) with their real line numbers;
`...` marks lines we left out. Function line ranges are computed from the source file.

## 1. Overall flow: main, create_tc, create_tc2_thread_func

Functions: `main()` project.cpp:2186-2195, `create_tc()` project.cpp:2141-2175, `create_tc2_thread_func()` project.cpp:2066-2097

```cpp
 2188      if (PIN_Init(argc, argv)) return Usage();
 2189      PIN_InitSymbols();
 2190      IMG_AddInstrumentFunction(create_tc, 0);
 2191      THREADID tid = PIN_SpawnInternalThread(create_tc2_thread_func, NULL, 0, NULL);
 2192      if (tid == INVALID_THREADID) TOOL_ERROR("failed to spawn the TC2 thread");
 2193      PIN_StartProgramProbed();
 2160      if (analyze_image(img) < 0) { TOOL_ERROR("image analysis failed"); tc_state = -1; return; }
 2161      compute_stack_folds();
 2162      if (allocate_region(text_bytes) < 0) { tc_state = -1; return; }
 2163      if (build_tc() < 0) { TOOL_ERROR("TC build failed; running the original code"); tc_state = -1; return; }
   ...
 2172      commit_tc();
 2173      now(&t_app_start);
 2174      tc_state = 1;
 2071      double want = (double)KnobNumSecsDuringProfile.Value();
 2072      for (;;) {
 2073          struct timespec t;
 2074          now(&t);
 2075          double left = want - tdiff(t_app_start, t);
 2076          if (left <= 0) break;
 2077          sleep_ms(left > 0.02 ? 10 : 1);
 2078      }
   ...
 2080      if (!KnobNoProfile) disable_profiling();
 2081      now(&t_prof_end);
 2082      prof_ended = true;
 2083      double win = tdiff(t_app_start, t_prof_end);
 2084      if (win > want + 0.25) TOOL_WARN("profiling window overshoot: " << win << " s for prof_time " << want);
 2085      derive_profile();
   ...
 2087      if (build_tc2() < 0) { TOOL_ERROR("TC2 build failed; execution stays in TC"); return; }
   ...
 2092          int n = commit_tc2();
```

main() registers create_tc() as the image-load callback, starts our tool thread and hands control to Pin in probe mode. Pin calls create_tc() for every image before the program runs a single instruction; for the main executable it analyses the code, computes the stack-fold rewrite, places the memory region, builds TC and redirects every routine to its TC copy - and only then starts our end-to-end clock (line 2173). The tool thread waits for the profiling window with short, clock-checked sleeps, switches profiling off, derives the profile, builds TC2 and commits it. Without this split the program could start before TC exists, or the window could overshoot badly (we saw sleep() overshoot on our VM).

## 2. Decoding and block formation: analyze_ins, analyze_image

Functions: `analyze_ins()` project.cpp:484-546, `analyze_image()` project.cpp:582-802

```cpp
  495      if (xed_decoded_inst_get_branch_displacement_width(x) > 0) {
  496          o.flags |= IF_HAS_TARG;
  497          o.targ = o.addr + o.size + xed_decoded_inst_get_branch_displacement(x);
   ...
  505      if (cat == XED_CATEGORY_CALL) o.flags |= (o.flags & IF_HAS_TARG) ? IF_CALL : (IF_CALL | IF_IND_CALL);
  506      else if (cat == XED_CATEGORY_RET) o.flags |= IF_RET | IF_NO_FT;
  507      else if (cat == XED_CATEGORY_COND_BR) o.flags |= IF_JCC;
  508      else if (cat == XED_CATEGORY_UNCOND_BR) o.flags |= (o.flags & IF_HAS_TARG) ? (IF_JMP | IF_NO_FT) : (IF_IND_JMP | IF_NO_FT);
   ...
  516          if (rd & STATUS_FLAGS) o.flags |= IF_FLAGS_READ;
  517          else if (xed_simple_flag_get_must_write(fl) && (wr & STATUS_FLAGS) == STATUS_FLAGS) o.flags |= IF_FLAGS_KILL;
   ...
  601      // Every direct branch target in the whole image's code (translated or not), decoded linearly.
  602      // Pin's probe overwrites the first bytes of a routine; its safety check only looks inside the routine,
  603      // but e.g. a function's ".cold" part is entered from its parent a few bytes after its start.
   ...
  612              if (xed_decoded_inst_get_branch_displacement_width(&x) > 0)
  613                  image_targets.insert(a + len + xed_decoded_inst_get_branch_displacement(&x));
   ...
  639              auto it = image_targets.upper_bound(c.a);
  640              if (it != image_targets.end() && *it < c.a + 16) r.no_probe = true;
   ...
  657                  if ((o.flags & IF_HAS_TARG) && !(o.flags & (IF_JCC | IF_JMP | IF_CALL))) { r.translated = false; break; }
  658                  if (ic == XED_ICLASS_JRCXZ || ic == XED_ICLASS_LOOP || ic == XED_ICLASS_LOOPE || ic == XED_ICLASS_LOOPNE ||
  659                      ic == XED_ICLASS_XBEGIN) { r.translated = false; break; }
   ...
  672              else if (r.n_ins && !(ins_v.back().flags & IF_NO_FT)) {
  673                  size_t ci = &c - &cands[0];
  674                  if (ci + 1 >= cands.size() || cands[ci + 1].a != c.e) r.no_probe = true;
   ...
  733              bool leader = (i == r.first_ins) || all_targets.count(o.addr) ||
  734                            (ins_v[i - 1].flags & (IF_JCC | IF_JMP | IF_RET | IF_IND_JMP));
```

analyze_ins() turns one XED-decoded instruction into what the rest of the tool needs: branch target, RIP-relative address, the kind of control flow, whether it reads the status flags or overwrites all six of them, and which registers it reads or fully kills. analyze_image() first decodes the whole image to collect every branch target, because Pin's probe overwrites the first bytes of a routine and other code may jump right into them (lines 601-641). Routines with JRCXZ/LOOP/XBEGIN or a branch into the middle of an instruction stay untranslated, and a routine that runs off its end is translated but not probed (lines 669-675). Blocks start at the routine entry, at every direct branch target and after every jump, so profiling and reordering see the same block structure.

## 3. Heap-safe placement of our memory: allocate_region

Functions: `allocate_region()` project.cpp:817-857

```cpp
  820      tc_cap = text_bytes * 6 + (8 << 20);
  821      tc2_cap = text_bytes * 3 + (8 << 20);
  822      d_sites = (D_CTR + n_counters * 8 + 63) & ~63u;
  823      d_jslots = d_sites + (unsigned)site_v.size() * SITE_SLOTS * 16;
  824      n_jslots_max = 65536;
  825      data_size = (d_jslots + n_jslots_max * 8 + page - 1) & ~(page - 1);
  826      region_size = data_size + ((tc_cap + page - 1) & ~(page - 1)) + ((tc2_cap + page - 1) & ~(page - 1));
   ...
  828      const ADDRINT two_gb = 0x7FF00000ULL;       // keep a margin under 2^31
  829      ADDRINT span = img_high - img_low;
  830      if (span + region_size >= two_gb) { TOOL_ERROR("image + region larger than 2GB"); return -1; }
  831      // Everything in [img_low, region_end) must be reachable with rel32 from everywhere in it.
  832      ADDRINT hi_ok = img_low + two_gb;
  833      ADDRINT gap = 1ULL << 30;                   // ~1 GB above the image: leaves room for brk/heap
  834      if (img_high + gap + region_size > hi_ok) gap = hi_ok - img_high - region_size;
  835      for (int tries = 0; tries < 8 && !region; tries++) {
  836          ADDRINT hint = (img_high + gap + (2 << 20) - 1) & ~((ADDRINT)(2 << 20) - 1);
  837          if (hint + region_size <= hi_ok && try_map(hint, region_size, img_high, hi_ok)) break;
  838          gap /= 2;
  839      }
  840      if (!region) {
  841          // Fallback: the base tool's search (first free address above the code).
  842          TOOL_WARN("region placement ~1GB above the image failed; using the first free address above the code");
  843          for (ADDRINT a = (img_high + page - 1) & ~(page - 1); a + region_size <= hi_ok; a += 16 * page)
  844              if (try_map(a, region_size, img_high, hi_ok)) break;
  845      }
  846      if (!region) { TOOL_ERROR("failed to allocate the TC region within 2GB of the code"); return -1; }
   ...
  851      uint8_t *nib = (uint8_t *)dptr(D_NIB);
  852      for (unsigned b = 0; b < 256; b++) nib[b] = (uint8_t)(((b >> 4) & 15) * 16);
```

All our code and data live in one region: data (counters, target tables, save slots) | TC | TC2. Every rel32 branch and RIP-relative access must reach both the program and the region, so everything has to fit within 2 GB (line 832). The base tool mapped its caches at the first free address above the code - exactly where the heap grows. We aim about 1 GB above the end of the whole image (line 833) and shrink that gap only if needed, with the old search as a logged fallback and an ERROR line if nothing works. The 256-byte table at line 852 is used by the flag-free indirect-target hash.

## 4. Cheap, flag-safe block counters: emit_block_counter, flags_dead_from, find_dead_reg

Functions: `emit_block_counter()` project.cpp:1433-1463, `flags_dead_from()` project.cpp:549-559, `find_dead_reg()` project.cpp:562-575

```cpp
 1436      add_pad(E, skip, orig);
   ...
 1439      if (!KnobNoCheapProf && flags_dead_from(at, end)) {      // flags dead: one add [rip+ctr],1
 1440          xed_inst2(&ei, dstate, XED_ICLASS_ADD, 64, rip_mem(64), xed_simm0(1, 8));
 1441          if (!add_syn(E, &ei, orig, true, caddr)) return false;
   ...
 1445      int r = find_dead_reg(at, end, avoid);
 1446      xed_reg_enum_t R = r >= 0 ? gpr64[r] : XED_REG_RAX;
 1447      if (r < 0) {
 1448          xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, rip_mem(64), xed_reg(XED_REG_RAX));
 1449          if (!add_syn(E, &ei, orig, true, dptr(D_SV_RAX))) return false;
 1450      }
 1451      xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, xed_reg(R), rip_mem(64));
 1452      if (!add_syn(E, &ei, orig, true, caddr)) return false;
 1453      xed_inst2(&ei, dstate, XED_ICLASS_LEA, 64, xed_reg(R), xed_mem_bd(R, xed_disp(1, 8), 64));
 1454      if (!add_syn(E, &ei, orig)) return false;
 1455      xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, rip_mem(64), xed_reg(R));
 1456      if (!add_syn(E, &ei, orig, true, caddr)) return false;
  551      for (int k = i; k < end && k < i + 24; k++) {
  552          const oins_t &o = ins_v[k];
  553          if (o.flags & IF_FLAGS_READ) return false;
  554          if (o.flags & IF_FLAGS_KILL) return true;
  555          if (o.flags & IF_CALL) return true;                 // ABI: flags are not preserved across/into calls
  556          if (o.flags & (IF_JCC | IF_JMP | IF_RET | IF_IND_JMP | IF_SYSCALL)) return false;
  557      }
   ...
  568          uint16_t dead = o.regs_kill & ~read & ~avoid & ~(1 << 4);
   ...
  572          if (o.flags & (IF_CALL | IF_JCC | IF_JMP | IF_RET | IF_IND_JMP | IF_SYSCALL)) return -1;
```

Every counter starts with an 8-byte patch pad (line 1436) so it can be switched off later. If flags_dead_from() proves that the six status flags are overwritten (or a call comes) before anything reads them, the counter is a single 'add qword [rip+ctr], 1'. Otherwise find_dead_reg() looks for a register the program overwrites before reading it and uses mov/lea/mov, which never touch the flags; as a last resort RAX is saved to a slot in our region. We never push: -O0 leaf functions keep locals in the red zone below RSP. Without this analysis a counter could silently change a flag the program is about to test (exactly what broke cc1 under the base tool).

## 5. Indirect jump and call targets, flag-free: emit_indirect_profile

Functions: `emit_indirect_profile()` project.cpp:1527-1560

```cpp
 1529      if (o.site < 0) return true;
 1530      target_op_t t = get_target_op(o);
 1531      if (!t.ok) return true;
 1532      const site_t &s = site_v[o.site];
 1533      int skip = E.new_label();
 1534      add_pad(E, skip, o.addr);
   ...
 1538      SYN2R(XED_ICLASS_MOV, rip_mem(64), xed_reg(XED_REG_RAX), dptr(D_SV_RAX));
 1539      SYN2R(XED_ICLASS_MOV, rip_mem(64), xed_reg(XED_REG_RCX), dptr(D_SV_RCX));
 1540      if (!emit_load_target(E, XED_REG_RAX, t, o.addr)) return false;             // rax = T
 1541      SYN2R(XED_ICLASS_MOV, rip_mem(64), xed_reg(XED_REG_RAX), dptr(D_SV_TMP));    // tmp = T
 1542      xed_inst2(&ei, dstate, XED_ICLASS_MOVZX, 32, xed_reg(XED_REG_ECX), rip_mem(8));
 1543      if (!add_syn(E, &ei, o.addr, true, dptr(D_SV_TMP))) return false;          // ecx = T & 0xff
 1544      SYN2R(XED_ICLASS_LEA, xed_reg(XED_REG_RAX), rip_mem(64), dptr(D_NIB));
 1545      xed_inst2(&ei, dstate, XED_ICLASS_MOVZX, 32, xed_reg(XED_REG_ECX), xed_mem_bisd(XED_REG_RAX, XED_REG_RCX, 1, xed_disp(0, 8), 8));
 1546      if (!add_syn(E, &ei, o.addr)) return false;                                // ecx = slot*16
 1547      SYN2R(XED_ICLASS_LEA, xed_reg(XED_REG_RAX), rip_mem(64), dptr(s.table));
 1548      SYN2(XED_ICLASS_LEA, xed_reg(XED_REG_RAX), xed_mem_bisd(XED_REG_RAX, XED_REG_RCX, 1, xed_disp(0, 8), 64));
 1549      SYN2R(XED_ICLASS_MOV, xed_reg(XED_REG_RCX), rip_mem(64), dptr(D_SV_TMP));
 1550      SYN2(XED_ICLASS_MOV, xed_mem_bd(XED_REG_RAX, xed_disp(0, 8), 64), xed_reg(XED_REG_RCX));
 1551      SYN2(XED_ICLASS_MOV, xed_reg(XED_REG_RCX), xed_mem_bd(XED_REG_RAX, xed_disp(8, 8), 64));
 1552      SYN2(XED_ICLASS_LEA, xed_reg(XED_REG_RCX), xed_mem_bd(XED_REG_RCX, xed_disp(1, 8), 64));
 1553      SYN2(XED_ICLASS_MOV, xed_mem_bd(XED_REG_RAX, xed_disp(8, 8), 64), xed_reg(XED_REG_RCX));
 1554      SYN2R(XED_ICLASS_MOV, xed_reg(XED_REG_RCX), rip_mem(64), dptr(D_SV_RCX));
 1555      SYN2R(XED_ICLASS_MOV, xed_reg(XED_REG_RAX), rip_mem(64), dptr(D_SV_RAX));
```

Every indirect jump and every indirect call gets a 16-slot table of {target, count}. The slot is (target >> 4) & 15, computed without any flag-changing instruction: the target is stored to a scratch slot, its low byte is loaded with MOVZX and translated through the 256-byte table (lines 1541-1546). RAX and RCX are saved to and restored from our region. The base tool used 'AND RAX,3' here, which both changed the flags and put all 16-byte-aligned targets into one slot; that stub is the reason cc1's output was wrong with the base tool.

## 6. Sparse profiling: the decision in analyze_image and derive_profile

Functions: `analyze_image()` project.cpp:582-802, `derive_profile()` project.cpp:1652-1680

```cpp
  774              bool derivable = !KnobNoSparseProf && npred[b] == 1 && spred[b] >= 0 && spred[b] != (int)b &&
  775                               (int)b != r.first_bbl && !r.has_ind_jmp;
  776              if (derivable && !via_ft[b]) {
  777                  B.dpred = spred[b];
  778                  B.dtaken = (ins_v[bbl_v[B.dpred].last].flags & IF_JCC) != 0;
  779              } else B.ctr = n_counters++;
   ...
  785              bool share = !KnobNoSparseProf && fb < (int)bbl_v.size() && bbl_v[fb].rtn == B.rtn && npred[fb] == 1 &&
  786                           via_ft[fb] && !rtn_v[B.rtn].has_ind_jmp && bbl_v[fb].ctr >= 0;
  787              if (share) { B.ft_ctr = bbl_v[fb].ctr; B.ft_own = false; }
  788              else { B.ft_ctr = n_counters++; B.ft_own = true; }
   ...
 1665          for (size_t k = 0; k < bbl_v.size(); k++) {
 1666              bbl_t &b = bbl_v[k];
 1667              if (known[k] || !known[b.dpred]) continue;
 1668              const bbl_t &p = bbl_v[b.dpred];
 1669              UINT64 ft = p.ft > p.count ? p.count : p.ft;
 1670              b.count = b.dtaken ? p.count - ft : p.count;
 1671              known[k] = 1;
 1672              again = true;
 1673          }
```

A block with exactly one predecessor in its routine does not need its own counter. If it is reached only by a fall-through, its counter doubles as the predecessor's fall-through counter (line 787); if it is reached only by a taken branch or a jmp, its count is computed after the window from the predecessor (lines 1667-1670). We only do this in routines without indirect jumps and never for entries or blocks entered from other routines, because there the predecessor set is not fully known. This removed most fall-through stubs from TC.

## 7. Switching live code safely: fill_pad, patch_pad_to_jump, disable_profiling, commit_tc2

Functions: `fill_pad()` project.cpp:230-236, `patch_pad_to_jump()` project.cpp:251-259, `disable_profiling()` project.cpp:1682-1688, `commit_tc2()` project.cpp:2036-2053

```cpp
  232      unsigned k = (4 - (a & 3)) & 3;
  233      if (k == 0) { memcpy(dst, NOPS[8], 8); return; }
  234      memcpy(dst, NOPS[k], k);
  235      memcpy(dst + k, NOPS[8 - k], 8 - k);
   ...
  253      ADDRINT j = pad + ((4 - (pad & 3)) & 3);
  254      INT64 d = (INT64)target - (INT64)(j + 2);
  255      if (d < -128 || d > 127) return false;
  256      uint32_t v = 0xEB | ((uint32_t)(uint8_t)d << 8) | (0xCCu << 16) | (0xCCu << 24);
  257      __atomic_store_n((uint32_t *)j, v, __ATOMIC_SEQ_CST);
   ...
 1684      int fails = 0;
 1685      for (auto &p : prof_pads) if (!patch_pad_to_jump(p.pad, p.target)) fails++;
 1686      if (fails) TOOL_WARN(fails << " profiling stubs could not be disabled (stay active; output unaffected)");
 1687      return fails;
   ...
 2039      for (auto &r : rtn_v) {
 2040          if (!r.in_tc2) continue;
 2041          uint8_t j[5];
 2042          j[0] = 0xE9;
 2043          INT64 d = (INT64)r.tc2_head - (INT64)(r.tc_tramp + 5);
 2044          if (!fits32(d)) { TOOL_ERROR("TC2 commit displacement"); continue; }
 2045          int32_t d32 = (int32_t)d;
 2046          memcpy(j + 1, &d32, 4);
 2047          memcpy((void *)r.tc_tramp, j, 5);
 2048          __atomic_thread_fence(__ATOMIC_SEQ_CST);
 2049          if (!patch_pad_to_jump(r.tc_head, r.tc_tramp)) { TOOL_ERROR("TC2 commit pad patch failed"); continue; }
 2050          n++;
 2051      }
```

The program keeps running while we change its code, so no CPU may ever fetch a half-written instruction. A pad is two NOPs, NOP(k)+NOP(8-k), chosen so that pad+k is 4-byte aligned; switching it is one aligned 4-byte atomic store of 'jmp short' (line 257), which never crosses a cache line. disable_profiling() uses this to jump over every profiling stub. commit_tc2() first writes 'jmp rel32 <TC2 copy>' into the 5-byte trampoline in front of each TC routine head (nothing executes it yet), then flips the head pad to 'jmp short trampoline'.

## 8. A fresh TC2 list: build_tc2

Functions: `build_tc2()` project.cpp:1981-2032

```cpp
 1988      for (auto &b : bbl_v) total += b.count;
 1989      UINT64 cold_thresh = (UINT64)((double)total * KnobColdPpm.Value() / 1e6);
 1990      if (cold_thresh < 1) cold_thresh = 1;
 1991      // Routines that get a TC2 copy: every translated routine that ran during the window.
 1992      vector<int> rts;
 1993      for (size_t ri = 0; ri < rtn_v.size(); ri++) {
 1994          rtn_t &r = rtn_v[ri];
 1995          r.in_tc2 = r.translated && r.hot > 0;
 1996          if (r.in_tc2) rts.push_back((int)ri);
 1997      }
 1998      if (reorder)   // hot routines sorted by hotness
 1999          stable_sort(rts.begin(), rts.end(), [](int a, int b) { return rtn_v[a].hot > rtn_v[b].hot; });
   ...
 2004          if (reorder) reorder_routine_blocks(r, cold_thresh, order, cold);
 2005          else for (int b = r.first_bbl; b < r.first_bbl + r.n_bbl; b++) order.push_back(b);
 2006          E.set_land(r.addr);
 2007          for (size_t k = 0; k < order.size(); k++)
 2008              if (!emit_tc2_block(E, r, order[k], k + 1 < order.size() ? order[k + 1] : -1)) return -1;
   ...
 2010      // One cold region at the end (original order inside each routine).
 2011      n_cold_blocks = cold.size();
 2012      for (size_t k = 0; k < cold.size(); k++) {
 2013          const rtn_t &r = rtn_v[bbl_v[cold[k]].rtn];
 2014          int next = (k + 1 < cold.size() && bbl_v[cold[k + 1]].rtn == bbl_v[cold[k]].rtn) ? cold[k + 1] : -1;
 2015          if (!emit_tc2_block(E, r, cold[k], next)) return -1;
   ...
 2017      if (!layout_and_copy(E, true)) return -1;
 2018      if (!verify_branches(E, true)) return -1;
```

TC2 is built from the original instruction records, not by editing the TC list, so every entry still knows its true original address and every RIP-relative operand is re-derived from it. Only routines that ran during the window get a TC2 copy; hot routines are ordered by hotness, each routine's blocks are laid out by reorder_routine_blocks(), and all cold blocks go to one region at the end. The cold threshold is relative: one part per million of all block executions (line 1989). layout_and_copy() and verify_branches() finish the job.

## 9. De-virtualization: devirt_targets, emit_devirt_call, emit_devirt_jmp

Functions: `devirt_targets()` project.cpp:1736-1769, `emit_devirt_call()` project.cpp:1773-1796, `emit_devirt_jmp()` project.cpp:1801-1832

```cpp
 1749      if (total < KnobDevirtMinCount.Value()) return;
   ...
 1756          if (out.size() >= KnobDevirtMaxTargets.Value()) break;
 1757          if (p.first * 100 < total * KnobDevirtMinShare.Value()) break;
   ...
 1768      if (out.empty() || covered * 100 < total * KnobDevirtMinPct.Value()) out.clear();
   ...
 1780              xed_inst2(&ei, dstate, XED_ICLASS_CMP, 64, xed_reg(t.reg), xed_simm0((xed_int32_t)T, 32));
   ...
 1786          if (!add_branch(E, XED_ICLASS_JNZ, T_LABEL, 0, next, o.addr)) return false;
 1787          if (!add_branch(E, XED_ICLASS_CALL_NEAR, T_ORIG, T, -1, o.addr)) return false;
 1788          if (!add_branch(E, XED_ICLASS_JMP, T_LABEL, 0, done, o.addr)) return false;
 1789          E.bind(next);
 1790      }
 1791      if (!emit_orig(E, o)) return false;
   ...
 1805      xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, rip_mem(64), xed_reg(XED_REG_RCX));
 1806      if (!add_syn(E, &ei, o.addr, true, slot)) return false;
 1807      if (!emit_load_target(E, XED_REG_RCX, t, o.addr)) return false;
   ...
 1810      for (ADDRINT T : tg) {
 1811          INT64 d = prev - (INT64)T;
 1812          if (!fits32(d)) return false;
 1813          xed_inst2(&ei, dstate, XED_ICLASS_LEA, 64, xed_reg(XED_REG_RCX), xed_mem_bd(XED_REG_RCX, xed_disp(d, 32), 64));
 1814          if (!add_syn(E, &ei, o.addr)) return false;
 1815          int h = E.new_label();
 1816          hits.push_back(h);
 1817          if (!add_branch(E, XED_ICLASS_JRCXZ, T_LABEL, 0, h, o.addr, 8)) return false;
   ...
 1820      xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RCX), rip_mem(64));
 1821      if (!add_syn(E, &ei, o.addr, true, slot)) return false;
 1822      if (!emit_orig(E, o)) return false;
```

devirt_targets() applies our threshold: at least 100 executions, at most 3 targets each with at least 10%, together at least 80% of the site. A call site becomes 'cmp operand, T; jnz next; call T'; ...' with the original call as the fallback; CMP is fine there because the calling convention does not keep flags across a call. A jump site may have live flags (cc1), so its guard uses only MOV, LEA and JRCXZ: rcx = target - T, and jrcxz fires when they are equal. RCX is saved to a slot in our region on every path. The original indirect branch always remains, so a wrong guess only costs a compare.

## 10. Code reordering: reorder_routine_blocks, emit_tc2_block, invert_jcc

Functions: `reorder_routine_blocks()` project.cpp:1929-1979, `emit_tc2_block()` project.cpp:1892-1925, `invert_jcc()` project.cpp:1855-1876

```cpp
 1941          if (o.flags & IF_JCC) {
 1942              int tb = block_at(r, o.targ);
 1943              if (tb >= 0 && is_hot[tb - f]) edges.push_back({B.count - B.ft, k, tb - f});
 1944              if (fb >= 0 && is_hot[fb]) edges.push_back({B.ft, k, fb});
   ...
 1952      sort(edges.begin(), edges.end(), [](const edge_t &a, const edge_t &b) {
 1953          if (a.w != b.w) return a.w > b.w;
   ...
 1958      for (auto &e : edges) {
 1959          if (e.w == 0) break;
 1960          if (e.t == 0 || e.s == e.t) continue;           // the entry block stays first
 1961          int cs = chain[e.s], ct = chain[e.t];
 1962          if (cs == ct || tail[cs] != e.s || head[ct] != e.t) continue;
 1963          nxt[e.s] = e.t;
 1964          tail[cs] = tail[ct];
 1965          for (int k = e.t; k >= 0; k = nxt[k]) chain[k] = cs;
 1903      if (o.flags & IF_JCC) {
 1904          int tb = block_at(r, o.targ);
 1905          if (fb >= 0 && next == fb) return emit_orig(E, o);
 1906          xed_iclass_enum_t inv = invert_jcc((xed_iclass_enum_t)o.iclass);
 1907          if (tb >= 0 && next == tb && inv != XED_ICLASS_INVALID) {
 1908              n_inverted++;
 1909              return add_branch(E, inv, T_ORIG, ft_addr, -1, o.addr);
 1910          }
 1911          if (!emit_orig(E, o)) return false;
 1912          n_jmp_added++;
 1913          return add_branch(E, XED_ICLASS_JMP, T_ORIG, ft_addr, -1, o.addr);
 1857      switch (ic) {
 1858      case XED_ICLASS_JB: return XED_ICLASS_JNB;
```

Edges between hot blocks are weighted with the profile (taken = executions - fall-through) and sorted heaviest first. Pettis-Hansen merging joins the chain ending in s with the chain starting in t, so t falls through after s; the entry chain stays first. emit_tc2_block() then emits each block knowing its new successor: keep the jcc, invert it with the link1.txt table when its taken target now follows, add a jmp when a fall-through was broken, and drop a jmp whose target now follows. Every target is an original address that is remapped after layout.

## 11. Stack fold and address fold: compute_stack_folds_rtn, address_fold

Functions: `compute_stack_folds_rtn()` project.cpp:1117-1400, `address_fold()` project.cpp:1043-1115

```cpp
 1124          if (o.iclass == XED_ICLASS_MOV && o.size == 3 && o.bytes[0] == 0x48 && o.bytes[1] == 0x89 && o.bytes[2] == 0xE5) has_frame = true;
   ...
 1129      vector<int> spares = {11, 10, 9, 8, 7, 6, 1};
   ...
 1146                      L = min<INT64>(L, xed_decoded_inst_get_memory_displacement(&x, m));
   ...
 1164          if (a.first >= 0 || a.first + (INT64)a.second > L) { bad.insert(a); continue; }
   ...
 1182      for (size_t k = 0; k < ws.size() && k < spares.size(); k++) slot_sp[ws[k].second] = (int)k;
   ...
 1220          if (o.flags & IF_CALL) return (uint16_t)((out & ~CALLER) | ARGS | gen);
   ...
 1246      auto dead_after = [&](int k, int j) -> bool { return !(lo[k] & (1 << spares[j])); };
   ...
 1257                  case FA_ST_REG: v = (!is_high8(f.reg) && dead) ? v | bit : v & ~bit; break;
   ...
 1261                  case FA_RMW_IMM: case FA_RMW_REG: if (!dead) v &= ~bit; break;  // valid stays valid through the lea sync
   ...
 1269          if (f.clear) v = 0;
   ...
 1277              if (!ext[b]) for (int p : preds[b]) v &= out[p];
   ...
 1316                  case FA_LD:
 1317                      if (valid && !is_high8(f.reg)) {
 1318                          xed_inst2(&ei, dstate, XED_ICLASS_MOV, f.w * 8, xed_reg(f.reg), xed_reg(reg_w(s, f.w)));
   ...
 1364                  case FA_RMW_IMM:
 1365                      need = vt && dead;
 1366                      if (need && fits32(f.imm)) {
 1367                          xed_inst2(&ei, dstate, XED_ICLASS_LEA, 64, xed_reg(gpr64[s]), xed_mem_bd(gpr64[s], xed_disp(f.imm, 32), 64));
 1093          if (!flags_dead_from(i4, rend)) continue;         // the add's flags must be unobservable
 1094          xed_encoder_request_init_from_decode(&x4);
 1095          xed_encoder_request_set_base0(&x4, gpr64[s]);
 1096          xed_encoder_request_set_index(&x4, gpr64[X]);
 1097          xed_encoder_request_set_scale(&x4, 1);
   ...
 1109          z.drop = true;                                    // I2 and I3 disappear
 1110          fold_of[i2] = z;
 1111          fold_of[i3] = z;
```

bzip2 is compiled -O0: every local lives in [rbp-X] and is reloaded for each use. For routines with a standard frame we keep the most-read slots in spare caller-saved registers. A backward liveness pass (line 1220) tells where a spare may be written; a forward must-dataflow (transfer, lines 1246-1269) tells where the spare provably equals its slot on every path. Then loads become register moves, stores stay and sync the spare, and add [slot],imm is followed by a flag-free lea on the spare. address_fold() turns 'mov P,spare; add P,X; op [P]' into 'op [spare+X]'. Address-taken slots, calls, push/pop and RSP stores all invalidate - when unsure we do not fold.

## 12. Layout and self-check: layout_and_copy, verify_branches

Functions: `layout_and_copy()` project.cpp:427-461, `verify_branches()` project.cpp:464-477

```cpp
  430      for (auto &n : E.e) { n.new_addr = a; a += n.size; }
   ...
  435          if (n.rip_off != 0xff) {
  436              INT64 d = (INT64)n.rip_abs - (INT64)(n.new_addr + n.size);
  437              if (!fits32(d)) { TOOL_ERROR("RIP displacement out of range at 0x" << hex << n.orig << dec); return false; }
  438              int32_t d32 = (int32_t)d;
  439              memcpy(n.bytes + n.rip_off, &d32, 4);
   ...
  446              } else if (n.tkind == T_ORIG) t = resolve_orig(E, n.targ, use_tc);
  447              else t = n.targ;
  448              INT64 d = (INT64)t - (INT64)(n.new_addr + n.size);
  449              if (n.disp_sz == 1) {
  450                  if (d < -128 || d > 127) { TOOL_ERROR("rel8 out of range at 0x" << hex << n.orig << dec); return false; }
  451                  n.bytes[n.disp_off] = (uint8_t)(int8_t)d;
  452              } else {
  453                  if (!fits32(d)) { TOOL_ERROR("branch displacement out of range at 0x" << hex << n.orig << dec); return false; }
  454                  int32_t d32 = (int32_t)d;
  455                  memcpy(n.bytes + n.disp_off, &d32, 4);
   ...
  459      for (auto &n : E.e) memcpy((void *)n.new_addr, n.bytes, n.size);
   ...
  470          if (!xdec(&x, (const uint8_t *)n.new_addr, n.size) || xed_decoded_inst_get_length(&x) != n.size) { bad++; continue; }
  471          ADDRINT want = n.tkind == T_LABEL ? E.entry_addr(E.labels[n.label]) : n.tkind == T_ORIG ? resolve_orig(E, n.targ, use_tc) : n.targ;
  472          ADDRINT got = n.new_addr + n.size + xed_decoded_inst_get_branch_displacement(&x);
  473          if (got != want) bad++;
```

Each list is laid out once: addresses are assigned, RIP-relative displacements are recomputed from the absolute address we stored, and branch displacements are resolved from labels, original addresses or absolute targets. Any value that does not fit (rel8 for JRCXZ, rel32 for everything else) is an ERROR, never a silent truncation. After the copy, verify_branches() decodes every branch from memory again and checks that it reaches exactly the intended target - a cheap guard against encoder or layout mistakes.

## 13. The timing report: Fini

Functions: `Fini()` project.cpp:2106-2130

```cpp
 2108      if (fini_done) return;
 2109      fini_done = true;
 2110      now(&t_exit);
 2111      if (KnobDumpProfile) { if (!prof_ended) derive_profile(); dump_profile(); }
 2112      if (tc_state <= 0) { cerr << "[time] no translated code ran" << endl; return; }
 2113      double tc_build = tdiff(t_tc_start, t_app_start);
 2114      double e2e = tdiff(t_app_start, t_exit);
 2115      cerr << fixed << setprecision(3);
 2116      cerr << "[time] TC build (not counted): " << tc_build << " s" << endl;
 2117      if (prof_ended) cerr << "[time] profiling window: " << tdiff(t_app_start, t_prof_end) << " s (prof_time " << KnobNumSecsDuringProfile.Value() << ")" << endl;
 2118      else cerr << "[time] profiling window: program exited before prof_time ended (" << e2e << " s)" << endl;
 2119      if (tc2_built) cerr << "[time] TC2 build: " << tdiff(t_prof_end, t_tc2_built) << " s" << endl;
 2120      if (tc2_committed) {
 2121          cerr << "[time] TC2 commit: " << tdiff(t_tc2_built, t_tc2_committed) << " s" << endl;
 2122          cerr << "[time] TC2 execution: " << tdiff(t_tc2_committed, t_exit) << " s" << endl;
 2123      } else cerr << "[time] TC2 was not committed before exit" << endl;
 2124      cerr << "[time] honest end-to-end total: " << e2e << " s" << endl;
 2125      // Base tool's line, for compatibility only (prof_time + time from the TC2 commit to exit).
 2126      double compat = tc2_committed ? (double)KnobNumSecsDuringProfile.Value() + tdiff(t_tc2_committed, t_exit) : e2e;
 2127      cerr << " Translated code run (including profiling) took: " << compat << " seconds" << endl;
```

Fini() runs from our _exit replacement. It prints the TC build time (before the program started, not counted), the profiling window, TC2 build, commit and execution, and the end-to-end total measured with one monotonic clock from the program's first instruction to _exit. It also prints the base tool's line 'Translated code run (including profiling) took', which is prof_time + TC2 execution; the assignment's requirement concerns that printed time, and it is always slightly smaller than our end-to-end total, so it beats native by at least as much.

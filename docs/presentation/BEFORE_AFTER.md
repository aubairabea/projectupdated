# Before / after: real assembly from our tool's dumps

Every instruction below is copied from a dump produced by our tool; only the `; comments` are added.
Sources (in `dumps/`): `bzip2_dumps.txt.gz` = `-verbose -dump_orig_code -dump_tc -dump_tc2` on
`./bzip2 -c input-long.txt` (output identical to native); `cc1_dumps.txt.gz` = `-verbose -dump_orig_code
-dump_tc2` on `./cc1 200.i` (output identical); `live_pad_*.txt` = bytes read from the running process by
`read_live_pad.py`. Line numbers refer to the uncompressed dump files.

## a. Profiling stubs in TC

### Cheap form: one `add` where the flags are dead (mainGtU entry in TC)

Original (`bzip2_dumps.txt`), lines 1432-1435:
```asm
mainGtU:
0x4020ad: push rbp                                  ; original entry of mainGtU
0x4020ae: mov rbp, rsp
0x4020b1: mov dword ptr [rbp-0x14], edi
```

TC (`bzip2_dumps.txt`), lines 26061-26070:
```asm
0x40689f8a: nop dword ptr [rax+rax*1]               ; trampoline (5 bytes; becomes "jmp TC2" at commit)
0x40689f8f: nop                                     ; head pad, part 1 (NOP(1))
0x40689f90: nop dword ptr [rax]                     ; head pad, part 2 (NOP(7)); the probe from 0x4020ad lands here
0x40689f97: nop                                     ; counter pad, part 1
0x40689f98: nop dword ptr [rax]                     ; counter pad, part 2
0x40689f9f: add qword ptr [rip-0x89a6f], 0x1        ; block counter: flags are dead here -> a single add
0x40689fa7: push rbp                                ; original code continues
0x40689fa8: mov rbp, rsp
0x40689fab: mov dword ptr [rbp-0x14], edi
0x40689fae: mov edi, edi
```

### Save/restore form: RAX is live (return value) and the flags are not provably dead (fallbackQSort3)

Original (`bzip2_dumps.txt`), lines 872-878:
```asm
0x401888: mov edx, dword ptr [rbp-0x44]
0x40188b: mov dword ptr [rbp+rax*4-0x380], edx
0x401892: add dword ptr [rbp-0x14], 0x1             ; i++
0x401896: cmp dword ptr [rbp-0x14], 0x0             ; loop test
0x40189a: jnle 0x4012d8
0x4018a0: leave                                     ; block that only returns
0x4018a1: ret 
```

TC (`bzip2_dumps.txt`), lines 25215-25229:
```asm
0x4068906d: add dword ptr [rbp-0x14], 0x1           ; i++ (unchanged)
0x40689071: nop dword ptr [rax]
0x40689074: nop dword ptr [rax+rax*1]
0x40689079: add qword ptr [rip-0x88d69], 0x1        ; counter of the loop-test block: cheap form, the cmp right after overwrites the flags
0x40689081: cmp dword ptr [rbp-0x14], 0x0
0x40689085: jnle 0x40688907                         ; loop back-edge
0x4068908b: nop                                     ; pad of the next counter
0x4068908c: nop dword ptr [rax]
0x40689093: mov qword ptr [rip-0x8909a], rax        ; save RAX to a slot in our region (never push: red zone)
0x4068909a: mov rax, qword ptr [rip-0x88d81]        ; load the counter
0x406890a1: lea rax, ptr [rax+0x1]                  ; increment with LEA (does not touch the flags)
0x406890a5: mov qword ptr [rip-0x88d8c], rax        ; store the counter
0x406890ac: mov rax, qword ptr [rip-0x890b3]        ; restore RAX (the function result)
0x406890b3: leave 
0x406890b4: ret 
```

### Dead-register form: the counter borrows RBP right before `pop rbp` overwrites it (deregister_tm_clones)

Original (`bzip2_dumps.txt`), lines 232-247:
```asm
deregister_tm_clones:
0x400fd0: mov eax, 0x61ce87
0x400fd5: push rbp
0x400fd6: sub rax, 0x61ce80
0x400fdc: cmp rax, 0xe
0x400fe0: mov rbp, rsp
0x400fe3: jnbe 0x400fe7
0x400fe5: pop rbp                                   ; block: pop rbp; ret
0x400fe6: ret 
0x400fe7: mov eax, 0x0
0x400fec: test rax, rax
0x400fef: jz 0x400fe5
0x400ff1: pop rbp
0x400ff2: mov edi, 0x61ce80
0x400ff7: jmp rax                                   ; indirect jump (profiled, see below)
0x400ff9: nop dword ptr [rax]
```

TC (`bzip2_dumps.txt`), lines 24304-24318:
```asm
0x406880e3: nop
0x406880e4: nop dword ptr [rax]
0x406880eb: mov rbp, qword ptr [rip-0x83362]        ; RBP is dead: the next original instruction is "pop rbp"
0x406880f2: lea rbp, ptr [rbp+0x1]                  ; LEA: flags untouched
0x406880f6: mov qword ptr [rip-0x8336d], rbp
0x406880fd: nop dword ptr [rax]
0x40688100: nop dword ptr [rax+rax*1]
0x40688105: mov rbp, qword ptr [rip-0x87fa4]        ; second counter (block "pop rbp; ret"), again with RBP
0x4068810c: lea rbp, ptr [rbp+0x1]
0x40688110: mov qword ptr [rip-0x87faf], rbp
0x40688117: pop rbp                                 ; original pop rbp overwrites our temporary value
0x40688118: ret 
0x40688119: nop dword ptr [rax]
0x4068811c: nop dword ptr [rax+rax*1]
0x40688121: add qword ptr [rip-0x87fb9], 0x1
```

### Indirect-jump target profile (flag-free hash), same routine

TC (`bzip2_dumps.txt`), lines 24335-24351:
```asm
0x40688174: nop dword ptr [rax+rax*1]               ; pad (switched off after the window)
0x40688179: mov qword ptr [rip-0x88180], rax        ; save RAX
0x40688180: mov qword ptr [rip-0x8817f], rcx        ; save RCX
0x40688187: mov qword ptr [rip-0x8817e], rax        ; store the target T (the jump is "jmp rax")
0x4068818e: movzx ecx, byte ptr [rip-0x88185]       ; low byte of T
0x40688195: lea rax, ptr [rip-0x8815c]              ; 256-byte table: byte -> ((byte>>4)&15)*16
0x4068819c: movzx ecx, byte ptr [rax+rcx*1]         ; slot offset, no flag-changing instruction
0x406881a1: lea rax, ptr [rip-0x826a8]              ; this site's 16-slot table
0x406881a8: lea rax, ptr [rax+rcx*1]
0x406881ad: mov rcx, qword ptr [rip-0x881a4]        ; T
0x406881b4: mov qword ptr [rax], rcx                ; slot.target = T
0x406881b8: mov rcx, qword ptr [rax+0x8]
0x406881bc: lea rcx, ptr [rcx+0x1]                  ; slot.count++ with LEA
0x406881c0: mov qword ptr [rax+0x8], rcx
0x406881c4: mov rcx, qword ptr [rip-0x881c3]        ; restore RCX
0x406881cb: mov rax, qword ptr [rip-0x881d2]        ; restore RAX
0x406881d2: jmp rax                                 ; the original indirect jump
```

## b. Indirect CALL guard in cc1 (htab_traverse calls its callback)

Original (`cc1_dumps.txt`), lines 558377-558384:
```asm
0x62c0c9: cmp qword ptr [rbx], 0x1
0x62c0cd: jbe 0x62c0c0
0x62c0cf: mov rsi, r12
0x62c0d2: mov rdi, rbx
0x62c0d5: call r13                                  ; indirect call of the callback in r13
0x62c0d8: test eax, eax
0x62c0da: jnz 0x62c0c0
0x62c0dc: data16 nop
```

TC2 (`cc1_dumps.txt`), lines 561754-561768:
```asm
0x41efa02e: cmp qword ptr [rbx], 0x1
0x41efa032: jbe 0x41efa021
0x41efa038: mov rsi, r12
0x41efa03b: mov rdi, rbx
0x41efa03e: cmp r13, 0x477ef0                       ; hottest target: cselib_invalidate_mem_1 (0x477ef0)
0x41efa045: jnz 0x41efa055                          ; not equal -> try the next target
0x41efa04b: call 0x41f06f5c                         ; direct call to its TC2 copy
0x41efa050: jmp 0x41efa06f
0x41efa055: cmp r13, 0x4e6700                       ; second target: ggc_htab_delete (0x4e6700)
0x41efa05c: jnz 0x41efa06c
0x41efa062: call 0x41f255cb                         ; direct call to its TC2 copy
0x41efa067: jmp 0x41efa06f
0x41efa06c: call r13                                ; fallback: the original indirect call
0x41efa06f: test eax, eax                           ; all paths continue here
0x41efa071: jnz 0x41efa021
```

CMP is allowed here because flags are not preserved across a call. Target names from `nm cc1`.

## c. Indirect JUMP guard in cc1 (switch in ggc_mark_rtx_children_1)

Original (`cc1_dumps.txt`), lines 232563-232567:
```asm
0x4e5388: lea eax, ptr [r13-0x21]
0x4e538c: cmp ax, 0x25
0x4e5390: jnbe 0x4e5408                             ; out of range -> 0x4e5408
0x4e5392: movzx eax, ax
0x4e5395: jmp qword ptr [rax*8+0x65e788]            ; switch jump through a table
```

TC2 (`cc1_dumps.txt`), lines 564391-564398:
```asm
0x41efce95: movzx eax, ax                           ; unchanged
0x41efce98: mov qword ptr [rip-0x16fce87], rcx      ; save RCX in our region (no push)
0x41efce9f: mov rcx, qword ptr [rax*8+0x65e788]     ; rcx = the real target, read from the same table
0x41efcea7: lea rcx, ptr [rcx-0x4e5408]             ; rcx = target - 0x4e5408; LEA keeps the flags
0x41efceae: jrcxz 0x41efcebe                        ; rcx == 0 -> the profiled hot target
0x41efceb0: mov rcx, qword ptr [rip-0x16fce9f]      ; miss: restore RCX ...
0x41efceb7: jmp qword ptr [rax*8+0x65e788]          ; ... and take the original indirect jump
0x41efcebe: mov rcx, qword ptr [rip-0x16fcead]      ; hit: restore RCX ...
```
```asm
0x41efcec5: jmp 0x41efcdef                                ; ... and jump directly to the TC2 copy of 0x4e5408
```

No instruction in the guard writes the flags, because cc1 can still need them after an indirect jump.

## d. Inverted conditional branch after reordering (mainGtU)

Original (`bzip2_dumps.txt`), lines 1451-1459:
```asm
0x4020e7: movzx eax, byte ptr [rbp-0x5]
0x4020eb: cmp al, byte ptr [rbp-0x6]
0x4020ee: jz 0x4020ff                               ; equal (taken) -> continue comparing at 0x4020ff
0x4020f0: movzx eax, byte ptr [rbp-0x5]             ; not equal (fall-through): compute the result and leave
0x4020f4: cmp al, byte ptr [rbp-0x6]
0x4020f7: setnbe al
0x4020fa: jmp 0x40282b
0x4020ff: add dword ptr [rbp-0x14], 0x1             ; the hot path: i1++
0x402103: add dword ptr [rbp-0x18], 0x1
```

TC2 (`bzip2_dumps.txt`), lines 60189-60193:
```asm
0x40f1303e: movzx r10d, al
0x40f13042: movzx eax, r9b
0x40f13046: cmp al, r10b                            ; same compare (folded to registers, see e.)
0x40f13049: jnz 0x40f13704                          ; INVERTED: jz -> jnz, the not-equal block is now placed elsewhere
0x40f1304f: add dword ptr [rbp-0x14], 0x1           ; the hot block (i1++) now falls through
```

In TC (original order) the same branch is still `jz` (TC, line 26089: `0x40689ff0: jz 0x4068a015`).

## e. Stack fold + address fold (mainGtU)

The -verbose line for this run: `fold mainGtU: 207 loads; [rbp-40]/8->r6 [rbp-32]/8->r11 [rbp-24]/4->r8 [rbp-20]/4->r7 [rbp-10]/2->r1 [rbp-6]/1->r10 [rbp-5]/1->r9`

Original (`bzip2_dumps.txt`), lines 1433-1446:
```asm
0x4020ad: push rbp
0x4020ae: mov rbp, rsp
0x4020b1: mov dword ptr [rbp-0x14], edi             ; store i1
0x4020b4: mov dword ptr [rbp-0x18], esi
0x4020b7: mov qword ptr [rbp-0x20], rdx
0x4020bb: mov qword ptr [rbp-0x28], rcx
0x4020bf: mov dword ptr [rbp-0x2c], r8d
0x4020c3: mov qword ptr [rbp-0x38], r9
0x4020c7: mov edx, dword ptr [rbp-0x14]             ; reload i1
0x4020ca: mov rax, qword ptr [rbp-0x20]             ; reload block
0x4020ce: add rax, rdx                              ; address arithmetic
0x4020d1: movzx eax, byte ptr [rax]                 ; block[i1]
0x4020d4: mov byte ptr [rbp-0x5], al                ; store c1
0x4020d7: mov edx, dword ptr [rbp-0x18]             ; reload i2
```

TC2 (`bzip2_dumps.txt`), lines 60172-60191:
```asm
0x40f13004: mov dword ptr [rbp-0x14], edi           ; store i1 stays ...
0x40f13007: mov edi, edi                            ; ... and syncs the spare (here the slot lives in RDI)
0x40f13009: mov dword ptr [rbp-0x18], esi
0x40f1300c: mov qword ptr [rbp-0x20], rdx
0x40f13010: mov r11, rdx                            ; block also kept in R11
0x40f13013: mov qword ptr [rbp-0x28], rcx
0x40f13017: mov rsi, rcx
0x40f1301a: mov dword ptr [rbp-0x2c], r8d
0x40f1301e: mov qword ptr [rbp-0x38], r9
0x40f13022: mov edx, edi                            ; reload of i1 replaced by a register move
0x40f13024: movzx eax, byte ptr [r11+rdx*1]         ; address fold: mov rax,[rbp-0x20]; add rax,rdx; movzx eax,[rax] -> one instruction
0x40f13029: mov byte ptr [rbp-0x5], al
0x40f1302c: movzx r9d, al                           ; sync c1 into R9
0x40f13030: mov edx, dword ptr [rbp-0x18]
0x40f13033: mov r8d, edx                            ; first load of i2 syncs R8
0x40f13036: movzx eax, byte ptr [r11+rdx*1]
0x40f1303b: mov byte ptr [rbp-0x6], al
0x40f1303e: movzx r10d, al                          ; sync c2 into R10
0x40f13042: movzx eax, r9b                          ; reload of c1 replaced (was movzx eax, byte ptr [rbp-0x5])
0x40f13046: cmp al, r10b                            ; compare with c2 from a register (was cmp al, byte ptr [rbp-0x6])
```

TC2, next block, lines 60193-60198:
```asm
0x40f1304f: add dword ptr [rbp-0x14], 0x1           ; i1++ in memory stays ...
0x40f13053: lea rdi, ptr [rdi+0x1]                  ; ... the spare follows with LEA (flags kept)
0x40f1305a: add dword ptr [rbp-0x18], 0x1           ; i2++
0x40f1305e: lea r8, ptr [r8+0x1]                    ; spare of i2 follows
0x40f13065: mov edx, edi
0x40f13067: movzx eax, byte ptr [r11+rdx*1]
```

## f. Patch pads before and after switching

### mainGtU's routine head in TC: switched to TC2

`dumps/live_pad_before_after.txt` (memory of the running bzip2 process, read twice and disassembled with objdump):
```asm
; during the profiling window (t~1.0 s): bytes at 0x40689fbb: 0f 1f 44 00 00 0f 1f 84 00 00 00 00 00 0f 1f 84 00 00 00 00 00 48 83 05 68 65 f7 ff 01
40689fbb:	0f 1f 44 00 00       	nop    DWORD PTR [rax+rax*1+0x0]
40689fc0:	0f 1f 84 00 00 00 00 	nop    DWORD PTR [rax+rax*1+0x0]
40689fc7:	00
40689fc8:	0f 1f 84 00 00 00 00 	nop    DWORD PTR [rax+rax*1+0x0]
40689fcf:	00
40689fd0:	48 83 05 68 65 f7 ff 	add    QWORD PTR [rip+0xfffffffffff76568],0x1        # 0x40600540
40689fd7:	01
; after the TC2 commit (t~3.2 s): bytes at 0x40689fbb: e9 40 90 88 00 eb f9 cc cc 00 00 00 00 eb 0e cc cc 00 00 00 00 48 83 05 68 65 f7 ff 01
40689fbb:	e9 40 90 88 00       	jmp    0x40f13000
40689fc0:	eb f9                	jmp    0x40689fbb
40689fc2:	cc                   	int3
40689fc3:	cc                   	int3
40689fc4:	00 00                	add    BYTE PTR [rax],al
40689fc6:	00 00                	add    BYTE PTR [rax],al
40689fc8:	eb 0e                	jmp    0x40689fd8
40689fca:	cc                   	int3
40689fcb:	cc                   	int3
40689fcc:	00 00                	add    BYTE PTR [rax],al
40689fce:	00 00                	add    BYTE PTR [rax],al
40689fd0:	48 83 05 68 65 f7 ff 	add    QWORD PTR [rip+0xfffffffffff76568],0x1        # 0x40600540
40689fd7:	01
```

After the commit: the trampoline holds `jmp 0x40f13000` (mainGtU's first instruction in TC2, see e.), the head
pad is `jmp short` back to the trampoline (`eb f9`), and the counter pad jumps over the counter (`eb 0e`).
Each pad changed with one aligned 4-byte store; the `cc cc` filler bytes and the old NOP tail are never
executed.

### A profiling stub pad switched off (NOP(3) + NOP(5) layout)

`dumps/live_pad_profiling_stub.txt` (a `-dump_orig_code` run lays TC out slightly differently, so these addresses are another stub) (memory of the running bzip2 process, read twice and disassembled with objdump):
```asm
; during the profiling window (t~1.0 s): bytes at 0x40689f8a: 00 00 e8 7d b5 00 00 0f 1f 00 0f 1f 44 00 00 48 89 05 60 60 f7 ff 48 8b 05 91 65 f7 ff
40689f8a:	00 00                	add    BYTE PTR [rax],al
40689f8c:	e8 7d b5 00 00       	call   0x4069550e
40689f91:	0f 1f 00             	nop    DWORD PTR [rax]
40689f94:	0f 1f 44 00 00       	nop    DWORD PTR [rax+rax*1+0x0]
40689f99:	48 89 05 60 60 f7 ff 	mov    QWORD PTR [rip+0xfffffffffff76060],rax        # 0x40600000
40689fa0:	48 8b 05 91 65 f7 ff 	mov    rax,QWORD PTR [rip+0xfffffffffff76591]        # 0x40600538
; after the TC2 commit (t~3.2 s): bytes at 0x40689f8a: 00 00 e8 7d b5 00 00 0f 1f 00 eb 23 cc cc 00 48 89 05 60 60 f7 ff 48 8b 05 91 65 f7 ff
40689f8a:	00 00                	add    BYTE PTR [rax],al
40689f8c:	e8 7d b5 00 00       	call   0x4069550e
40689f91:	0f 1f 00             	nop    DWORD PTR [rax]
40689f94:	eb 23                	jmp    0x40689fb9
40689f96:	cc                   	int3
40689f97:	cc                   	int3
40689f98:	00 48 89             	add    BYTE PTR [rax-0x77],cl
40689f9b:	05 60 60 f7 ff       	add    eax,0xfff76060
40689fa0:	48 8b 05 91 65 f7 ff 	mov    rax,QWORD PTR [rip+0xfffffffffff76591]        # 0x40600538
```

The pad's second NOP started at the 4-byte aligned address 0x40689f94; the store wrote `eb 23 cc cc` there,
turning it into `jmp 0x40689fb9` over the save/restore counter that follows.

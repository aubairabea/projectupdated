# Diagrams (Mermaid)

Paste each block into a Mermaid renderer (for example mermaid.live) and export as PNG/SVG for the slides.
Addresses and sizes in the diagrams are from our bzip2 run (`dumps/bzip2_tc_only.txt.gz`, `-verbose` line
`region at 0x40600000 size 0x1159000 image 0x400000-0x41b944`).

## a. Architecture: from the original binary to TC2

```mermaid
flowchart LR
    subgraph IMG["Original binary (bzip2, cc1, ...)"]
        R1["routine entry<br/>(first bytes overwritten<br/>by a Pin probe)"]
    end
    subgraph TC["TC: translated copy + profiling"]
        T1["5-byte trampoline<br/>+ 8-byte head pad"]
        T2["block counters,<br/>fall-through counters,<br/>indirect-target tables"]
    end
    subgraph THR["Our tool thread"]
        W["wait -prof_time s<br/>(clock-checked)"] --> D["disable_profiling():<br/>one 4-byte store per pad"]
        D --> P["derive_profile()"]
        P --> B["build_tc2():<br/>reorder + devirt + stack fold"]
        B --> C["commit_tc2():<br/>trampoline := jmp TC2,<br/>head pad := jmp short trampoline"]
    end
    subgraph TC2["TC2: optimized copy, no profiling"]
        X["hot routines,<br/>hot blocks first,<br/>cold region at the end"]
    end
    R1 -- "RTN_ReplaceProbed<br/>(commit_tc)" --> T1
    T1 --> T2
    T2 -. "counts" .-> W
    C -- "patch pad switch" --> T1
    T1 == "after the commit" ==> X
```

## b. Memory layout

```mermaid
flowchart TB
    subgraph AS["Process address space (bzip2 run)"]
        direction TB
        IM["Executable image<br/>0x400000 - 0x41b944"]
        HP["Heap (brk) grows upwards<br/>right after the image"]
        GAP["free gap (~1 GB) left for the heap"]
        subgraph REG["Our region at 0x40600000 (size 0x1159000)"]
            direction LR
            DA["data:<br/>save slots, 256-byte hash table,<br/>counters, indirect-target tables,<br/>jump slots"]
            TCB["TC<br/>(profiled copy)"]
            TC2B["TC2<br/>(optimized copy)"]
            DA --- TCB --- TC2B
        end
        IM --> HP --> GAP --> REG
    end
    NOTE["Everything within 2 GB of the image:<br/>rel32 branches and RIP-relative accesses<br/>reach code and data in both directions"]
    REG -.- NOTE
```

## c. A hot indirect jump in TC2 (guard hit / miss)

```mermaid
flowchart TD
    A["mov [slot], rcx<br/>(save RCX in our region)"] --> B["mov rcx, operand<br/>(the real target)"]
    B --> C["lea rcx, [rcx - T1]<br/>(flags untouched)"]
    C --> D{"jrcxz:<br/>rcx == 0 ?"}
    D -- "yes: target == T1" --> H1["mov rcx, [slot]<br/>jmp T1' (TC2 copy)"]
    D -- "no" --> E["lea rcx, [rcx + T1 - T2]"]
    E --> F{"jrcxz:<br/>rcx == 0 ?"}
    F -- "yes: target == T2" --> H2["mov rcx, [slot]<br/>jmp T2' (TC2 copy)"]
    F -- "no" --> M["mov rcx, [slot]<br/>original indirect jmp<br/>(always correct)"]
```

## d. Pettis-Hansen chain merging (README example)

A ends with `jl C` (taken 990, fall-through to B 10); B ends with `jmp D` (10); C falls into D (990).

```mermaid
flowchart LR
    subgraph S0["Start: one chain per block"]
        a0[A] ~~~ b0[B] ~~~ c0[C] ~~~ d0[D]
    end
    subgraph S1["1. A->C (990): A ends [A], C starts [C]"]
        a1[A] --> c1[C]
        b1[B] ~~~ d1[D]
    end
    subgraph S2["2. C->D (990): join"]
        a2[A] --> c2[C] --> d2[D]
        b2[B]
    end
    subgraph S3["3. A->B, B->D (10): skipped<br/>(A not a chain end, D not a chain start)"]
        a3[A] --> c3[C] --> d3[D] --> b3[B]
    end
    S0 --> S1 --> S2 --> S3
```

Final layout `A C D B`: A's `jl C` becomes `jnl B` (inverted, see BEFORE_AFTER d), C falls into D, B keeps `jmp D`.

## e. Stack fold: the dataflow idea

```mermaid
flowchart LR
    S["store<br/>mov [rbp-0x14], edi"] --> Y["sync<br/>mov spare, edi<br/>(spare valid)"]
    Y --> L1["later load<br/>mov edx, [rbp-0x14]"]
    L1 -- "spare valid on ALL paths<br/>(forward must-dataflow)" --> R1["mov edx, spare<br/>(no memory load)"]
    Y --> U["add [rbp-0x14], 1"] --> U2["lea spare, [spare+1]<br/>(still valid, flags kept)"]
    Y --> K["call / push / pop / syscall /<br/>RSP store / RBP write /<br/>program writes the spare"]
    K --> INV["spare invalid:<br/>next load stays a memory load<br/>(and re-syncs the spare)"]
    LV["backward liveness:<br/>spare written only where the<br/>program's value is dead"] -.-> Y
```

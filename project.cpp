/*BEGIN_LEGAL
Intel Open Source License
Copyright (c) 2002-2011 Intel Corporation. All rights reserved.
(The translation framework follows the course's bprofile-with-gearing.cpp, distributed under this license.)
END_LEGAL */
/* ===================================================================== */
/*! @file project.cpp
 *
 * Probe-mode pintool "project.so": profile in TC, then re-optimize into TC2 ("gearing").
 *
 * BASED ON
 *  - Exercise 4 (hw4.cpp): per basic block execution counters, fall-through counters after
 *    conditional branches (taken = exec - fallthru), per indirect jump target counters, dead
 *    register analysis for cheaper stubs, skipping untranslatable routines, atomic disabling of
 *    profiling stubs, and the edge-profile CSV dump sorted hottest first.
 *  - bprofile-with-gearing.cpp (course): the TC / TC2 flow (translate every routine into an
 *    instruction list, fix RIP-relative and branch displacements, copy into the cache, commit with
 *    RTN_ReplaceProbed; a tool thread waits -prof_time seconds, builds TC2 and switches every TC
 *    routine head to its TC2 copy; Fini on _exit prints the elapsed time).
 *
 * Mapping hw4 feature -> function here:
 *   BBL exec counter            -> emit_block_counter()
 *   fall-through counter        -> emit_block_counter() on the fall-through path of a jcc
 *   taken = exec - fallthru     -> derive_profile()
 *   indirect jump target table  -> emit_indirect_profile()   (now jumps AND calls, flag-free)
 *   insKillsFullReg / dead RAX  -> analyze_ins() + find_dead_reg() / flags_dead_from()
 *   skip untranslatable rtns    -> analyze_image() (mid-instruction targets, JRCXZ/LOOP, ...)
 *   atomic stub disabling       -> patch pads, disable_profiling()
 *   sorted edge-profile dump    -> dump_profile() (-dump_prof)
 */
/* ===================================================================== */

#include "pin.H"
extern "C" {
#include "xed-interface.h"
}
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>

using namespace std;

/*======================================================================*/
/* commandline switches                                                 */
/*======================================================================*/
KNOB<BOOL> KnobVerbose(KNOB_MODE_WRITEONCE, "pintool", "verbose", "0", "Verbose run");
KNOB<BOOL> KnobDumpOrigCode(KNOB_MODE_WRITEONCE, "pintool", "dump_orig_code", "0", "Dump Original non-translated Code");
KNOB<BOOL> KnobDumpTranslatedCode(KNOB_MODE_WRITEONCE, "pintool", "dump_tc", "0", "Dump Translated Code");
KNOB<BOOL> KnobDumpTranslatedCode2(KNOB_MODE_WRITEONCE, "pintool", "dump_tc2", "0", "Dump 2nd Translated Code");
KNOB<BOOL> KnobDoNotCommitTranslatedCode(KNOB_MODE_WRITEONCE, "pintool", "no_tc_commit", "0", "Do not commit translated code");
KNOB<UINT> KnobNumSecsDuringProfile(KNOB_MODE_WRITEONCE, "pintool", "prof_time", "2", "Number of seconds for collecting BBL counters");
KNOB<BOOL> KnobDumpProfile(KNOB_MODE_WRITEONCE, "pintool", "dump_prof", "0", "Dump profiling information (edge-profile.csv)");
KNOB<BOOL> KnobNoProfile(KNOB_MODE_WRITEONCE, "pintool", "no_prof", "0", "Do not collect profile information");
KNOB<BOOL> KnobNoTC2(KNOB_MODE_WRITEONCE, "pintool", "no_tc2", "0", "Do not build TC2 (stay in TC)");
KNOB<INT32> KnobDbgFirst(KNOB_MODE_WRITEONCE, "pintool", "dbg_first", "0", "Debug: translate only candidate routines with index >= this");
KNOB<INT32> KnobDbgCount(KNOB_MODE_WRITEONCE, "pintool", "dbg_count", "-1", "Debug: translate at most this many candidate routines (-1: all)");
KNOB<BOOL> KnobNoCheapProf(KNOB_MODE_WRITEONCE, "pintool", "no_cheap_prof", "0",
    "Disable the cheap counter form (add [rip+ctr],1 placed where the flags are dead)");
KNOB<BOOL> KnobNoSparseProf(KNOB_MODE_WRITEONCE, "pintool", "no_sparse_prof", "0",
    "Disable sparse profiling (counters derived from a single predecessor)");
KNOB<BOOL> KnobNoDevirt(KNOB_MODE_WRITEONCE, "pintool", "no_devirt", "0", "Disable de-virtualization in TC2");
KNOB<UINT64> KnobDevirtMinCount(KNOB_MODE_WRITEONCE, "pintool", "devirt_min_count", "100",
    "De-virtualize a site only if it executed at least this many times during the profile window");
KNOB<UINT> KnobDevirtMinPct(KNOB_MODE_WRITEONCE, "pintool", "devirt_min_pct", "80",
    "De-virtualize only if the guarded targets cover at least this percent of the site's executions");
KNOB<UINT> KnobDevirtMinShare(KNOB_MODE_WRITEONCE, "pintool", "devirt_min_share", "10",
    "A target is guarded only if it has at least this percent of the site's executions");
KNOB<UINT> KnobDevirtMaxTargets(KNOB_MODE_WRITEONCE, "pintool", "devirt_max_targets", "3", "Maximum guarded targets per site");
KNOB<BOOL> KnobNoReorder(KNOB_MODE_WRITEONCE, "pintool", "no_reorder", "0", "Disable code reordering in TC2");
KNOB<UINT> KnobColdPpm(KNOB_MODE_WRITEONCE, "pintool", "cold_ppm", "1",
    "A block is rarely executed (cold) if count < cold_ppm/1e6 of the total block count (and always if 0)");

/* ===================================================================== */
/* Global state                                                          */
/* ===================================================================== */
static xed_state_t dstate = {XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b};
const unsigned max_inst_len = XED_MAX_INSTRUCTION_BYTES;

static unsigned n_errors = 0;
#define TOOL_ERROR(msg) do { n_errors++; cerr << "ERROR: " << msg << endl; } while (0)
#define TOOL_WARN(msg) do { cerr << "WARNING: " << msg << endl; } while (0)

// Register numbering used by the liveness masks: RAX=0 RCX=1 RDX=2 RBX=3 RSP=4 RBP=5 RSI=6 RDI=7 R8..R15=8..15
static const xed_reg_enum_t gpr64[16] = {
    XED_REG_RAX, XED_REG_RCX, XED_REG_RDX, XED_REG_RBX, XED_REG_RSP, XED_REG_RBP, XED_REG_RSI, XED_REG_RDI,
    XED_REG_R8,  XED_REG_R9,  XED_REG_R10, XED_REG_R11, XED_REG_R12, XED_REG_R13, XED_REG_R14, XED_REG_R15};

static int gpr_index(xed_reg_enum_t r)
{
    if (r == XED_REG_INVALID) return -1;
    xed_reg_enum_t f = xed_get_largest_enclosing_register(r);
    for (int i = 0; i < 16; i++) if (gpr64[i] == f) return i;
    return -1;
}

/* ---------------- the original program ---------------- */
enum {
    IF_HAS_TARG = 1 << 0,   // direct branch/call with a target (explicit flag: target 0 is legal, P7)
    IF_HAS_RIP = 1 << 1,    // RIP-relative memory operand (explicit flag, P7)
    IF_CALL = 1 << 2,
    IF_RET = 1 << 3,
    IF_JCC = 1 << 4,
    IF_JMP = 1 << 5,        // direct unconditional jump
    IF_IND_JMP = 1 << 6,
    IF_IND_CALL = 1 << 7,
    IF_LEADER = 1 << 8,
    IF_NO_FT = 1 << 9,      // never falls through (jmp, ret, indirect jmp)
    IF_FLAGS_READ = 1 << 10,  // reads a status flag
    IF_FLAGS_KILL = 1 << 11,  // always writes all 6 status flags without reading them
    IF_SYSCALL = 1 << 12,
};

struct oins_t {
    ADDRINT addr;
    ADDRINT targ;        // valid if IF_HAS_TARG
    ADDRINT rip_addr;    // valid if IF_HAS_RIP
    uint32_t flags;
    uint16_t iclass;
    uint16_t regs_read;  // GPR read mask (explicit + implicit + address registers)
    uint16_t regs_kill;  // GPRs fully (32/64-bit) written without being read
    uint16_t regs_touch; // GPRs referenced in any way (read, written, partially written)
    uint8_t size;
    uint8_t imm_bytes;
    uint8_t bytes[15];
    int bbl;
    int site;            // indirect profiling site, -1 if none
};

struct bbl_t {
    int rtn;
    int first, last;     // instruction indices (inclusive)
    int ctr;             // counter index (-1: none)
    int ft_ctr;          // fall-through counter index for a jcc-terminated block (-1: none)
    bool ft_own;         // ft_ctr is a counter of its own (else it is the fall-through block's counter)
    int dpred;           // sparse profiling: count derived from this single predecessor block (-1: none)
    bool dtaken;         // derived via the predecessor's jcc taken edge (else via its jmp)
    UINT64 count, ft;    // profile after the window
};

struct rtn_t {
    ADDRINT addr, end;
    string name;
    int first_ins, n_ins;
    int first_bbl, n_bbl;
    bool translated;
    bool has_ind_jmp;
    bool no_probe;       // some original branch targets its first bytes: do not overwrite them
    uint16_t fold_spares; // registers used by the stack fold in this routine (TC stubs must not use them)
    ADDRINT tc_tramp, tc_head;   // TC addresses of the 5-byte trampoline and the 8-byte head pad
    bool in_tc2;
    ADDRINT tc2_head;
    UINT64 hot;
};

#define SITE_SLOTS 16
struct site_t {
    int ins;
    bool is_call;
    unsigned table;      // byte offset of its 16 x {target,count} table inside the data area
};

static vector<oins_t> ins_v;
static vector<bbl_t> bbl_v;
static vector<rtn_t> rtn_v;
static vector<site_t> site_v;
static std::unordered_map<ADDRINT, int> ins_index;   // original address -> translated instruction
static unsigned n_counters = 0;
static ADDRINT img_low = 0, img_high = 0;
static ADDRINT exit_rtn_addr = 0;

/* ---------------- the region: data | TC | TC2 ---------------- */
static char *region = nullptr;
static size_t region_size = 0;
static char *data_area = nullptr;
static size_t data_size = 0;
static char *tc = nullptr, *tc2 = nullptr;
static size_t tc_cap = 0, tc2_cap = 0, tc_size = 0, tc2_size = 0;

// data area layout (byte offsets)
enum { D_SV_RAX = 0, D_SV_RCX = 8, D_SV_TMP = 16, D_SV_GUARD = 24, D_NIB = 64, D_CTR = 320 };
static unsigned d_sites = 0, d_jslots = 0, n_jslots_max = 0, n_jslots = 0;
static std::unordered_map<ADDRINT, unsigned> jslot_of;   // orig target -> slot (P3: real lookup)

static inline ADDRINT dptr(unsigned off) { return (ADDRINT)data_area + off; }
static inline UINT64 *counters() { return (UINT64 *)(data_area + D_CTR); }

/* ---------------- timing ---------------- */
static struct timespec t_tc_start, t_app_start, t_prof_end, t_tc2_built, t_tc2_committed, t_exit;
static volatile int tc_state = 0;          // 0 pending, 1 TC running, -1 no TC
static volatile bool prof_ended = false, tc2_built = false, tc2_committed = false;

static double tdiff(const struct timespec &a, const struct timespec &b)
{
    return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}
static void now(struct timespec *t) { clock_gettime(CLOCK_MONOTONIC, t); }

/* ===================================================================== */
/* Patch pads (P4)                                                        */
/* ===================================================================== */
// Canonical multi-byte NOPs.
static const uint8_t NOPS[9][9] = {
    {0},
    {0x90},
    {0x66, 0x90},
    {0x0F, 0x1F, 0x00},
    {0x0F, 0x1F, 0x40, 0x00},
    {0x0F, 0x1F, 0x44, 0x00, 0x00},
    {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00},
    {0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00},
    {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00}};
static bool pads_ok = true;

// An 8-byte pad at address A is NOP(k) + NOP(8-k) with A+k 4-byte aligned. Turning it into a jump is
// one aligned 4-byte store at A+k (never crosses a cache line), so another thread either sees the
// old NOP or the whole new jump.
static void fill_pad(uint8_t *dst, ADDRINT a)
{
    unsigned k = (4 - (a & 3)) & 3;
    if (k == 0) { memcpy(dst, NOPS[8], 8); return; }
    memcpy(dst, NOPS[k], k);
    memcpy(dst + k, NOPS[8 - k], 8 - k);
}

static bool check_nops()
{
    for (unsigned n = 1; n <= 8; n++) {
        xed_decoded_inst_t x;
        xed_decoded_inst_zero_set_mode(&x, &dstate);
        if (xed_decode(&x, NOPS[n], n) != XED_ERROR_NONE || xed_decoded_inst_get_length(&x) != n) return false;
        xed_category_enum_t c = xed_decoded_inst_get_category(&x);
        if (c != XED_CATEGORY_NOP && c != XED_CATEGORY_WIDENOP) return false;
    }
    return true;
}

// Turn the pad at 'pad' into "jmp short target" with one aligned 4-byte store.
static bool patch_pad_to_jump(ADDRINT pad, ADDRINT target)
{
    ADDRINT j = pad + ((4 - (pad & 3)) & 3);
    INT64 d = (INT64)target - (INT64)(j + 2);
    if (d < -128 || d > 127) return false;
    uint32_t v = 0xEB | ((uint32_t)(uint8_t)d << 8) | (0xCCu << 16) | (0xCCu << 24);
    __atomic_store_n((uint32_t *)j, v, __ATOMIC_SEQ_CST);
    return true;
}

/* ===================================================================== */
/* XED helpers                                                            */
/* ===================================================================== */
static bool xenc(xed_encoder_instruction_t *ei, uint8_t *out, unsigned *len)
{
    xed_encoder_request_t req;
    xed_encoder_request_zero_set_mode(&req, &dstate);
    if (!xed_convert_to_encoder_request(&req, ei)) return false;
    return xed_encode(&req, out, XED_MAX_INSTRUCTION_BYTES, len) == XED_ERROR_NONE;
}

static bool xdec(xed_decoded_inst_t *x, const uint8_t *p, unsigned n = XED_MAX_INSTRUCTION_BYTES)
{
    xed_decoded_inst_zero_set_mode(x, &dstate);
    return xed_decode(x, p, n) == XED_ERROR_NONE;
}

/* ===================================================================== */
/* Debug dump routines (as in the base tool)                             */
/* ===================================================================== */
void dump_instr_from_xedd(xed_decoded_inst_t *xedd, ADDRINT address)
{
    char buf[2048];
    xed_format_context(XED_SYNTAX_INTEL, xedd, buf, sizeof(buf), (UINT64)address, 0, 0);
    cerr << hex << address << ": " << buf << dec << endl;
}

void dump_instr_from_mem(ADDRINT *address, ADDRINT new_addr)
{
    xed_decoded_inst_t x;
    if (!xdec(&x, (const uint8_t *)address)) { cerr << "invalid opcode" << endl; return; }
    char buf[2048];
    xed_format_context(XED_SYNTAX_INTEL, &x, buf, sizeof(buf), (UINT64)new_addr, 0, 0);
    cerr << "0x" << hex << new_addr << ": " << buf << dec << endl;
}

void dump_tc(char *base, size_t size)
{
    ADDRINT a = (ADDRINT)base, end = (ADDRINT)base + size;
    while (a < end) {
        xed_decoded_inst_t x;
        if (!xdec(&x, (const uint8_t *)a)) { cerr << "0x" << hex << a << ": (bad)" << dec << endl; a++; continue; }
        char buf[2048];
        xed_format_context(XED_SYNTAX_INTEL, &x, buf, sizeof(buf), (UINT64)a, 0, 0);
        cerr << "0x" << hex << a << ": " << buf << dec << endl;
        a += xed_decoded_inst_get_length(&x);
    }
}

void dump_image_instrs(IMG img)
{
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            RTN_Open(rtn);
            cerr << RTN_Name(rtn) << ":" << endl;
            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
                cerr << "0x" << hex << INS_Address(ins) << ": " << INS_Disassemble(ins) << dec << endl;
            RTN_Close(rtn);
            cerr << endl;
        }
    }
}

/* ===================================================================== */
/* Emitter: a fresh instruction list, laid out and fixed up (TC and TC2)  */
/* ===================================================================== */
enum { T_NONE = 0, T_ORIG, T_LABEL, T_ABS };
enum { K_INS = 0, K_SYN, K_PAD, K_TRAMP };

struct ent_t {
    ADDRINT orig;        // true original address of the instruction this entry belongs to (0: none)
    ADDRINT new_addr;
    ADDRINT targ;        // T_ORIG: original address, T_ABS: absolute address
    ADDRINT rip_abs;     // absolute address of the RIP-relative operand (if rip_off != 0xff)
    int label;           // T_LABEL target / K_PAD skip label
    uint8_t bytes[15];
    uint8_t size;
    uint8_t tkind;
    uint8_t disp_off, disp_sz;
    uint8_t rip_off;
    uint8_t kind;
};

struct emitter_t {
    vector<ent_t> e;
    vector<int> labels;
    std::unordered_map<ADDRINT, int> land;   // original address -> landing entry
    ADDRINT base = 0;
    size_t cap = 0;
    size_t size = 0;

    int new_label() { labels.push_back(-1); return (int)labels.size() - 1; }
    void bind(int l) { labels[l] = (int)e.size(); }
    ADDRINT entry_addr(int idx) const { return idx < (int)e.size() ? e[idx].new_addr : base + size; }
    ent_t &add() { e.push_back(ent_t()); ent_t &n = e.back(); memset(&n, 0, sizeof(n)); n.rip_off = 0xff; n.label = -1; return n; }
    void set_land(ADDRINT a) { if (!land.count(a)) land[a] = (int)e.size(); }
};

// TC landing address of every translated original instruction (filled after the TC layout).
static std::unordered_map<ADDRINT, ADDRINT> tc_land;
static std::unordered_map<ADDRINT, ADDRINT> tc2_land;

static bool add_syn(emitter_t &E, xed_encoder_instruction_t *ei, ADDRINT orig, bool rip = false, ADDRINT rip_abs = 0)
{
    uint8_t buf[16];
    unsigned len = 0;
    if (!xenc(ei, buf, &len)) { TOOL_ERROR("encode failed (synthetic instruction)"); return false; }
    ent_t &n = E.add();
    memcpy(n.bytes, buf, len);
    n.size = len;
    n.orig = orig;
    n.kind = K_SYN;
    if (rip) {
        xed_decoded_inst_t x;
        if (!xdec(&x, buf, len)) { TOOL_ERROR("decode failed (synthetic)"); return false; }
        n.rip_off = len - xed_decoded_inst_get_immediate_width(&x) - 4;
        n.rip_abs = rip_abs;
    }
    return true;
}

// A direct branch/call with a rel32 (or rel8) displacement fixed at layout time.
static bool add_branch(emitter_t &E, xed_iclass_enum_t ic, int tkind, ADDRINT targ, int label, ADDRINT orig, unsigned width = 32)
{
    xed_encoder_instruction_t ei;
    xed_inst1(&ei, dstate, ic, 64, xed_relbr(0, width));
    uint8_t buf[16];
    unsigned len = 0;
    if (!xenc(&ei, buf, &len)) { TOOL_ERROR("encode failed (branch)"); return false; }
    ent_t &n = E.add();
    memcpy(n.bytes, buf, len);
    n.size = len;
    n.orig = orig;
    n.kind = K_SYN;
    n.tkind = tkind;
    n.targ = targ;
    n.label = label;
    n.disp_sz = width / 8;
    n.disp_off = len - n.disp_sz;
    return true;
}

static void add_pad(emitter_t &E, int skip_label, ADDRINT orig)
{
    ent_t &n = E.add();
    n.kind = K_PAD;
    n.size = 8;
    n.label = skip_label;
    n.orig = orig;
}

// Resolve a T_ORIG target: this list first, then TC2 (when building TC2... not used), then TC, then original.
static ADDRINT resolve_orig(const emitter_t &E, ADDRINT t, bool use_tc)
{
    auto it = E.land.find(t);
    if (it != E.land.end()) return E.entry_addr(it->second);
    if (use_tc) {
        auto j = tc_land.find(t);
        if (j != tc_land.end()) return j->second;
    }
    return t;
}

static bool fits32(INT64 d) { return d >= (INT64)INT32_MIN && d <= (INT64)INT32_MAX; }

// Lay out the list at E.base, fix every displacement, copy into memory.
static bool layout_and_copy(emitter_t &E, bool use_tc)
{
    ADDRINT a = E.base;
    for (auto &n : E.e) { n.new_addr = a; a += n.size; }
    E.size = a - E.base;
    if (E.size > E.cap) { TOOL_ERROR("translation cache overflow (" << E.size << " > " << E.cap << ")"); return false; }
    for (auto &n : E.e) {
        if (n.kind == K_PAD) { fill_pad(n.bytes, n.new_addr); continue; }
        if (n.rip_off != 0xff) {
            INT64 d = (INT64)n.rip_abs - (INT64)(n.new_addr + n.size);
            if (!fits32(d)) { TOOL_ERROR("RIP displacement out of range at 0x" << hex << n.orig << dec); return false; }
            int32_t d32 = (int32_t)d;
            memcpy(n.bytes + n.rip_off, &d32, 4);
        }
        if (n.tkind != T_NONE) {
            ADDRINT t = 0;
            if (n.tkind == T_LABEL) {
                if (n.label < 0 || E.labels[n.label] < 0) { TOOL_ERROR("unbound label"); return false; }
                t = E.entry_addr(E.labels[n.label]);
            } else if (n.tkind == T_ORIG) t = resolve_orig(E, n.targ, use_tc);
            else t = n.targ;
            INT64 d = (INT64)t - (INT64)(n.new_addr + n.size);
            if (n.disp_sz == 1) {
                if (d < -128 || d > 127) { TOOL_ERROR("rel8 out of range at 0x" << hex << n.orig << dec); return false; }
                n.bytes[n.disp_off] = (uint8_t)(int8_t)d;
            } else {
                if (!fits32(d)) { TOOL_ERROR("branch displacement out of range at 0x" << hex << n.orig << dec); return false; }
                int32_t d32 = (int32_t)d;
                memcpy(n.bytes + n.disp_off, &d32, 4);
            }
        }
    }
    for (auto &n : E.e) memcpy((void *)n.new_addr, n.bytes, n.size);
    return true;
}

// Self-check: decode every branch of the list in memory and compare its target with the intended one.
static bool verify_branches(const emitter_t &E, bool use_tc)
{
    unsigned bad = 0;
    for (auto &n : E.e) {
        if (n.tkind == T_NONE) continue;
        xed_decoded_inst_t x;
        if (!xdec(&x, (const uint8_t *)n.new_addr, n.size) || xed_decoded_inst_get_length(&x) != n.size) { bad++; continue; }
        ADDRINT want = n.tkind == T_LABEL ? E.entry_addr(E.labels[n.label]) : n.tkind == T_ORIG ? resolve_orig(E, n.targ, use_tc) : n.targ;
        ADDRINT got = n.new_addr + n.size + xed_decoded_inst_get_branch_displacement(&x);
        if (got != want) bad++;
    }
    if (bad) TOOL_ERROR("self-check: " << bad << " branches decode to a wrong target");
    return bad == 0;
}

/* ===================================================================== */
/* Instruction analysis                                                   */
/* ===================================================================== */
static const uint32_t STATUS_FLAGS = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 11);

static bool analyze_ins(oins_t &o, const xed_decoded_inst_t *x)
{
    xed_iclass_enum_t ic = xed_decoded_inst_get_iclass(x);
    xed_category_enum_t cat = xed_decoded_inst_get_category(x);
    o.iclass = ic;
    o.size = xed_decoded_inst_get_length(x);
    o.imm_bytes = xed_decoded_inst_get_immediate_width(x);
    o.flags = 0;
    o.site = -1;
    o.bbl = -1;

    if (xed_decoded_inst_get_branch_displacement_width(x) > 0) {
        o.flags |= IF_HAS_TARG;
        o.targ = o.addr + o.size + xed_decoded_inst_get_branch_displacement(x);
    }
    for (unsigned m = 0; m < 2; m++) {
        if (xed_decoded_inst_get_base_reg(x, m) == XED_REG_RIP) {
            o.flags |= IF_HAS_RIP;
            o.rip_addr = o.addr + o.size + xed_decoded_inst_get_memory_displacement(x, m);
        }
    }
    if (cat == XED_CATEGORY_CALL) o.flags |= (o.flags & IF_HAS_TARG) ? IF_CALL : (IF_CALL | IF_IND_CALL);
    else if (cat == XED_CATEGORY_RET) o.flags |= IF_RET | IF_NO_FT;
    else if (cat == XED_CATEGORY_COND_BR) o.flags |= IF_JCC;
    else if (cat == XED_CATEGORY_UNCOND_BR) o.flags |= (o.flags & IF_HAS_TARG) ? (IF_JMP | IF_NO_FT) : (IF_IND_JMP | IF_NO_FT);
    if (cat == XED_CATEGORY_SYSCALL || cat == XED_CATEGORY_INTERRUPT || cat == XED_CATEGORY_SYSTEM) o.flags |= IF_SYSCALL;

    // flags
    const xed_simple_flag_t *fl = xed_decoded_inst_get_rflags_info(x);
    if (fl) {
        uint32_t rd = xed_simple_flag_get_read_flag_set(fl)->flat;
        uint32_t wr = xed_simple_flag_get_written_flag_set(fl)->flat;
        if (rd & STATUS_FLAGS) o.flags |= IF_FLAGS_READ;
        else if (xed_simple_flag_get_must_write(fl) && (wr & STATUS_FLAGS) == STATUS_FLAGS) o.flags |= IF_FLAGS_KILL;
    }
    // registers
    uint16_t rd = 0, kill = 0;
    const xed_inst_t *xi = xed_decoded_inst_inst(x);
    unsigned nops = xed_inst_noperands(xi);
    for (unsigned i = 0; i < nops; i++) {
        xed_operand_enum_t name = xed_operand_name(xed_inst_operand(xi, i));
        if (!xed_operand_is_register(name) && !xed_operand_is_memory_addressing_register(name)) continue;
        xed_reg_enum_t r = xed_decoded_inst_get_reg(x, name);
        int gi = gpr_index(r);
        if (gi < 0) continue;
        xed_operand_action_enum_t a = xed_decoded_inst_operand_action(x, i);
        o.regs_touch |= 1 << gi;
        if (xed_operand_is_memory_addressing_register(name) || xed_operand_action_read(a) || xed_operand_action_conditional_read(a)) rd |= 1 << gi;
        else if (a == XED_OPERAND_ACTION_W && xed_reg_class(r) == XED_REG_CLASS_GPR && xed_get_register_width_bits64(r) >= 32) kill |= 1 << gi;
    }
    for (unsigned m = 0; m < 2; m++) {
        int b = gpr_index(xed_decoded_inst_get_base_reg(x, m));
        int ix = gpr_index(xed_decoded_inst_get_index_reg(x, m));
        if (b >= 0) rd |= 1 << b;
        if (ix >= 0) rd |= 1 << ix;
    }
    if (o.flags & IF_SYSCALL) rd = 0xFFFF;
    o.regs_touch |= rd;
    kill &= ~rd;
    o.regs_read = rd;
    o.regs_kill = kill;
    return true;
}

// Is the given status-flags state dead at instruction index i (scanning the straight-line path)?
static bool flags_dead_from(int i, int end)
{
    for (int k = i; k < end && k < i + 24; k++) {
        const oins_t &o = ins_v[k];
        if (o.flags & IF_FLAGS_READ) return false;
        if (o.flags & IF_FLAGS_KILL) return true;
        if (o.flags & IF_CALL) return true;                 // ABI: flags are not preserved across/into calls
        if (o.flags & (IF_JCC | IF_JMP | IF_RET | IF_IND_JMP | IF_SYSCALL)) return false;
    }
    return false;
}

// A GPR (not RSP) that is dead at instruction index i along the straight-line path, or -1.
static int find_dead_reg(int i, int end, uint16_t avoid)
{
    uint16_t read = 0;
    for (int k = i; k < end && k < i + 24; k++) {
        const oins_t &o = ins_v[k];
        read |= o.regs_read;
        uint16_t dead = o.regs_kill & ~read & ~avoid & ~(1 << 4);
        if (dead) {
            for (int r = 0; r < 16; r++) if (dead & (1 << r)) return r;
        }
        if (o.flags & (IF_CALL | IF_JCC | IF_JMP | IF_RET | IF_IND_JMP | IF_SYSCALL)) return -1;
    }
    return -1;
}

/* ===================================================================== */
/* Image analysis: routines, instructions, basic blocks                  */
/* ===================================================================== */
static bool in_image(ADDRINT a) { return a >= img_low && a < img_high; }

static int analyze_image(IMG img)
{
    struct cand_t { ADDRINT a, e; string name; bool safe; };
    vector<cand_t> cands;
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        if (!SEC_IsExecutable(sec) || SEC_IsWriteable(sec) || !SEC_Address(sec)) continue;
        if (SEC_Name(sec).find(".plt") == 0) continue;           // PLT stubs stay in place
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            if (RTN_Size(rtn) == 0) continue;
            cand_t c;
            c.a = RTN_Address(rtn);
            c.e = c.a + RTN_Size(rtn);
            c.name = RTN_Name(rtn);
            c.safe = RTN_IsSafeForProbedReplacement(rtn);
            cands.push_back(c);
        }
    }
    sort(cands.begin(), cands.end(), [](const cand_t &x, const cand_t &y) { return x.a < y.a; });

    // Every direct branch target in the whole image's code (translated or not), decoded linearly.
    // Pin's probe overwrites the first bytes of a routine; its safety check only looks inside the routine,
    // but e.g. a function's ".cold" part is entered from its parent a few bytes after its start.
    std::set<ADDRINT> image_targets;
    for (auto &c : cands) {
        ADDRINT a = c.a;
        while (a < c.e) {
            xed_decoded_inst_t x;
            unsigned avail = (unsigned)min<ADDRINT>(XED_MAX_INSTRUCTION_BYTES, c.e - a);
            if (!xdec(&x, (const uint8_t *)a, avail)) break;
            unsigned len = xed_decoded_inst_get_length(&x);
            if (xed_decoded_inst_get_branch_displacement_width(&x) > 0)
                image_targets.insert(a + len + xed_decoded_inst_get_branch_displacement(&x));
            a += len;
        }
    }
    // Decode all candidates linearly.
    std::set<ADDRINT> all_targets;
    ADDRINT prev_end = 0;
    for (auto &c : cands) {
        rtn_t r;
        r.addr = c.a; r.end = c.e; r.name = c.name;
        r.first_ins = (int)ins_v.size(); r.n_ins = 0;
        r.first_bbl = 0; r.n_bbl = 0;
        r.translated = c.safe && c.a >= prev_end && c.a != exit_rtn_addr;
        {
            int idx = (int)rtn_v.size();
            if (idx < KnobDbgFirst.Value() || (KnobDbgCount.Value() >= 0 && idx >= KnobDbgFirst.Value() + KnobDbgCount.Value()))
                r.translated = false;
            else if (KnobDbgCount.Value() >= 0 && KnobDbgCount.Value() <= 16)
                cerr << "dbg: candidate " << idx << " " << c.name << " 0x" << hex << c.a << "-0x" << c.e << dec << endl;
        }
        r.has_ind_jmp = false;
        r.no_probe = false;
        r.fold_spares = 0;
        r.tc_tramp = r.tc_head = r.tc2_head = 0;
        r.in_tc2 = false; r.hot = 0;
        {
            auto it = image_targets.upper_bound(c.a);
            if (it != image_targets.end() && *it < c.a + 16) r.no_probe = true;
        }
        if (c.a < prev_end) { r.translated = false; }
        else prev_end = c.e;
        if (r.translated) {
            ADDRINT a = c.a;
            while (a < c.e) {
                oins_t o;
                memset(&o, 0, sizeof(o));
                o.addr = a;
                xed_decoded_inst_t x;
                unsigned avail = (unsigned)min<ADDRINT>(XED_MAX_INSTRUCTION_BYTES, c.e - a);
                if (!xdec(&x, (const uint8_t *)a, avail)) { r.translated = false; break; }
                analyze_ins(o, &x);
                memcpy(o.bytes, (const void *)a, o.size);
                xed_iclass_enum_t ic = (xed_iclass_enum_t)o.iclass;
                // Branches with only an 8-bit form (P8) or other relative forms we do not rewrite.
                if ((o.flags & IF_HAS_TARG) && !(o.flags & (IF_JCC | IF_JMP | IF_CALL))) { r.translated = false; break; }
                if (ic == XED_ICLASS_JRCXZ || ic == XED_ICLASS_LOOP || ic == XED_ICLASS_LOOPE || ic == XED_ICLASS_LOOPNE ||
                    ic == XED_ICLASS_XBEGIN) { r.translated = false; break; }
                if (xed_decoded_inst_get_seg_reg(&x, 0) == XED_REG_FS || xed_decoded_inst_get_seg_reg(&x, 0) == XED_REG_GS) {
                    if (o.flags & (IF_IND_JMP | IF_IND_CALL)) { r.translated = false; break; }
                }
                if (o.flags & IF_IND_JMP) r.has_ind_jmp = true;
                ins_v.push_back(o);
                r.n_ins++;
                a += o.size;
            }
            if (!r.translated) { ins_v.resize(r.first_ins); r.n_ins = 0; }
            // Falls off its end into bytes that are not the next routine (symbol size too small, e.g. the
            // final ret lies outside it): the TC copy continues in the original code there, which a probe
            // could overwrite -> do not probe this routine.
            else if (r.n_ins && !(ins_v.back().flags & IF_NO_FT)) {
                size_t ci = &c - &cands[0];
                if (ci + 1 >= cands.size() || cands[ci + 1].a != c.e) r.no_probe = true;
            }
        }
        rtn_v.push_back(r);
    }
    // All direct targets inside translated code must be instruction starts (P8: jz 1f; lock; 1: cmpxchg).
    std::unordered_map<ADDRINT, int> ins_at;
    ins_at.reserve(ins_v.size() * 2);
    for (size_t i = 0; i < ins_v.size(); i++) ins_at[ins_v[i].addr] = (int)i;
    auto rtn_of = [&](ADDRINT a) -> int {
        int lo = 0, hi = (int)rtn_v.size() - 1, res = -1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (rtn_v[mid].addr <= a) { res = mid; lo = mid + 1; } else hi = mid - 1;
        }
        if (res >= 0 && a < rtn_v[res].end) return res;
        return -1;
    };
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t ri = 0; ri < rtn_v.size(); ri++) {
            rtn_t &r = rtn_v[ri];
            if (!r.translated) continue;
            for (int i = r.first_ins; i < r.first_ins + r.n_ins; i++) {
                const oins_t &o = ins_v[i];
                if (!(o.flags & IF_HAS_TARG)) continue;
                int tr = rtn_of(o.targ);
                if (tr < 0) continue;
                if (!rtn_v[tr].translated || ins_at.count(o.targ)) continue;
                // Target inside a translated routine but not at an instruction start: leave that routine
                // untranslated (its original bytes stay intact, so the branch keeps working).
                rtn_v[tr].translated = false;
                changed = true;
                if (KnobVerbose) cerr << "skip routine (branch into the middle of an instruction): " << rtn_v[tr].name << endl;
            }
        }
    }
    // Compact: drop instructions of untranslated routines.
    vector<oins_t> keep;
    keep.reserve(ins_v.size());
    for (auto &r : rtn_v) {
        int f = (int)keep.size();
        if (r.translated) for (int i = r.first_ins; i < r.first_ins + r.n_ins; i++) keep.push_back(ins_v[i]);
        else r.n_ins = 0;
        r.first_ins = f;
    }
    ins_v.swap(keep);
    for (auto &o : ins_v) if (o.flags & IF_HAS_TARG) all_targets.insert(o.targ);
    ins_index.reserve(ins_v.size() * 2);
    for (size_t i = 0; i < ins_v.size(); i++) ins_index[ins_v[i].addr] = (int)i;

    // Basic blocks (leaders: routine head, direct branch targets, instruction after a jump/jcc/ret).
    for (size_t ri = 0; ri < rtn_v.size(); ri++) {
        rtn_t &r = rtn_v[ri];
        if (!r.translated) continue;
        r.first_bbl = (int)bbl_v.size();
        for (int i = r.first_ins; i < r.first_ins + r.n_ins; i++) {
            oins_t &o = ins_v[i];
            bool leader = (i == r.first_ins) || all_targets.count(o.addr) ||
                          (ins_v[i - 1].flags & (IF_JCC | IF_JMP | IF_RET | IF_IND_JMP));
            if (leader) {
                o.flags |= IF_LEADER;
                bbl_t b;
                b.rtn = (int)ri; b.first = i; b.last = i; b.ctr = -1; b.ft_ctr = -1; b.count = 0; b.ft = 0;
                b.ft_own = false; b.dpred = -1; b.dtaken = false;
                bbl_v.push_back(b);
            }
            o.bbl = (int)bbl_v.size() - 1;
            bbl_v.back().last = i;
        }
        r.n_bbl = (int)bbl_v.size() - r.first_bbl;
    }
    // Profiling resources: a counter per block, a fall-through counter per jcc block, a site per indirect jmp/call.
    if (!KnobNoProfile) {
        // Sparse profiling: count the predecessors of every block. A block with exactly one predecessor
        // in the same routine (routines without indirect jumps only, never a routine entry) needs no
        // counter of its own: via a jcc fall-through it serves as that jcc's fall-through counter; via a
        // jcc taken edge its count is exec - fallthru of the predecessor; via a jmp it is exec.
        vector<int> npred(bbl_v.size(), 0), spred(bbl_v.size(), -1);
        vector<char> via_ft(bbl_v.size(), 0);
        for (size_t i = 0; i < ins_v.size(); i++) {
            const oins_t &o = ins_v[i];
            if (!(o.flags & (IF_JCC | IF_JMP))) continue;
            auto it = ins_index.find(o.targ);
            if (it == ins_index.end() || !(ins_v[it->second].flags & IF_LEADER)) continue;
            int tb = ins_v[it->second].bbl;
            npred[tb]++;
            spred[tb] = (bbl_v[tb].rtn == bbl_v[o.bbl].rtn) ? o.bbl : -2;
        }
        for (size_t b = 0; b < bbl_v.size(); b++) {
            if (b == 0 || bbl_v[b - 1].rtn != bbl_v[b].rtn) continue;
            if (ins_v[bbl_v[b - 1].last].flags & IF_NO_FT) continue;
            npred[b]++;
            spred[b] = (int)b - 1;
            via_ft[b] = 1;
        }
        for (size_t b = 0; b < bbl_v.size(); b++) {
            bbl_t &B = bbl_v[b];
            const rtn_t &r = rtn_v[B.rtn];
            bool derivable = !KnobNoSparseProf && npred[b] == 1 && spred[b] >= 0 && spred[b] != (int)b &&
                             (int)b != r.first_bbl && !r.has_ind_jmp;
            if (derivable && !via_ft[b]) {
                B.dpred = spred[b];
                B.dtaken = (ins_v[bbl_v[B.dpred].last].flags & IF_JCC) != 0;
            } else B.ctr = n_counters++;
        }
        for (size_t b = 0; b < bbl_v.size(); b++) {
            bbl_t &B = bbl_v[b];
            if (!(ins_v[B.last].flags & IF_JCC)) continue;
            int fb = (int)b + 1;
            bool share = !KnobNoSparseProf && fb < (int)bbl_v.size() && bbl_v[fb].rtn == B.rtn && npred[fb] == 1 &&
                         via_ft[fb] && !rtn_v[B.rtn].has_ind_jmp && bbl_v[fb].ctr >= 0;
            if (share) { B.ft_ctr = bbl_v[fb].ctr; B.ft_own = false; }
            else { B.ft_ctr = n_counters++; B.ft_own = true; }
        }
        for (size_t i = 0; i < ins_v.size(); i++) {
            oins_t &o = ins_v[i];
            if (!(o.flags & (IF_IND_JMP | IF_IND_CALL))) continue;
            site_t s;
            s.ins = (int)i;
            s.is_call = (o.flags & IF_IND_CALL) != 0;
            s.table = 0;
            o.site = (int)site_v.size();
            site_v.push_back(s);
        }
    }
    return 0;
}

/* ===================================================================== */
/* Region allocation (P2)                                                 */
/* ===================================================================== */
static bool try_map(ADDRINT hint, size_t sz, ADDRINT lo_ok, ADDRINT hi_ok)
{
    void *p = mmap((void *)hint, sz, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return false;
    ADDRINT a = (ADDRINT)p;
    if (a >= lo_ok && a + sz <= hi_ok) { region = (char *)p; return true; }
    munmap(p, sz);
    return false;
}

static int allocate_region(size_t text_bytes)
{
    size_t page = sysconf(_SC_PAGE_SIZE);
    tc_cap = text_bytes * 6 + (8 << 20);
    tc2_cap = text_bytes * 3 + (8 << 20);
    d_sites = (D_CTR + n_counters * 8 + 63) & ~63u;
    d_jslots = d_sites + (unsigned)site_v.size() * SITE_SLOTS * 16;
    n_jslots_max = 65536;
    data_size = (d_jslots + n_jslots_max * 8 + page - 1) & ~(page - 1);
    region_size = data_size + ((tc_cap + page - 1) & ~(page - 1)) + ((tc2_cap + page - 1) & ~(page - 1));

    const ADDRINT two_gb = 0x7FF00000ULL;       // keep a margin under 2^31
    ADDRINT span = img_high - img_low;
    if (span + region_size >= two_gb) { TOOL_ERROR("image + region larger than 2GB"); return -1; }
    // Everything in [img_low, region_end) must be reachable with rel32 from everywhere in it.
    ADDRINT hi_ok = img_low + two_gb;
    ADDRINT gap = 1ULL << 30;                   // ~1 GB above the image: leaves room for brk/heap
    if (img_high + gap + region_size > hi_ok) gap = hi_ok - img_high - region_size;
    for (int tries = 0; tries < 8 && !region; tries++) {
        ADDRINT hint = (img_high + gap + (2 << 20) - 1) & ~((ADDRINT)(2 << 20) - 1);
        if (hint + region_size <= hi_ok && try_map(hint, region_size, img_high, hi_ok)) break;
        gap /= 2;
    }
    if (!region) {
        // Fallback: the base tool's search (first free address above the code).
        TOOL_WARN("region placement ~1GB above the image failed; using the first free address above the code");
        for (ADDRINT a = (img_high + page - 1) & ~(page - 1); a + region_size <= hi_ok; a += 16 * page)
            if (try_map(a, region_size, img_high, hi_ok)) break;
    }
    if (!region) { TOOL_ERROR("failed to allocate the TC region within 2GB of the code"); return -1; }
    data_area = region;
    tc = region + data_size;
    tc2 = tc + ((tc_cap + page - 1) & ~(page - 1));
    // nibble table: byte b -> ((b >> 4) & 15) * 16 (slot offset for the indirect target hash, P9)
    uint8_t *nib = (uint8_t *)dptr(D_NIB);
    for (unsigned b = 0; b < 256; b++) nib[b] = (uint8_t)(((b >> 4) & 15) * 16);
    for (size_t s = 0; s < site_v.size(); s++) site_v[s].table = d_sites + (unsigned)s * SITE_SLOTS * 16;
    if (KnobVerbose)
        cerr << "region at 0x" << hex << (ADDRINT)region << " size 0x" << region_size << " image 0x" << img_low << "-0x" << img_high << dec << endl;
    return 0;
}

// Slot holding an original address, for a jmp/call [rip+slot] when a rel32 cannot reach (P3: map lookup).
static int jslot_for(ADDRINT t)
{
    auto it = jslot_of.find(t);
    if (it != jslot_of.end()) return (int)it->second;
    if (n_jslots >= n_jslots_max) return -1;
    unsigned s = n_jslots++;
    *(ADDRINT *)dptr(d_jslots + s * 8) = t;
    jslot_of[t] = s;
    return (int)s;
}

/* ===================================================================== */
/* Emitting original instructions                                        */
/* ===================================================================== */
// Emit a (translated) original instruction. Direct jmp/jcc/call become rel32 forms with a T_ORIG
// target; RIP-relative operands keep their original bytes and get the displacement re-derived from
// the absolute address at layout (P6).
static bool emit_orig(emitter_t &E, const oins_t &o)
{
    if (o.flags & IF_HAS_TARG) {
        xed_iclass_enum_t ic = (xed_iclass_enum_t)o.iclass;
        if (!in_image(o.targ) && (o.flags & (IF_CALL | IF_JMP))) {
            int s = jslot_for(o.targ);
            if (s < 0) { TOOL_ERROR("jump slot table full"); return false; }
            xed_encoder_instruction_t ei;
            xed_inst1(&ei, dstate, (o.flags & IF_CALL) ? XED_ICLASS_CALL_NEAR : XED_ICLASS_JMP, 64, xed_mem_bd(XED_REG_RIP, xed_disp(0, 32), 64));
            return add_syn(E, &ei, o.addr, true, dptr(d_jslots + s * 8));
        }
        return add_branch(E, ic, T_ORIG, o.targ, -1, o.addr);
    }
    ent_t &n = E.add();
    memcpy(n.bytes, o.bytes, o.size);
    n.size = o.size;
    n.orig = o.addr;
    n.kind = K_INS;
    if (o.flags & IF_HAS_RIP) {
        n.rip_off = o.size - o.imm_bytes - 4;
        n.rip_abs = o.rip_addr;
    }
    return true;
}

/* ===================================================================== */
/* Redundant stack-load elimination ("stack fold"), used in TC and TC2   */
/* ===================================================================== */
// -O0 code keeps every local in [rbp-X] and reloads it all the time. For the most-read slots of a routine
// we keep a copy of the slot in a spare register (a caller-saved register the routine never touches):
// every store to the slot stays and also updates the register; a later load/compare of the slot reads the
// register instead of memory when a CFG dataflow proves the register is up to date on all paths.
// Safety: slots whose address is never taken (a lea/indexed access [rbp+d] makes slots at and above d
// unfoldable), routines without indirect jumps, cache cleared at calls, push/pop, syscalls, RSP-based
// stores and RBP writes. When unsure, don't fold.
KNOB<BOOL> KnobNoStackFold(KNOB_MODE_WRITEONCE, "pintool", "no_stackfold", "0", "Disable redundant stack-load elimination");
KNOB<BOOL> KnobNoAddrFold(KNOB_MODE_WRITEONCE, "pintool", "no_addrfold", "0",
    "Disable folding 'mov p,spare; add p,idx; op [p]' into 'op [spare+idx]'");
static bool flags_dead_from(int i, int end);

struct fold_t { bool drop; uint8_t len; uint8_t bytes[15]; };
static std::unordered_map<int, fold_t> fold_of;
static unsigned n_fold_rtns = 0, n_fold_loads = 0, n_fold_syncs = 0, n_addr_folds = 0;

static const xed_reg_enum_t gpr32[16] = {
    XED_REG_EAX, XED_REG_ECX, XED_REG_EDX, XED_REG_EBX, XED_REG_ESP, XED_REG_EBP, XED_REG_ESI, XED_REG_EDI,
    XED_REG_R8D, XED_REG_R9D, XED_REG_R10D, XED_REG_R11D, XED_REG_R12D, XED_REG_R13D, XED_REG_R14D, XED_REG_R15D};
static const xed_reg_enum_t gpr16[16] = {
    XED_REG_AX, XED_REG_CX, XED_REG_DX, XED_REG_BX, XED_REG_SP, XED_REG_BP, XED_REG_SI, XED_REG_DI,
    XED_REG_R8W, XED_REG_R9W, XED_REG_R10W, XED_REG_R11W, XED_REG_R12W, XED_REG_R13W, XED_REG_R14W, XED_REG_R15W};
static const xed_reg_enum_t gpr8[16] = {
    XED_REG_AL, XED_REG_CL, XED_REG_DL, XED_REG_BL, XED_REG_SPL, XED_REG_BPL, XED_REG_SIL, XED_REG_DIL,
    XED_REG_R8B, XED_REG_R9B, XED_REG_R10B, XED_REG_R11B, XED_REG_R12B, XED_REG_R13B, XED_REG_R14B, XED_REG_R15B};

static xed_reg_enum_t reg_w(int gi, unsigned bytes)
{
    return bytes == 8 ? gpr64[gi] : bytes == 4 ? gpr32[gi] : bytes == 2 ? gpr16[gi] : gpr8[gi];
}
static bool is_high8(xed_reg_enum_t r) { return r == XED_REG_AH || r == XED_REG_BH || r == XED_REG_CH || r == XED_REG_DH; }
static int block_at(const rtn_t &r, ADDRINT a);

enum { FA_NONE = 0, FA_ST_REG, FA_ST_IMM, FA_LD, FA_LDX, FA_ALU_RM, FA_CMP_MI, FA_CMP_MR, FA_RMW_IMM, FA_RMW_REG, FA_OTHER };
struct facc_t {
    int kind;
    INT64 disp;
    unsigned w;            // slot width in bytes
    xed_reg_enum_t reg;    // register operand (dest for loads, source for stores/ALU)
    INT64 imm;
    unsigned immbits;
    xed_iclass_enum_t ic;
    bool mem_w, clear;
};

// Classify one instruction w.r.t. [rbp+disp] slots.
static facc_t classify_fold(const oins_t &o)
{
    facc_t f;
    memset(&f, 0, sizeof(f));
    xed_decoded_inst_t x;
    if (!xdec(&x, o.bytes, o.size)) { f.clear = true; f.kind = FA_OTHER; return f; }
    xed_iclass_enum_t ic = xed_decoded_inst_get_iclass(&x);
    f.ic = ic;
    if ((o.flags & (IF_CALL | IF_SYSCALL | IF_RET)) || ic == XED_ICLASS_PUSH || ic == XED_ICLASS_POP || ic == XED_ICLASS_LEAVE ||
        ic == XED_ICLASS_ENTER || ic == XED_ICLASS_PUSHF || ic == XED_ICLASS_POPF || ic == XED_ICLASS_PUSHFQ || ic == XED_ICLASS_POPFQ)
        f.clear = true;
    if (o.regs_touch & (1 << 5)) {          // RBP written (fully or partially) -> clear
        const xed_inst_t *xi = xed_decoded_inst_inst(&x);
        for (unsigned i = 0; i < xed_inst_noperands(xi); i++) {
            xed_operand_enum_t nm = xed_operand_name(xed_inst_operand(xi, i));
            if (!xed_operand_is_register(nm)) continue;
            if (gpr_index(xed_decoded_inst_get_reg(&x, nm)) == 5 && xed_operand_action_written(xed_decoded_inst_operand_action(&x, i))) f.clear = true;
        }
    }
    unsigned nm = xed_decoded_inst_number_of_memory_operands(&x);
    for (unsigned m = 0; m < nm; m++)
        if (xed_decoded_inst_get_base_reg(&x, m) == XED_REG_RSP && xed_decoded_inst_mem_written(&x, m)) f.clear = true;
    if (nm != 1 || ic == XED_ICLASS_LEA) return f;
    if (xed_decoded_inst_get_base_reg(&x, 0) != XED_REG_RBP || xed_decoded_inst_get_index_reg(&x, 0) != XED_REG_INVALID) return f;
    xed_reg_enum_t seg = xed_decoded_inst_get_seg_reg(&x, 0);
    if (seg == XED_REG_FS || seg == XED_REG_GS) return f;
    f.kind = FA_OTHER;
    f.disp = xed_decoded_inst_get_memory_displacement(&x, 0);
    f.w = xed_decoded_inst_get_memory_operand_length(&x, 0);
    f.mem_w = xed_decoded_inst_mem_written(&x, 0);
    // explicit operands
    const xed_inst_t *xi = xed_decoded_inst_inst(&x);
    xed_operand_enum_t ex[4];
    unsigned nex = 0;
    for (unsigned i = 0; i < xed_inst_noperands(xi) && nex < 4; i++) {
        const xed_operand_t *op = xed_inst_operand(xi, i);
        if (xed_operand_operand_visibility(op) == XED_OPVIS_EXPLICIT) ex[nex++] = xed_operand_name(op);
    }
    auto isimm = [](xed_operand_enum_t n) { return n == XED_OPERAND_IMM0 || n == XED_OPERAND_IMM0SIGNED; };
    if (f.w != 1 && f.w != 2 && f.w != 4 && f.w != 8) return f;
    if (nex == 2 && ex[0] == XED_OPERAND_MEM0 && ex[1] == XED_OPERAND_REG0) {
        f.reg = xed_decoded_inst_get_reg(&x, XED_OPERAND_REG0);
        if (gpr_index(f.reg) < 0 || xed_get_register_width_bits64(f.reg) != f.w * 8) return f;
        if (ic == XED_ICLASS_MOV) f.kind = FA_ST_REG;
        else if (ic == XED_ICLASS_CMP) f.kind = FA_CMP_MR;
        else if (ic == XED_ICLASS_ADD && f.w >= 4) f.kind = FA_RMW_REG;
    } else if (nex == 2 && ex[0] == XED_OPERAND_MEM0 && isimm(ex[1])) {
        f.imm = xed_decoded_inst_get_signed_immediate(&x);
        f.immbits = xed_decoded_inst_get_immediate_width_bits(&x);
        if (ic == XED_ICLASS_MOV) f.kind = FA_ST_IMM;
        else if (ic == XED_ICLASS_CMP) f.kind = FA_CMP_MI;
        else if (ic == XED_ICLASS_ADD) f.kind = FA_RMW_IMM;
        else if (ic == XED_ICLASS_SUB && f.imm != INT32_MIN) { f.kind = FA_RMW_IMM; f.imm = -f.imm; }
    } else if (nex == 1 && ex[0] == XED_OPERAND_MEM0 && (ic == XED_ICLASS_INC || ic == XED_ICLASS_DEC)) {
        f.kind = FA_RMW_IMM;
        f.imm = ic == XED_ICLASS_INC ? 1 : -1;
    } else if (nex == 2 && ex[0] == XED_OPERAND_REG0 && ex[1] == XED_OPERAND_MEM0) {
        f.reg = xed_decoded_inst_get_reg(&x, XED_OPERAND_REG0);
        if (gpr_index(f.reg) < 0) return f;
        unsigned rw = xed_get_register_width_bits64(f.reg);
        if (ic == XED_ICLASS_MOV && rw == f.w * 8) f.kind = FA_LD;
        else if ((ic == XED_ICLASS_MOVZX || ic == XED_ICLASS_MOVSX || ic == XED_ICLASS_MOVSXD) && rw > f.w * 8 && rw >= 16) f.kind = FA_LDX;
        else if ((ic == XED_ICLASS_ADD || ic == XED_ICLASS_SUB || ic == XED_ICLASS_AND || ic == XED_ICLASS_OR || ic == XED_ICLASS_XOR ||
                  ic == XED_ICLASS_CMP || (ic == XED_ICLASS_IMUL && f.w >= 2)) && rw == f.w * 8)
            f.kind = FA_ALU_RM;
    }
    return f;
}

static bool enc_bytes(xed_encoder_instruction_t *ei, fold_t &fo)
{
    unsigned len = 0;
    if (!xenc(ei, fo.bytes, &len)) return false;
    fo.len = (uint8_t)len;
    return true;
}

// "mov s, reg" keeping the low w bytes of the spare equal to the slot.
static bool enc_sync(fold_t &fo, int s, xed_reg_enum_t src, unsigned w)
{
    xed_encoder_instruction_t ei;
    if (is_high8(src)) return false;
    int gi = gpr_index(src);
    if (w == 8) xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, xed_reg(gpr64[s]), xed_reg(gpr64[gi]));
    else if (w == 4) xed_inst2(&ei, dstate, XED_ICLASS_MOV, 32, xed_reg(gpr32[s]), xed_reg(gpr32[gi]));
    else xed_inst2(&ei, dstate, XED_ICLASS_MOVZX, 32, xed_reg(gpr32[s]), xed_reg(src));
    return enc_bytes(&ei, fo);
}

// Address fold: I2 "mov P,spare" (a folded 64-bit slot load), I3 "add P,X", I4 "op ..,[P+d]" where I4 fully
// overwrites P and does not otherwise read it, and the flags of the add are dead
//   -> drop I2 and I3, I4 uses [spare+X*1+d]. I2 may be one instruction earlier if that one does not touch P.
static void address_fold(const rtn_t &r, const std::map<int, int> &ptr_from_spare)
{
    int rend = r.first_ins + r.n_ins;
    for (auto &kv : ptr_from_spare) {
        int i2 = kv.first, s = kv.second;
        const oins_t &o2 = ins_v[i2];
        xed_decoded_inst_t x2;
        if (!xdec(&x2, o2.bytes, o2.size)) continue;
        int P = gpr_index(xed_decoded_inst_get_reg(&x2, XED_OPERAND_REG0));
        if (P < 0) continue;
        int i3 = i2 + 1;
        if (i3 < rend && !(ins_v[i3].flags & IF_LEADER) && ins_v[i3].iclass != XED_ICLASS_ADD && !(ins_v[i3].regs_touch & (1 << P)) &&
            !(ins_v[i3].flags & (IF_CALL | IF_JCC | IF_JMP | IF_RET | IF_IND_JMP | IF_SYSCALL)))
            i3++;
        int i4 = i3 + 1;
        if (i4 >= rend || (ins_v[i3].flags & IF_LEADER) || (ins_v[i4].flags & IF_LEADER)) continue;
        if (fold_of.count(i3) || fold_of.count(i4) || (i3 == i2 + 2 && fold_of.count(i2 + 1) && fold_of[i2 + 1].drop)) continue;
        const oins_t &o3 = ins_v[i3], &o4 = ins_v[i4];
        xed_decoded_inst_t x3, x4;
        if (!xdec(&x3, o3.bytes, o3.size) || !xdec(&x4, o4.bytes, o4.size)) continue;
        if (xed_decoded_inst_get_iclass(&x3) != XED_ICLASS_ADD || xed_decoded_inst_number_of_memory_operands(&x3) != 0) continue;
        const xed_inst_t *xi3 = xed_decoded_inst_inst(&x3);
        if (xed_operand_name(xed_inst_operand(xi3, 0)) != XED_OPERAND_REG0 || xed_operand_name(xed_inst_operand(xi3, 1)) != XED_OPERAND_REG1) continue;
        xed_reg_enum_t d3 = xed_decoded_inst_get_reg(&x3, XED_OPERAND_REG0), s3 = xed_decoded_inst_get_reg(&x3, XED_OPERAND_REG1);
        if (d3 != gpr64[P] || xed_get_register_width_bits64(s3) != 64) continue;
        int X = gpr_index(s3);
        if (X < 0 || X == P || X == 4 || X == s) continue;
        // I4: one memory operand [P+d], P fully written by I4 and not read except as the base
        if (xed_decoded_inst_number_of_memory_operands(&x4) != 1 || o4.iclass == XED_ICLASS_LEA) continue;
        if (xed_decoded_inst_get_base_reg(&x4, 0) != gpr64[P] || xed_decoded_inst_get_index_reg(&x4, 0) != XED_REG_INVALID) continue;
        xed_reg_enum_t seg = xed_decoded_inst_get_seg_reg(&x4, 0);
        if (seg == XED_REG_FS || seg == XED_REG_GS) continue;
        {
            // P must be the (fully written) destination register REG0 of I4
            const xed_inst_t *xi4d = xed_decoded_inst_inst(&x4);
            if (xed_operand_name(xed_inst_operand(xi4d, 0)) != XED_OPERAND_REG0) continue;
            xed_reg_enum_t d4 = xed_decoded_inst_get_reg(&x4, XED_OPERAND_REG0);
            if (gpr_index(d4) != P || xed_get_register_width_bits64(d4) < 32 ||
                xed_decoded_inst_operand_action(&x4, 0) != XED_OPERAND_ACTION_W) continue;
        }
        {
            bool other_read = false;
            const xed_inst_t *xi4 = xed_decoded_inst_inst(&x4);
            for (unsigned k = 0; k < xed_inst_noperands(xi4); k++) {
                xed_operand_enum_t nm = xed_operand_name(xed_inst_operand(xi4, k));
                if (!xed_operand_is_register(nm)) continue;
                if (gpr_index(xed_decoded_inst_get_reg(&x4, nm)) == P && xed_operand_action_read(xed_decoded_inst_operand_action(&x4, k))) other_read = true;
            }
            if (other_read) continue;
        }
        if (!flags_dead_from(i4, rend)) continue;         // the add's flags must be unobservable
        xed_encoder_request_init_from_decode(&x4);
        xed_encoder_request_set_base0(&x4, gpr64[s]);
        xed_encoder_request_set_index(&x4, gpr64[X]);
        xed_encoder_request_set_scale(&x4, 1);
        fold_t f4;
        memset(&f4, 0, sizeof(f4));
        unsigned len = 0;
        if (xed_encode(&x4, f4.bytes, XED_MAX_INSTRUCTION_BYTES, &len) != XED_ERROR_NONE) continue;
        xed_decoded_inst_t chk;
        if (!xdec(&chk, f4.bytes, len) || xed_decoded_inst_get_base_reg(&chk, 0) != gpr64[s] || xed_decoded_inst_get_index_reg(&chk, 0) != gpr64[X])
            continue;
        f4.len = len;
        f4.drop = true;
        fold_t z;
        memset(&z, 0, sizeof(z));
        z.drop = true;                                    // I2 and I3 disappear
        fold_of[i2] = z;
        fold_of[i3] = z;
        fold_of[i4] = f4;
        n_addr_folds++;
    }
}

static void compute_stack_folds_rtn(rtn_t &r)
{
    if (!r.translated || r.has_ind_jmp || r.n_ins == 0) return;
    int f0 = r.first_ins, n = r.n_ins;
    bool has_frame = false;
    for (int i = f0; i < f0 + n; i++) {
        const oins_t &o = ins_v[i];
        if (o.iclass == XED_ICLASS_MOV && o.size == 3 && o.bytes[0] == 0x48 && o.bytes[1] == 0x89 && o.bytes[2] == 0xE5) has_frame = true;
    }
    if (!has_frame) return;
    // Spare registers: caller-saved registers, usable only at points where the application's value is dead
    // (register liveness below). r10/r11 first (never arguments or return values).
    vector<int> spares = {11, 10, 9, 8, 7, 6, 1};
    // classify accesses, find the address-taken limit L and inconsistent slots
    vector<facc_t> fa(n);
    INT64 L = 0;
    bool bad_rbp = false;
    for (int k = 0; k < n; k++) {
        const oins_t &o = ins_v[f0 + k];
        fa[k] = classify_fold(o);
        xed_decoded_inst_t x;
        if (!xdec(&x, o.bytes, o.size)) { bad_rbp = true; break; }
        unsigned nm = xed_decoded_inst_number_of_memory_operands(&x);
        bool rbp_addr = false;
        for (unsigned m = 0; m < nm; m++) {
            if (xed_decoded_inst_get_index_reg(&x, m) == XED_REG_RBP) bad_rbp = true;
            if (xed_decoded_inst_get_base_reg(&x, m) == XED_REG_RBP) {
                rbp_addr = true;
                if (xed_decoded_inst_get_index_reg(&x, m) != XED_REG_INVALID || o.iclass == XED_ICLASS_LEA)
                    L = min<INT64>(L, xed_decoded_inst_get_memory_displacement(&x, m));
            }
        }
        if (o.iclass == XED_ICLASS_LEA && xed_decoded_inst_get_base_reg(&x, 0) == XED_REG_RBP)
            L = min<INT64>(L, xed_decoded_inst_get_memory_displacement(&x, 0));
        // RBP used as a plain value (not an address, not push rbp / mov rbp,rsp / mov rsp,rbp): give up
        if ((o.regs_read & (1 << 5)) && !rbp_addr && o.iclass != XED_ICLASS_PUSH && o.iclass != XED_ICLASS_LEAVE && o.iclass != XED_ICLASS_POP) {
            bool mov_rsp_rbp = o.iclass == XED_ICLASS_MOV && o.size == 3 && o.bytes[0] == 0x48 && o.bytes[1] == 0x89 && o.bytes[2] == 0xEC;
            if (!mov_rsp_rbp) bad_rbp = true;
        }
    }
    if (bad_rbp) return;
    std::map<pair<INT64, unsigned>, int> weight;
    std::set<pair<INT64, unsigned>> bad;
    vector<pair<INT64, unsigned>> accs;
    for (int k = 0; k < n; k++) if (fa[k].kind != FA_NONE) accs.push_back(make_pair(fa[k].disp, fa[k].w));
    std::set<pair<INT64, unsigned>> uniq(accs.begin(), accs.end());
    for (auto &a : uniq) {
        if (a.first >= 0 || a.first + (INT64)a.second > L) { bad.insert(a); continue; }
        for (auto &b : uniq) {
            if (a == b) continue;
            if (a.first < b.first + (INT64)b.second && b.first < a.first + (INT64)a.second) { bad.insert(a); break; }
        }
    }
    for (int k = 0; k < n; k++) {
        int kd = fa[k].kind;
        if (kd == FA_LD || kd == FA_LDX || kd == FA_ALU_RM || kd == FA_CMP_MI || kd == FA_CMP_MR) {
            auto key = make_pair(fa[k].disp, fa[k].w);
            if (!bad.count(key)) weight[key]++;
        }
    }
    vector<pair<int, pair<INT64, unsigned>>> ws;
    for (auto &kv : weight) if (kv.second >= 2) ws.push_back(make_pair(kv.second, kv.first));
    sort(ws.begin(), ws.end(), [](const pair<int, pair<INT64, unsigned>> &a, const pair<int, pair<INT64, unsigned>> &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::map<pair<INT64, unsigned>, int> slot_sp;   // slot -> spare index in 'spares'
    for (size_t k = 0; k < ws.size() && k < spares.size(); k++) slot_sp[ws[k].second] = (int)k;
    if (slot_sp.empty()) return;
    unsigned ns = slot_sp.size();
    const unsigned ALL = (1u << ns) - 1;

    // CFG: predecessors inside the routine; external/unknown entries force "nothing valid".
    int b0 = r.first_bbl, nb = r.n_bbl;
    vector<vector<int>> preds(nb);
    vector<char> ext(nb, 0);
    ext[0] = 1;
    for (int b = 0; b < nb; b++) {
        const bbl_t &B = bbl_v[b0 + b];
        const oins_t &o = ins_v[B.last];
        if ((o.flags & (IF_JCC | IF_JMP))) {
            int tb = block_at(r, o.targ);
            if (tb >= 0) preds[tb - b0].push_back(b);
        }
        if (!(o.flags & IF_NO_FT) && b + 1 < nb) preds[b + 1].push_back(b);
    }
    // direct branches from other routines / calls into the middle of this one
    for (size_t i = 0; i < ins_v.size(); i++) {
        const oins_t &o = ins_v[i];
        if (!(o.flags & IF_HAS_TARG) || o.targ < r.addr || o.targ >= r.end) continue;
        if (i >= (size_t)f0 && i < (size_t)(f0 + n) && !(o.flags & IF_CALL)) continue;
        int tb = block_at(r, o.targ);
        if (tb >= 0) ext[tb - b0] = 1;
    }
    // Register liveness (backward, per instruction): lo[k] = registers live after instruction k.
    const uint16_t ALLR = 0xFFFF;
    const uint16_t CALLER = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11);
    const uint16_t ARGS = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9);
    const uint16_t RETL = (1 << 0) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15);
    vector<uint16_t> lo(n, 0), bin(nb, 0);
    auto live_before = [&](int i, uint16_t out) -> uint16_t {
        const oins_t &o = ins_v[i];
        uint16_t gen = o.regs_touch & ~o.regs_kill;
        if (o.flags & IF_SYSCALL) return ALLR;
        if (o.flags & IF_RET) return RETL | gen;
        if (o.flags & IF_CALL) return (uint16_t)((out & ~CALLER) | ARGS | gen);
        return (uint16_t)((out & ~o.regs_kill) | gen);
    };
    auto block_out = [&](int b) -> uint16_t {
        const bbl_t &B = bbl_v[b0 + b];
        const oins_t &o = ins_v[B.last];
        uint16_t out = 0;
        if (o.flags & IF_RET) return 0;
        if (o.flags & (IF_JCC | IF_JMP)) {
            int tb = block_at(r, o.targ);
            out |= tb >= 0 ? bin[tb - b0] : ALLR;
        }
        if (!(o.flags & IF_NO_FT)) out |= b + 1 < nb ? bin[b + 1] : ALLR;
        else if (!(o.flags & (IF_JMP | IF_RET))) out = ALLR;
        return out;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (int b = nb - 1; b >= 0; b--) {
            const bbl_t &B = bbl_v[b0 + b];
            uint16_t v = block_out(b);
            for (int i = B.last; i >= B.first; i--) { lo[i - f0] = v; v = live_before(i, v); }
            if (v != bin[b]) { bin[b] = v; changed = true; }
        }
    }
    // Can the spare of slot bit 'j' be written right after instruction k (application value dead there)?
    auto dead_after = [&](int k, int j) -> bool { return !(lo[k] & (1 << spares[j])); };

    auto transfer = [&](int k, unsigned v) -> unsigned {
        const facc_t &f = fa[k];
        for (unsigned j = 0; j < ns; j++) if (ins_v[f0 + k].regs_touch & (1 << spares[j])) v &= ~(1u << j);
        if (f.kind != FA_NONE && f.kind != FA_OTHER) {
            auto it = slot_sp.find(make_pair(f.disp, f.w));
            if (it != slot_sp.end()) {
                unsigned bit = 1u << it->second;
                bool dead = dead_after(k, it->second);
                switch (f.kind) {
                case FA_ST_REG: v = (!is_high8(f.reg) && dead) ? v | bit : v & ~bit; break;
                case FA_ST_IMM: v = dead ? v | bit : v & ~bit; break;
                case FA_LD: if (!is_high8(f.reg) && dead) v |= bit; break;
                case FA_LDX: if (xed_get_register_width_bits64(f.reg) >= 32 && dead) v |= bit; break;
                case FA_RMW_IMM: case FA_RMW_REG: if (!dead) v &= ~bit; break;  // valid stays valid through the lea sync
                default: break;
                }
            }
        } else if (f.kind == FA_OTHER && f.mem_w) {
            for (auto &kv : slot_sp)
                if (f.disp < kv.first.first + (INT64)kv.first.second && kv.first.first < f.disp + (INT64)f.w) v &= ~(1u << kv.second);
        }
        if (f.clear) v = 0;
        return v;
    };
    vector<unsigned> in(nb, ALL), out(nb, ALL);
    for (bool changed = true; changed;) {
        changed = false;
        for (int b = 0; b < nb; b++) {
            unsigned v = ext[b] ? 0 : ALL;
            if (!ext[b]) for (int p : preds[b]) v &= out[p];
            if (preds[b].empty()) v = 0;
            in[b] = v;
            const bbl_t &B = bbl_v[b0 + b];
            for (int i = B.first; i <= B.last; i++) v = transfer(i - f0, v);
            if (v != out[b]) { out[b] = v; changed = true; }
        }
    }
    // Emission decisions.
    unsigned loads = 0;
    std::map<int, int> ptr_from_spare;     // instruction -> spare, for "mov r64,[slot]" replaced by "mov r64,spare"
    for (int b = 0; b < nb; b++) {
        const bbl_t &B = bbl_v[b0 + b];
        unsigned v = in[b];
        for (int i = B.first; i <= B.last; i++) {
            int k = i - f0;
            const facc_t &f = fa[k];
            auto it = f.kind > FA_NONE && f.kind < FA_OTHER ? slot_sp.find(make_pair(f.disp, f.w)) : slot_sp.end();
            if (it != slot_sp.end()) {
                int s = spares[it->second];
                bool valid = (v >> it->second) & 1;                        // spare == slot before the instruction
                bool vt = valid && !(ins_v[i].regs_touch & (1 << s));    // ... and after its own register effects
                bool dead = dead_after(k, it->second);                   // spare may be written after it
                fold_t fo;
                memset(&fo, 0, sizeof(fo));
                xed_encoder_instruction_t ei;
                bool ok = false, need = false;   // need: the dataflow (transfer) assumed a sync is emitted here
                switch (f.kind) {
                case FA_ST_REG:
                    need = !is_high8(f.reg) && dead;
                    if (need) ok = enc_sync(fo, s, f.reg, f.w);
                    break;
                case FA_ST_IMM:
                    need = dead;
                    if (f.w == 8) xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, xed_reg(gpr64[s]), xed_simm0((xed_int32_t)f.imm, 32));
                    else xed_inst2(&ei, dstate, XED_ICLASS_MOV, 32, xed_reg(gpr32[s]),
                                   xed_imm0((UINT64)f.imm & (f.w == 4 ? 0xFFFFFFFFULL : f.w == 2 ? 0xFFFFULL : 0xFFULL), 32));
                    if (need) ok = enc_bytes(&ei, fo);
                    break;
                case FA_LD:
                    if (valid && !is_high8(f.reg)) {
                        xed_inst2(&ei, dstate, XED_ICLASS_MOV, f.w * 8, xed_reg(f.reg), xed_reg(reg_w(s, f.w)));
                        ok = enc_bytes(&ei, fo);
                        fo.drop = ok;
                        if (ok) loads++;
                        if (ok && f.w == 8) ptr_from_spare[i] = s;
                    } else if (!is_high8(f.reg) && dead) {
                        need = true;
                        ok = enc_sync(fo, s, f.reg, f.w);
                    }
                    break;
                case FA_LDX:
                    if (valid) {
                        xed_inst2(&ei, dstate, f.ic, xed_get_register_width_bits64(f.reg), xed_reg(f.reg), xed_reg(reg_w(s, f.w)));
                        ok = enc_bytes(&ei, fo);
                        fo.drop = ok;
                        if (ok) loads++;
                    } else if (xed_get_register_width_bits64(f.reg) >= 32 && dead) {
                        need = true;
                        xed_inst2(&ei, dstate, XED_ICLASS_MOV, 32, xed_reg(gpr32[s]), xed_reg(gpr32[gpr_index(f.reg)]));
                        ok = enc_bytes(&ei, fo);
                    }
                    break;
                case FA_ALU_RM:
                    if (valid && !is_high8(f.reg)) {
                        xed_inst2(&ei, dstate, f.ic, f.w * 8, xed_reg(f.reg), xed_reg(reg_w(s, f.w)));
                        ok = enc_bytes(&ei, fo);
                        fo.drop = ok;
                        if (ok) loads++;
                    }
                    break;
                case FA_CMP_MR:
                    if (valid && !is_high8(f.reg)) {
                        xed_inst2(&ei, dstate, XED_ICLASS_CMP, f.w * 8, xed_reg(reg_w(s, f.w)), xed_reg(f.reg));
                        ok = enc_bytes(&ei, fo);
                        fo.drop = ok;
                        if (ok) loads++;
                    }
                    break;
                case FA_CMP_MI:
                    if (valid) {
                        xed_inst2(&ei, dstate, XED_ICLASS_CMP, f.w * 8, xed_reg(reg_w(s, f.w)), xed_simm0((xed_int32_t)f.imm, f.immbits));
                        ok = enc_bytes(&ei, fo);
                        fo.drop = ok;
                        if (ok) loads++;
                    }
                    break;
                case FA_RMW_IMM:
                    need = vt && dead;
                    if (need && fits32(f.imm)) {
                        xed_inst2(&ei, dstate, XED_ICLASS_LEA, 64, xed_reg(gpr64[s]), xed_mem_bd(gpr64[s], xed_disp(f.imm, 32), 64));
                        ok = enc_bytes(&ei, fo);
                    }
                    break;
                case FA_RMW_REG:
                    need = vt && dead;
                    if (need) {
                        xed_inst2(&ei, dstate, XED_ICLASS_LEA, 64, xed_reg(gpr64[s]),
                                  xed_mem_bisd(gpr64[s], gpr64[gpr_index(f.reg)], 1, xed_disp(0, 8), 64));
                        ok = enc_bytes(&ei, fo);
                    }
                    break;
                }
                if (need && ok) n_fold_syncs++;
                // A sync the dataflow relied on could not be encoded: give up on the whole routine (safe).
                if (need && !ok) {
                    for (int j = f0; j < f0 + n; j++) fold_of.erase(j);
                    return;
                }
                if (ok) fold_of[i] = fo;
            }
            v = transfer(k, v);
        }
    }
    for (auto &kv : slot_sp) r.fold_spares |= 1 << spares[kv.second];
    if (!KnobNoAddrFold) address_fold(r, ptr_from_spare);
    if (KnobVerbose && loads >= 20) {
        cerr << "fold " << r.name << ": " << loads << " loads;";
        for (auto &kv : slot_sp) cerr << " [rbp" << kv.first.first << "]/" << kv.first.second << "->r" << spares[kv.second];
        cerr << endl;
    }
    n_fold_rtns++;
    n_fold_loads += loads;
}

static void compute_stack_folds()
{
    if (KnobNoStackFold) return;
    for (auto &r : rtn_v) compute_stack_folds_rtn(r);
    if (KnobVerbose)
        cerr << "stack fold: " << n_fold_rtns << " routines, " << n_fold_loads << " loads replaced, " << n_fold_syncs << " register syncs, " << n_addr_folds << " address folds" << endl;
}

// Emit original instruction i, applying its stack-fold rewrite if any.
static bool emit_ins(emitter_t &E, int i)
{
    const oins_t &o = ins_v[i];
    auto it = fold_of.find(i);
    if (it == fold_of.end()) return emit_orig(E, o);
    if (!it->second.drop && !emit_orig(E, o)) return false;
    if (it->second.len == 0) return true;
    ent_t &n = E.add();
    memcpy(n.bytes, it->second.bytes, it->second.len);
    n.size = it->second.len;
    n.orig = o.addr;
    n.kind = K_SYN;
    return true;
}

/* ===================================================================== */
/* Profiling stubs (TC)                                                   */
/* ===================================================================== */
static inline xed_encoder_operand_t rip_mem(unsigned w) { return xed_mem_bd(XED_REG_RIP, xed_disp(0, 32), w); }

// Counter increment at a point where 'at' is the next original instruction (straight-line path until 'end').
// Flag-free forms: a dead register, or save/restore RAX through a slot in the region (never push/pop: P13).
static bool emit_block_counter(emitter_t &E, unsigned ctr, int at, int end, ADDRINT orig, uint16_t avoid)
{
    int skip = E.new_label();
    add_pad(E, skip, orig);
    ADDRINT caddr = dptr(D_CTR + ctr * 8);
    xed_encoder_instruction_t ei;
    if (!KnobNoCheapProf && flags_dead_from(at, end)) {      // flags dead: one add [rip+ctr],1
        xed_inst2(&ei, dstate, XED_ICLASS_ADD, 64, rip_mem(64), xed_simm0(1, 8));
        if (!add_syn(E, &ei, orig, true, caddr)) return false;
        E.bind(skip);
        return true;
    }
    int r = find_dead_reg(at, end, avoid);
    xed_reg_enum_t R = r >= 0 ? gpr64[r] : XED_REG_RAX;
    if (r < 0) {
        xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, rip_mem(64), xed_reg(XED_REG_RAX));
        if (!add_syn(E, &ei, orig, true, dptr(D_SV_RAX))) return false;
    }
    xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, xed_reg(R), rip_mem(64));
    if (!add_syn(E, &ei, orig, true, caddr)) return false;
    xed_inst2(&ei, dstate, XED_ICLASS_LEA, 64, xed_reg(R), xed_mem_bd(R, xed_disp(1, 8), 64));
    if (!add_syn(E, &ei, orig)) return false;
    xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, rip_mem(64), xed_reg(R));
    if (!add_syn(E, &ei, orig, true, caddr)) return false;
    if (r < 0) {
        xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RAX), rip_mem(64));
        if (!add_syn(E, &ei, orig, true, dptr(D_SV_RAX))) return false;
    }
    E.bind(skip);
    return true;
}

// Operand of an indirect jmp/call, taken from the FIRST EXPLICIT operand (P5: "call reg" also has a
// hidden memory operand for the return-address push).
struct target_op_t {
    bool ok, is_reg, rip;
    xed_reg_enum_t reg, base, index;
    unsigned scale;
    INT64 disp;
    ADDRINT rip_abs;
};

static target_op_t get_target_op(const oins_t &o)
{
    target_op_t t;
    memset(&t, 0, sizeof(t));
    xed_decoded_inst_t x;
    if (!xdec(&x, o.bytes, o.size)) return t;
    const xed_inst_t *xi = xed_decoded_inst_inst(&x);
    xed_operand_enum_t n0 = xed_operand_name(xed_inst_operand(xi, 0));
    if (n0 == XED_OPERAND_REG0) {
        t.is_reg = true;
        t.reg = xed_decoded_inst_get_reg(&x, XED_OPERAND_REG0);
        t.ok = xed_get_register_width_bits64(t.reg) == 64 && gpr_index(t.reg) >= 0;
        return t;
    }
    if (n0 != XED_OPERAND_MEM0) return t;
    if (xed_decoded_inst_get_memop_address_width(&x, 0) != 64) return t;
    xed_reg_enum_t seg = xed_decoded_inst_get_seg_reg(&x, 0);
    if (seg == XED_REG_FS || seg == XED_REG_GS) return t;
    t.base = xed_decoded_inst_get_base_reg(&x, 0);
    t.index = xed_decoded_inst_get_index_reg(&x, 0);
    t.scale = xed_decoded_inst_get_scale(&x, 0);
    t.disp = xed_decoded_inst_get_memory_displacement(&x, 0);
    if (t.base == XED_REG_RIP) {
        if (t.index != XED_REG_INVALID) return t;
        t.rip = true;
        t.rip_abs = o.rip_addr;
    }
    if (!fits32(t.disp)) return t;
    t.ok = true;
    return t;
}

static xed_encoder_operand_t target_mem(const target_op_t &t)
{
    if (t.rip) return rip_mem(64);
    return xed_mem_bisd(t.base, t.index, t.index == XED_REG_INVALID ? 0 : t.scale, xed_disp(t.disp, 32), 64);
}

// "mov dst, <target operand>" (dst != operand register)
static bool emit_load_target(emitter_t &E, xed_reg_enum_t dst, const target_op_t &t, ADDRINT orig)
{
    xed_encoder_instruction_t ei;
    if (t.is_reg) {
        if (t.reg == dst) return true;
        xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, xed_reg(dst), xed_reg(t.reg));
        return add_syn(E, &ei, orig);
    }
    xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, xed_reg(dst), target_mem(t));
    return add_syn(E, &ei, orig, t.rip, t.rip_abs);
}

// Flag-free indirect-target profile for jumps AND calls (P1): hash = ((T >> 4) & 15) (P9).
static bool emit_indirect_profile(emitter_t &E, const oins_t &o)
{
    if (o.site < 0) return true;
    target_op_t t = get_target_op(o);
    if (!t.ok) return true;
    const site_t &s = site_v[o.site];
    int skip = E.new_label();
    add_pad(E, skip, o.addr);
    xed_encoder_instruction_t ei;
#define SYN2(ic, a, b) do { xed_inst2(&ei, dstate, ic, 64, a, b); if (!add_syn(E, &ei, o.addr)) return false; } while (0)
#define SYN2R(ic, a, b, abs) do { xed_inst2(&ei, dstate, ic, 64, a, b); if (!add_syn(E, &ei, o.addr, true, abs)) return false; } while (0)
    SYN2R(XED_ICLASS_MOV, rip_mem(64), xed_reg(XED_REG_RAX), dptr(D_SV_RAX));
    SYN2R(XED_ICLASS_MOV, rip_mem(64), xed_reg(XED_REG_RCX), dptr(D_SV_RCX));
    if (!emit_load_target(E, XED_REG_RAX, t, o.addr)) return false;             // rax = T
    SYN2R(XED_ICLASS_MOV, rip_mem(64), xed_reg(XED_REG_RAX), dptr(D_SV_TMP));    // tmp = T
    xed_inst2(&ei, dstate, XED_ICLASS_MOVZX, 32, xed_reg(XED_REG_ECX), rip_mem(8));
    if (!add_syn(E, &ei, o.addr, true, dptr(D_SV_TMP))) return false;          // ecx = T & 0xff
    SYN2R(XED_ICLASS_LEA, xed_reg(XED_REG_RAX), rip_mem(64), dptr(D_NIB));
    xed_inst2(&ei, dstate, XED_ICLASS_MOVZX, 32, xed_reg(XED_REG_ECX), xed_mem_bisd(XED_REG_RAX, XED_REG_RCX, 1, xed_disp(0, 8), 8));
    if (!add_syn(E, &ei, o.addr)) return false;                                // ecx = slot*16
    SYN2R(XED_ICLASS_LEA, xed_reg(XED_REG_RAX), rip_mem(64), dptr(s.table));
    SYN2(XED_ICLASS_LEA, xed_reg(XED_REG_RAX), xed_mem_bisd(XED_REG_RAX, XED_REG_RCX, 1, xed_disp(0, 8), 64));
    SYN2R(XED_ICLASS_MOV, xed_reg(XED_REG_RCX), rip_mem(64), dptr(D_SV_TMP));
    SYN2(XED_ICLASS_MOV, xed_mem_bd(XED_REG_RAX, xed_disp(0, 8), 64), xed_reg(XED_REG_RCX));
    SYN2(XED_ICLASS_MOV, xed_reg(XED_REG_RCX), xed_mem_bd(XED_REG_RAX, xed_disp(8, 8), 64));
    SYN2(XED_ICLASS_LEA, xed_reg(XED_REG_RCX), xed_mem_bd(XED_REG_RCX, xed_disp(1, 8), 64));
    SYN2(XED_ICLASS_MOV, xed_mem_bd(XED_REG_RAX, xed_disp(8, 8), 64), xed_reg(XED_REG_RCX));
    SYN2R(XED_ICLASS_MOV, xed_reg(XED_REG_RCX), rip_mem(64), dptr(D_SV_RCX));
    SYN2R(XED_ICLASS_MOV, xed_reg(XED_REG_RAX), rip_mem(64), dptr(D_SV_RAX));
#undef SYN2
#undef SYN2R
    E.bind(skip);
    return true;
}

/* ===================================================================== */
/* TC construction                                                        */
/* ===================================================================== */
static emitter_t tcE;
struct pad_rec_t { ADDRINT pad, target; };
static vector<pad_rec_t> prof_pads;

static bool emit_tc_routine(emitter_t &E, rtn_t &r)
{
    bool prof = !KnobNoProfile;
    int rend = r.first_ins + r.n_ins;
    ent_t &tr = E.add();                    // trampoline space, written at TC2 commit
    tr.kind = K_TRAMP; tr.size = 5; memcpy(tr.bytes, NOPS[5], 5); tr.orig = r.addr;
    E.set_land(r.addr);
    add_pad(E, -1, r.addr);                 // head pad (routine entry; becomes "jmp tramp" at commit)
    for (int b = r.first_bbl; b < r.first_bbl + r.n_bbl; b++) {
        bbl_t &B = bbl_v[b];
        E.set_land(ins_v[B.first].addr);
        // Where to count the block: at its start, or (cheap form) at the first point inside the block
        // where the flags are dead - any point of the straight-line block runs once per execution.
        int cpos = B.first;
        if (prof && B.ctr >= 0 && !KnobNoCheapProf)
            for (int p = B.first; p <= B.last; p++) if (flags_dead_from(p, rend)) { cpos = p; break; }
        for (int i = B.first; i <= B.last; i++) {
            const oins_t &o = ins_v[i];
            if (prof && B.ctr >= 0 && i == cpos && !emit_block_counter(E, B.ctr, i, rend, o.addr, r.fold_spares)) return false;
            E.set_land(o.addr);
            if (prof && !emit_indirect_profile(E, o)) return false;
            if (!emit_ins(E, i)) return false;
            if (prof && (o.flags & IF_JCC) && B.ft_own && !emit_block_counter(E, B.ft_ctr, i + 1, rend, o.addr, r.fold_spares)) return false;
        }
    }
    const oins_t &last = ins_v[rend - 1];
    if (!(last.flags & IF_NO_FT))           // falls off the end of the routine: continue at the next original address
        if (!add_branch(E, XED_ICLASS_JMP, T_ORIG, last.addr + last.size, -1, last.addr)) return false;
    return true;
}

static int build_tc()
{
    emitter_t &E = tcE;
    E.base = (ADDRINT)tc;
    E.cap = tc_cap;
    vector<int> head_ent(rtn_v.size(), -1);
    for (size_t ri = 0; ri < rtn_v.size(); ri++) {
        rtn_t &r = rtn_v[ri];
        if (!r.translated) continue;
        head_ent[ri] = (int)E.e.size();
        if (!emit_tc_routine(E, r)) return -1;
    }
    if (!layout_and_copy(E, false)) return -1;
    if (!verify_branches(E, false)) return -1;
    tc_size = E.size;
    for (size_t ri = 0; ri < rtn_v.size(); ri++) {
        if (head_ent[ri] < 0) continue;
        rtn_v[ri].tc_tramp = E.e[head_ent[ri]].new_addr;
        rtn_v[ri].tc_head = E.e[head_ent[ri] + 1].new_addr;
    }
    tc_land.reserve(E.land.size() * 2);
    for (auto &kv : E.land) tc_land[kv.first] = E.entry_addr(kv.second);
    for (size_t i = 0; i < E.e.size(); i++) {
        const ent_t &n = E.e[i];
        if (n.kind == K_PAD && n.label >= 0) prof_pads.push_back({n.new_addr, E.entry_addr(E.labels[n.label])});
    }
    return 0;
}

// Probe every translated routine's original entry to its TC copy. If Pin refuses a probe (tiny routine,
// probe area overlapping the next symbol, ...), the routine keeps running correctly: calls from TC/TC2
// code still reach its TC copy, other entries run the untouched original code.
static void commit_tc()
{
    unsigned refused = 0;
    for (auto &r : rtn_v) {
        if (!r.translated) continue;
        if (r.no_probe) { refused++; continue; }
        RTN rtn = RTN_FindByAddress(r.addr);
        if (!RTN_Valid(rtn) || RTN_Address(rtn) != r.addr) { refused++; continue; }
        AFUNPTR p = RTN_ReplaceProbed(rtn, (AFUNPTR)r.tc_head);
        if (p == NULL) {
            refused++;
            if (KnobVerbose) cerr << "probe refused by Pin for " << r.name << " (original entry stays)" << endl;
        }
    }
    if (refused && KnobVerbose) cerr << "note: " << refused << " routine entries were not probed" << endl;
}

/* ===================================================================== */
/* Profile                                                                */
/* ===================================================================== */
static void derive_profile()
{
    UINT64 *c = counters();
    vector<char> known(bbl_v.size(), 0);
    for (size_t k = 0; k < bbl_v.size(); k++) {
        bbl_t &b = bbl_v[k];
        b.count = b.ctr >= 0 ? c[b.ctr] : 0;
        b.ft = b.ft_ctr >= 0 ? c[b.ft_ctr] : 0;
        known[k] = b.ctr >= 0 || b.dpred < 0;
    }
    // Sparse profiling: derive the blocks without a counter from their single predecessor.
    for (bool again = true; again;) {
        again = false;
        for (size_t k = 0; k < bbl_v.size(); k++) {
            bbl_t &b = bbl_v[k];
            if (known[k] || !known[b.dpred]) continue;
            const bbl_t &p = bbl_v[b.dpred];
            UINT64 ft = p.ft > p.count ? p.count : p.ft;
            b.count = b.dtaken ? p.count - ft : p.count;
            known[k] = 1;
            again = true;
        }
    }
    for (auto &b : bbl_v) if (b.ft > b.count) b.ft = b.count;
    for (auto &r : rtn_v) {
        r.hot = 0;
        for (int b = r.first_bbl; b < r.first_bbl + r.n_bbl; b++) r.hot += bbl_v[b].count;
    }
}

static int disable_profiling()
{
    int fails = 0;
    for (auto &p : prof_pads) if (!patch_pad_to_jump(p.pad, p.target)) fails++;
    if (fails) TOOL_WARN(fails << " profiling stubs could not be disabled (stay active; output unaffected)");
    return fails;
}

// Edge profile in the exercise-4 CSV format: addr, exec, taken, fallthru[, target, count]*, hottest first.
void dump_profile()
{
    std::ofstream out("edge-profile.csv");
    vector<int> order;
    for (size_t b = 0; b < bbl_v.size(); b++) if (bbl_v[b].count) order.push_back((int)b);
    sort(order.begin(), order.end(), [](int a, int b) {
        if (bbl_v[a].count != bbl_v[b].count) return bbl_v[a].count > bbl_v[b].count;
        return ins_v[bbl_v[a].first].addr < ins_v[bbl_v[b].first].addr; });
    for (int b : order) {
        const bbl_t &B = bbl_v[b];
        const oins_t &last = ins_v[B.last];
        UINT64 taken = (last.flags & IF_JCC) ? B.count - B.ft : 0, ft = (last.flags & IF_JCC) ? B.ft : 0;
        out << "0x" << hex << ins_v[B.first].addr << dec << ", " << B.count << ", " << taken << ", " << ft;
        for (int i = B.first; i <= B.last; i++) {
            if (ins_v[i].site < 0) continue;
            const UINT64 *t = (const UINT64 *)dptr(site_v[ins_v[i].site].table);
            vector<pair<UINT64, ADDRINT>> v;
            for (int k = 0; k < SITE_SLOTS; k++) if (t[2 * k + 1]) v.push_back(make_pair(t[2 * k + 1], (ADDRINT)t[2 * k]));
            sort(v.rbegin(), v.rend());
            for (auto &p : v) out << ", 0x" << hex << p.second << dec << ", " << p.first;
        }
        out << "\n";
    }
    out.flush();
}

// dump of the TC instruction list (debug)
void dump_entire_instr_map()
{
    for (auto &n : tcE.e) {
        if (!n.size) continue;
        cerr << "orig 0x" << hex << n.orig << dec << " ";
        dump_instr_from_mem((ADDRINT *)n.new_addr, n.new_addr);
    }
}

/* ===================================================================== */
/* TC2 construction                                                       */
/* ===================================================================== */
static emitter_t tc2E;
static unsigned n_devirt_calls = 0, n_devirt_jmps = 0, n_devirt_targets = 0;
static unsigned n_inverted = 0, n_jmp_added = 0, n_jmp_removed = 0, n_cold_blocks = 0;

/* ---------------- de-virtualization ---------------- */
// Profiled targets of an indirect site that qualify for guarding (most frequent first).
static void devirt_targets(const oins_t &o, vector<ADDRINT> &out)
{
    out.clear();
    if (o.site < 0) return;
    const UINT64 *tab = (const UINT64 *)dptr(site_v[o.site].table);
    std::map<ADDRINT, UINT64> agg;
    UINT64 total = 0;
    for (int k = 0; k < SITE_SLOTS; k++) {
        UINT64 c = tab[2 * k + 1];
        if (!c) continue;
        agg[(ADDRINT)tab[2 * k]] += c;
        total += c;
    }
    if (total < KnobDevirtMinCount.Value()) return;
    vector<pair<UINT64, ADDRINT>> v;
    for (auto &kv : agg) v.push_back(make_pair(kv.second, kv.first));
    sort(v.begin(), v.end(), [](const pair<UINT64, ADDRINT> &a, const pair<UINT64, ADDRINT> &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second; });
    UINT64 covered = 0;
    for (auto &p : v) {
        if (out.size() >= KnobDevirtMaxTargets.Value()) break;
        if (p.first * 100 < total * KnobDevirtMinShare.Value()) break;
        ADDRINT t = p.second;
        if (t > 0x7FFFFFFFULL) continue;                         // must fit a sign-extended imm32
        auto it = ins_index.find(t);                             // must have a TC2 copy
        if (it == ins_index.end()) continue;
        const rtn_t &tr = rtn_v[bbl_v[ins_v[it->second].bbl].rtn];
        if (!tr.in_tc2) continue;
        if ((o.flags & IF_IND_CALL) && tr.addr != t) continue;   // calls: only routine entries
        out.push_back(t);
        covered += p.first;
    }
    if (out.empty() || covered * 100 < total * KnobDevirtMinPct.Value()) out.clear();
}

// Indirect CALL: cmp <operand>, T ; jne next ; call T' ; jmp done ; ... ; original call ; done:
// (flags are dead at a call by the ABI, so CMP is allowed here)
static bool emit_devirt_call(emitter_t &E, const oins_t &o, const vector<ADDRINT> &tg, const target_op_t &t)
{
    int done = E.new_label();
    for (ADDRINT T : tg) {
        int next = E.new_label();
        xed_encoder_instruction_t ei;
        if (t.is_reg) {
            xed_inst2(&ei, dstate, XED_ICLASS_CMP, 64, xed_reg(t.reg), xed_simm0((xed_int32_t)T, 32));
            if (!add_syn(E, &ei, o.addr)) return false;
        } else {
            xed_inst2(&ei, dstate, XED_ICLASS_CMP, 64, target_mem(t), xed_simm0((xed_int32_t)T, 32));
            if (!add_syn(E, &ei, o.addr, t.rip, t.rip_abs)) return false;
        }
        if (!add_branch(E, XED_ICLASS_JNZ, T_LABEL, 0, next, o.addr)) return false;
        if (!add_branch(E, XED_ICLASS_CALL_NEAR, T_ORIG, T, -1, o.addr)) return false;
        if (!add_branch(E, XED_ICLASS_JMP, T_LABEL, 0, done, o.addr)) return false;
        E.bind(next);
    }
    if (!emit_orig(E, o)) return false;
    E.bind(done);
    n_devirt_calls++;
    n_devirt_targets += tg.size();
    return true;
}

// Indirect JMP: flags may be live (P1), so the guard is flag-free:
//   mov [slot],rcx ; mov rcx,<operand> ; lea rcx,[rcx-T1] ; jrcxz hit1 ; lea rcx,[rcx+T1-T2] ; jrcxz hit2 ...
//   mov rcx,[slot] ; original jmp ; hitK: mov rcx,[slot] ; jmp TK'
static bool emit_devirt_jmp(emitter_t &E, const oins_t &o, const vector<ADDRINT> &tg, const target_op_t &t)
{
    xed_encoder_instruction_t ei;
    ADDRINT slot = dptr(D_SV_GUARD);
    xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, rip_mem(64), xed_reg(XED_REG_RCX));
    if (!add_syn(E, &ei, o.addr, true, slot)) return false;
    if (!emit_load_target(E, XED_REG_RCX, t, o.addr)) return false;
    vector<int> hits;
    INT64 prev = 0;
    for (ADDRINT T : tg) {
        INT64 d = prev - (INT64)T;
        if (!fits32(d)) return false;
        xed_inst2(&ei, dstate, XED_ICLASS_LEA, 64, xed_reg(XED_REG_RCX), xed_mem_bd(XED_REG_RCX, xed_disp(d, 32), 64));
        if (!add_syn(E, &ei, o.addr)) return false;
        int h = E.new_label();
        hits.push_back(h);
        if (!add_branch(E, XED_ICLASS_JRCXZ, T_LABEL, 0, h, o.addr, 8)) return false;
        prev = (INT64)T;
    }
    xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RCX), rip_mem(64));
    if (!add_syn(E, &ei, o.addr, true, slot)) return false;
    if (!emit_orig(E, o)) return false;
    for (size_t k = 0; k < tg.size(); k++) {
        E.bind(hits[k]);
        xed_inst2(&ei, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RCX), rip_mem(64));
        if (!add_syn(E, &ei, o.addr, true, slot)) return false;
        if (!add_branch(E, XED_ICLASS_JMP, T_ORIG, tg[k], -1, o.addr)) return false;
    }
    n_devirt_jmps++;
    n_devirt_targets += tg.size();
    return true;
}

// Emit one original instruction into TC2 (with de-virtualization of indirect jumps/calls).
static bool emit_tc2_ins(emitter_t &E, const oins_t &o)
{
    if (!KnobNoDevirt && (o.flags & (IF_IND_CALL | IF_IND_JMP)) && o.site >= 0) {
        vector<ADDRINT> tg;
        devirt_targets(o, tg);
        if (!tg.empty()) {
            target_op_t t = get_target_op(o);
            if (t.ok) {
                size_t mark = E.e.size();
                bool ok = (o.flags & IF_IND_CALL) ? emit_devirt_call(E, o, tg, t) : emit_devirt_jmp(E, o, tg, t);
                if (ok) return true;
                E.e.resize(mark);                                 // guard not possible: plain copy
            }
        }
    }
    return emit_ins(E, (int)(&o - &ins_v[0]));
}

/* ---------------- code reordering ---------------- */
// link1.txt: inverse of each conditional jump (JRCXZ/LOOP never reach here: such routines are skipped).
static xed_iclass_enum_t invert_jcc(xed_iclass_enum_t ic)
{
    switch (ic) {
    case XED_ICLASS_JB: return XED_ICLASS_JNB;
    case XED_ICLASS_JBE: return XED_ICLASS_JNBE;
    case XED_ICLASS_JL: return XED_ICLASS_JNL;
    case XED_ICLASS_JLE: return XED_ICLASS_JNLE;
    case XED_ICLASS_JNB: return XED_ICLASS_JB;
    case XED_ICLASS_JNBE: return XED_ICLASS_JBE;
    case XED_ICLASS_JNL: return XED_ICLASS_JL;
    case XED_ICLASS_JNLE: return XED_ICLASS_JLE;
    case XED_ICLASS_JNO: return XED_ICLASS_JO;
    case XED_ICLASS_JNP: return XED_ICLASS_JP;
    case XED_ICLASS_JNS: return XED_ICLASS_JS;
    case XED_ICLASS_JNZ: return XED_ICLASS_JZ;
    case XED_ICLASS_JO: return XED_ICLASS_JNO;
    case XED_ICLASS_JP: return XED_ICLASS_JNP;
    case XED_ICLASS_JS: return XED_ICLASS_JNS;
    case XED_ICLASS_JZ: return XED_ICLASS_JNZ;
    default: return XED_ICLASS_INVALID;
    }
}

// Block (global index) that starts at original address a inside routine r, or -1.
static int block_at(const rtn_t &r, ADDRINT a)
{
    auto it = ins_index.find(a);
    if (it == ins_index.end()) return -1;
    const oins_t &o = ins_v[it->second];
    if (!(o.flags & IF_LEADER)) return -1;
    int b = o.bbl;
    if (b < r.first_bbl || b >= r.first_bbl + r.n_bbl) return -1;
    return b;
}

// Emit block b knowing which block follows it in the new layout (next, -1 if none):
// keep/invert the conditional jump, add a jmp where a fall-through was broken, drop a jmp whose target follows.
static bool emit_tc2_block(emitter_t &E, const rtn_t &r, int b, int next)
{
    const bbl_t &B = bbl_v[b];
    for (int i = B.first; i < B.last; i++) {
        E.set_land(ins_v[i].addr);
        if (!emit_tc2_ins(E, ins_v[i])) return false;
    }
    const oins_t &o = ins_v[B.last];
    E.set_land(o.addr);
    ADDRINT ft_addr = o.addr + o.size;
    int fb = (b + 1 < r.first_bbl + r.n_bbl) ? b + 1 : -1;
    if (o.flags & IF_JCC) {
        int tb = block_at(r, o.targ);
        if (fb >= 0 && next == fb) return emit_orig(E, o);
        xed_iclass_enum_t inv = invert_jcc((xed_iclass_enum_t)o.iclass);
        if (tb >= 0 && next == tb && inv != XED_ICLASS_INVALID) {
            n_inverted++;
            return add_branch(E, inv, T_ORIG, ft_addr, -1, o.addr);
        }
        if (!emit_orig(E, o)) return false;
        n_jmp_added++;
        return add_branch(E, XED_ICLASS_JMP, T_ORIG, ft_addr, -1, o.addr);
    }
    if (o.flags & IF_JMP) {
        int tb = block_at(r, o.targ);
        if (tb >= 0 && next == tb) { n_jmp_removed++; return true; }
        return emit_orig(E, o);
    }
    if (!emit_tc2_ins(E, o)) return false;
    if (o.flags & IF_NO_FT) return true;
    if (fb >= 0 && next == fb) return true;
    if (fb >= 0) n_jmp_added++;
    return add_branch(E, XED_ICLASS_JMP, T_ORIG, ft_addr, -1, o.addr);
}

// Pettis-Hansen chain merging on the profiled CFG of one routine. Returns the hot layout; cold blocks
// (count below the threshold) are appended to 'cold' for the cold region at the end of TC2.
static void reorder_routine_blocks(const rtn_t &r, UINT64 cold_thresh, vector<int> &hot, vector<int> &cold)
{
    int nb = r.n_bbl, f = r.first_bbl;
    vector<char> is_hot(nb);
    for (int k = 0; k < nb; k++) is_hot[k] = (k == 0) || bbl_v[f + k].count >= cold_thresh;
    struct edge_t { UINT64 w; int s, t; };
    vector<edge_t> edges;
    for (int k = 0; k < nb; k++) {
        if (!is_hot[k]) continue;
        const bbl_t &B = bbl_v[f + k];
        const oins_t &o = ins_v[B.last];
        int fb = k + 1 < nb ? k + 1 : -1;
        if (o.flags & IF_JCC) {
            int tb = block_at(r, o.targ);
            if (tb >= 0 && is_hot[tb - f]) edges.push_back({B.count - B.ft, k, tb - f});
            if (fb >= 0 && is_hot[fb]) edges.push_back({B.ft, k, fb});
        } else if (o.flags & IF_JMP) {
            int tb = block_at(r, o.targ);
            if (tb >= 0 && is_hot[tb - f]) edges.push_back({B.count, k, tb - f});
        } else if (!(o.flags & IF_NO_FT)) {
            if (fb >= 0 && is_hot[fb]) edges.push_back({B.count, k, fb});
        }
    }
    sort(edges.begin(), edges.end(), [](const edge_t &a, const edge_t &b) {
        if (a.w != b.w) return a.w > b.w;
        if (a.s != b.s) return a.s < b.s;
        return a.t < b.t; });
    vector<int> chain(nb), nxt(nb, -1), head(nb), tail(nb);
    for (int k = 0; k < nb; k++) { chain[k] = k; head[k] = k; tail[k] = k; }
    for (auto &e : edges) {
        if (e.w == 0) break;
        if (e.t == 0 || e.s == e.t) continue;           // the entry block stays first
        int cs = chain[e.s], ct = chain[e.t];
        if (cs == ct || tail[cs] != e.s || head[ct] != e.t) continue;
        nxt[e.s] = e.t;
        tail[cs] = tail[ct];
        for (int k = e.t; k >= 0; k = nxt[k]) chain[k] = cs;
    }
    // Chains: the entry chain first, the others by decreasing hottest block.
    vector<int> heads;
    for (int k = 0; k < nb; k++) if (is_hot[k] && chain[k] == k) heads.push_back(k);
    auto chain_heat = [&](int c) { UINT64 m = 0; for (int k = c; k >= 0; k = nxt[k]) m = max(m, bbl_v[f + k].count); return m; };
    vector<pair<UINT64, int>> order;
    for (int c : heads) if (c != chain[0]) order.push_back(make_pair(chain_heat(c), c));
    sort(order.begin(), order.end(), [](const pair<UINT64, int> &a, const pair<UINT64, int> &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second; });
    hot.clear();
    for (int k = chain[0]; k >= 0; k = nxt[k]) hot.push_back(f + k);
    for (auto &p : order) for (int k = p.second; k >= 0; k = nxt[k]) hot.push_back(f + k);
    for (int k = 0; k < nb; k++) if (!is_hot[k]) cold.push_back(f + k);
}

static int build_tc2()
{
    emitter_t &E = tc2E;
    E.base = (ADDRINT)tc2;
    E.cap = tc2_cap;
    bool reorder = !KnobNoReorder;
    UINT64 total = 0;
    for (auto &b : bbl_v) total += b.count;
    UINT64 cold_thresh = (UINT64)((double)total * KnobColdPpm.Value() / 1e6);
    if (cold_thresh < 1) cold_thresh = 1;
    // Routines that get a TC2 copy: every translated routine that ran during the window.
    vector<int> rts;
    for (size_t ri = 0; ri < rtn_v.size(); ri++) {
        rtn_t &r = rtn_v[ri];
        r.in_tc2 = r.translated && r.hot > 0;
        if (r.in_tc2) rts.push_back((int)ri);
    }
    if (reorder)   // hot routines sorted by hotness
        stable_sort(rts.begin(), rts.end(), [](int a, int b) { return rtn_v[a].hot > rtn_v[b].hot; });
    vector<int> cold;
    for (int ri : rts) {
        const rtn_t &r = rtn_v[ri];
        vector<int> order;
        if (reorder) reorder_routine_blocks(r, cold_thresh, order, cold);
        else for (int b = r.first_bbl; b < r.first_bbl + r.n_bbl; b++) order.push_back(b);
        E.set_land(r.addr);
        for (size_t k = 0; k < order.size(); k++)
            if (!emit_tc2_block(E, r, order[k], k + 1 < order.size() ? order[k + 1] : -1)) return -1;
    }
    // One cold region at the end (original order inside each routine).
    n_cold_blocks = cold.size();
    for (size_t k = 0; k < cold.size(); k++) {
        const rtn_t &r = rtn_v[bbl_v[cold[k]].rtn];
        int next = (k + 1 < cold.size() && bbl_v[cold[k + 1]].rtn == bbl_v[cold[k]].rtn) ? cold[k + 1] : -1;
        if (!emit_tc2_block(E, r, cold[k], next)) return -1;
    }
    if (!layout_and_copy(E, true)) return -1;
    if (!verify_branches(E, true)) return -1;
    tc2_size = E.size;
    for (int ri : rts) {
        rtn_t &r = rtn_v[ri];
        auto it = E.land.find(r.addr);
        if (it == E.land.end()) { r.in_tc2 = false; continue; }
        r.tc2_head = E.entry_addr(it->second);
    }
    for (auto &kv : E.land) tc2_land[kv.first] = E.entry_addr(kv.second);
    if (KnobVerbose)
        cerr << "TC2: " << rts.size() << " routines, " << tc2_size << " bytes; devirt calls " << n_devirt_calls << " jmps " << n_devirt_jmps
             << " targets " << n_devirt_targets << "; inverted " << n_inverted << " jmp added " << n_jmp_added << " removed " << n_jmp_removed
             << " cold blocks " << n_cold_blocks << endl;
    return 0;
}

// Switch each TC routine head to its TC2 copy: write "jmp rel32" into the (never executed) trampoline,
// then one aligned 4-byte store turns the head pad into "jmp short trampoline" (P4).
static int commit_tc2()
{
    int n = 0;
    for (auto &r : rtn_v) {
        if (!r.in_tc2) continue;
        uint8_t j[5];
        j[0] = 0xE9;
        INT64 d = (INT64)r.tc2_head - (INT64)(r.tc_tramp + 5);
        if (!fits32(d)) { TOOL_ERROR("TC2 commit displacement"); continue; }
        int32_t d32 = (int32_t)d;
        memcpy(j + 1, &d32, 4);
        memcpy((void *)r.tc_tramp, j, 5);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (!patch_pad_to_jump(r.tc_head, r.tc_tramp)) { TOOL_ERROR("TC2 commit pad patch failed"); continue; }
        n++;
    }
    return n;
}

/* ===================================================================== */
/* The TC2 thread                                                         */
/* ===================================================================== */
static void sleep_ms(unsigned ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void create_tc2_thread_func(void *v)
{
    while (tc_state == 0) sleep_ms(5);
    if (tc_state < 0) return;
    // Wait for the window with a clock check (P11): short sleeps, never one long sleep.
    double want = (double)KnobNumSecsDuringProfile.Value();
    for (;;) {
        struct timespec t;
        now(&t);
        double left = want - tdiff(t_app_start, t);
        if (left <= 0) break;
        sleep_ms(left > 0.02 ? 10 : 1);
    }
    if (!pads_ok) return;                   // cannot patch safely: stay in TC with profiling on
    if (!KnobNoProfile) disable_profiling();
    now(&t_prof_end);
    prof_ended = true;
    double win = tdiff(t_app_start, t_prof_end);
    if (win > want + 0.25) TOOL_WARN("profiling window overshoot: " << win << " s for prof_time " << want);
    derive_profile();
    if (KnobNoTC2) return;
    if (build_tc2() < 0) { TOOL_ERROR("TC2 build failed; execution stays in TC"); return; }
    now(&t_tc2_built);
    tc2_built = true;
    if (KnobDumpTranslatedCode2) { cerr << "Translation Cache 2 dump:" << endl; dump_tc(tc2, tc2_size); }
    if (!KnobDoNotCommitTranslatedCode) {
        int n = commit_tc2();
        now(&t_tc2_committed);
        tc2_committed = true;
        if (KnobVerbose) cerr << "committed " << n << " routines to TC2 (" << tc2_size << " bytes)" << endl;
    }
}

/* ===================================================================== */
/* Exit and timing report                                                 */
/* ===================================================================== */
typedef VOID (*EXITFUNCPTR)(INT code);
EXITFUNCPTR origExit;
static volatile bool fini_done = false;

VOID Fini(INT32 code, VOID *v)
{
    if (fini_done) return;
    fini_done = true;
    now(&t_exit);
    if (KnobDumpProfile) { if (!prof_ended) derive_profile(); dump_profile(); }
    if (tc_state <= 0) { cerr << "[time] no translated code ran" << endl; return; }
    double tc_build = tdiff(t_tc_start, t_app_start);
    double e2e = tdiff(t_app_start, t_exit);
    cerr << fixed << setprecision(3);
    cerr << "[time] TC build (not counted): " << tc_build << " s" << endl;
    if (prof_ended) cerr << "[time] profiling window: " << tdiff(t_app_start, t_prof_end) << " s (prof_time " << KnobNumSecsDuringProfile.Value() << ")" << endl;
    else cerr << "[time] profiling window: program exited before prof_time ended (" << e2e << " s)" << endl;
    if (tc2_built) cerr << "[time] TC2 build: " << tdiff(t_prof_end, t_tc2_built) << " s" << endl;
    if (tc2_committed) {
        cerr << "[time] TC2 commit: " << tdiff(t_tc2_built, t_tc2_committed) << " s" << endl;
        cerr << "[time] TC2 execution: " << tdiff(t_tc2_committed, t_exit) << " s" << endl;
    } else cerr << "[time] TC2 was not committed before exit" << endl;
    cerr << "[time] honest end-to-end total: " << e2e << " s" << endl;
    // Base tool's line, for compatibility only (prof_time + time from the TC2 commit to exit).
    double compat = tc2_committed ? (double)KnobNumSecsDuringProfile.Value() + tdiff(t_tc2_committed, t_exit) : e2e;
    cerr << " Translated code run (including profiling) took: " << compat << " seconds" << endl;
    cerr.unsetf(ios::floatfield);
    if (n_errors) cerr << "ERROR: " << n_errors << " error(s) reported during the run" << endl;
}

VOID ExitInProbeMode(INT code)
{
    Fini(code, 0);
    (*origExit)(code);
}

/* ===================================================================== */
/* create_tc: image load callback                                         */
/* ===================================================================== */
VOID create_tc(IMG img, VOID *v)
{
    RTN exitRtn = RTN_FindByName(img, "_exit");
    if (RTN_Valid(exitRtn) && RTN_IsSafeForProbedReplacement(exitRtn)) {
        origExit = (EXITFUNCPTR)RTN_ReplaceProbed(exitRtn, AFUNPTR(ExitInProbeMode));
        if (IMG_IsMainExecutable(img)) exit_rtn_addr = RTN_Address(exitRtn);
    }
    if (!IMG_IsMainExecutable(img)) return;
    if (KnobDumpOrigCode) dump_image_instrs(img);
    now(&t_tc_start);
    pads_ok = check_nops();
    if (!pads_ok) TOOL_WARN("NOP encodings are not as assumed; profiling stays on and TC2 is not used");

    img_low = IMG_LowAddress(img);
    img_high = IMG_HighAddress(img) + 1;
    size_t text_bytes = 0;
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
        if (SEC_IsExecutable(sec)) text_bytes += SEC_Size(sec);

    if (analyze_image(img) < 0) { TOOL_ERROR("image analysis failed"); tc_state = -1; return; }
    compute_stack_folds();
    if (allocate_region(text_bytes) < 0) { tc_state = -1; return; }
    if (build_tc() < 0) { TOOL_ERROR("TC build failed; running the original code"); tc_state = -1; return; }
    if (KnobDumpTranslatedCode) { cerr << "Translation Cache dump:" << endl; dump_tc(tc, tc_size); }
    if (KnobVerbose) {
        unsigned nt = 0;
        for (auto &r : rtn_v) nt += r.translated;
        cerr << "translated " << nt << "/" << rtn_v.size() << " routines, " << ins_v.size() << " instructions, "
             << bbl_v.size() << " blocks, " << site_v.size() << " indirect sites, TC " << tc_size << " bytes" << endl;
    }
    if (KnobDoNotCommitTranslatedCode) { tc_state = -1; return; }
    commit_tc();
    now(&t_app_start);
    tc_state = 1;
}

/* ===================================================================== */
/* main                                                                   */
/* ===================================================================== */
INT32 Usage()
{
    cerr << "This tool translates routines of an Intel(R) 64 binary" << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) return Usage();
    PIN_InitSymbols();
    IMG_AddInstrumentFunction(create_tc, 0);
    THREADID tid = PIN_SpawnInternalThread(create_tc2_thread_func, NULL, 0, NULL);
    if (tid == INVALID_THREADID) TOOL_ERROR("failed to spawn the TC2 thread");
    PIN_StartProgramProbed();
    return 0;
}

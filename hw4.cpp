/*BEGIN_LEGAL
Intel Open Source License
Copyright (c) 2002-2011 Intel Corporation. All rights reserved.
END_LEGAL */
/* ===================================================================== */
/*! @file
 * Exercise 4 - optimized probe-mode edge profiler
 * Generates edge-profile.csv sorted from hottest to coldest BBLs,
 * avoids saving/restoring dead registers (fixed partial register bugs), 
 * prevents RTN_Open leaks, skips untranslatable routines, and flushes output.
 */

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
#include <malloc.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <values.h>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <time.h>

using namespace std;

/*======================================================================*/
/* commandline switches                                                 */
/*======================================================================*/
KNOB<BOOL> KnobVerbose(KNOB_MODE_WRITEONCE, "pintool", "verbose", "0", "Verbose run");
KNOB<BOOL> KnobDumpOrigCode(KNOB_MODE_WRITEONCE, "pintool", "dump_orig_code", "0", "Dump Original non-translated Code");
KNOB<BOOL> KnobDumpTranslatedCode(KNOB_MODE_WRITEONCE, "pintool", "dump_tc", "0", "Dump Translated Code");
KNOB<BOOL> KnobDoNotCommitTranslatedCode(KNOB_MODE_WRITEONCE, "pintool", "no_tc_commit", "0", "Do not commit translated code");
KNOB<UINT> KnobNumSecsDuringProfile(KNOB_MODE_WRITEONCE, "pintool", "prof_time", "2", "Number of seconds for collecting BBL counters");
KNOB<BOOL> KnobDumpProfile(KNOB_MODE_WRITEONCE, "pintool", "dump_prof", "0", "Dump profiling information");
KNOB<BOOL> KnobNoProfile(KNOB_MODE_WRITEONCE, "pintool", "no_prof", "0", "Do not collect profile information");

std::ofstream* out = 0;

#if defined(TARGET_IA32E)
    xed_state_t dstate = {XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b};
#else
    xed_state_t dstate = { XED_MACHINE_MODE_LEGACY_32, XED_ADDRESS_WIDTH_32b};
#endif

const unsigned max_inst_len = XED_MAX_INSTRUCTION_BYTES;
ADDRINT lowest_sec_addr = 0;
ADDRINT highest_sec_addr = 0;
char *tc = nullptr;
unsigned tc_size = 0;
unsigned max_tc_size = 0;
volatile bool tc_ready = false;
ADDRINT *jump_to_orig_addr_map = nullptr;
unsigned jump_to_orig_addr_num = 0;

typedef enum { RegularIns = 0, RtnHeadIns, ProfilingIns } ins_enum_t;

typedef struct {
    ADDRINT orig_ins_addr;
    ADDRINT new_ins_addr;
    ADDRINT orig_targ_addr;
    ADDRINT orig_rip_addr;
    ins_enum_t ins_type;
    char encoded_ins[XED_MAX_INSTRUCTION_BYTES];
    unsigned size;
    int targ_map_entry;
    unsigned bbl_num;
    xed_category_enum_t xed_category;
} instr_map_t;

instr_map_t *instr_map = NULL;
unsigned num_of_instr_map_entries = 0;
unsigned max_ins_count = 0;

#define MAX_TARG_ADDRS 0x3

typedef struct {
  UINT64 counter;
  UINT64 fallthru_counter;
  ADDRINT targ_addr[MAX_TARG_ADDRS+1];
  UINT64  targ_count[MAX_TARG_ADDRS+1];
  unsigned starting_ins_entry;
  unsigned terminating_ins_entry;
  ADDRINT first_ins_addr;       
  unsigned ends_with_cond_br;   
} bbl_map_t;

bbl_map_t *bbl_map;
unsigned bbl_num = 0;
std::map<ADDRINT, unsigned> entry_map;
unsigned max_rtn_count = 0;
struct timespec start_running_time;
struct timespec end_running_time;

/* ============================================================= */
/* Dead-register analysis for the profiling stubs                */
/* ============================================================= */
bool insKillsFullReg(INS ins, LEVEL_BASE::REG full_reg) {
    if (INS_IsPredicated(ins)) return false;

    xed_iclass_enum_t iclass = (xed_iclass_enum_t)INS_Opcode(ins);
    if ((iclass == XED_ICLASS_XOR || iclass == XED_ICLASS_SUB) && INS_OperandCount(ins) >= 2 &&
        INS_OperandIsReg(ins, 0) && INS_OperandIsReg(ins, 1) && INS_OperandReg(ins, 0) == INS_OperandReg(ins, 1)) {
        LEVEL_BASE::REG r = INS_OperandReg(ins, 0);
        if (LEVEL_BASE::REG_FullRegName(r) == full_reg && (LEVEL_BASE::REG_is_gr64(r) || LEVEL_BASE::REG_is_gr32(r))) return true;
    }

    bool fully_written = false;
    for (UINT32 i = 0; i < INS_MaxNumWRegs(ins); i++) {
        LEVEL_BASE::REG w = INS_RegW(ins, i);
        if (LEVEL_BASE::REG_FullRegName(w) == full_reg && (LEVEL_BASE::REG_is_gr64(w) || LEVEL_BASE::REG_is_gr32(w))) {
            fully_written = true; break;
        }
    }
    if (!fully_written) return false;

    for (UINT32 i = 0; i < INS_MaxNumRRegs(ins); i++) {
        if (LEVEL_BASE::REG_FullRegName(INS_RegR(ins, i)) == full_reg) return false;
    }
    return true;
}

#define MAX_DEAD_REG_SCAN_DEPTH 16
bool isRegDeadOnFallthruPath(INS branch_ins, LEVEL_BASE::REG full_reg) {
    unsigned depth = 0;
    for (INS ins = INS_Next(branch_ins); INS_Valid(ins) && depth < MAX_DEAD_REG_SCAN_DEPTH; ins = INS_Next(ins), depth++) {
        if (INS_IsCall(ins) || INS_IsRet(ins) || INS_IsSyscall(ins) || INS_IsInterrupt(ins) ||
            INS_IsDirectControlFlow(ins) || INS_IsIndirectControlFlow(ins)) return false;

        if (insKillsFullReg(ins, full_reg)) return true;

        for (UINT32 i = 0; i < INS_MaxNumRRegs(ins); i++)
            if (LEVEL_BASE::REG_FullRegName(INS_RegR(ins, i)) == full_reg) return false;
        for (UINT32 i = 0; i < INS_MaxNumWRegs(ins); i++)
            if (LEVEL_BASE::REG_FullRegName(INS_RegW(ins, i)) == full_reg) return false;
    }
    return false;
}

/* ============================================================= */
/* Service instr routines                                        */
/* ============================================================= */
bool isUncondJump(INS ins) {
    return (xed_decoded_inst_get_category(INS_XedDec(ins)) == XED_CATEGORY_UNCOND_BR);
}

bool isJumpOrRet(INS ins) {
   return (!INS_IsCall(ins) && (INS_IsIndirectControlFlow(ins) || INS_IsDirectControlFlow(ins) || INS_IsRet(ins)));
}

int create_nop7_xedd_instr(xed_decoded_inst_t *xedd) {
  xed_encoder_instruction_t enc_instr;
  xed_encoder_request_t enc_req;
  char encoded_ins[XED_MAX_INSTRUCTION_BYTES];
  unsigned int olen = 0;
  xed_inst0(&enc_instr, dstate, XED_ICLASS_NOP7, 64);
  xed_encoder_request_zero_set_mode(&enc_req, &dstate);
  if (!xed_convert_to_encoder_request(&enc_req, &enc_instr)) return -1;
  if (xed_encode(&enc_req, reinterpret_cast<UINT8*>(encoded_ins), XED_MAX_INSTRUCTION_BYTES, &olen) != XED_ERROR_NONE) return -1;
  xed_decoded_inst_zero_set_mode(xedd, &dstate);
  if (xed_decode(xedd, reinterpret_cast<UINT8*>(&encoded_ins), max_inst_len) != XED_ERROR_NONE) return -1;
  return 0;
}

void dump_image_instrs(IMG img) {
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            RTN_Open( rtn );
            cerr << RTN_Name(rtn) << ":" << endl;
            for( INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins) )
                cerr << "0x" << hex << INS_Address(ins) << ": " << INS_Disassemble(ins) << endl;
            RTN_Close( rtn );
            cerr << endl;
        }
    }
}

void dump_instr_from_mem (ADDRINT *address, ADDRINT new_addr) {
  char disasm_buf[2048];
  xed_decoded_inst_t new_xedd;
  xed_decoded_inst_zero_set_mode(&new_xedd,&dstate);
  xed_decode(&new_xedd, reinterpret_cast<UINT8*>(address), max_inst_len);
  xed_format_context(XED_SYNTAX_INTEL, &new_xedd, disasm_buf, 2048, static_cast<UINT64>(new_addr), 0, 0);
  cerr << "0x" << hex << new_addr << ": " << disasm_buf <<  endl;
}

void dump_instr_map_entry(unsigned instr_map_entry) {
    cerr << dec << instr_map_entry << ": orig_ins_addr: 0x" << hex << instr_map[instr_map_entry].orig_ins_addr;
    cerr << " new_ins_addr: 0x" << hex << instr_map[instr_map_entry].new_ins_addr;
    if (instr_map[instr_map_entry].orig_targ_addr) {
      ADDRINT new_targ_addr = (instr_map[instr_map_entry].targ_map_entry >= 0) ? instr_map[instr_map[instr_map_entry].targ_map_entry].new_ins_addr : instr_map[instr_map_entry].orig_targ_addr;
      cerr << " orig_targ_addr: 0x" << hex << instr_map[instr_map_entry].orig_targ_addr << " new_targ_addr: 0x" << hex << new_targ_addr;
    }
    cerr << "    new instr:";
    dump_instr_from_mem((ADDRINT *)instr_map[instr_map_entry].encoded_ins, instr_map[instr_map_entry].new_ins_addr);
}

void dump_tc(char *tc, unsigned size_tc) {
  char disasm_buf[2048];
  xed_decoded_inst_t new_xedd;
  ADDRINT address = (ADDRINT)&tc[0];
  while (address < (ADDRINT)&tc[size_tc]) {
      xed_decoded_inst_zero_set_mode(&new_xedd,&dstate);
      if (xed_decode(&new_xedd, reinterpret_cast<UINT8*>(address), max_inst_len) != XED_ERROR_NONE) return;
      xed_format_context(XED_SYNTAX_INTEL, &new_xedd, disasm_buf, 2048, static_cast<UINT64>(address), 0, 0);
      cerr << "0x" << hex << address << ": " << disasm_buf <<  endl;
      address += xed_decoded_inst_get_length (&new_xedd);
  }
}

int disable_profiling_in_tc(instr_map_t * instr_map, unsigned num_of_instr_map_entries) {
    for (unsigned i = 0; i < num_of_instr_map_entries; i++) {
        if (instr_map[i].ins_type == ProfilingIns && instr_map[i].xed_category == XED_CATEGORY_WIDENOP) {
            unsigned j = 1;
            xed_int64_t disp = 0;
            while (i + j < num_of_instr_map_entries && instr_map[i+j].ins_type == ProfilingIns) {
                disp += instr_map[i+j].size;
                j++;
            }
          xed_encoder_instruction_t enc_instr;
          xed_encoder_request_t enc_req;
          char encoded_jmp_ins[XED_MAX_INSTRUCTION_BYTES];
          unsigned int olen = 5; 
          disp += (instr_map[i].size - olen);
          xed_inst1(&enc_instr, dstate,  XED_ICLASS_JMP, 64, xed_relbr(disp, 32));
          xed_encoder_request_zero_set_mode(&enc_req, &dstate);
          if (!xed_convert_to_encoder_request(&enc_req, &enc_instr)) return -1;
          if (xed_encode(&enc_req, reinterpret_cast<UINT8*>(encoded_jmp_ins), XED_MAX_INSTRUCTION_BYTES, &olen) != XED_ERROR_NONE) return -1;
          
          if (olen > instr_map[i].size) {
              i += (j - 1);
              continue;
          }

          // Atomic patching to avoid application crashes
          volatile unsigned char *head = (volatile unsigned char *)instr_map[i].new_ins_addr;
          if (((ADDRINT)head & 63) == 63) { i += (j - 1); continue; }
          unsigned char tail_skip = (unsigned char)(instr_map[i].size - 2);
          *(volatile unsigned short *)head = (unsigned short)(0xEB | (tail_skip << 8));
          __sync_synchronize();
          for (unsigned b = 2; b < olen; b++) head[b] = encoded_jmp_ins[b];
          __sync_synchronize();
          *(volatile unsigned short *)head = (unsigned short)((unsigned char)encoded_jmp_ins[0] | ((unsigned char)encoded_jmp_ins[1] << 8));
          i += (j - 1);
       }
    }
    return 0;
}    

int add_new_instr_entry(xed_decoded_inst_t *xedd, ADDRINT pc, ins_enum_t ins_type) {
    ADDRINT orig_targ_addr = 0x0;
    xed_uint_t disp_byts = xed_decoded_inst_get_branch_displacement_width(xedd);
    if (disp_byts > 0) orig_targ_addr = pc + xed_decoded_inst_get_length (xedd) + xed_decoded_inst_get_branch_displacement(xedd);

    ADDRINT orig_rip_addr = 0x0;
    if (xed_decoded_inst_number_of_memory_operands(xedd)) {
      if (xed_decoded_inst_get_base_reg(xedd, 0) == XED_REG_RIP) {
         orig_rip_addr = (ADDRINT)(pc + xed_decoded_inst_get_memory_displacement(xedd, 0) + xed_decoded_inst_get_length (xedd));
      }
    }
    xed_encoder_request_init_from_decode (xedd);
    unsigned new_size = 0;
    if (xed_encode (xedd, reinterpret_cast<UINT8*>(instr_map[num_of_instr_map_entries].encoded_ins), max_inst_len , &new_size) != XED_ERROR_NONE) return -1;

    instr_map[num_of_instr_map_entries].orig_ins_addr = pc;
    instr_map[num_of_instr_map_entries].new_ins_addr = 0x0;
    instr_map[num_of_instr_map_entries].orig_targ_addr = orig_targ_addr;
    instr_map[num_of_instr_map_entries].orig_rip_addr = orig_rip_addr;
    instr_map[num_of_instr_map_entries].targ_map_entry = -1;
    instr_map[num_of_instr_map_entries].size = new_size;
    instr_map[num_of_instr_map_entries].ins_type = ins_type;
    instr_map[num_of_instr_map_entries].bbl_num = bbl_num;
    instr_map[num_of_instr_map_entries].xed_category = xed_decoded_inst_get_category(xedd);
    num_of_instr_map_entries++;
    if (num_of_instr_map_entries >= max_ins_count) return -1;
    return new_size;
}

int add_new_encoded_instr(ADDRINT ins_addr, xed_encoder_instruction_t *enc_instr, ins_enum_t ins_type) {
    char encoded_ins[XED_MAX_INSTRUCTION_BYTES];
    unsigned int olen = 0;
    xed_encoder_request_t enc_req;    
    xed_encoder_request_zero_set_mode(&enc_req, &dstate);
    if (!xed_convert_to_encoder_request(&enc_req, enc_instr)) return -1;
    if (xed_encode(&enc_req, reinterpret_cast<UINT8*>(encoded_ins), XED_MAX_INSTRUCTION_BYTES, &olen) != XED_ERROR_NONE) return -1;
    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd,&dstate);
    if (xed_decode(&xedd, reinterpret_cast<UINT8*>(&encoded_ins), max_inst_len) != XED_ERROR_NONE) return -1;
    return add_new_instr_entry(&xedd, ins_addr, ins_type);
}

int add_profiling_instrs(INS ins, ADDRINT ins_addr, UINT64 *counter_addr, unsigned bbl_num, bool rax_is_dead, bool add_counter) {
    xed_encoder_instruction_t enc_instr;
    static uint64_t rax_mem = 0;

    bool need_indirect_prof = (INS_IsIndirectControlFlow(ins) && !INS_IsRet(ins) && !INS_IsCall(ins));
    if (!add_counter && !need_indirect_prof) return 0;
    if (need_indirect_prof) rax_is_dead = false;

    xed_decoded_inst_t nop_xedd;
    if (create_nop7_xedd_instr(&nop_xedd) < 0) return -1;
    if (add_new_instr_entry(&nop_xedd, ins_addr, ProfilingIns) < 0) return -1;

    if (!rax_is_dead) {
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rax_mem, 64), 64), xed_reg(XED_REG_RAX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
    }

    if (need_indirect_prof) {
        static uint64_t rbx_mem = 0;
        static uint64_t rcx_mem = 0;

        xed_decoded_inst_t *xedd = INS_XedDec(ins);
        xed_reg_enum_t base_reg = xed_decoded_inst_get_base_reg(xedd, 0);
        xed_reg_enum_t index_reg = xed_decoded_inst_get_index_reg(xedd, 0);
        xed_int64_t disp = xed_decoded_inst_get_memory_displacement(xedd, 0);
        xed_uint_t scale = xed_decoded_inst_get_scale(xedd, 0);
        xed_uint_t width = xed_decoded_inst_get_memory_displacement_width_bits(xedd, 0);
        unsigned mem_addr_width = xed_decoded_inst_get_memop_address_width(xedd, 0);

        xed_reg_enum_t targ_reg = XED_REG_INVALID;
        unsigned memops = xed_decoded_inst_number_of_memory_operands(xedd);
        if (!memops) targ_reg = xed_decoded_inst_get_reg(xedd, XED_OPERAND_REG0);

        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RAX), xed_reg(XED_REG_RBX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rbx_mem, 64), 64), xed_reg(XED_REG_RAX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;

        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RAX), xed_reg(XED_REG_RCX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rcx_mem, 64), 64), xed_reg(XED_REG_RAX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;

        if (targ_reg == XED_REG_RAX || base_reg == XED_REG_RAX || index_reg == XED_REG_RAX) {
            xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RAX), xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rax_mem, 64), 64));
            if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        }

        if (base_reg == XED_REG_RIP) {
            unsigned int orig_size = xed_decoded_inst_get_length (xedd);
            xed_int64_t new_disp = ins_addr + disp + orig_size;
            xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RAX), xed_mem_bisd(XED_REG_INVALID, index_reg, scale, xed_disp(new_disp, 32), mem_addr_width));
        } else if (targ_reg != XED_REG_RAX) {
            xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RAX), (targ_reg != XED_REG_INVALID ? xed_reg(targ_reg) : xed_mem_bisd(base_reg, index_reg, scale, xed_disp(disp, width), mem_addr_width)));
        }
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;

        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RBX), xed_reg(XED_REG_RAX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_AND, 64, xed_reg(XED_REG_RAX), xed_imm0(MAX_TARG_ADDRS, 8));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RCX), xed_imm0((ADDRINT)&(bbl_map[bbl_num].targ_addr[0]), 64));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_mem_bisd(XED_REG_RCX, XED_REG_RAX, 8, xed_disp(0, 32), 64), xed_reg(XED_REG_RBX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RBX), xed_imm0((ADDRINT)&(bbl_map[bbl_num].targ_count[0]), 64));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RCX), xed_mem_bisd(XED_REG_RBX, XED_REG_RAX, 8, xed_disp(0, 32), 64));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_LEA, 64, xed_reg(XED_REG_RCX), xed_mem_bd(XED_REG_RCX, xed_disp(1, 8), 64));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_mem_bisd(XED_REG_RBX, XED_REG_RAX, 8, xed_disp(0, 32), 64), xed_reg(XED_REG_RCX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;

        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RAX), xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rcx_mem, 64), 64));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RCX), xed_reg(XED_REG_RAX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;

        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RAX), xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rbx_mem, 64), 64));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RBX), xed_reg(XED_REG_RAX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
    }

    if (add_counter) {
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RAX), xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)counter_addr, 64), 64));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_LEA,  64, xed_reg(XED_REG_RAX), xed_mem_bd(XED_REG_RAX, xed_disp(1, 8), 64));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)counter_addr, 64), 64), xed_reg(XED_REG_RAX));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
    }

    if (!rax_is_dead) {
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64, xed_reg(XED_REG_RAX), xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rax_mem, 64), 64));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0) return -1;
    }
    return 0;
}

void chain_all_direct_jmp_and_call_target_entries(unsigned from_entry, unsigned until_entry) {
    entry_map.clear();
    for (unsigned i = from_entry; i < until_entry; i++) {
        instr_map[i].targ_map_entry = -1;
        ADDRINT orig_ins_addr = instr_map[i].orig_ins_addr;
        if (orig_ins_addr) entry_map.emplace(orig_ins_addr, i);
    }
    for (unsigned i = from_entry; i < until_entry; i++) {
        ADDRINT orig_targ_addr = instr_map[i].orig_targ_addr;
        if (orig_targ_addr == 0 || instr_map[i].targ_map_entry > 0) continue;
        if (!entry_map.count(orig_targ_addr) || !instr_map[i].size) continue;
        instr_map[i].targ_map_entry = entry_map[orig_targ_addr];
    }
}

int set_initial_estimated_new_ins_addrs_in_tc(char *tc) {
    unsigned tc_cursor = 0;
    for (unsigned i=0; i < num_of_instr_map_entries; i++) {
        instr_map[i].new_ins_addr = (ADDRINT)&tc[tc_cursor];
        tc_cursor += instr_map[i].size;
        if (tc_cursor >= max_tc_size) return -1;
    }
    return 0;
}

int fix_rip_displacement(unsigned instr_map_entry) {
    if (!instr_map[instr_map_entry].size || !instr_map[instr_map_entry].orig_rip_addr || instr_map[instr_map_entry].orig_targ_addr != 0) return 0;
    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd, &dstate);
    if (xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), max_inst_len) != XED_ERROR_NONE) return -1;
    xed_int64_t new_disp = (xed_int64_t)(instr_map[instr_map_entry].orig_rip_addr - instr_map[instr_map_entry].new_ins_addr - instr_map[instr_map_entry].size);
    if (new_disp > 0x7FFFFFFF || new_disp < -0x7FFFFFFF) return -1;
    xed_encoder_request_set_memory_displacement (&xedd, new_disp, 4);
    unsigned new_size = 0;
    xed_encoder_request_init_from_decode (&xedd);
    if (xed_encode (&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), XED_MAX_INSTRUCTION_BYTES , &new_size) != XED_ERROR_NONE) return -1;
    return new_size;
}

int fix_direct_jmp_or_call_to_orig_addr(unsigned instr_map_entry) {
    if (!instr_map[instr_map_entry].size || instr_map[instr_map_entry].targ_map_entry >= 0) return 0;
    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd,&dstate);
    if (xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), max_inst_len) != XED_ERROR_NONE) return -1;
    xed_category_enum_t category_enum = xed_decoded_inst_get_category(&xedd);
    if (category_enum != XED_CATEGORY_CALL && category_enum != XED_CATEGORY_UNCOND_BR) return -1;

    int jump_to_orig_addr_map_entry = -1;
    for (unsigned i = 0; i < jump_to_orig_addr_num; i++) {
        if (instr_map[instr_map_entry].orig_targ_addr == jump_to_orig_addr_map[i]) {
            jump_to_orig_addr_map_entry = i; break;
        }
    }
    if (jump_to_orig_addr_map_entry < 0) {
        jump_to_orig_addr_num++;
        jump_to_orig_addr_map_entry = jump_to_orig_addr_num;
        if ((unsigned)jump_to_orig_addr_map_entry >= max_rtn_count) return -1;
        jump_to_orig_addr_map[jump_to_orig_addr_map_entry] = instr_map[instr_map_entry].orig_targ_addr;
    }

    xed_int64_t new_disp = (ADDRINT)&jump_to_orig_addr_map[jump_to_orig_addr_map_entry] - instr_map[instr_map_entry].new_ins_addr - xed_decoded_inst_get_length (&xedd);
    if (new_disp > 0x7FFFFFFF || new_disp < -0x7FFFFFFF) return -1;

    xed_encoder_instruction_t enc_instr;
    if (category_enum == XED_CATEGORY_CALL) xed_inst1(&enc_instr, dstate, XED_ICLASS_CALL_NEAR, 64, xed_mem_bd (XED_REG_RIP, xed_disp(new_disp, 32), 64));
    if (category_enum == XED_CATEGORY_UNCOND_BR) xed_inst1(&enc_instr, dstate, XED_ICLASS_JMP, 64, xed_mem_bd (XED_REG_RIP, xed_disp(new_disp, 32), 64));

    xed_encoder_request_t enc_req;
    xed_encoder_request_zero_set_mode(&enc_req, &dstate);
    if (!xed_convert_to_encoder_request(&enc_req, &enc_instr)) return -1;
    unsigned int olen = 0;
    if (xed_encode(&enc_req, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), XED_MAX_INSTRUCTION_BYTES, &olen) != XED_ERROR_NONE) return -1;
    return olen;
}

int fix_direct_jmp_or_call_displacement(unsigned instr_map_entry) {
    if (!instr_map[instr_map_entry].size || instr_map[instr_map_entry].orig_targ_addr == 0) return 0;
    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd,&dstate);
    if (xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), max_inst_len) != XED_ERROR_NONE) return -1;
    xed_category_enum_t category_enum = xed_decoded_inst_get_category(&xedd);
    if (category_enum != XED_CATEGORY_CALL && category_enum != XED_CATEGORY_COND_BR && category_enum != XED_CATEGORY_UNCOND_BR) return -1;

    if (instr_map[instr_map_entry].targ_map_entry < 0) return fix_direct_jmp_or_call_to_orig_addr(instr_map_entry);

    xed_int64_t new_disp = (instr_map[instr_map[instr_map_entry].targ_map_entry].new_ins_addr - instr_map[instr_map_entry].new_ins_addr) - instr_map[instr_map_entry].size; 
    if (new_disp > 0x7FFFFFFF || new_disp < -0x7FFFFFFF) return -1;

    xed_uint_t new_disp_byts = 4;
    xed_iclass_enum_t iclass_enum = xed_decoded_inst_get_iclass(&xedd);
    if (iclass_enum == XED_ICLASS_LOOP || iclass_enum == XED_ICLASS_LOOPE || iclass_enum == XED_ICLASS_LOOPNE || xed_decoded_inst_get_iform_enum (&xedd) == XED_IFORM_JRCXZ_RELBRb) new_disp_byts = 1;

    xed_encoder_request_init_from_decode (&xedd);
    xed_encoder_request_set_branch_displacement (&xedd, new_disp, new_disp_byts);
    unsigned new_size = 0;
    if (xed_encode (&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), XED_MAX_INSTRUCTION_BYTES, &new_size) != XED_ERROR_NONE) return -1;
    return new_size;
}

int fix_instructions_displacements() {
    int size_diff = 0; bool is_diff = false;
    do {
        size_diff = 0; is_diff = false;
        for (unsigned i=0; i < num_of_instr_map_entries; i++) {
            instr_map[i].new_ins_addr += size_diff;
            int new_size = fix_rip_displacement(i);
            if (new_size < 0) return -1;
            if (new_size && instr_map[i].size != (unsigned)new_size) {
                size_diff += ((int)new_size - (int)instr_map[i].size);
                instr_map[i].size = (unsigned)new_size;
                is_diff = true; continue;
            }
            new_size = fix_direct_jmp_or_call_displacement(i);
            if (new_size < 0) return -1;
            if (new_size && instr_map[i].size != (unsigned)new_size) {
                size_diff += ((int)new_size - (int)instr_map[i].size);
                instr_map[i].size = (unsigned)new_size;
                is_diff = true; continue;
            }
        }  
    } while (is_diff);
   return 0;
}

/********************************/
/* Translation Implementation   */
/********************************/
int find_candidate_rtns_for_tc(IMG img) {
    int rc = 0;

    // Build map of translatable instructions to skip unresolvable functions
    std::set<ADDRINT> all_ins_starts;
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        if (!SEC_IsExecutable(sec) || SEC_IsWriteable(sec) || !SEC_Address(sec)) continue;
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            RTN_Open(rtn);
            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
                all_ins_starts.insert(INS_Address(ins));
            RTN_Close(rtn);
        }
    }

    unsigned counter_inserted_for_bbl = (unsigned)-1;

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        if (!SEC_IsExecutable(sec) || SEC_IsWriteable(sec) || !SEC_Address(sec)) continue;

        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {

            // Skip problematic routines to prevent crashes
            if (RTN_Name(rtn) == "_init") continue;
            bool untranslatable = false;
            RTN_Open(rtn);
            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
                if (INS_IsDirectControlFlow(ins)) {
                    ADDRINT targ_addr = INS_DirectControlFlowTargetAddress(ins);
                    if (targ_addr == 0) { untranslatable = true; break; }
                    if (INS_IsBranch(ins) && INS_HasFallThrough(ins) && all_ins_starts.find(targ_addr) == all_ins_starts.end()) {
                        untranslatable = true; break;
                    }
                }
                const xed_decoded_inst_t *xedd_scan = INS_XedDec(ins);
                if (xed_decoded_inst_number_of_memory_operands(xedd_scan) && xed_decoded_inst_get_base_reg(xedd_scan, 0) == XED_REG_RIP) {
                    xed_int64_t disp = xed_decoded_inst_get_memory_displacement(xedd_scan, 0);
                    ADDRINT rip_targ = (ADDRINT)(INS_Address(ins) + disp + INS_Size(ins));
                    if (rip_targ == 0) { untranslatable = true; break; }
                }
            }
            RTN_Close(rtn);
            if (untranslatable) continue;

            RTN_Open( rtn );
            std::map<ADDRINT, bool>is_targ_map;
            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
               if (INS_IsDirectControlFlow(ins)) is_targ_map[INS_DirectControlFlowTargetAddress(ins)] = true;
            }

            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
                ADDRINT ins_addr = INS_Address(ins);
                xed_decoded_inst_t xedd;
                
                bool isRtnHeadIns = (RTN_Address(rtn) == ins_addr);
                ins_enum_t ins_type = (isRtnHeadIns ? RtnHeadIns : RegularIns);

                if (!KnobNoProfile && isRtnHeadIns) {
                  if (create_nop7_xedd_instr(&xedd) < 0 || add_new_instr_entry(&xedd, ins_addr, ins_type) < 0) { RTN_Close(rtn); return -1; }
                  ins_type = RegularIns;
                }

                INS next_ins = INS_Next(ins);
                bool isNextInsJumpTarget = (!INS_Valid(next_ins) ? false : is_targ_map[INS_Address(next_ins)]);
                bool isInsTerminatesBBL = (isJumpOrRet(ins) || isNextInsJumpTarget);

                if (!KnobNoProfile) {
                    bool counterNotYetInserted = (counter_inserted_for_bbl != bbl_num);
                    bool insKillsRax = insKillsFullReg(ins, LEVEL_BASE::REG_RAX);
                    bool isIndirectJmp = (INS_IsIndirectControlFlow(ins) && !INS_IsRet(ins) && !INS_IsCall(ins));

                    if (counterNotYetInserted && (insKillsRax || isInsTerminatesBBL)) {
                        rc = add_profiling_instrs(ins, ins_addr, &bbl_map[bbl_num].counter, bbl_num, insKillsRax, true);
                        if (rc < 0) return -1;
                        counter_inserted_for_bbl = bbl_num;
                    } else if (!counterNotYetInserted && isIndirectJmp) {
                        rc = add_profiling_instrs(ins, ins_addr, &bbl_map[bbl_num].counter, bbl_num, false, false);
                        if (rc < 0) return -1;
                    }
                }
          
                xed_decoded_inst_zero_set_mode(&xedd,&dstate);
                if (xed_decode(&xedd, reinterpret_cast<UINT8*>(ins_addr), max_inst_len) != XED_ERROR_NONE) { RTN_Close(rtn); return -1; }
                if (add_new_instr_entry(&xedd, INS_Address(ins), ins_type) < 0) { RTN_Close(rtn); return -1; }

                if (!bbl_map[bbl_num].first_ins_addr) bbl_map[bbl_num].first_ins_addr = ins_addr;

                if (isInsTerminatesBBL) {
                  bbl_map[bbl_num].terminating_ins_entry = num_of_instr_map_entries - 1;
                  if (INS_Category(ins) == XED_CATEGORY_COND_BR) bbl_map[bbl_num].ends_with_cond_br = 1;
                  bbl_num++;
                  bbl_map[bbl_num].starting_ins_entry = num_of_instr_map_entries;
                }

                if (!KnobNoProfile && INS_Category(ins) == XED_CATEGORY_COND_BR) {
                  bool rax_dead_on_ft = isRegDeadOnFallthruPath(ins, LEVEL_BASE::REG_RAX);
                  rc = add_profiling_instrs(ins, ins_addr, &bbl_map[bbl_num - 1].fallthru_counter, bbl_num-1, rax_dead_on_ft, true);
                  if (rc < 0) return -1;
                }
            } 
            RTN_Close( rtn );
         } 
    } 
    return 0;
}

int copy_instrs_to_tc(char *tc) {
    int cursor = 0;
    for (unsigned i=0; i < num_of_instr_map_entries; i++) {
      if ((ADDRINT)&tc[cursor] != instr_map[i].new_ins_addr) return -1;
      memcpy(&tc[cursor], (char *)instr_map[i].encoded_ins, instr_map[i].size);
      cursor += instr_map[i].size;
    }
    return cursor;
}

inline void commit_translated_rtns_to_tc() {
    for (unsigned i=0; i < num_of_instr_map_entries; i++) {
        if (instr_map[i].ins_type != RtnHeadIns) continue;
        RTN rtn = RTN_FindByAddress(instr_map[i].orig_ins_addr);
        if (rtn == RTN_Invalid()) continue;
        RTN_ReplaceProbed(rtn,  (AFUNPTR)instr_map[i].new_ins_addr);
    }
}

void start_stop_profile_gathering_thread_func(void *v) {
    sleep(KnobNumSecsDuringProfile);
    disable_profiling_in_tc(instr_map, num_of_instr_map_entries);
}

int allocate_and_init_memory(IMG img) {
    ADDRINT highest_addr = 0;
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        if (!SEC_IsExecutable(sec) || SEC_IsWriteable(sec) || !SEC_Address(sec)) continue;
        if (!lowest_sec_addr || lowest_sec_addr > SEC_Address(sec)) lowest_sec_addr = SEC_Address(sec);
        if (highest_sec_addr < SEC_Address(sec) + SEC_Size(sec)) highest_sec_addr = SEC_Address(sec) + SEC_Size(sec);

        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            if (highest_addr < RTN_Address(rtn) + RTN_Size(rtn)) highest_addr = RTN_Address(rtn) + RTN_Size(rtn);
            max_rtn_count++;
            max_ins_count += RTN_NumIns(rtn);
        }
    }
    max_ins_count *= 10; 
    int pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize == -1) return -1;

    ADDRINT text_size = (highest_sec_addr - lowest_sec_addr) * 2 + pagesize * 4;
    max_tc_size = 10 * text_size + pagesize * 4;   
    if (max_tc_size >= 0x7FFFFFFF) return -1;

    const size_t mem_size = max_tc_size + max_rtn_count * sizeof(ADDRINT);  
    char *addr = nullptr;
    ADDRINT max_distance = 0x7FFFFFFF;
    const size_t step = pagesize; 
    ADDRINT aligned_target = ((ADDRINT)highest_addr) & ~(pagesize - 1);
    
    void* result = mmap((void*)aligned_target, mem_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    if (result != MAP_FAILED && (abs((long)((ADDRINT)result - aligned_target)) <= (long)max_distance)) addr = (char *)result;

    if (!addr) {
        for (size_t offset = step; offset <= max_distance; offset += step) {
            ADDRINT try_addr = aligned_target + offset;
            result = mmap((void*)try_addr, mem_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            if (result != MAP_FAILED && (abs((long)((ADDRINT)result - try_addr)) <= (long)max_distance)) { addr = (char *)result; break; }
            if (result != MAP_FAILED) munmap(result, mem_size);

            if (highest_addr >= offset) {
                try_addr = aligned_target - offset;
                result = mmap((void*)try_addr, mem_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
                if (result != MAP_FAILED && (abs((long)((ADDRINT)result - try_addr)) <= (long)max_distance)) { addr = (char *)result; break; }
                if (result != MAP_FAILED) munmap(result, mem_size);
            }
        }
    }
    if (!addr) return -1;

    tc = (char *)addr;
    addr += max_tc_size;
    jump_to_orig_addr_map = (ADDRINT *)addr;

    instr_map = (instr_map_t *)calloc(max_ins_count, sizeof(instr_map_t));
    if (instr_map == NULL) return -1;

    bbl_map = (bbl_map_t *)calloc(max_ins_count, sizeof(bbl_map_t));
    if (bbl_map == NULL) return -1;

    return 0;
}

/*****************************************************************/
/* CSV Writer with Flusher                                       */
/*****************************************************************/
typedef struct {
    ADDRINT addr;
    UINT64  exec;
    UINT64  taken;
    UINT64  fallthru;
    unsigned bbl;
} edge_prof_row_t;

static bool edge_prof_row_hotter(const edge_prof_row_t &a, const edge_prof_row_t &b) {
    if (a.exec != b.exec) return a.exec > b.exec;
    return a.addr < b.addr;
}

void dump_edge_profile() {
    std::vector<edge_prof_row_t> rows;
    for (unsigned b = 0; b <= bbl_num; b++) {
        if (!bbl_map[b].counter) continue;

        edge_prof_row_t row;
        row.bbl  = b;
        row.addr = bbl_map[b].first_ins_addr;
        row.exec = bbl_map[b].counter;
        if (bbl_map[b].ends_with_cond_br) {
            row.fallthru = bbl_map[b].fallthru_counter;
            row.taken = (row.exec >= row.fallthru) ? (row.exec - row.fallthru) : 0;
        } else {
            row.taken = 0;
            row.fallthru = 0;
        }
        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(), edge_prof_row_hotter);

    for (unsigned i = 0; i < rows.size(); i++) {
        unsigned b = rows[i].bbl;
        *out << "0x" << hex << rows[i].addr << dec << ", " << rows[i].exec << ", " << rows[i].taken << ", " << rows[i].fallthru;

        ADDRINT taddr[MAX_TARG_ADDRS + 1];
        UINT64  tcnt[MAX_TARG_ADDRS + 1];
        int n = 0;
        for (unsigned j = 0; j <= MAX_TARG_ADDRS; j++) {
            if (bbl_map[b].targ_count[j]) {
                taddr[n] = bbl_map[b].targ_addr[j];
                tcnt[n]  = bbl_map[b].targ_count[j];
                n++;
            }
        }
        for (int j = 0; j < n; j++) {         
            for (int k = j + 1; k < n; k++) {
                if (tcnt[k] > tcnt[j]) {
                    UINT64  c = tcnt[j];  tcnt[j]  = tcnt[k];  tcnt[k]  = c;
                    ADDRINT a = taddr[j]; taddr[j] = taddr[k]; taddr[k] = a;
                }
            }
        }
        for (int j = 0; j < n; j++) {
            *out << ", 0x" << hex << taddr[j] << dec << ", " << tcnt[j];
        }
        *out << "\n";
    }
    out->flush(); 
}

/* ============================================ */
/* Main translation routine                     */
/* ============================================ */
typedef VOID (*EXITFUNCPTR)(INT code);
EXITFUNCPTR origExit;

VOID Fini(INT32 code, VOID* v) {
    dump_edge_profile();
    clock_gettime(CLOCK_MONOTONIC, &end_running_time);
    double elapsed = (end_running_time.tv_sec - start_running_time.tv_sec) + (end_running_time.tv_nsec - start_running_time.tv_nsec) / 1e9;
	cerr << " Translated code run took: " << elapsed << " seconds\n";
}

VOID ExitInProbeMode(INT code) {
    Fini(code, 0);
    (*origExit)(code);
}

VOID create_tc(IMG img, VOID *v) {
    RTN exitRtn = RTN_FindByName(img, "_exit");
    if (RTN_Valid(exitRtn) && RTN_IsSafeForProbedReplacement(exitRtn)) {
      origExit = (EXITFUNCPTR)RTN_ReplaceProbed(exitRtn, AFUNPTR(ExitInProbeMode));
    }

    if (!IMG_IsMainExecutable(img)) return;
    if (allocate_and_init_memory(img) < 0) return;
    if (find_candidate_rtns_for_tc(img) < 0) return;
    chain_all_direct_jmp_and_call_target_entries(0, num_of_instr_map_entries);
    if (set_initial_estimated_new_ins_addrs_in_tc(tc) < 0 ) return;
    if (fix_instructions_displacements() < 0 ) return;
    
    int rc = copy_instrs_to_tc(tc);
    if (rc < 0 ) return;
    tc_size = rc;

    if (!KnobDoNotCommitTranslatedCode) commit_translated_rtns_to_tc();

    tc_ready = true; 
	clock_gettime(CLOCK_MONOTONIC, &start_running_time);
}

INT32 Usage() {
    cerr << "This tool translates routines of an Intel(R) 64 binary\n" << KNOB_BASE::StringKnobSummary() << "\n";
    return -1;
}

int main(int argc, char * argv[]) {
    out = new std::ofstream("edge-profile.csv");
    if( PIN_Init(argc,argv) ) return Usage();
    PIN_InitSymbols();
    IMG_AddInstrumentFunction(create_tc, 0);
    THREADID tid = PIN_SpawnInternalThread(start_stop_profile_gathering_thread_func, NULL, 0, NULL);
    if (tid == INVALID_THREADID) cerr << "failed to spawn a thread for commit\n";
    PIN_StartProgramProbed();
    return 0;
}

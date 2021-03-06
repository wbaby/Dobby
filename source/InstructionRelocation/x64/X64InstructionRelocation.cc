#include "common/macros/platform_macro.h"
#if defined(TARGET_ARCH_X64)

#include "./X64InstructionRelocation.h"

#include "dobby_internal.h"

#include "InstructionRelocation/x86/X86OpcodoDecodeTable.h"

#include "core/arch/x64/registers-x64.h"
#include "core/modules/assembler/assembler-x64.h"
#include "core/modules/codegen/codegen-x64.h"

using namespace zz::x64;

static int GenRelocateCodeFixed(void *buffer, AssemblyCode *origin, AssemblyCode *relocated) {
  TurboAssembler turbo_assembler_(0);
  // Set fixed executable code chunk address
  turbo_assembler_.CommitRealizeAddress((void *)relocated->raw_instruction_start());
#define _ turbo_assembler_.
#define __ turbo_assembler_.GetCodeBuffer()->

  uint64_t curr_orig_ip = origin->raw_instruction_start();
  uint64_t curr_relo_ip = relocated->raw_instruction_start();

  addr_t buffer_cursor = (addr_t)buffer;

  byte_t opcode1      = *(byte_t *)buffer_cursor;
  InstrMnemonic instr = {0};

  int predefined_relocate_size = origin->raw_instruction_size();

  while ((buffer_cursor < ((addr_t)buffer + predefined_relocate_size))) {
    int last_relo_offset = turbo_assembler_.GetCodeBuffer()->getSize();

    OpcodeDecodeItem *decodeItem = &OpcodeDecodeTable[opcode1];
    decodeItem->DecodeHandler(&instr, (uint64_t)buffer_cursor);

    // Jcc Relocate OpcodeEncoding=D and rel8
    // Solution:
    // Convert to 32bit AKA rel32
    if (instr.instr.opcode1 >= 0x70 && instr.instr.opcode1 <= 0x7F) {
      int orig_offset = *(byte_t *)&instr.instr.Immediate;
      int offset      = (int)(curr_orig_ip + orig_offset - curr_relo_ip);
      __ Emit8(0x0F);
      __ Emit8(opcode1);
      __ Emit32(offset);
    } else if (instr.instr.opcode1 >= 0xE0 && instr.instr.opcode1 <= 0xE2) {
      // LOOP/LOOPcc
      UNIMPLEMENTED();
    } else if (instr.instr.opcode1 == 0xE3) {
      // JCXZ JCEXZ JCRXZ
      UNIMPLEMENTED();
    } else if (instr.instr.opcode1 == 0xEB) {
      // JMP rel8
      byte_t orig_offset = *(byte_t *)&instr.instr.Immediate;
      // FIXME: security cast
      byte_t offset = (byte_t)(curr_orig_ip + orig_offset - curr_relo_ip);
      __ Emit8(0xE9);
      __ Emit32(offset);
    } else if (instr.instr.opcode1 == 0xE8 || instr.instr.opcode1 == 0xE9) {
      // JMP/CALL rel32
      dword orig_offset = *(dword *)&instr.instr.Immediate;
      dword offset      = (dword)(curr_orig_ip + orig_offset - curr_relo_ip);
      __ Emit8(instr.instr.opcode1);
      __ Emit32(offset);
    } else if (instr.flag & kIPRelativeAddress) {
      // IP-Relative Address
      dword orig_disp = *(dword *)(buffer_cursor + instr.instr.DisplacementOffset);
      dword disp      = (dword)(curr_orig_ip + orig_disp - curr_relo_ip);
#if 0
      byte_t InstrArray[15];
      LiteMemOpt::copy(InstrArray, curr_ip, instr.len);
      *(dword *)(InstrArray + instr.instr.DisplacementOffset) = disp;
      _ Emit(InstrArray, instr.len);

#else
      __ EmitBuffer((void *)buffer_cursor, instr.instr.DisplacementOffset);
      __ Emit32(disp);
#endif
    } else {
      // Emit the origin instrution
      __ EmitBuffer((void *)buffer_cursor, instr.len);
    }

    // go next
    curr_orig_ip += instr.len;
    buffer_cursor += instr.len;

    {
      // 1 orignal instrution => ? relocated instruction
      int relo_offset = turbo_assembler_.GetCodeBuffer()->getSize();
      int relo_len    = relo_offset - last_relo_offset;
      curr_relo_ip += relo_len;
    }

    opcode1 = *(byte_t *)buffer_cursor;

    // clear instr structure
    _memset((void *)&instr, 0, sizeof(InstrMnemonic));
  }

  // jmp to the origin rest instructions
  CodeGen codegen(&turbo_assembler_);
  // TODO: 6 == jmp [RIP + disp32] instruction size
  uint64_t stub_addr = curr_relo_ip + 6;
  codegen.JmpNearIndirect(stub_addr);
  turbo_assembler_.GetCodeBuffer()->Emit64(curr_orig_ip);


  int relo_len = turbo_assembler_.GetCodeBuffer()->getSize();
  if (relo_len > relocated->raw_instruction_size()) {
    LOG("pre-alloc code chunk not enough");
    return RT_FAILED;
  }

  // Generate executable code
  {
    AssemblyCode *code = NULL;
    code               = AssemblyCode::FinalizeFromTurboAssember(&turbo_assembler_);
    delete code;
  }

  return RT_SUCCESS;
}

void GenRelocateCode(void *buffer, AssemblyCode *origin, AssemblyCode *relocated) {
  // pre-alloc code chunk
  AssemblyCodeChunk *codeChunk = NULL;

  int relo_code_chunk_size = 32;
  const int chunk_size_step      = 16;

  if (relocated->raw_instruction_start() == 0) {
    codeChunk = MemoryArena::AllocateCodeChunk(relo_code_chunk_size);
    relocated->reInitWithAddressRange((addr_t)codeChunk->address, (int)codeChunk->length);
  }

  if (GenRelocateCodeFixed(buffer, origin, relocated) != RT_SUCCESS) {
    // free the codeChunk
    MemoryArena::Destory(codeChunk);

    relo_code_chunk_size += chunk_size_step;
    codeChunk = MemoryArena::AllocateCodeChunk(relo_code_chunk_size);
    relocated->reInitWithAddressRange((addr_t)codeChunk->address, (int)codeChunk->length);
  }
}

#endif
#include "r4300i.h"
#include "../common/log.h"
#include "disassemble.h"
#include "mips32.h"

#define MIPS32_CP    0b010000
#define MIPS32_LUI   0b001111
#define MIPS32_ADDI  0b001000
#define MIPS32_ADDIU 0b001001
#define MIPS32_ANDI  0b001100
#define MIPS32_LW    0b100011
#define MIPS32_BEQ   0b000100
#define MIPS32_BEQL  0b010100
#define MIPS32_BNE   0b000101
#define MIPS32_SPCL  0b000000
#define MIPS32_SB    0b101000
#define MIPS32_SW    0b101011
#define MIPS32_ORI   0b001101
#define MIPS32_JAL   0b000011
#define MIPS32_SLTI  0b001010
#define MIPS32_XORI  0b001110

// Coprocessor
#define MTC0_MASK  0b11111111111000000000011111111111
#define MTC0_VALUE 0b01000000100000000000000000000000

// Special
#define FUNCT_NOP 0b000000
#define FUNCT_SRL 0b000010
#define FUNCT_JR  0b001000
#define FUNCT_OR  0b100101

mips32_instruction_type_t decode_cp(r4300i_t* cpu, mips32_instruction_t instr) {
    if ((instr.raw & MTC0_MASK) == MTC0_VALUE) {
        return CP_MTC0;
    } else {
        logfatal("other/unknown MIPS32 Coprocessor: 0x%08X", instr.raw)
    }
}

mips32_instruction_type_t decode_special(r4300i_t* cpu, mips32_instruction_t instr) {
    switch (instr.r.funct) {
        case FUNCT_NOP: return NOP;
        case FUNCT_SRL: return SPC_SRL;
        case FUNCT_JR:  return SPC_JR;
        case FUNCT_OR:  return SPC_OR;
        default: logfatal("other/unknown MIPS32 Special 0x%08X with FUNCT: %d%d%d%d%d%d", instr.raw,
                instr.funct0, instr.funct1, instr.funct2, instr.funct3, instr.funct4, instr.funct5)
    }
}

mips32_instruction_type_t decode(r4300i_t* cpu, dword pc, mips32_instruction_t instr) {
    char buf[50];
    disassemble32(pc, instr.raw, buf, 50);
    logdebug("[0x%08lX] %s", pc, buf)
    switch (instr.op) {
        case MIPS32_CP:    return decode_cp(cpu, instr);
        case MIPS32_SPCL:  return decode_special(cpu, instr);

        case MIPS32_LUI:   return LUI;
        case MIPS32_ADDIU: return ADDIU;
        case MIPS32_ADDI:  return ADDI;
        case MIPS32_ANDI:  return ANDI;
        case MIPS32_LW:    return LW;
        case MIPS32_BEQ:   return BEQ;
        case MIPS32_BEQL:  return BEQL;
        case MIPS32_BNE:   return BNE;
        case MIPS32_SB:    return SB;
        case MIPS32_SW:    return SW;
        case MIPS32_ORI:   return ORI;
        case MIPS32_JAL:   return JAL;
        case MIPS32_SLTI:  return SLTI;
        case MIPS32_XORI:  return XORI;
        default:
            logfatal("Failed to decode instruction 0x%08X opcode %d%d%d%d%d%d [%s]",
                     instr.raw, instr.op0, instr.op1, instr.op2, instr.op3, instr.op4, instr.op5, buf)
    }
}

#define exec_instr(key, fn) case key: fn(cpu, instruction); break;

void r4300i_step(r4300i_t* cpu) {
    if (cpu->branch) {
        if (cpu->branch_delay == 0) {
            logdebug("[BRANCH DELAY] Branching to 0x%08X", cpu->branch_pc)
            cpu->pc = cpu->branch_pc;
            cpu->branch = false;
        } else {
            logdebug("[BRANCH DELAY] Need to execute %d more instructions.", cpu->branch_delay)
            cpu->branch_delay--;
        }
    }

    dword pc = cpu->pc;

    mips32_instruction_t instruction;
    instruction.raw = cpu->read_word(pc);
    cpu->pc += 4;

    switch (decode(cpu, pc, instruction)) {
        case NOP: break;

        exec_instr(LUI,   lui)
        exec_instr(ADDI,  addi)
        exec_instr(ADDIU, addiu)
        exec_instr(ANDI,  andi)
        exec_instr(LW,    lw)
        exec_instr(BNE,   bne)
        exec_instr(BEQ,   beq)
        exec_instr(SB,    sb)
        exec_instr(SW,    sw)
        exec_instr(ORI,   ori)
        exec_instr(JAL,   jal)
        exec_instr(SLTI,  slti)
        exec_instr(BEQL,  beql)
        exec_instr(XORI,  xori)

        // Coprocessor
        exec_instr(CP_MTC0, mtc0)

        // Special
        exec_instr(SPC_SRL, spc_srl)
        exec_instr(SPC_JR, spc_jr)
        exec_instr(SPC_OR, spc_or)
        default: logfatal("Unknown instruction type!")
    }
}

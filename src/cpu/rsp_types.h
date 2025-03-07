#ifndef N64_RSP_TYPES_H
#define N64_RSP_TYPES_H

#include <stdbool.h>
#include <assert.h>
#ifdef N64_USE_SIMD
#include <emmintrin.h>
#endif
#include <util.h>
#include <cpu/dynarec/rsp_dynarec.h>
#include "mips_instruction_decode.h"

#define vecr __m128i

#define SP_DMEM_SIZE 0x1000
#define SP_IMEM_SIZE 0x1000

typedef union vu_reg {
    // Used by instructions
    u8 bytes[16];
    s16 signed_elements[8];
    u16 elements[8];
#ifdef N64_USE_SIMD
    vecr single;
#endif
    // Only used for loading
    u32 words[4];
} vu_reg_t;

#ifdef N64_BIG_ENDIAN
#define VU_BYTE_INDEX(i) (i)
#define VU_ELEM_INDEX(i) (i)
#else
#define VU_BYTE_INDEX(i) (15 - (i))
#define VU_ELEM_INDEX(i) (7 - (i))
#endif

static_assert(sizeof(vu_reg_t) == 16, "vu_reg_t incorrect size!");

typedef union rsp_types {
    u32 raw;
    struct {
#ifdef N64_BIG_ENDIAN
        unsigned:17;
        unsigned signal_7:1;
        unsigned signal_6:1;
        unsigned signal_5:1;
        unsigned signal_4:1;
        unsigned signal_3:1;
        unsigned signal_2:1;
        unsigned signal_1:1;
        unsigned signal_0:1;
        unsigned intr_on_break:1;
        unsigned single_step:1;
        unsigned io_full:1;
        unsigned dma_full:1;
        unsigned dma_busy:1;
        unsigned broke:1;
        unsigned halt:1;
#else
        unsigned halt:1;
        unsigned broke:1;
        unsigned dma_busy:1;
        unsigned dma_full:1;
        unsigned io_full:1;
        unsigned single_step:1;
        unsigned intr_on_break:1;
        unsigned signal_0:1;
        unsigned signal_1:1;
        unsigned signal_2:1;
        unsigned signal_3:1;
        unsigned signal_4:1;
        unsigned signal_5:1;
        unsigned signal_6:1;
        unsigned signal_7:1;
        unsigned:17;
#endif
    };
} PACKED rsp_status_t;

ASSERTWORD(rsp_status_t);

typedef struct rsp rsp_t;

void cache_rsp_instruction(mips_instruction_t instr);

typedef void(*rspinstr_handler_t)(mips_instruction_t);

typedef struct rsp_icache_entry {
    mips_instruction_t instruction;
    rspinstr_handler_t handler;
} rsp_icache_entry_t;

typedef union mem_addr {
    u32 raw;
    struct {
        unsigned address:12;
        unsigned imem:1;
        unsigned:19;
    };
} mem_addr_t;

ASSERTWORD(mem_addr_t);

typedef union dram_addr {
    u32 raw;
    struct {
        unsigned address:24;
        unsigned:8;
    };
} dram_addr_t;

ASSERTWORD(dram_addr_t);

typedef struct rsp_dynarec rsp_dynarec_t;

typedef struct rsp {
    rsp_dynarec_t *dynarec;

    u32 gpr[32];
    u16 prev_pc;
    u16 pc;
    u16 next_pc;
    //dword mult_hi;
    //dword mult_lo;

    int steps;

#ifdef N64_USE_SIMD
    vecr zero;
#endif

    rsp_status_t status;

    struct {
        mem_addr_t mem_addr;
        dram_addr_t dram_addr;

        // values are stored in shadow registers until the DMA actually runs
        mem_addr_t shadow_mem_addr;
        dram_addr_t shadow_dram_addr;
        union {
            struct {
                unsigned length: 12;
                unsigned count: 8;
                unsigned skip: 12;
            };
            u32 raw;
        } dma;
    } io;

    rsp_icache_entry_t icache[0x1000 / 4];

    vu_reg_t vu_regs[32];

    struct {
        vu_reg_t l;
        vu_reg_t h;
    } vcc;

    struct {
        vu_reg_t l;
        vu_reg_t h;
    } vco;

    vu_reg_t vce;

    struct {
        vu_reg_t h;
        vu_reg_t m;
        vu_reg_t l;
    } acc;

    int sync; // For syncing RSP with CPU

    s16 divin;
    bool divin_loaded;
    s16 divout;

    bool semaphore_held;

    u8 sp_dmem[SP_DMEM_SIZE];
    u8 sp_imem[SP_IMEM_SIZE];
} rsp_t;

#endif //N64_RSP_TYPES_H

#include "n64system.h"

#include <string.h>
#include <unistd.h>

#include <mem/n64bus.h>
#include <frontend/render.h>
#include <interface/vi.h>
#include <interface/ai.h>
#include <n64_rsp_bus.h>
#include <cpu/rsp.h>
#include <cpu/dynarec/dynarec.h>
#include <sys/mman.h>
#include <errno.h>
#include <mem/backup.h>
#include <frontend/game_db.h>
#include <metrics.h>

static bool should_quit = false;


n64_system_t n64sys;

// 128MiB codecache
#define CODECACHE_SIZE (1 << 27)
static byte codecache[CODECACHE_SIZE] __attribute__((aligned(4096)));

bool n64_should_quit() {
    return should_quit;
}

void write_physical_word_wrapper(word address, word value) {
    n64_write_word(address, value);
}

dword virtual_read_dword_wrapper(dword address) {
    word physical = resolve_virtual_address(address);
    return n64_read_dword(physical);
}

void virtual_write_dword_wrapper(dword address, dword value) {
    word physical = resolve_virtual_address(address);
    n64_write_dword(physical, value);
}

word virtual_read_word_wrapper(dword address) {
    word physical = resolve_virtual_address(address);
    return n64_read_physical_word(physical);
}

void virtual_write_word_wrapper(dword address, word value) {
    word physical = resolve_virtual_address(address);
    n64_write_word(physical, value);
}

half virtual_read_half_wrapper(dword address) {
    word physical = resolve_virtual_address(address);
    return n64_read_half(physical);
}

void virtual_write_half_wrapper(dword address, half value) {
    word physical = resolve_virtual_address(address);
    n64_write_half(physical, value);
}

byte virtual_read_byte_wrapper(dword address) {
    word physical = resolve_virtual_address(address);
    return n64_read_byte(physical);
}

void virtual_write_byte_wrapper(dword address, byte value) {
    word physical = resolve_virtual_address(address);
    n64_write_byte(physical, value);
}

void n64_load_rom(const char* rom_path) {
    logalways("Loading %s", rom_path);
    load_n64rom(&n64sys.mem.rom, rom_path);
    gamedb_match(&n64sys);
    init_savedata(&n64sys.mem, rom_path);
    strcpy(n64sys.rom_path, rom_path);
}

void init_n64system(const char* rom_path, bool enable_frontend, bool enable_debug, n64_video_type_t video_type, bool use_interpreter) {
    // align to page boundary
    memset(&n64sys, 0x00, sizeof(n64_system_t));
    init_mem(&n64sys.mem);

    n64sys.video_type = video_type;

    n64sys.cpu.read_dword = &virtual_read_dword_wrapper;
    n64sys.cpu.write_dword = &virtual_write_dword_wrapper;

    n64sys.cpu.read_word = &virtual_read_word_wrapper;
    n64sys.cpu.write_word = &virtual_write_word_wrapper;

    n64sys.cpu.read_half = &virtual_read_half_wrapper;
    n64sys.cpu.write_half = &virtual_write_half_wrapper;

    n64sys.cpu.read_byte = &virtual_read_byte_wrapper;
    n64sys.cpu.write_byte = &virtual_write_byte_wrapper;

    n64sys.rsp.read_physical_word = &n64_read_physical_word;
    n64sys.rsp.write_physical_word = &write_physical_word_wrapper;

    if (mprotect(&codecache, CODECACHE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        logfatal("mprotect codecache failed! %s", strerror(errno));
    }
    n64sys.dynarec = n64_dynarec_init(codecache, CODECACHE_SIZE);

    if (enable_frontend) {
        render_init(video_type);
    }
    n64sys.debugger_state.enabled = enable_debug;
    if (enable_debug) {
        debugger_init();
    }
    n64sys.use_interpreter = use_interpreter;

    reset_n64system();

    if (rom_path != NULL) {
        n64_load_rom(rom_path);
    }
}

void reset_n64system() {
    force_persist_backup();
    if (n64sys.mem.save_data != NULL) {
        free(n64sys.mem.save_data);
        n64sys.mem.save_data = NULL;
    }
    if (n64sys.mem.mempack_data != NULL) {
        free(n64sys.mem.mempack_data);
        n64sys.mem.mempack_data = NULL;
    }
    n64sys.cpu.branch = false;
    n64sys.cpu.exception = false;


    for (int i = 0; i < SP_IMEM_SIZE / 4; i++) {
        n64sys.rsp.icache[i].instruction.raw = 0;
        n64sys.rsp.icache[i].handler = cache_rsp_instruction;
    }

    n64sys.rsp.status.halt = true; // RSP starts halted

    n64sys.vi.vi_v_intr = 256;

    n64sys.dpc.status.raw = 0x80;


    n64sys.ai.dac.frequency = 44100;
    n64sys.ai.dac.precision = 16;
    n64sys.ai.dac.period = CPU_HERTZ / n64sys.ai.dac.frequency;

    n64sys.si.controllers[0].plugged_in = true;
    n64sys.si.controllers[1].plugged_in = false;
    n64sys.si.controllers[2].plugged_in = false;
    n64sys.si.controllers[3].plugged_in = false;

    n64sys.cpu.cp0.status.bev = true;
    n64sys.cpu.cp0.cause.raw  = 0xB000007C;
    n64sys.cpu.cp0.EPC        = 0xFFFFFFFFFFFFFFFF;
    n64sys.cpu.cp0.PRId       = 0x00000B22;
    n64sys.cpu.cp0.config     = 0x70000000;
    n64sys.cpu.cp0.error_epc  = 0xFFFFFFFFFFFFFFFF;

    memset(n64sys.mem.rdram, 0, N64_RDRAM_SIZE);
    memset(n64sys.rsp.sp_dmem, 0, SP_DMEM_SIZE);
    memset(n64sys.rsp.sp_imem, 0, SP_IMEM_SIZE);
    memset(n64sys.mem.pif_ram, 0, PIF_RAM_SIZE);

    invalidate_dynarec_all_pages(n64sys.dynarec);
}

INLINE int jit_system_step() {
    /* Commented out for now since the game never actually reads cp0.random
     * TODO: when a game does, consider generating a random number rather than updating this every instruction
    if (N64CP0.random <= N64CP0.wired) {
        N64CP0.random = 31;
    } else {
        N64CP0.random--;
    }
     */

    if (unlikely(N64CPU.interrupts > 0)) {
        if(N64CP0.status.ie && !N64CP0.status.exl && !N64CP0.status.erl) {
            r4300i_handle_exception(&N64CPU, N64CPU.pc, EXCEPTION_INTERRUPT, -1);
            return CYCLES_PER_INSTR;
        }
    }
    static int cpu_steps = 0;
    int taken = n64_dynarec_step();
    {
        uint64_t oldcount = N64CP0.count >> 1;
        uint64_t newcount = (N64CP0.count + (taken * CYCLES_PER_INSTR)) >> 1;
        if (unlikely(oldcount < N64CP0.compare && newcount >= N64CP0.compare)) {
            N64CP0.cause.ip7 = true;
            loginfo("Compare interrupt! oldcount: 0x%08lX newcount: 0x%08lX compare 0x%08X", oldcount, newcount, N64CP0.compare);
            r4300i_interrupt_update(&N64CPU);
        }
        N64CP0.count += taken;
        N64CP0.count &= 0x1FFFFFFFF;
    }
    cpu_steps += taken;

    if (!N64RSP.status.halt) {
        // 2 RSP steps per 3 CPU steps
        while (cpu_steps > 2) {
            N64RSP.steps += 2;
            cpu_steps -= 3;
        }

        rsp_run(&N64RSP);
    } else {
        N64RSP.steps = 0;
        cpu_steps = 0;
    }

    return taken;
}

INLINE int interpreter_system_step() {
#ifdef N64_DEBUG_MODE
    if (n64sys.debugger_state.enabled && check_breakpoint(&n64sys.debugger_state, N64CPU.pc)) {
        debugger_breakpoint_hit();
    }
    while (n64sys.debugger_state.broken) {
        usleep(1000);
        debugger_tick();
    }
#endif
    int taken = CYCLES_PER_INSTR;
    r4300i_step(&N64CPU);
    static int cpu_steps = 0;
    cpu_steps += taken;

    if (N64RSP.status.halt) {
        cpu_steps = 0;
        N64RSP.steps = 0;
    } else {
        // 2 RSP steps per 3 CPU steps
        while (cpu_steps > 2) {
            N64RSP.steps += 2;
            cpu_steps -= 3;
        }
        rsp_run(&N64RSP);
    }

    return taken;
}

// This is used for debugging tools, it's fine for now if timing is a little off.
void n64_system_step(bool dynarec) {
    if (dynarec) {
        jit_system_step();
    } else {
        r4300i_step(&N64CPU);
        if (!N64RSP.status.halt) {
            rsp_step(&N64RSP);
        }
    }
}

void check_vsync() {
    if (n64sys.vi.v_current == n64sys.vi.vsync >> 1) {
        rdp_update_screen();
    }
}

void jit_system_loop() {
    int cycles = 0;
    while (!should_quit) {
        for (n64sys.vi.v_current = 0; n64sys.vi.v_current < NUM_SHORTLINES; n64sys.vi.v_current++) {
            check_vi_interrupt();
            check_vsync();
            while (cycles <= SHORTLINE_CYCLES) {
                cycles += jit_system_step();
                n64sys.debugger_state.steps = 0;
            }
            cycles -= SHORTLINE_CYCLES;
            ai_step(SHORTLINE_CYCLES);
        }
        for (; n64sys.vi.v_current < NUM_SHORTLINES + NUM_LONGLINES; n64sys.vi.v_current++) {
            check_vi_interrupt();
            check_vsync();
            while (cycles <= LONGLINE_CYCLES) {
                cycles += jit_system_step();
                n64sys.debugger_state.steps = 0;
            }
            cycles -= LONGLINE_CYCLES;
            ai_step(LONGLINE_CYCLES);
        }
        check_vi_interrupt();
        check_vsync();
#ifdef N64_DEBUG_MODE
        if (n64sys.debugger_state.enabled) {
            debugger_tick();
        }
#endif
#ifdef LOG_ENABLED
update_delayed_log_verbosity();
#endif
        persist_backup();
        reset_all_metrics();
    }
    force_persist_backup();
}

void interpreter_system_loop() {
    int cycles = 0;
    while (!should_quit) {
        for (n64sys.vi.v_current = 0; n64sys.vi.v_current < NUM_SHORTLINES; n64sys.vi.v_current++) {
            check_vi_interrupt();
            check_vsync();
            while (cycles <= SHORTLINE_CYCLES) {
                cycles += interpreter_system_step();
                n64sys.debugger_state.steps = 0;
            }
            cycles -= SHORTLINE_CYCLES;
            ai_step(SHORTLINE_CYCLES);
        }
        for (; n64sys.vi.v_current < NUM_SHORTLINES + NUM_LONGLINES; n64sys.vi.v_current++) {
            check_vi_interrupt();
            check_vsync();
            while (cycles <= LONGLINE_CYCLES) {
                cycles += interpreter_system_step();
                n64sys.debugger_state.steps = 0;
            }
            cycles -= LONGLINE_CYCLES;
            ai_step(LONGLINE_CYCLES);
        }
        check_vi_interrupt();
        check_vsync();
#ifdef N64_DEBUG_MODE
        if (n64sys.debugger_state.enabled) {
            debugger_tick();
        }
#endif
#ifdef LOG_ENABLED
        update_delayed_log_verbosity();
#endif
        persist_backup();
        reset_all_metrics();
    }
    force_persist_backup();
}

void n64_system_loop() {
    if (n64sys.use_interpreter) {
        interpreter_system_loop();
    } else {
        jit_system_loop();
    }
}

void n64_system_cleanup() {
    rdp_cleanup();
    if (n64sys.dynarec != NULL) {
        free(n64sys.dynarec);
        n64sys.dynarec = NULL;
    }
    debugger_cleanup();

    free(n64sys.mem.rom.rom);
    n64sys.mem.rom.rom = NULL;

    free(n64sys.mem.rom.pif_rom);
    n64sys.mem.rom.pif_rom = NULL;
}

void n64_request_quit() {
    should_quit = true;
}

void on_interrupt_change() {
    bool interrupt = n64sys.mi.intr.raw & n64sys.mi.intr_mask.raw;
    loginfo("ip2 is now: %d", interrupt);
    N64CPU.cp0.cause.ip2 = interrupt;
    r4300i_interrupt_update(&N64CPU);
}

void interrupt_raise(n64_interrupt_t interrupt) {
    switch (interrupt) {
        case INTERRUPT_VI:
            loginfo("Raising VI interrupt");
            n64sys.mi.intr.vi = true;
            break;
        case INTERRUPT_SI:
            loginfo("Raising SI interrupt");
            n64sys.mi.intr.si = true;
            break;
        case INTERRUPT_PI:
            loginfo("Raising PI interrupt");
            n64sys.mi.intr.pi = true;
            break;
        case INTERRUPT_AI:
            loginfo("Raising AI interrupt");
            n64sys.mi.intr.ai = true;
            break;
        case INTERRUPT_DP:
            loginfo("Raising DP interrupt");
            n64sys.mi.intr.dp = true;
            break;
        case INTERRUPT_SP:
            loginfo("Raising SP interrupt");
            n64sys.mi.intr.sp = true;
            break;
        default:
            logfatal("Raising unimplemented interrupt: %d", interrupt);
    }

    on_interrupt_change();
}

void interrupt_lower(n64_interrupt_t interrupt) {
    switch (interrupt) {
        case INTERRUPT_VI:
            n64sys.mi.intr.vi = false;
            loginfo("Lowering VI interrupt");
            break;
        case INTERRUPT_SI:
            n64sys.mi.intr.si = false;
            loginfo("Lowering SI interrupt");
            break;
        case INTERRUPT_PI:
            n64sys.mi.intr.pi = false;
            loginfo("Lowering PI interrupt");
            break;
        case INTERRUPT_DP:
            n64sys.mi.intr.dp = false;
            loginfo("Lowering DP interrupt");
            break;
        case INTERRUPT_AI:
            n64sys.mi.intr.ai = false;
            loginfo("Lowering DP interrupt");
            break;
        case INTERRUPT_SP:
            n64sys.mi.intr.sp = false;
            loginfo("Lowering SP interrupt");
            break;
        default:
            logfatal("Lowering unimplemented interrupt: %d", interrupt);
    }

    on_interrupt_change();
}

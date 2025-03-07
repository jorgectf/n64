#include "vi.h"
#include <rdp/rdp.h>

#define ADDR_VI_STATUS_REG    0x04400000
#define ADDR_VI_ORIGIN_REG    0x04400004
#define ADDR_VI_WIDTH_REG     0x04400008
#define ADDR_VI_V_INTR_REG    0x0440000C
#define ADDR_VI_V_CURRENT_REG 0x04400010
#define ADDR_VI_BURST_REG     0x04400014
#define ADDR_VI_V_SYNC_REG    0x04400018
#define ADDR_VI_H_SYNC_REG    0x0440001C
#define ADDR_VI_LEAP_REG      0x04400020
#define ADDR_VI_H_START_REG   0x04400024
#define ADDR_VI_V_START_REG   0x04400028
#define ADDR_VI_V_BURST_REG   0x0440002C
#define ADDR_VI_X_SCALE_REG   0x04400030
#define ADDR_VI_Y_SCALE_REG   0x04400034

void write_word_vireg(u32 address, u32 value) {
    switch (address) {
        case ADDR_VI_STATUS_REG: {
            n64sys.vi.status.raw = value;
            // If serrate = 1, render two fields for an interlaced frame.
            n64sys.vi.num_fields = n64sys.vi.status.serrate ? 2 : 1;
            break;
        }
        case ADDR_VI_ORIGIN_REG: {
            u32 masked = value & 0xFFFFFF;
            if (n64sys.vi.vi_origin != masked) {
                n64sys.vi.swaps++;
            }
            n64sys.vi.vi_origin = masked;
            loginfo("VI origin is now 0x%08X (wrote 0x%08X)", value & 0xFFFFFF, value);
            break;
        }
        case ADDR_VI_WIDTH_REG: {
            n64sys.vi.vi_width = value & 0x7FF;
            loginfo("VI width is now 0x%X (wrote 0x%08X)", value & 0xFFF, value);
            break;
        }
        case ADDR_VI_V_INTR_REG:
            n64sys.vi.vi_v_intr = value & 0x3FF;
            loginfo("VI interrupt is now 0x%X (wrote 0x%08X) will VI interrupt when v_current == %d", value & 0x3FF, value, value >> 1);
            break;
        case ADDR_VI_V_CURRENT_REG:
            loginfo("V_CURRENT written, V Intr cleared");
            interrupt_lower(INTERRUPT_VI);
            break;
        case ADDR_VI_BURST_REG:
            n64sys.vi.vi_burst.raw = value;
            break;
        case ADDR_VI_V_SYNC_REG:
            n64sys.vi.vsync = value & 0x3FF;
            n64sys.vi.num_halflines = n64sys.vi.vsync >> 1;
            n64sys.vi.cycles_per_halfline = CPU_CYCLES_PER_FRAME / n64sys.vi.num_halflines;
            loginfo("VI vsync is now 0x%X / %d, wrote 0x%08X", value & 0x3FF, value & 0x3FF, value);
            break;
        case ADDR_VI_H_SYNC_REG:
            n64sys.vi.hsync = value & 0x3FF;
            loginfo("VI hsync is now 0x%X (wrote 0x%08X)", value & 0x3FF, value);
            break;
        case ADDR_VI_LEAP_REG:
            n64sys.vi.leap = value;
            loginfo("VI leap is now 0x%X (wrote 0x%08X)", value, value);
            break;
        case ADDR_VI_H_START_REG:
            n64sys.vi.hstart.raw = value;
            loginfo("VI hstart is now 0x%X (wrote 0x%08X)", value, value);
            break;
        case ADDR_VI_V_START_REG:
            n64sys.vi.vstart.raw = value;
            break;
        case ADDR_VI_V_BURST_REG:
            n64sys.vi.vburst = value;
            loginfo("VI vburst is now 0x%X (wrote 0x%08X)", value, value);
            break;
        case ADDR_VI_X_SCALE_REG:
            n64sys.vi.xscale.raw = value;
            loginfo("VI xscale is now 0x%X (wrote 0x%08X)", value, value);
            break;
        case ADDR_VI_Y_SCALE_REG:
            n64sys.vi.yscale.raw = value;
            loginfo("VI yscale is now 0x%X (wrote 0x%08X)", value, value);
            break;
        default:
            logfatal("Writing word 0x%08X to address 0x%08X in region: REGION_VI_REGS", value, address);
    }
}

u32 read_word_vireg(u32 address) {
    switch (address) {
        case ADDR_VI_STATUS_REG:
            return n64sys.vi.status.raw;
        case ADDR_VI_ORIGIN_REG:
            logfatal("Reading of ADDR_VI_ORIGIN_REG is unsupported");
        case ADDR_VI_WIDTH_REG:
            logfatal("Reading of ADDR_VI_WIDTH_REG is unsupported");
        case ADDR_VI_V_INTR_REG:
            logfatal("Reading of ADDR_VI_V_INTR_REG is unsupported");
        case ADDR_VI_V_CURRENT_REG:
            return n64sys.vi.v_current;
        case ADDR_VI_BURST_REG:
            logfatal("Reading of ADDR_VI_BURST_REG is unsupported");
        case ADDR_VI_V_SYNC_REG:
            logfatal("Reading of ADDR_VI_V_SYNC_REG is unsupported");
        case ADDR_VI_H_SYNC_REG:
            logfatal("Reading of ADDR_VI_H_SYNC_REG is unsupported");
        case ADDR_VI_LEAP_REG:
            logfatal("Reading of ADDR_VI_LEAP_REG is unsupported");
        case ADDR_VI_H_START_REG:
            logfatal("Reading of ADDR_VI_H_START_REG is unsupported");
        case ADDR_VI_V_START_REG:
            logfatal("Reading of ADDR_VI_V_START_REG is unsupported");
        case ADDR_VI_V_BURST_REG:
            logfatal("Reading of ADDR_VI_V_BURST_REG is unsupported");
        case ADDR_VI_X_SCALE_REG:
            logfatal("Reading of ADDR_VI_X_SCALE_REG is unsupported");
        case ADDR_VI_Y_SCALE_REG:
            logfatal("Reading of ADDR_VI_Y_SCALE_REG is unsupported");
        default:
            logfatal("Attempted to read word from unknown VI reg: 0x%08X", address);
    }
}

void check_vi_interrupt() {
    if ((n64sys.vi.v_current & 0x3FE) == n64sys.vi.vi_v_intr) {
        logdebug("Checking for VI interrupt: %d == %d? YES", n64sys.vi.v_current % 0x3FE, n64sys.vi.vi_v_intr);
        interrupt_raise(INTERRUPT_VI);
    } else {
        logdebug("Checking for VI interrupt: %d == %d? nah", n64sys.vi.v_current & 0x3FE, n64sys.vi.vi_v_intr);
    }
}

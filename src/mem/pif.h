#ifndef N64_PIF_H
#define N64_PIF_H

#include <system/n64system.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum n64_button {
    N64_BUTTON_A,
    N64_BUTTON_B,
    N64_BUTTON_Z,
    N64_BUTTON_START,
    N64_BUTTON_DPAD_UP,
    N64_BUTTON_DPAD_DOWN,
    N64_BUTTON_DPAD_LEFT,
    N64_BUTTON_DPAD_RIGHT,
    N64_BUTTON_L,
    N64_BUTTON_R,
    N64_BUTTON_C_UP,
    N64_BUTTON_C_DOWN,
    N64_BUTTON_C_LEFT,
    N64_BUTTON_C_RIGHT,

} n64_button_t;

void pif_rom_execute();
void process_pif_command();
void update_button(int controller, n64_button_t button, bool held);
void update_joyaxis(int controller, sbyte x, sbyte y);
void update_joyaxis_x(int controller, sbyte x);
void update_joyaxis_y(int controller, sbyte y);
void load_pif_rom(const char* pif_rom_path);

#ifdef __cplusplus
}
#endif
#endif //N64_PIF_H

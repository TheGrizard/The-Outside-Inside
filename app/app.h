/* The Outside Inside: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "THE OUTSIDE INSIDE"
#define APP_BIG_FONT FONT_M          /* the splash's name in 24 px, so all three words fit across the screen */
#define APP_SPLASH_RGB RGB(91, 209, 122)
#define APP_SPLASH                       /* our own first screen (app_splash): black, the name in white */
void app_splash(void);
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */

/* the store: the settings (with the user environments) and the panel's calibration */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks (host/plat_host.c "expect") */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);

/* The Outside Inside: generative ambience for the M-VAVE FM-1.
 * The app: the sound engine (dsp/) driven by the panel, an animated home screen, settings and user environments in
 * flash. Part of fm1-base's unity build (device/unity.c, host/plat_host.c): gfx.c and announce.c are in scope.
 *
 * Controls
 *   K1-K4            layer volumes: WEATHER, ANIMALS, LANDSCAPE, HUMANS
 *   SEL held + K1-K4 layer densities (how often each layer's events happen); SEL tapped: the next layer
 *   HOME held + K1-K4 macros: WEATHER, TIME, ACTIVITY, ENTROPY
 *   ALGORITHM        environment (nine built in, then the user's)
 *   SELECT (turn)    the layer to edit
 *   PRESET           the selected layer's patch (the preset list shows while it turns)
 *   REC              randomise the macros
 *   SAVE             save as a user environment (a new one, or the user environment being played)
 *   PLAY             start / stop (a fade)
 *   OCT- / OCT+      weather type down / up
 *   first four white keys: one event from WEATHER, ANIMALS, LANDSCAPE, HUMANS */
#include "fastmath.h"
#include "dsp/engine.h"
#include "backdrops.h"

static Engine eng PLAT_POOL;

/* ------------------------------------------------------------------ settings */
#define ALGO_JOURNEY 250                 /* set.algo: the journey (not an environment) */
#define ALGO_DREAM 251                   /* set.algo: the dream (the journey through places mixed at random) */
#define SET_MAGIC 0x4F555433u            /* "OUT3" (OUT2: ten places; OUT1: before MEADOW and THUNDER; moved on at load) */
#define SET_MAGIC2 0x4F555432u
#define SET_MAGIC1 0x4F555431u
#define NUSER 8
typedef struct { uint8_t patch[NUM_LAYERS], level[NUM_LAYERS], dens[NUM_LAYERS], mac[4]; } env_t;
typedef struct {
    uint32_t magic;
    uint8_t algo, nusers, playing, rsv;
    env_t cur;
    env_t user[NUSER];
} settings_t;
static settings_t set;
static uint8_t set_dirty;                 /* changed since the environment was loaded or saved: SAVE lights */
static int store_ok = 1;                 /* saves land in flash: the FM-1's flash chip is one the storage knows */
static uint32_t autosave_at;             /* the current scene kept in flash a while after the last change */
#define AUTOSAVE_MS 8000u

static const char *const LAYER_NAME[NUM_LAYERS] = { "WEATHER", "ANIMALS", "LANDSCAPE", "HUMANS" };
static const char *const MACRO_NAME[4] = { "WEATHER", "TIME", "ACTIVITY", "ENTROPY" };
#define C_W RGB(90, 169, 255)
#define C_A RGB(255, 210, 63)
#define C_L RGB(91, 209, 122)
#define C_H RGB(255, 111, 174)
static const uint16_t LAYER_C[NUM_LAYERS] = { C_W, C_A, C_L, C_H };
#define C_TXT RGB(230, 230, 230)
#define C_JOURNEY RGB(255, 178, 70)      /* JOURNEY's amber */
#define C_DREAM RGB(196, 146, 255)       /* DREAM's violet */
#define C_SUB RGB(168, 168, 168)
#define C_RULE RGB(38, 38, 38)
#define C_BACK RGB(60, 60, 60)

static void settings_defaults(void)
{
    static const uint8_t LV[NUM_LAYERS] = { 60, 75, 65, 35 };
    set.magic = SET_MAGIC;
    set.algo = 4;                        /* FOREST */
    set.nusers = 0;
    set.playing = 1;
    for (int l = 0; l < NUM_LAYERS; l++) { set.cur.patch[l] = 4; set.cur.level[l] = LV[l]; set.cur.dens[l] = 50; }
    set.cur.mac[0] = 33; set.cur.mac[1] = 33; set.cur.mac[2] = 50; set.cur.mac[3] = 40;
}

static int settings_ok(void)
{
    if (set.magic != SET_MAGIC || set.nusers > NUSER || (set.algo >= NUM_ENVS + set.nusers && set.algo != ALGO_JOURNEY && set.algo != ALGO_DREAM)) return 0;
    for (int l = 0; l < NUM_LAYERS; l++) if (set.cur.patch[l] >= NUM_ENVS) return 0;
    return 1;
}

/* OUT1's settings: nine environments (the user's numbered from 9) and ten Earth weathers (THUNDER came in at 8) */
static void settings_from_1(void)
{
    if (set.algo >= 9) set.algo++;
    for (int i = -1; i < NUSER; i++) {
        env_t *v = i < 0 ? &set.cur : &set.user[i];
        if (weather_set(v->patch[L_WEATHER]) != WX_EARTH) continue;
        int t = (int)(v->mac[0] / 100.0f * 9 + 0.5f);
        if (t >= 8) t++;
        v->mac[0] = (uint8_t)(weather_value(WX_EARTH, t) * 100.0f + 0.5f);
    }
    set.magic = SET_MAGIC2;
}
/* OUT2's: ten places, the user's numbered from 10 (the five more go in before them) */
static void settings_from_2(void)
{
    if (set.algo >= 10 && set.algo < ALGO_JOURNEY) set.algo += 5;
    set.magic = SET_MAGIC;
}

static void settings_load(void)
{
    int n = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    if (n == (int)sizeof set && (set.magic == SET_MAGIC1 || set.magic == SET_MAGIC2)) {
        if (set.magic == SET_MAGIC1) settings_from_1();
        settings_from_2();
        if (settings_ok()) store_ok = plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0;
    }
    if (n != (int)sizeof set || !settings_ok()) {
        settings_defaults();
        store_ok = plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0;   /* a first save: does the flash take it? */
    }
    set.playing = 1;                     /* it always starts playing */
}

/* the journey's state (the journey itself is further on, after the environments) */
static struct {
    int on, dream, at, travelling, held, wx_own;
    float stay, t, cyc, cyc_aim, wx, wx_from, wx_to;
    uint8_t from[NUM_LAYERS], to[NUM_LAYERS], swapped[NUM_LAYERS], mac0, mac1;
    float lv_from[NUM_LAYERS], mix[NUM_LAYERS], swap_at[NUM_LAYERS], share[NUM_LAYERS], agc;   /* agc: the dream's evening out (dB) */
    uint32_t last;
} J;

/* the settings into the engine (the render reads them; single words, no lock needed) */
static void engine_follow(void)
{
    for (int l = 0; l < NUM_LAYERS; l++) {
        engine_want(&eng, l, set.cur.patch[l]);
        eng.level[l] = set.cur.level[l] / 100.0f;
        eng.dens[l] = set.cur.dens[l] / 50.0f;
    }
    eng.user.weather = set.cur.mac[0] / 100.0f;
    eng.user.time = set.cur.mac[1] / 100.0f;
    eng.user.activity = set.cur.mac[2] / 100.0f;
    eng.user.entropy = set.cur.mac[3] / 100.0f;
    eng.playing = set.playing;
}

/* ------------------------------------------------------------------ sound */
#define OUT_GAIN 2.4f
static volatile int muted;
static volatile float out_pow;           /* the scene's power, smoothed over seconds */
static float gain_now;

static inline float soft(float x)       /* a gentle limiter: transparent below 0.7, never past 1.0 */
{
    float a = fm_fabsf(x);
    if (a < 0.7f) return x;
    float y = 0.7f + 0.3f * fm_tanhf((a - 0.7f) / 0.3f);
    return x < 0 ? -y : y;
}

static void midi_in(void)                /* CC 20-23 layer volumes, 24-27 macros, on any channel */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xF0u, cc = (pkt >> 16) & 0x7Fu, v = pkt >> 24 & 0x7Fu;
        if (st != 0xB0u) continue;
        if (cc >= 20u && cc <= 23u) { if (J.on) J.mix[cc - 20u] = v * 100.0f / 127.0f; else eng.level[cc - 20u] = v / 127.0f; }
        else if (cc == 24u) { if (J.on) set.cur.mac[0] = (uint8_t)(v * 100u / 127u); else eng.user.weather = v / 127.0f; }
        else if (cc == 25u) { if (J.on) set.cur.mac[1] = (uint8_t)(v * 100u / 127u); else eng.user.time = v / 127.0f; }
        else if (cc == 26u) eng.user.activity = v / 127.0f;
        else if (cc == 27u) eng.user.entropy = v / 127.0f;
    }
}

/* Two cores: the four layers are independent of each other, so the second core renders some of them while this one
 * renders the rest; then this one mixes them and runs the reverb. Which layers go where follows what each has cost
 * lately (eng.cost), so the two finish at about the same time whatever the environment. */
static int two_cores;
static uint8_t on2_mask;                 /* the layers the second core takes */
static void layers_job(uint32_t mask)
{
    for (int l = 0; l < NUM_LAYERS; l++) if (mask >> l & 1u) engine_block_layer(&eng, l);
}

static uint8_t balance(void)             /* the split that leaves the busier core least to do */
{
    uint8_t best = 0x3u;                 /* (WEATHER and ANIMALS there, at first) */
    float best_t = 1e30f, total = 0;
    for (int l = 0; l < NUM_LAYERS; l++) total += eng.cost[l];
    if (total <= 0) return best;
    for (uint8_t m = 1; m < 15; m++) {
        float t2 = 0;
        for (int l = 0; l < NUM_LAYERS; l++) if (m >> l & 1u) t2 += eng.cost[l];
        float t = t2 > total - t2 ? t2 : total - t2;
        if (t < best_t - 1.0f) { best_t = t; best = m; }
    }
    return best;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float L[ENGINE_SUB * OUT_BLOCK], R[ENGINE_SUB * OUT_BLOCK];
    float g = muted ? 0.0f : (float)master_q12 / 4096.0f * OUT_GAIN;
    int nsub = (int)(n / OUT_BLOCK);
    midi_in();
    if (nsub > ENGINE_SUB) nsub = ENGINE_SUB;
    engine_block_begin(&eng, nsub);
    uint8_t mask = two_cores && plat_cpu2_on() ? on2_mask : 0;
    int sent = mask && plat_cpu2_run(layers_job, mask);
    layers_job(sent ? (uint32_t)(~mask & 0xFu) : 0xFu);
    if (sent && plat_cpu2_wait() != 0) layers_job(mask);   /* it dropped the job: do it here */
    engine_block_events(&eng);
    engine_block_mix(&eng, L, R);
    if (two_cores) on2_mask = balance();
    {   /* how loud the scene is (before the volume knob): the dream holds it steady by it, slowly */
        float ss = 0; int m = nsub * OUT_BLOCK;
        for (int i = 0; i < m; i++) ss += L[i] * L[i] + R[i] * R[i];
        out_pow += (ss / (2.0f * (float)(m > 0 ? m : 1)) - out_pow) * 0.002f;   /* (about three seconds) */
    }
    for (uint32_t i = 0; i < n && i < (uint32_t)(nsub * OUT_BLOCK); i++) {
        gain_now += (g - gain_now) * 0.002f;
        lr[2 * i] = (int32_t)(soft(L[i] * gain_now) * 8388000.0f);
        lr[2 * i + 1] = (int32_t)(soft(R[i] * gain_now) * 8388000.0f);
    }
}

void app_panic(void) { muted = 1; gain_now = 0; }

static void j_start(int dream);
static float frand(void);

void app_init(void)
{
    dsp_init();
    settings_load();
    engine_init(&eng, 0x5EED0000u ^ plat_ms());
    eng.clock = plat_ticks24;
    eng.core = plat_core;
    engine_follow();
    if (set.algo == ALGO_JOURNEY || set.algo == ALGO_DREAM) j_start(set.algo == ALGO_DREAM);   /* (on again: on from the land it was in) */
    on2_mask = 0x3u;
    two_cores = plat_cpu2_set(1) == 0;   /* else everything on this core */
}

/* ------------------------------------------------------------------ text helpers */
static void text_cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static void int_text(int32_t v, char *b)
{
    char t[12]; int n = 0; uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do t[n++] = (char)('0' + u % 10u); while ((u /= 10u) && n < 11);
    if (v < 0) *b++ = '-';
    while (n) *b++ = t[--n];
    *b = 0;
}

/* ------------------------------------------------------------------ UI state */
static struct {
    uint32_t btn, keys;
    int sel;                             /* the layer being edited */
    int sel_used, home_used;             /* SEL / HOME: tapped (no knob turned) or held with the knobs */
    char pop_t[28], pop_s[28], pop_3[30], pop_4[40]; uint16_t pop_c; uint32_t pop_until;
    int list_layer; uint32_t list_until; /* a layer's preset list */
    uint32_t envs_until;                 /* the environment list */
    uint32_t panel_until; int panel;     /* the knobs' panel: levels, densities or macros */
    uint32_t last_view, last_head;
    float flash; uint16_t flash_c;
    uint32_t evq_tail;
    float t;                             /* animation time, seconds */
    char head_key[80];
    uint32_t splash_t0; int splash_on, touched;   /* the opening animation; any control skips it */
} ui;
enum { PANEL_LEVELS, PANEL_DENS, PANEL_MACROS };

/* ------------------------------------------------------------------ orders: A to Z */
static uint8_t env_order[NUM_ENVS];                 /* the built-in environments, by name */
static uint8_t patch_order[NUM_LAYERS][NUM_ENVS];   /* each layer's presets, by name */
static uint8_t wx_order[WX_SETS][WEATHER_MAX];      /* each world's weather types, by name */

static int str_less(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a < (uint8_t)*b;
}
static void sort_by(uint8_t *o, int n, const char *(*name)(int, int), int arg)
{
    for (int i = 0; i < n; i++) o[i] = (uint8_t)i;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && str_less(name(o[j], arg), name(o[j - 1], arg)); j--) { uint8_t t = o[j]; o[j] = o[j - 1]; o[j - 1] = t; }
}
static const char *env_nm(int i, int arg) { (void)arg; return ENV_NAMES[i]; }
static const char *patch_nm(int i, int l) { return PATCHES[l][i].name; }
static const char *wx_nm(int i, int set) { return weather_name(set, weather_value(set, i)); }
static void orders_init(void)
{
    sort_by(env_order, NUM_ENVS, env_nm, 0);
    for (int l = 0; l < NUM_LAYERS; l++) sort_by(patch_order[l], NUM_ENVS, patch_nm, l);
    for (int w = 0; w < WX_SETS; w++) sort_by(wx_order[w], weather_count(w), wx_nm, w);
}
static int pos_of(const uint8_t *o, int n, int v) { for (int i = 0; i < n; i++) if (o[i] == v) return i; return 0; }

/* each built-in environment's own starting volumes: WEATHER, ANIMALS, LANDSCAPE, HUMANS */
static const uint8_t ENV_LEVELS[NUM_ENVS][NUM_LAYERS] = {
    { 50, 45, 60, 70 },   /* CITY */
    { 55, 80, 65, 25 },   /* JUNGLE */
    { 45, 55, 75, 55 },   /* SPACE */
    { 45, 55, 75, 30 },   /* CAVES */
    { 60, 75, 65, 35 },   /* FOREST */
    { 50, 75, 65, 25 },   /* SWAMP */
    { 65, 55, 75, 35 },   /* OCEAN */
    { 55, 70, 65, 40 },   /* ALIEN */
    { 50, 35, 60, 75 },   /* INDUSTRIAL */
    { 50, 70, 65, 30 },   /* MEADOW */
    { 65, 60, 60, 40 },   /* MOUNTAIN */
    { 65, 60, 65, 40 },   /* ARCTIC */
    { 70, 65, 70, 40 },   /* DESERT */
    { 65, 80, 70, 40 },   /* SAVANNA */
    { 55, 70, 60, 50 },   /* VILLAGE */
};

/* the environments in the order ALGORITHM steps: JOURNEY, DREAM, the fifteen A to Z, then the user's */
static int env_count(void) { return 2 + NUM_ENVS + set.nusers; }
static int env_at(int pos) { return pos == 0 ? ALGO_JOURNEY : pos == 1 ? ALGO_DREAM : pos - 2 < NUM_ENVS ? env_order[pos - 2] : pos - 2; }
static int env_pos(int algo) { return algo == ALGO_JOURNEY ? 0 : algo == ALGO_DREAM ? 1 : 2 + (algo < NUM_ENVS ? pos_of(env_order, NUM_ENVS, algo) : algo); }

static void env_label(int algo, char *t)
{
    t[0] = 0;
    if (algo == ALGO_JOURNEY) text_cat(t, "JOURNEY");
    else if (algo == ALGO_DREAM) text_cat(t, "DREAM");
    else if (algo < NUM_ENVS) text_cat(t, ENV_NAMES[algo]);
    else { char n[4]; text_cat(t, "USER "); int_text(algo - NUM_ENVS + 1, n); text_cat(t, n); }
}
static void env_name(char *t)            /* with a star when a layer's preset has been changed */
{
    env_label(set.algo, t);
    if (set.algo == ALGO_JOURNEY || set.algo == ALGO_DREAM) return;
    for (int l = 0; l < NUM_LAYERS; l++) {
        int want = set.algo < NUM_ENVS ? set.algo : set.user[set.algo - NUM_ENVS].patch[l];
        if (set.cur.patch[l] != want) { text_cat(t, "*"); break; }
    }
}

/* the weather: the engine's scale runs calm to stormy (it blends neighbours and ENTROPY drifts along it); the knob
 * and OCT-/OCT+ step through the types A to Z */
static int wx_set(void) { return weather_set(set.cur.patch[L_WEATHER]); }   /* the weather preset's world */
static int wx_n(void) { return weather_count(wx_set()); }
static int wx_type(void) { int t = (int)(set.cur.mac[0] / 100.0f * (wx_n() - 1) + 0.5f); return clamp(t, 0, wx_n() - 1); }
static int wx_pos(void) { return pos_of(wx_order[wx_set()], wx_n(), wx_type()); }

static void say_text(const char *a, const char *b)
{
    if (!ann_on()) return;
    char t[64] = "", *q = t;
    text_cat(t, a); text_cat(t, " "); text_cat(t, b);
    for (char *p = t; *p; p++) if (*p != '<' && *p != '>' && *p != '*') *q++ = *p;   /* the marks are for the eye */
    *q = 0;
    ann_say(t, 0, ANN_NOW);
}

void app_say(const char *a, const char *b)
{
    int i;
    for (i = 0; i < 27 && a && a[i]; i++) ui.pop_t[i] = a[i];
    ui.pop_t[i] = 0;
    for (i = 0; i < 27 && b && b[i]; i++) ui.pop_s[i] = b[i];
    ui.pop_s[i] = 0;
    ui.pop_3[0] = ui.pop_4[0] = 0;
    ui.pop_c = C_TXT;
    ui.pop_until = plat_ms() + 1300u;
    ui.list_until = ui.envs_until = 0;
    say_text(ui.pop_t, ui.pop_s);
}
static void pop(const char *a, const char *b, uint16_t c) { app_say(a, b); ui.pop_c = c; }

static void show_list(int layer)
{
    ui.list_layer = layer;
    ui.list_until = plat_ms() + 2000u;
    ui.pop_until = ui.envs_until = 0;
    say_text(LAYER_NAME[layer], PATCHES[layer][set.cur.patch[layer]].name);
}

static void show_envs(void)
{
    char t[24];
    ui.envs_until = plat_ms() + 2000u;
    ui.pop_until = ui.list_until = 0;
    env_name(t);
    say_text("ENVIRONMENT", t);
}

static void show_panel(int which)
{
    ui.panel = which;
    ui.panel_until = plat_ms() + 2000u;
    ui.pop_until = 0;
}

/* ------------------------------------------------------------------ actions */
static void changed(void) { set_dirty = 1; autosave_at = plat_ms() + AUTOSAVE_MS; engine_follow(); }

static void select_layer(int l) { ui.sel = (l % NUM_LAYERS + NUM_LAYERS) % NUM_LAYERS; show_list(ui.sel); }

/* ------------------------------------------------------------------ the journey
 * Round the Earth's places, about an hour: forest, meadow, city, industrial, ocean, swamp, jungle, caves, and back.
 * Six minutes in each, then a minute and a half on the way to the next: the weather turns first, then the
 * land, then the animals, then the people, each layer fading down, changing and coming back up while the others
 * play on, so the two places overlap on the way. The weather glides to one that suits the next place, the day goes
 * round (a day an hour, so each lap is one day: each place at the same time of day, lap after lap, unless held or
 * sent on), and the places are evened out in loudness. OCT+ travels on now, OCT- holds here;
 * K1-K4 set the mix of the four layers for the whole journey; turning WEATHER or TIME (or REC) takes them over
 * (the weather until the next place, the time from there on). Nothing of it is saved but that it is on. */
#define J_STAY 360.0f
#define J_LOUDNESS -27.0f                        /* the journey's usual loudness (dB, before the volume): the dream's aim */
#define J_TRAVEL 90.0f
#define J_DAY 3600.0f                            /* a day an hour: a loop of the eight, to the minute */
static const uint8_t J_ROUTE[] = { 4, 9, 0, 8, 6, 5, 1, 3 };   /* FOREST MEADOW CITY INDUSTRIAL OCEAN SWAMP JUNGLE CAVES */
#define J_N ((int)sizeof J_ROUTE)
static const uint8_t J_WX[NUM_ENVS][5] = {     /* the weathers that suit each place (its weather set's: Earth's 0 SNOW .. 10 WINDY) */
    { 4, 5, 3, 10, 2 },   /* CITY: drizzle, rain, breeze, windy, calm */
    { 6, 7, 5, 2, 3 },    /* JUNGLE: downpour, thunderstorm, rain, calm, breeze */
    { 2, 2, 2, 2, 2 },    /* (SPACE) */
    { 2, 4, 5, 3, 1 },    /* CAVES */
    { 3, 4, 5, 1, 2 },    /* FOREST: breeze, drizzle, rain, fog, calm */
    { 1, 2, 7, 4, 5 },    /* SWAMP: fog, calm, thunderstorm, drizzle, rain */
    { 10, 9, 3, 5, 2 },   /* OCEAN: windy, gale, breeze, rain, calm */
    { 2, 2, 2, 2, 2 },    /* (ALIEN) */
    { 4, 1, 10, 2, 5 },   /* INDUSTRIAL: drizzle, fog, windy, calm, rain */
    { 2, 3, 2, 8, 4 },    /* MEADOW: calm, breeze, calm, thunder, drizzle */
    { 3, 10, 1, 9, 0 },   /* MOUNTAIN: breeze, windy, fog, gale, snow */
    { 1, 3, 4, 5, 7 },    /* ARCTIC (its own): diamond dust, light snow, snow, windy, blizzard */
    { 1, 2, 3, 4, 5 },    /* DESERT (its own): heat haze, breeze, windy, dry thunder, sandstorm */
    { 0, 2, 3, 6, 5 },    /* SAVANNA (its own): calm, heat haze, breeze, thunderstorm, downpour */
    { 3, 4, 5, 1, 2 },    /* VILLAGE: breeze, drizzle, rain, fog, calm */
};
static const float J_TRIM[NUM_ENVS] = {         /* each place's level, to even them out (measured: 10^(dB/40)) */
    0.987f, 1.077f, 1.382f, 1.096f, 0.982f, 1.062f, 0.733f, 0.934f, 0.980f, 1.279f, 1.086f, 1.201f, 1.225f, 1.164f, 1.190f };
/* each layer's loudness in each place at the journey's levels (dB, measured), so the way there never empties: the
 * weather changes first, then of the rest the one that gains most (loud where you're going, quiet where you were)
 * and last the one that loses most. Into the city: the traffic before the birds go */
static const int8_t J_LOUD[NUM_ENVS][NUM_LAYERS] = {
    { -33, -40, -41, -20 }, { -26, -23, -28, -47 }, { -23, -31, -35, -28 }, { -29, -43, -21, -38 }, { -27, -27, -22, -39 },
    { -30, -23, -24, -47 }, { -32, -40, -19, -53 }, { -24, -27, -24, -33 }, { -35, -45, -35, -20 }, { -27, -24, -29, -39 },
    { -24, -34, -23, -43 }, { -22, -29, -36, -42 }, { -22, -30, -29, -45 }, { -23, -28, -28, -46 }, { -26, -23, -32, -34 } };
/* what each layer costs the processor in each place (measured, the engine's meter; W A L H), so the dream never mixes
 * a scene heavier than the heaviest place (INDUSTRIAL: 97), which plays without a glitch */
static const uint8_t J_COST[NUM_ENVS][NUM_LAYERS] = {
    { 18, 1, 7, 40 }, { 19, 8, 28, 4 }, { 20, 3, 9, 23 }, { 8, 1, 44, 6 }, { 23, 2, 32, 11 },
    { 19, 4, 16, 6 }, { 17, 2, 21, 29 }, { 14, 4, 37, 11 }, { 18, 1, 18, 59 }, { 16, 19, 9, 9 },
    { 18, 1, 34, 14 }, { 32, 13, 4, 15 }, { 32, 5, 7, 12 }, { 20, 2, 11, 27 }, { 19, 4, 10, 14 } };
#define J_COST_MAX 97
static const float J_SLOT[NUM_LAYERS] = { 12, 34, 56, 78 };   /* when on the way the changes come (s) */
#define J_DOWN 9.0f                              /* each layer eases down this long before (to J_LOW, never out: the place
                                                  * never empties), changes over J_SWAP_S, comes up this long after */
#define J_UP 10.0f
#define J_LOW 0.35f                              /* (more for a layer that is most of the sound: up to J_LOW + 0.4) */
#define J_SWAP_S 3.0f
static float smooth01(float u) { u = u < 0 ? 0 : u > 1 ? 1 : u; return u * u * (3 - 2 * u); }
static float j_level(int env, int l)
{
    float v = ENV_LEVELS[env][l] * J_TRIM[env] * J.mix[l] / 75.0f * (J.dream ? fm_exp2f(J.agc * 0.0830f) : 1.0f);   /* (10^(dB/40)) */
    return v > 100 ? 100 : v;
}

static void j_set_off(const uint8_t to[NUM_LAYERS], float wx_to)   /* on the way: each layer to its place, the weather to wx_to */
{
    int env = to[L_LANDSCAPE];
    J.travelling = 1; J.t = 0;
    for (int l = 0; l < NUM_LAYERS; l++) { J.from[l] = set.cur.patch[l]; J.to[l] = to[l]; J.swapped[l] = J.from[l] == J.to[l]; J.lv_from[l] = set.cur.level[l]; }
    J.wx_from = J.wx; J.wx_to = wx_to; J.wx_own = 0;
    J.swap_at[L_WEATHER] = J_SLOT[0];
    (void)env;
    int order[3] = { L_ANIMALS, L_LANDSCAPE, L_HUMANS };
    for (int i = 0; i < 3; i++) for (int k = i + 1; k < 3; k++) {   /* by what each gains, most first */
        int a = order[i], b = order[k];
        if (J_LOUD[to[b]][b] - J_LOUD[J.from[b]][b] > J_LOUD[to[a]][a] - J_LOUD[J.from[a]][a]) { order[i] = b; order[k] = a; }
    }
    for (int i = 0; i < 3; i++) J.swap_at[order[i]] = J_SLOT[1 + i];
    {   /* how much of each place's sound each layer is (the more, the less it eases down, the more the rest fill in) */
        float pf[NUM_LAYERS], pt[NUM_LAYERS], sf = 0, st = 0;
        for (int l = 0; l < NUM_LAYERS; l++) { pf[l] = fm_exp2f(J_LOUD[J.from[l]][l] * 0.3322f); pt[l] = fm_exp2f(J_LOUD[to[l]][l] * 0.3322f); sf += pf[l]; st += pt[l]; }
        for (int l = 0; l < NUM_LAYERS; l++) { float a = pf[l] / sf, b = pt[l] / st; J.share[l] = J.swapped[l] ? 0 : a > b ? a : b; }
    }
}
static float j_weather_for(int env, float not)   /* a weather that suits the place (from its own set), not the one it has */
{
    int w, tries = 0, ws = weather_set(env);
    do w = J_WX[env][(int)(frand() * 5) % 5]; while (++tries < 6 && weather_value(ws, w) == not);
    return weather_value(ws, w);
}
static void j_go(int to)                 /* the journey: on the way to stop `to` of the route */
{
    uint8_t d[NUM_LAYERS];
    for (int l = 0; l < NUM_LAYERS; l++) d[l] = J_ROUTE[to];
    J.at = to;
    j_set_off(d, j_weather_for(J_ROUTE[to], J.wx_to));
}
/* The dream: a place at random for the land, and each of the others from it or, more often, from anywhere else (an
 * alien in a cave, a factory in a forest); the animals or the people always from somewhere else. The weather is the weather layer's
 * world's: one that suits an Earth place, any of Space's or the alien world's own */
static void j_dream(void)
{
    uint8_t d[NUM_LAYERS];
    int land, cost, tries = 0;
    do {                                 /* (drawn again while the mix would cost more than the heaviest place) */
        int mixed = 0;
        land = (int)(frand() * NUM_ENVS) % NUM_ENVS;
        if (land == set.cur.patch[L_LANDSCAPE]) land = (land + 1 + (int)(frand() * (NUM_ENVS - 1))) % NUM_ENVS;
        for (int l = 0; l < NUM_LAYERS; l++) {
            d[l] = (uint8_t)land;
            if (l != L_LANDSCAPE && frand() < 0.65f) { d[l] = (uint8_t)((int)(frand() * NUM_ENVS) % NUM_ENVS); if (d[l] != land && l != L_WEATHER) mixed = 1; }
        }
        if (!mixed) d[frand() < 0.5f ? L_ANIMALS : L_HUMANS] = (uint8_t)((land + 1 + (int)(frand() * (NUM_ENVS - 1))) % NUM_ENVS);   /* (the life or the people from elsewhere) */
        cost = 0;
        for (int l = 0; l < NUM_LAYERS; l++) cost += J_COST[d[l]][l];
    } while (cost > J_COST_MAX && ++tries < 40);
    if (cost > J_COST_MAX) for (int l = 0; l < NUM_LAYERS; l++) d[l] = (uint8_t)land;   /* (all from one place, which is within it) */
    int ws = weather_set(d[L_WEATHER]);   /* the weather layer's own: one that suits it, or any of Space's or the alien world's */
    float wx = ws != WX_SPACE && ws != WX_ALIEN ? j_weather_for(d[L_WEATHER], J.wx_to) : weather_value(ws, (int)(frand() * weather_count(ws)) % weather_count(ws));
    j_set_off(d, wx);
}
static void j_next(void) { if (J.dream) j_dream(); else j_go((J.at + 1) % J_N); }
static void j_start(int dream)
{
    J.dream = dream; J.agc = 0;
    eng.swap_out = J_SWAP_S * 0.4f; eng.swap_in = J_SWAP_S * 0.6f;
    int p = set.cur.patch[L_LANDSCAPE], at = 0;
    for (int i = 0; i < J_N; i++) if (J_ROUTE[i] == p) at = i;
    J.on = 1; J.held = 0; J.last = plat_ms();
    for (int l = 0; l < NUM_LAYERS; l++) J.mix[l] = 75;
    J.wx = J.wx_to = set.cur.mac[0] / 100.0f;
    J.cyc = J.cyc_aim = set.cur.mac[1] / 100.0f * 0.75f;
    J.mac0 = set.cur.mac[0]; J.mac1 = set.cur.mac[1];
    if (dream) j_dream(); else j_go(at);
}
static void j_stop(void) { J.on = 0; eng.cycle = -1; eng.swap_out = 0.25f; eng.swap_in = 0.4f; }
static void j_place_name(char *t) { t[0] = 0; text_cat(t, ENV_NAMES[J.travelling && !J.swapped[L_LANDSCAPE] ? J.from[L_LANDSCAPE] : J.to[L_LANDSCAPE]]); }
/* the header's second line on a journey or a dream: where it is, as fully as `room` (pixels) allows. A journey: the
 * place, "> NEXT" on the way; a dream: its land / where its life is from / where its people are from */
static void j_status(char *t, int room)
{
    char p[16], full[48] = "";
    j_place_name(p);
    for (int pass = 0; pass < 3; pass++) {
        full[0] = 0; text_cat(full, p);
        if (J.dream) {
            if (pass < 2) { text_cat(full, "/"); text_cat(full, ENV_NAMES[J.to[L_ANIMALS]]); }
            if (pass < 1) { text_cat(full, "/"); text_cat(full, ENV_NAMES[J.to[L_HUMANS]]); }
            if (J.travelling && pass == 2) text_cat(full, " ...");
        } else if (J.travelling && !J.swapped[L_LANDSCAPE] && pass < 2) { text_cat(full, " > "); text_cat(full, ENV_NAMES[J_ROUTE[J.at]]); }
        if (J.held && pass < 2) text_cat(full, " (HELD)");
        if (text_w(&FONT_S, full) <= room) break;
    }
    t[0] = 0; text_cat(t, full);
}

static void j_tick(uint32_t now)
{
    if (!J.on) return;
    float dt = (now - J.last) / 1000.0f;
    J.last = now;
    if (dt > 0.5f) dt = 0.5f;
    if (!set.playing) return;
    if (J.dream) {   /* a mix of quiet places, or a fog over it all: brought up (or a loud one down) to the journey's
                      * loudness, slowly (a few dB a minute at most), never more than 9 dB either way */
        float db = 10.0f * fm_log2f(out_pow + 1e-12f) * 0.30103f, step = (J_LOUDNESS - db) * dt / 20.0f;
        if (step > dt * 0.1f) step = dt * 0.1f; else if (step < -dt * 0.1f) step = -dt * 0.1f;
        J.agc += step; J.agc = J.agc > 9.0f ? 9.0f : J.agc < -9.0f ? -9.0f : J.agc;
    }
    /* the user's hands first: WEATHER, TIME (the knob, MIDI, REC) */
    if (set.cur.mac[0] != J.mac0) { J.wx = set.cur.mac[0] / 100.0f; J.wx_own = 1; }
    if (set.cur.mac[1] != J.mac1) J.cyc_aim = set.cur.mac[1] / 100.0f * 0.75f + (J.cyc > 0.875f ? 1.0f : 0);
    /* the day going round; a turned TIME glided to, the short way */
    J.cyc_aim += dt / J_DAY;
    J.cyc += (J.cyc_aim - J.cyc) * (dt / 8.0f > 1 ? 1 : dt / 8.0f);
    if (J.cyc >= 1.0f) { J.cyc -= 1.0f; J.cyc_aim -= 1.0f; }
    eng.cycle = J.cyc;
    if (J.travelling) {
        J.t += dt;
        if (!J.wx_own) J.wx = J.wx_from + (J.wx_to - J.wx_from) * smooth01((J.t - 10.0f) / 60.0f);
        int done = 1;
        float lvs[NUM_LAYERS], dip[NUM_LAYERS], fill = 0;
        for (int l = 0; l < NUM_LAYERS; l++) {
            float lv, to = j_level(J.to[l], l), low = J_LOW + 0.4f * J.share[l];
            dip[l] = 0;
            if (J.from[l] == J.to[l]) lv = J.lv_from[l] + (to - J.lv_from[l]) * smooth01(J.t / J_TRAVEL);   /* the same here: a glide */
            else if (J.t < J.swap_at[l]) { dip[l] = smooth01((J.t - (J.swap_at[l] - J_DOWN)) / J_DOWN); lv = J.lv_from[l] * (1.0f - (1.0f - low) * dip[l]); }
            else {
                if (!J.swapped[l]) { J.swapped[l] = 1; set.cur.patch[l] = J.to[l]; set.cur.dens[l] = 50; engine_want(&eng, l, J.to[l]); eng.dens[l] = 1.0f; }
                float u = (J.t - J.swap_at[l] - J_SWAP_S * 0.5f) / J_UP;   /* (up once the engine's change is half done) */
                dip[l] = 1.0f - smooth01(u);
                lv = to * (low + (1.0f - low) * smooth01(u));
                if (u < 1.0f) done = 0;
            }
            if (J.t < J_TRAVEL) done = 0;
            lvs[l] = lv;
            float sh = J.share[l] > 0.9f ? 0.9f : J.share[l];
            fill += dip[l] * sh / (1.0f - sh);   /* the power the rest must make up while this one is down */
        }
        for (int l = 0; l < NUM_LAYERS; l++) {
            float k = 1.0f;
            if (dip[l] < 0.01f && fill > 0) { k = fm_sqrtf(fm_sqrtf(1.0f + fill)); if (k > 1.41f) k = 1.41f; }   /* (level^4 is power; at most +6 dB) */
            float lv = lvs[l] * k; if (lv > 100) lv = 100;
            set.cur.level[l] = (uint8_t)(lv + 0.5f); eng.level[l] = lv / 100.0f;
        }
        if (done) { J.travelling = 0; J.stay = J_STAY; }
    } else {
        for (int l = 0; l < NUM_LAYERS; l++) { float lv = j_level(J.to[l], l); set.cur.level[l] = (uint8_t)(lv + 0.5f); eng.level[l] = lv / 100.0f; }
        if (!J.held && (J.stay -= dt) <= 0) j_next();
    }
    set.cur.mac[0] = J.mac0 = (uint8_t)(J.wx * 100.0f + 0.5f); eng.user.weather = J.wx;
    float tm = J.cyc <= 0.75f ? J.cyc / 0.75f : J.cyc < 0.875f ? 1.0f : 0.0f;
    set.cur.mac[1] = J.mac1 = (uint8_t)(tm * 100.0f + 0.5f);
}
static void j_travel_on(void)
{
    if (J.travelling) { pop("ON THE WAY", "ALREADY", C_TXT); return; }
    J.held = 0;
    j_next();
    pop(J.dream ? "DRIFTING ON" : "TRAVELLING ON", ENV_NAMES[J.to[L_LANDSCAPE]], C_TXT);
}
static void j_hold(void)
{
    J.held = !J.held;
    if (J.held) pop(J.dream ? "DREAM HELD" : "JOURNEY HELD", J.travelling ? "ON ARRIVAL" : "STAYING HERE", C_TXT);
    else { if (!J.travelling && J.stay < 20.0f) J.stay = 20.0f; pop(J.dream ? "DREAM" : "JOURNEY", "MOVING ON AGAIN", C_TXT); }
}

static void env_load(int algo)
{
    if (algo == ALGO_JOURNEY || algo == ALGO_DREAM) {   /* the journey, the dream: on from where the land is now */
        set.algo = (uint8_t)algo;
        j_start(algo == ALGO_DREAM);
        changed();
        set_dirty = 0;
        return;
    }
    if (J.on) j_stop();
    set.algo = (uint8_t)algo;
    if (algo < NUM_ENVS) {               /* a built-in: its presets, its own volumes, densities as designed */
        for (int l = 0; l < NUM_LAYERS; l++) { set.cur.patch[l] = (uint8_t)algo; set.cur.level[l] = ENV_LEVELS[algo][l]; set.cur.dens[l] = 50; }
        static const int8_t START_WX[NUM_ENVS] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, -1, 3, 1, 3, -1 };
        if (START_WX[algo] >= 0)          /* some places start in their own weather: the meadow CALM, the Arctic LIGHT SNOW,
                                           * the desert HEAT HAZE, the savanna a BREEZE */
            set.cur.mac[0] = (uint8_t)(weather_value(weather_set(algo), START_WX[algo]) * 100.0f + 0.5f);
    } else set.cur = set.user[algo - NUM_ENVS];   /* the user's: everything as it was saved */
    changed();
    set_dirty = 0;
}

static void env_step(int d)
{
    int n = env_count();
    int pos = (env_pos(set.algo) + d % n + n) % n;
    env_load(env_at(pos));
    show_envs();
}

static void patch_step(int d)
{
    int l = ui.sel, pos = pos_of(patch_order[l], NUM_ENVS, set.cur.patch[l]);
    pos = (pos + d % NUM_ENVS + NUM_ENVS) % NUM_ENVS;
    set.cur.patch[l] = patch_order[l][pos];
    changed();
    show_list(l);
}

static void level_step(int l, int d)
{
    if (J.on) {                          /* the journey: its mix of the layers, wherever it goes */
        J.mix[l] = J.mix[l] + 2 * d < 0 ? 0 : J.mix[l] + 2 * d > 100 ? 100 : J.mix[l] + 2 * d;
        show_panel(PANEL_LEVELS);
        if (ann_on()) { char n[8]; int_text((int32_t)J.mix[l], n); say_text(LAYER_NAME[l], n); }
        return;
    }
    set.cur.level[l] = (uint8_t)clamp(set.cur.level[l] + 2 * d, 0, 100);
    changed();
    show_panel(PANEL_LEVELS);
    if (ann_on()) { char n[8]; int_text(set.cur.level[l], n); say_text(LAYER_NAME[l], n); }
}

static void dens_step(int l, int d)
{
    set.cur.dens[l] = (uint8_t)clamp(set.cur.dens[l] + 2 * d, 0, 100);
    changed();
    show_panel(PANEL_DENS);
    if (ann_on()) { char n[16] = "density "; char v[8]; int_text(set.cur.dens[l], v); text_cat(n, v); say_text(LAYER_NAME[l], n); }
}

static void macro_text(int m, char *s)
{
    s[0] = 0;
    if (m == 0) text_cat(s, weather_name(wx_set(), set.cur.mac[0] / 100.0f));
    else if (m == 1) text_cat(s, time_name(set.cur.mac[1] / 100.0f));
    else if (m == 3 && set.cur.mac[3] < 3) text_cat(s, "FIXED");
    else int_text(set.cur.mac[m], s);
}

static void weather_set_pos(int pos)
{
    pos = clamp(pos, 0, wx_n() - 1);
    set.cur.mac[0] = (uint8_t)(weather_value(wx_set(), wx_order[wx_set()][pos]) * 100.0f + 0.5f);
}

static void macro_step(int m, int d)
{
    if (m == 0) weather_set_pos(wx_pos() + d);               /* WEATHER: a type a detent, A to Z */
    else set.cur.mac[m] = (uint8_t)clamp(set.cur.mac[m] + 2 * d, 0, 100);
    changed();
    show_panel(PANEL_MACROS);
    if (ann_on()) { char s[20]; macro_text(m, s); say_text(MACRO_NAME[m], s); }
}

static void weather_type_step(int d)     /* OCT- / OCT+: the previous or next weather, A to Z */
{
    char s[20];
    weather_set_pos(wx_pos() + d);
    changed();
    macro_text(0, s);
    pop("WEATHER", s, C_W);
}

static void randomise(void)
{
    engine_randomise(&eng);
    set.cur.mac[0] = (uint8_t)(eng.user.weather * 100.0f + 0.5f);
    set.cur.mac[1] = (uint8_t)(eng.user.time * 100.0f + 0.5f);
    set.cur.mac[2] = (uint8_t)(eng.user.activity * 100.0f + 0.5f);
    set.cur.mac[3] = (uint8_t)(eng.user.entropy * 100.0f + 0.5f);
    set_dirty = 1;
    autosave_at = plat_ms() + AUTOSAVE_MS;
    char s[30] = "";
    text_cat(s, weather_name(eng.wset, eng.user.weather)); text_cat(s, " "); text_cat(s, time_name(eng.user.time));
    pop("RANDOMISED", s, RGB(255, 90, 78));
}

static void save_env(void)
{
    int slot;
    if (set.algo >= NUM_ENVS) slot = set.algo - NUM_ENVS;
    else if (set.nusers < NUSER) slot = set.nusers++;
    else slot = NUSER - 1;
    set.user[slot] = set.cur;
    char s[16];
    env_label((int)(NUM_ENVS + slot), s);
    if (!J.on) set.algo = (uint8_t)(NUM_ENVS + slot);   /* (a journey goes on; what was here is kept as a user's) */
    set_dirty = 0;
    autosave_at = 0;
    store_ok = plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0;
    if (store_ok) pop("SAVED AS", s, C_TXT);
    else pop("NOT SAVED", "UNTIL POWER OFF", RGB(255, 90, 78));   /* (a flash part the storage does not know) */
}

static void play_toggle(void)
{
    set.playing = (uint8_t)!set.playing;
    eng.playing = set.playing;
    pop(set.playing ? "PLAY" : "STOP", "", C_TXT);
}

/* HOME tapped: how the FM-1 is doing. The processor's load, the cores, and whether settings are being kept */
static void info(void)
{
    char s[28] = "CPU ", n[8];
    int_text((int32_t)plat_cpu_pct(), n); text_cat(s, n); text_cat(s, "%");
    pop("THE OUTSIDE INSIDE", s, store_ok ? C_TXT : RGB(255, 90, 78));
    ui.pop_3[0] = 0;
    text_cat(ui.pop_3, two_cores && plat_cpu2_on() ? "2 CORES" : "1 CORE");
    text_cat(ui.pop_3, store_ok ? "  SAVING OK" : "  NO SAVING");
    /* what each part costs, in % of the time an app block lasts: WEATHER ANIMALS LANDSCAPE HUMANS, then the mix */
    static const char *const K[NUM_LAYERS + 1] = { "W", "A", "L", "H", "M" };
    float budget = (float)APP_BLOCK * 1000000.0f / 44100.0f * PLAT_TICKS_PER_US;
    ui.pop_4[0] = 0;
    for (int k = 0; k <= NUM_LAYERS; k++) {
        char v[8];
        text_cat(ui.pop_4, K[k]); int_text((int32_t)(eng.cost[k] * 100.0f / budget + 0.5f), v); text_cat(ui.pop_4, v);
        if (k < NUM_LAYERS) text_cat(ui.pop_4, " ");
    }
    if (eng.gov < 0.99f) { char v[8]; text_cat(ui.pop_4, " G"); int_text((int32_t)(eng.gov * 100.0f), v); text_cat(ui.pop_4, v); }
    ui.pop_until = plat_ms() + 4000u;
}

/* ------------------------------------------------------------------ input */
static int32_t enc(int e) { int32_t d = plat_enc(e); if (d) ui.touched = 1; return d; }

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys();
    uint32_t down = btn & ~ui.btn, up = ui.btn & ~btn;
    if (down || (keys & ~ui.keys)) ui.touched = 1;
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    int32_t d;

    if (down >> B_SEL & 1u) { ui.sel_used = 0; show_panel(PANEL_DENS); }
    if ((up >> B_SEL & 1u) && !ui.sel_used) { ui.panel_until = 0; select_layer(ui.sel + 1); }
    if (down >> B_HOME & 1u) { ui.home_used = 0; show_panel(PANEL_MACROS); }
    if ((up >> B_HOME & 1u) && !ui.home_used) { ui.panel_until = 0; info(); }
    if (down >> B_REC & 1u) randomise();
    if (down >> B_SAVE & 1u) save_env();
    if (down >> B_PLAY & 1u) play_toggle();
    if (down >> B_OCTDN & 1u) { if (J.on) j_hold(); else weather_type_step(-1); }
    if (down >> B_OCTUP & 1u) { if (J.on) j_travel_on(); else weather_type_step(1); }
    for (int l = 0; l < NUM_LAYERS; l++)          /* the first four white keys: one event from each layer */
        if ((keys & ~ui.keys) >> WK[l] & 1u) eng.oneshot[l] = 1;

    for (int k = 0; k < 4; k++) {
        if ((d = enc(EN_K1 + k)) == 0) continue;
        if (btn >> B_HOME & 1u) { macro_step(k, (int)d); ui.home_used = 1; }
        else if (btn >> B_SEL & 1u) { dens_step(k, (int)d); ui.sel_used = 1; }
        else level_step(k, (int)d);
    }
    if ((d = enc(EN_ALGO)) != 0) env_step((int)d);
    if ((d = enc(EN_PRESET)) != 0) patch_step((int)d);
    if ((d = enc(EN_SELECT)) != 0) select_layer(ui.sel + (int)d);
    if (btn & (1u << B_SEL | 1u << B_HOME)) ui.panel_until = plat_ms() + 1500u;   /* held: the panel stays */

    ui.btn = btn;
    ui.keys = keys;
    /* the lights: only what does something glows (bright while pressed); the rest stay dark. PLAY is green, lit while
     * playing; SAVE lit while there is something unsaved */
    {
        const uint32_t used = 1u << B_HOME | 1u << B_SEL | 1u << B_REC | 1u << B_SAVE | 1u << B_OCTDN | 1u << B_OCTUP;
        uint32_t wkeys = 0;
        for (int l = 0; l < NUM_LAYERS; l++) wkeys |= 1u << WK[l];
        uint32_t lit = (btn & used) | (set_dirty ? 1u << B_SAVE : 0u) | (set.playing ? LED_PLAY_GREEN : 0u);
        plat_lights(lit, keys & wkeys, used | LED_PLAY_GREEN, wkeys, 2);
    }
}

void app_input_only(void) { input(); }

/* ------------------------------------------------------------------ the scene: sprites */
enum { SK_BIRD, SK_DOT, SK_RING, SK_DRIP, SK_STREAK, SK_FLAKE, SK_MOVER, SK_DIAMOND, SK_BAR, SK_SQUARE, SK_SPARK, SK_GUST, SK_SHEEP };
typedef struct { uint8_t kind, layer, on, blink; float x, y, vx, vy, g, age, life, a, b, ty; } spr_t;
#define NSPR 140
static spr_t spr[NSPR] PLAT_POOL;        /* (the pool is not cleared at boot: app_ui_init clears it) */

#define HEAD_H 46                        /* the header: environment, time, state, weather */
#define VIEW_Y HEAD_H
#define VIEW_H (240 - HEAD_H)          /* the scene: the rest of the screen */
#define SCENE_H 120                      /* the scene's own units (sprites, backdrops), stretched to VIEW_H */

static spr_t *spawn(int kind, int layer, float x, float y, float life)
{
    for (int i = 0; i < NSPR; i++) {
        if (spr[i].on) continue;
        spr_t *s = &spr[i];
        *s = (spr_t){ 0 };
        s->on = 1; s->kind = (uint8_t)kind; s->layer = (uint8_t)layer; s->x = x; s->y = y; s->life = life;
        return s;
    }
    return 0;
}

static float frand(void) { static uint32_t r = 0x1234567u; r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r >> 8) * (1.0f / 16777216.0f); }

static int place(void) { return eng.patch[L_LANDSCAPE] < 0 ? 4 : eng.patch[L_LANDSCAPE]; }
static float ground_y(float d) { return 114.0f - d * 46.0f; }       /* near the bottom, far up towards the horizon */
static float sky_y(float d) { return 8.0f + (1.0f - d) * 34.0f + frand() * 8.0f; }

/* an engine event becomes sprites */
static void event_sprites(const UiEvent *u)
{
    float x = 6.0f + (u->pan + 100) / 200.0f * 228.0f;   /* stereo pan -100..100 across the screen */
    float d = u->dist / 100.0f;
    spr_t *s;
    int l = u->layer, p = place();
    switch (u->glyph) {
    case GL_BIRD:
        if ((s = spawn(SK_BIRD, l, x, sky_y(d), 1.6f))) { s->a = 1.4f - d * 0.7f; s->b = 9.0f; }
        for (int i = 0; i < 2; i++) if ((s = spawn(SK_DOT, l, x + 4, sky_y(d) - 2, 0.5f))) { s->vy = -12; s->vx = 3; s->age = -0.25f * i; s->a = 1; }
        break;
    case GL_BIGBIRD:
        if ((s = spawn(SK_BIRD, l, x, sky_y(d), 3.0f))) { s->a = 2.0f - d; s->b = 3.0f; s->vx = (frand() - 0.5f) * 16; }
        break;
    case GL_BAT:
        for (int i = 0; i < 4; i++) if ((s = spawn(SK_BIRD, l, x + (frand() - 0.5f) * 10, 20 + frand() * 30, 1.0f))) { s->a = 0.7f; s->b = 14; s->vx = (frand() - 0.5f) * 90; s->vy = (frand() - 0.5f) * 40; s->age = -0.06f * i; }
        break;
    case GL_DOT: case GL_SQUARE:
        if ((s = spawn(u->glyph == GL_DOT ? SK_DOT : SK_SQUARE, l, x, ground_y(d) - 4 - frand() * 6, 0.15f))) s->a = 1;
        break;
    case GL_BIGDOT:
        if ((s = spawn(SK_DOT, l, x, ground_y(d) - 2, 0.22f))) s->a = 2;
        break;
    case GL_RING:
        if ((s = spawn(SK_RING, l, x, ground_y(d), 0.8f))) { s->a = 1.5f; s->b = 11.0f * (1.2f - d); s->g = 0.4f; }
        break;
    case GL_DRIP: {
        float top = p == 3 ? 6.0f + frand() * 10 : p == 1 ? 16.0f : 30.0f + frand() * 20;
        if ((s = spawn(SK_DRIP, l, x, top, 3.0f))) { s->ty = ground_y(d); s->g = 260.0f; }
        break;
    }
    case GL_FLASH: ui.flash = 1.0f; ui.flash_c = eng.patch[L_WEATHER] == 7 ? RGB(150, 120, 230) : RGB(150, 170, 210); break;
    case GL_CHIME: {
        int best = 0; float bd = 999;
        for (int i = 0; i < 7; i++) { float dd = fm_fabsf(SPIRES[i][0] - x); if (dd < bd) { bd = dd; best = i; } }
        if (p == 7) x = SPIRES[best][0];
        float y = p == 7 ? SPIRES[best][1] : ground_y(d) - 20;
        if ((s = spawn(SK_DIAMOND, l, x, y, 0.5f))) s->a = 3;
        if ((s = spawn(SK_RING, l, x, y, 0.8f))) { s->a = 2; s->b = 12; s->g = 1.0f; }
        break;
    }
    case GL_STEAM:
        for (int i = 0; i < 4; i++) {
            float cx = p == 8 ? (i & 1 ? 123.0f : 104.0f) : x, cy = p == 8 ? (i & 1 ? 50.0f : 42.0f) : ground_y(d);
            if ((s = spawn(SK_RING, l, cx, cy, 1.8f))) { s->a = 2; s->b = 12; s->g = 0.8f; s->vy = -14; s->vx = 3 + eng.gust * 10; s->age = -0.25f * i; }
        }
        break;
    case GL_BAR:
        if ((s = spawn(SK_BAR, l, x, ground_y(d) - 10 - frand() * 20, 0.35f))) s->a = 10 + frand() * 18;
        break;
    case GL_CAR: case GL_PLANE: case GL_TRAIN: case GL_CRAFT: case GL_BOAT: {
        float dur = u->dur_ds / 10.0f, dir = u->dir ? u->dir : (frand() < 0.5f ? -1.0f : 1.0f);
        float y, w, h; int blink = 0;
        if (u->glyph == GL_PLANE) { y = 4 + frand() * 10; w = 5; h = 2; blink = 1; }
        else if (u->glyph == GL_CRAFT) { y = 14 + frand() * 16; w = 13; h = 2; blink = 1; }
        else if (u->glyph == GL_TRAIN) { y = p == 0 ? 89 : p == 8 ? 94 : ground_y(d) - 6; w = 64; h = 4; }
        else if (u->glyph == GL_BOAT) { y = p == 6 ? 46 : p == 5 ? 78 : ground_y(d); w = 9; h = 3; }
        else { y = p == 0 ? 106 + frand() * 8 : ground_y(d); w = 12; h = 4; }
        if (dur <= 0.05f) {                       /* a horn: a long line on the horizon */
            if ((s = spawn(SK_BAR, l, 120, 47, 2.5f))) s->a = 100;
            break;
        }
        if ((s = spawn(SK_MOVER, l, dir > 0 ? -w : 240 + w, y, dur))) { s->vx = dir * (240.0f + 2 * w) / dur; s->a = w; s->b = h; s->blink = (uint8_t)blink; }
        break;
    }
    case GL_BEE: {                               /* a speck that flies across, wavering */
        float dur = u->dur_ds / 10.0f, dir = u->dir ? u->dir : 1.0f;
        if (dur < 0.5f) dur = 3.0f;
        if ((s = spawn(SK_MOVER, l, dir > 0 ? -4 : 244, ground_y(d) - 10 - frand() * 14, dur))) { s->vx = dir * 248.0f / dur; s->a = 2; s->b = 1; s->ty = 3; }
        break;
    }
    case GL_SHEEP:                               /* a sheep pops up in the field as it bleats, then fades */
        if ((s = spawn(SK_SHEEP, l, x < 12 ? 12 : x > 228 ? 228 : x, 86 + (1.0f - d) * 40 + frand() * 6, 2.4f))) s->b = frand() < 0.5f ? -1.0f : 1.0f;
        break;
    case GL_ROCK:
        for (int i = 0; i < 6; i++) if ((s = spawn(SK_DRIP, l, x + (frand() - 0.5f) * 14, 10 + frand() * 6, 3.0f))) { s->ty = ground_y(d) + (frand() - 0.5f) * 6; s->g = 200 + frand() * 120; s->age = -0.05f * i; s->a = 2; }
        break;
    case GL_SPRAY:
        for (int i = 0; i < 12; i++) if ((s = spawn(SK_DOT, l, x + (frand() - 0.5f) * 40, 84, 0.9f))) { s->vx = (frand() - 0.5f) * 40; s->vy = -40 - frand() * 40; s->g = 120; s->a = 1; }
        break;
    case GL_BEACON:
        if ((s = spawn(SK_RING, l, p == 7 ? 30 : x, p == 7 ? 94 : ground_y(d) - 6, 1.2f))) { s->a = 2; s->b = 24; s->g = 0.4f; }
        break;
    case GL_PRESS:
        if ((s = spawn(SK_SQUARE, l, 180, 72, 0.15f))) s->a = 4;
        break;
    case GL_GROWL: case GL_FLAT:
        if ((s = spawn(SK_RING, l, x, u->glyph == GL_FLAT ? 49 : ground_y(d) - 2, 1.4f))) { s->a = 4; s->b = 50; s->g = 0.2f; }
        break;
    case GL_SIGNAL:
        for (int i = 0; i < 3; i++) if ((s = spawn(SK_RING, l, x, 10 + d * 40, 2.0f))) { s->a = 2; s->b = 40; s->g = 1.0f; s->age = -0.5f * i; }
        break;
    case GL_GEYSER:
        for (int i = 0; i < 10; i++) if ((s = spawn(SK_DOT, l, x + (frand() - 0.5f) * 3, 98, 2.0f))) { s->vx = (frand() - 0.5f) * 12; s->vy = -50 - frand() * 40; s->g = 40; s->a = 1; s->age = -0.06f * i; }
        break;
    default: break;
    }
}

/* the weather's continuous sprites: rain, snow, static, shards and gusts */
static void weather_sprites(float dt)
{
    int w = eng.patch[L_WEATHER];
    int inside = place() == 3;                   /* a cave: no rain in view */
    float rain = eng.rain, snow = eng.snow, wind = eng.wind, gust = eng.gust;
    spr_t *s;
    if (w == 2) {                                /* space: static sparks */
        int n = (int)((rain * 20 + wind * 6 + 1) * dt * 30 * frand());
        for (int i = 0; i < n; i++) spawn(SK_SPARK, 0, frand() * 240, frand() * 90, 0.05f);
        return;
    }
    if (!inside) {
        float rate = (w == 7 ? rain * 70 : rain * 260) * (0.3f + eng.level[0]);
        int n = (int)(rate * dt + frand());
        for (int i = 0; i < n; i++)
            if ((s = spawn(SK_STREAK, 0, frand() * 260 - 10, -6, 0.7f))) { s->vx = w == 7 ? -40 : 20 + gust * 80; s->vy = w == 7 ? 110 : 260; s->a = w == 7 ? 8 : 5; }
        n = (int)(snow * 40 * dt + frand());
        for (int i = 0; i < n; i++)
            if ((s = spawn(SK_FLAKE, 0, frand() * 250 - 5, -3, 6.0f))) { s->vx = 4 + gust * 20; s->vy = 14 + frand() * 10; s->b = frand() * 6; }
    }
    if (frand() < wind * 2.0f * dt)
        if ((s = spawn(SK_GUST, 0, -30, 6 + frand() * 90, 1.5f))) { s->vx = 160 + frand() * 80; s->a = 10 + frand() * 18; }
}

static void sprites_step(float dt)
{
    uint32_t head = eng.uiq_head;
    if (head - ui.evq_tail > UI_EVQ) ui.evq_tail = head - UI_EVQ;   /* fell behind: the newest */
    while (ui.evq_tail != head) { UiEvent u = eng.uiq[ui.evq_tail % UI_EVQ]; ui.evq_tail++; event_sprites(&u); }
    weather_sprites(dt);
    for (int i = 0; i < NSPR; i++) {
        spr_t *s = &spr[i];
        if (!s->on) continue;
        s->age += dt;
        if (s->age < 0) continue;
        s->vy += s->g * dt * (s->kind == SK_DRIP || s->kind == SK_DOT ? 1.0f : 0.0f);
        s->x += s->vx * dt; s->y += s->vy * dt;
        if (s->kind == SK_FLAKE) s->x += fm_sinf((s->age + s->b) * 2.0f) * 0.3f;
        if (s->kind == SK_DRIP && s->y >= s->ty) {        /* lands: a ripple */
            spr_t *r;
            s->on = 0;
            if ((r = spawn(SK_RING, s->layer, s->x, s->ty, 0.7f))) { r->a = 1.5f; r->b = s->a > 1 ? 6 : 10; r->g = 0.4f; }
            continue;
        }
        if (s->age >= s->life || s->y > SCENE_H + 10 || s->x < -80 || s->x > 320) s->on = 0;
    }
}

/* ------------------------------------------------------------------ drawing */
static uint16_t dim(uint16_t c, float k)       /* a colour scaled towards black */
{
    if (k >= 1) return c;
    if (k <= 0) return C_BLACK;
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return (uint16_t)(((uint32_t)(r * k) << 11) | ((uint32_t)(g * k) << 5) | (uint32_t)(b * k));
}
static uint16_t mix565(uint16_t a, uint16_t b, float k)
{
    uint32_t ar = (a >> 11) & 31u, ag = (a >> 5) & 63u, ab = a & 31u, br = (b >> 11) & 31u, bg = (b >> 5) & 63u, bb = b & 31u;
    return (uint16_t)(((uint32_t)(ar + (br - (float)ar) * k) << 11) | ((uint32_t)(ag + (bg - (float)ag) * k) << 5) | (uint32_t)(ab + (bb - (float)ab) * k));
}

/* The scene is drawn in its own units, 240 x 120, stretched to the view's height; text and panels in view pixels.
 * The view is taller than one canvas, so it is drawn twice, a half at a time (view_half). */
#define VK ((float)VIEW_H / SCENE_H)
static inline int32_t SY(float y) { return (int32_t)(y * VK); }
static void s_line(float x0, float y0, float x1, float y1, uint16_t c) { cv_line((int32_t)x0, SY(y0), (int32_t)x1, SY(y1), c); }
static void s_pset(float x, float y, uint16_t c) { cv_pset((int32_t)x, SY(y), c); }
static void s_rect(float x, float y, float w, float h, uint16_t c)
{
    int32_t y0 = SY(y), y1 = SY(y + h);
    cv_rect((int32_t)x, y0, (int32_t)w, y1 > y0 ? y1 - y0 : 1, c);
}
static void view_half(int top, int h, uint16_t bg)
{
    cv_begin(240, (uint32_t)h, bg);
    cv_oy = -top; cv_y0 = 0; cv_y1 = h;  /* view rows top .. top+h-1 into this canvas */
}

static void draw_ring(float cx, float cy, float rx, float ry, uint16_t c)
{
    float px = cx + rx, py = cy;
    for (int i = 1; i <= 16; i++) {
        float a = i * (FM_TWO_PI / 16.0f), x = cx + rx * fm_cosf(a), y = cy + ry * fm_sinf(a);
        s_line(px, py, x, y, c);
        px = x; py = y;
    }
}

static void draw_backdrop(int p)
{
    const uint8_t *b = BACKDROPS[p];
    int pen = 0, px = 0, py = 0;
    for (; *b != 254; b += 2) {
        if (b[0] == 255) { pen = 0; continue; }
        if (pen) s_line(px, py, b[0], b[1], C_BACK);
        else if (p == 2) s_pset(b[0], b[1], C_BACK);
        px = b[0]; py = b[1]; pen = 1;
    }
    if (p == 2) for (int i = 0; i < 90; i++) s_pset((i * 97) % 240, (i * 53) % 88, C_BACK);   /* stars */
}

static void draw_landscape_moving(int p)
{
    uint16_t lc = dim(C_L, 0.35f + 0.65f * eng.level[L_LANDSCAPE]);
    if (p == 6 || p == 5) {                      /* waves: the swell, or the marsh's lapping */
        float base = p == 6 ? 86.0f : 100.0f, amp = p == 6 ? 12.0f : 1.5f, py = 0;
        for (int x = 0; x <= 240; x += 6) {
            float y = base - eng.swell_ui * amp + fm_sinf(x * 0.06f + ui.t * 2.0f) * (p == 6 ? 2.0f : 1.0f);
            if (x) s_line(x - 6, py, x, y, lc);
            py = y;
        }
        if (p == 6) { py = 0; for (int x = 0; x <= 240; x += 8) { float y = 56 + fm_sinf(x * 0.09f + ui.t); if (x) s_line(x - 8, py, x, y, dim(lc, 0.5f)); py = y; } }
    }
    if (p == 4 || p == 1 || p == 5 || p == 9 || p == 13) {   /* grass, ferns, reeds, the meadow, the savanna's tall grass: they lean with the gust */
        for (int x = 2; x < 240; x += (p == 5 ? 5 : 4)) {
            if (p == 5 && x > 55 && x < 185) continue;
            int h = (p == 5 ? 12 : p == 9 || p == 13 ? 7 : 4) + (x * 7) % (p == 5 ? 16 : p == 9 || p == 13 ? 9 : 6);
            float lean = eng.gust * 6.0f + fm_sinf(ui.t * 2.0f + x * 0.1f) * (0.4f + eng.gust);
            s_line(x, 119.5f, x + lean, 119.5f - h, dim(lc, 0.7f));
        }
    }
}

static void draw_band(int hp)
{
    float y = -1;
    if (hp == 2) y = 92; else if (hp == 3) y = 115; else if (hp == 8) y = 88;
    if (y < 0) return;
    float k = (0.2f + 0.5f * eng.meter[L_HUMANS]) * (0.6f + 0.4f * fm_sinf(ui.t * 1.5f));
    s_rect(0, y, 240, hp == 3 ? 4 : 2, dim(C_H, k));
}

static void draw_scene(void)
{
    int p = place();
    draw_backdrop(p);
    draw_band(eng.patch[L_HUMANS]);
    draw_landscape_moving(p);
    for (int i = 0; i < NSPR; i++) {
        spr_t *s = &spr[i];
        if (!s->on || s->age < 0) continue;
        float lv = 0.35f + 0.65f * eng.level[s->layer];
        float q = s->age / s->life;
        uint16_t c = dim(LAYER_C[s->layer], lv);
        float x = s->x, y = s->y;
        switch (s->kind) {
        case SK_BIRD: {
            float flap = fm_sinf(s->age * s->b) * 3.0f * s->a, w = 4.0f * s->a;
            s_line(x - w, y - 1 - flap, x, y, c);
            s_line(x, y, x + w, y - 1 - flap, c);
            break;
        }
        case SK_DOT: s_rect(x, y, s->a + 1, s->a, c); break;
        case SK_SHEEP: {                         /* fleece, legs, a dark face; the baa from its mouth */
            float k = q > 0.6f ? 1.0f - (q - 0.6f) / 0.4f : 1.0f, f = s->b;
            uint16_t wool = dim(C_WHITE, 0.85f * k), face = dim(c, k);
            if (s->age < 0.12f) y -= 1.5f * (1.0f - s->age / 0.12f);   /* (pops up) */
            s_rect(x - 4, y - 3, 9, 4, wool);
            s_rect(x - 3, y - 4, 7, 1, wool);
            for (int g = 0; g < 4; g++) s_rect(x - 3 + g * 2, y + 1, 1, 2, face);
            float hx = f > 0 ? x + 5 : x - 7;
            s_rect(hx, y - 4, 2, 3, face);
            s_pset(f > 0 ? hx + 2 : hx - 1, y - 4, face);   /* an ear */
            if (s->age < 0.7f) {                 /* the bleat: two short strokes, spreading */
                float u = s->age / 0.7f, mx = f > 0 ? hx + 3 : hx - 2, o = 2 + 4 * u;
                uint16_t b = dim(c, k * (1.0f - u));
                s_line(mx + f * o, y - 4, mx + f * (o + 2), y - 5.5f, b);
                s_line(mx + f * o, y - 2, mx + f * (o + 2), y - 1, b);
            }
            break;
        }
        case SK_SQUARE: s_rect(x - 1, y - 1, s->a > 0 ? s->a : 2, s->a > 0 ? s->a : 2, c); break;
        case SK_RING: { float r = s->a + (s->b - s->a) * q; draw_ring(x, y, r, r * (s->g > 0 ? s->g : 0.4f), dim(c, 1.0f - q)); break; }
        case SK_DRIP: s_rect(x, y, s->a > 1 ? 2 : 1, 2, c); break;
        case SK_STREAK: s_line(x, y, s->x - s->vx * 0.02f, s->y - s->a, dim(c, 0.8f)); break;
        case SK_FLAKE: s_rect(x, y, 2, 1, RGB(210, 225, 255)); break;
        case SK_SPARK: s_pset(x, y, c); break;
        case SK_GUST: s_line(x, y, s->x + s->a, y, dim(c, 0.4f)); break;
        case SK_MOVER: {
            int on = !s->blink || ((int)(s->age * 4) & 1);
            if (s->ty > 0) y += fm_sinf(s->age * 9.0f) * s->ty;   /* (a bee's wavering flight) */
            s_rect(s->x - s->a / 2, y, s->a, s->b, c);
            if (s->blink && on) s_rect(s->x, y - 1.5f, 2, 1, C_WHITE);
            break;
        }
        case SK_DIAMOND: { float r = 3; s_line(x - r, y, x, y - r, c); s_line(x, y - r, x + r, y, c); s_line(x + r, y, x, y + r, c); s_line(x, y + r, x - r, y, c); break; }
        case SK_BAR: s_line(x - s->a / 2, y, x + s->a / 2, y, dim(c, 1.0f - q)); break;
        default: break;
        }
    }
}

static void frame_box(int x, int y, int w, int h, uint16_t c)
{
    cv_rect(x, y, w, h, C_BLACK);
    cv_rect(x, y, w, 2, c); cv_rect(x, y + h - 2, w, 2, c); cv_rect(x, y, 2, h, c); cv_rect(x + w - 2, y, 2, h, c);
}

/* a list: title, then up to 7 rows around the current one (the preset list, the environment list) */
#define LIST_ROWS 7
static const char *list_env(int pos, int arg)
{
    static char t[16];
    (void)arg;
    env_label(env_at(pos), t);
    return t;
}
static void draw_list(const char *title, const char *sub, uint16_t c, int n, int cur, const char *(*name)(int, int), int arg)
{
    int first = clamp(cur - LIST_ROWS / 2, 0, n > LIST_ROWS ? n - LIST_ROWS : 0);
    frame_box(6, 4, 228, 34 + LIST_ROWS * 21, c);
    int x = cv_text(14, 10, &FONT_S, title, c);
    if (sub) cv_text(x + 8, 10, &FONT_S, sub, C_SUB);
    for (int i = 0; i < LIST_ROWS && first + i < n; i++) {
        int y = 32 + i * 21;
        const char *nm = name(first + i, arg);
        uint16_t rc = c, tc = C_SUB;     /* JOURNEY and DREAM in their own colours */
        if (name == list_env) { int a = env_at(first + i); if (a == ALGO_JOURNEY) rc = tc = C_JOURNEY; else if (a == ALGO_DREAM) rc = tc = C_DREAM; }
        if (first + i == cur) { cv_rect(10, y - 2, 220, 21, rc); cv_text(16, y, &FONT_S, nm, C_BLACK); }
        else cv_text(16, y, &FONT_S, nm, tc);
    }
}
static const char *list_patch(int pos, int l) { return PATCHES[l][patch_order[l][pos]].name; }

/* the knobs' panel: four columns, one a knob, left to right as the knobs are */
#define PANEL_Y (VIEW_H - 104)
static void draw_panel(void)
{
    const char *lab[4]; uint16_t col[4]; int val[4]; char vtxt[4][12]; const char *title = 0; char big[24] = "";
    for (int k = 0; k < 4; k++) {
        if (ui.panel == PANEL_MACROS) {
            lab[k] = MACRO_NAME[k]; col[k] = C_TXT;
            val[k] = k == 0 ? wx_pos() * 100 / (wx_n() - 1) : set.cur.mac[k];
            if (k == 0) vtxt[k][0] = 0; else macro_text(k, vtxt[k]);
        } else {
            lab[k] = LAYER_NAME[k]; col[k] = LAYER_C[k];
            val[k] = ui.panel == PANEL_DENS ? set.cur.dens[k] : J.on ? (int)J.mix[k] : set.cur.level[k];
            int_text(val[k], vtxt[k]);
        }
    }
    if (ui.panel == PANEL_MACROS) { title = "WEATHER"; macro_text(0, big); }
    else if (ui.panel == PANEL_DENS) { title = "DENSITY"; }
    /* labels: centred on their columns; where whole words would touch (LANDSCAPE is wider than its column), on two
     * lines, every other one lower, so each has twice the room */
    int lx[4], lw[4], ly[4], stagger = 0;
    for (int k = 0; k < 4; k++) { lw[k] = text_w(&FONT_S, lab[k]); lx[k] = 30 + 60 * k - lw[k] / 2; }
    for (int k = 0; k < 3; k++) if (lx[k] + lw[k] + 3 > lx[k + 1]) stagger = 1;
    if (lx[0] < 1 || lx[3] + lw[3] > 239) stagger = 1;
    for (int k = 0; k < 4; k++) {
        if (lx[k] < 1) lx[k] = 1;
        if (lx[k] + lw[k] > 239) lx[k] = 239 - lw[k];
        ly[k] = PANEL_Y + 3 + (stagger && (k & 1) ? 15 : 0);
    }

    if (title) {                         /* what the knobs are doing now, large, above the panel */
        int w = text_w(&FONT_M, big[0] ? big : title) + 24;
        frame_box(120 - w / 2, PANEL_Y - 40, w, 36, C_TXT);
        cv_text(120 - text_w(&FONT_M, big[0] ? big : title) / 2, PANEL_Y - 34, &FONT_M, big[0] ? big : title, C_TXT);
    }
    cv_rect(0, PANEL_Y, 240, VIEW_H - PANEL_Y, C_BLACK);
    cv_rect(0, PANEL_Y, 240, 1, C_RULE);
    for (int k = 0; k < 4; k++) {
        int cx = 30 + 60 * k, top = PANEL_Y + 36, h = 42;
        if (ui.panel != PANEL_MACROS && k == ui.sel) cv_rect(60 * k + 1, PANEL_Y + 1, 58, VIEW_H - PANEL_Y - 1, RGB(22, 22, 22));
        cv_text(lx[k], ly[k], &FONT_S, lab[k], col[k]);
        cv_rect(cx - 8, top, 16, h, RGB(34, 34, 34));                        /* the bar's well */
        if (ui.panel == PANEL_LEVELS) {                                     /* the sound's level now */
            int m = (int)(eng.meter[k] * h);
            cv_rect(cx - 6, top + h - m, 12, m, dim(col[k], 0.6f));
        } else {
            int m = val[k] * h / 100;
            cv_rect(cx - 6, top + h - m, 12, m, dim(col[k], 0.6f));
        }
        int my = top + h - val[k] * h / 100;                               /* where the knob is set */
        cv_rect(cx - 11, my - 1, 22, 3, C_WHITE);
        if (vtxt[k][0]) cv_text(cx - text_w(&FONT_S, vtxt[k]) / 2, top + h + 4, &FONT_S, vtxt[k], C_TXT);
    }
}

static void draw_popup(void)
{
    int wt = text_w(&FONT_S, ui.pop_t), ws = text_w(&FONT_M, ui.pop_s), w3 = ui.pop_3[0] ? text_w(&FONT_S, ui.pop_3) : 0;
    int w = clamp((wt > ws ? (wt > w3 ? wt : w3) : (ws > w3 ? ws : w3)) + 32, 140, 236);
    int w4 = ui.pop_4[0] ? text_w(&FONT_S, ui.pop_4) : 0;
    if (w4 + 24 > w) w = clamp(w4 + 24, w, 236);
    int h = ui.pop_4[0] ? 100 : ui.pop_3[0] ? 80 : 60, y0 = 46;
    frame_box(120 - w / 2, y0, w, h, ui.pop_c);
    cv_text(120 - wt / 2, y0 + 8, &FONT_S, ui.pop_t, ui.pop_c);
    cv_text(120 - ws / 2, y0 + 28, &FONT_M, ui.pop_s, C_TXT);
    if (ui.pop_3[0]) cv_text(120 - w3 / 2, y0 + 56, &FONT_S, ui.pop_3, C_SUB);
    if (ui.pop_4[0]) cv_text(120 - w4 / 2, y0 + 76, &FONT_S, ui.pop_4, C_SUB);
}

static void draw_view(void)
{
    uint16_t bg = C_BLACK;
    float fog = eng.muffle;
    uint32_t now = plat_ms();
    if (fog > 0.01f) bg = mix565(C_BLACK, eng.snow > 0.5f ? RGB(40, 46, 60) : RGB(34, 40, 48), fog);
    if (ui.flash > 0.01f) bg = mix565(bg, ui.flash_c, ui.flash * 0.8f);
    for (int half = 0; half < 2; half++) {
        int top = half ? VIEW_H / 2 : 0, h = half ? VIEW_H - VIEW_H / 2 : VIEW_H / 2;
        view_half(top, h, bg);
        draw_scene();
        if (now < ui.list_until)
            draw_list(LAYER_NAME[ui.list_layer], "PRESETS", LAYER_C[ui.list_layer], NUM_ENVS,
                      pos_of(patch_order[ui.list_layer], NUM_ENVS, set.cur.patch[ui.list_layer]), list_patch, ui.list_layer);
        else if (now < ui.envs_until)
            draw_list("ENVIRONMENT", 0, C_TXT, env_count(), env_pos(set.algo), list_env, 0);
        else {
            if (now < ui.panel_until) draw_panel();
            if (now < ui.pop_until) draw_popup();
        }
        cv_blit(0, (uint32_t)(VIEW_Y + top));
    }
}

static void draw_header(void)
{
    char name[24], right[24] = "", js[48];
    const char *state = eng.state_name ? eng.state_name : "";
    const char *w = weather_name(eng.wset, eng.eff.weather);
    env_name(name);                      /* (on a journey or a dream: JOURNEY or DREAM, in its colour; the place under it) */
    if (J.on) { j_status(js, 236 - text_w(&FONT_S, w) - 10 - 4); state = js; }
    text_cat(right, set.playing ? time_name(eng.eff.time) : "STOPPED");
    cv_begin(240, HEAD_H, C_BLACK);
    cv_text(4, 1, &FONT_M, name, J.on ? (J.dream ? C_DREAM : C_JOURNEY) : C_TXT);
    cv_text(236 - text_w(&FONT_S, right), 5, &FONT_S, right, set.playing ? C_SUB : RGB(255, 90, 78));
    cv_text(4, 26, &FONT_S, state, J.on ? C_TXT : C_SUB);
    cv_text(236 - text_w(&FONT_S, w), 26, &FONT_S, w, C_W);
    cv_rect(0, HEAD_H - 1, 240, 1, C_RULE);
    cv_blit(0, 0);
}

/* a screen reader that asks hears the whole screen */
static void speak(void)
{
    if (ann_on() && ann_where()) {
        char t[120] = "", n[24];
        env_name(n); text_cat(t, n); text_cat(t, ". ");
        if (J.on) { char js[48]; j_status(js, 1000); text_cat(t, js); text_cat(t, ". "); }
        text_cat(t, weather_name(eng.wset, eng.eff.weather)); text_cat(t, ", "); text_cat(t, time_name(eng.eff.time)); text_cat(t, ". ");
        text_cat(t, "Editing "); text_cat(t, LAYER_NAME[ui.sel]);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

/* ------------------------------------------------------------------ the opening: two seconds
 * Black, THE OUTSIDE INSIDE in white; under it a line draws itself out as a wave, then turns into a treeline, then
 * into a city's skyline, and the home screen takes over. The name is up from the first instant the FM-1 starts
 * (app_splash, before the sound); the line moves once the firmware runs, with the sound fading in under it. */
#define SPLASH_MS 2000u
#define BAND_Y 132                       /* the line's band on the screen, and its height */
#define BAND_H 72
#define BASE 56                          /* the ground in the band */
static int8_t sp_tree[240], sp_city[240];  /* the treeline's and the skyline's heights over the ground */

static void splash_shapes(void)
{
    uint32_t r = 0x5EED5u;
#define SRND(n) ((int)((r = r * 1664525u + 1013904223u) >> 16) % (n))
    memset(sp_tree, 0, sizeof sp_tree);
    for (int x = 12; x < 230; x += 17 + SRND(11)) {       /* firs and round trees, side by side */
        if (SRND(2)) {
            int h = 18 + SRND(16), w = 8 + SRND(4);
            for (int dx = -w; dx <= w; dx++)
                if (x + dx >= 0 && x + dx < 240) { int v = h - (dx < 0 ? -dx : dx) * h / w; if (v > sp_tree[x + dx]) sp_tree[x + dx] = (int8_t)v; }
        } else {
            int rr = 8 + SRND(5), top = 6 + SRND(9);
            for (int dx = -rr; dx <= rr; dx++)
                if (x + dx >= 0 && x + dx < 240) {
                    int v = top + (int)fm_sqrtf((float)(rr * rr - dx * dx));
                    if (v > sp_tree[x + dx]) sp_tree[x + dx] = (int8_t)v;
                }
        }
    }
    memset(sp_city, 0, sizeof sp_city);
    for (int x = 12; x < 228;) {                           /* blocks, now and then a step or a mast */
        int w = 10 + SRND(15), h = 12 + SRND(30);
        for (int i = 0; i < w && x + i < 228; i++) sp_city[x + i] = (int8_t)h;
        if (!SRND(3)) for (int i = w / 3; i < w - w / 3 && x + i < 228; i++) sp_city[x + i] = (int8_t)(h + 6);
        if (!SRND(4) && x + w / 2 < 228) { sp_city[x + w / 2] = (int8_t)(h + 14); sp_city[x + w / 2 + 1] = (int8_t)(h + 14); }
        x += w + SRND(3);
    }
#undef SRND
}

static void splash_title(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 32, C_BLACK);                          /* one line: 18 letters of 12 px, 216 of the 240 */
    cv_text(120 - text_w(&FONT_M, APP_NAME) / 2, 4, &FONT_M, APP_NAME, C_WHITE);
    cv_blit(0, 84);
}

void app_splash(void) { splash_title(); }

static float smooth(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3.0f - 2.0f * t); }

static void splash_frame(uint32_t ms)
{
    float t = ms / 1000.0f;
    float draw = smooth((t - 0.1f) / 0.55f);               /* the pen: 0.1 - 0.65 s, left to right */
    float to_trees = smooth((t - 0.85f) / 0.4f), to_city = smooth((t - 1.45f) / 0.4f);
    int x0 = 12, x1 = 228, head = x0 + (int)((x1 - x0) * draw);
    cv_begin(240, BAND_H, C_BLACK);
    int py = 0;
    for (int x = x0; x <= head; x++) {
        float edge = (x - x0) < 18 ? (x - x0) / 18.0f : (x1 - x) < 18 ? (x1 - x) / 18.0f : 1.0f;
        float wave = 7.0f * fm_sinf(x * 0.121f - t * 3.0f) + 2.5f * fm_sinf(x * 0.27f + 1.3f + t * 2.0f);
        float h = wave + (sp_tree[x] - wave) * to_trees;
        h += (sp_city[x] - h) * to_city;
        int y = BASE - (int)(h * edge);
        if (x > x0) cv_line(x - 1, py, x, y, C_WHITE);
        py = y;
    }
    if (draw < 1.0f) cv_rect(head - 1, py - 1, 3, 3, C_WHITE);   /* the pen's tip */
    cv_blit(0, BAND_Y);
}

static void splash_end(void)
{
    ui.splash_on = 0;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.head_key[0] = 0;
    ui.last_view = ui.last_head = plat_ms();
    draw_header();
}

void app_ui_init(void)
{
    memset(spr, 0, sizeof spr);
    orders_init();
    splash_shapes();
    ui.sel = 1;
    ui.evq_tail = eng.uiq_head;
    splash_title();                      /* (again: the host has no boot splash, and the device's is the same) */
    ui.splash_on = 1;
    ui.splash_t0 = plat_ms();
    ui.touched = 0;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    speak();
    if (ui.splash_on) {
        static uint32_t last;
        if (ui.touched || now - ui.splash_t0 >= SPLASH_MS) splash_end();
        else { if (now - last >= 30u) { last = now; splash_frame(now - ui.splash_t0); } return; }
    }
    if (now - ui.last_view >= 40u) {             /* the scene at 25 frames a second */
        float dt = (now - ui.last_view) / 1000.0f;
        if (dt > 0.2f) dt = 0.2f;
        ui.last_view = now;
        ui.t += dt;
        ui.flash *= 0.8f;
        sprites_step(dt);
        draw_view();
    }
    {   /* the load guard: past 85 % of the render's time, fewer events (their voices are what costs); back as it eases */
        uint32_t cpu = plat_cpu_pct();
        if (cpu > 85u) eng.gov -= 0.02f; else if (cpu < 70u) eng.gov += 0.002f;
        eng.gov = eng.gov < 0.3f ? 0.3f : eng.gov > 1.0f ? 1.0f : eng.gov;
    }
    j_tick(now);
    if (autosave_at && (int32_t)(now - autosave_at) >= 0) {
        autosave_at = 0;
        store_ok = plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0;
    }
    if (now - ui.last_head >= 250u) {            /* the header: when its words change */
        char key[80] = "", n[24];
        ui.last_head = now;
        env_name(n); text_cat(key, n); text_cat(key, eng.state_name ? eng.state_name : "");
        if (J.on) { char js[48]; j_status(js, 1000); text_cat(key, js); }
        text_cat(key, weather_name(eng.wset, eng.eff.weather)); text_cat(key, time_name(eng.eff.time)); text_cat(key, set.playing ? "1" : "0");
        int same = 1; for (int i = 0; i < 80; i++) { if (key[i] != ui.head_key[i]) { same = 0; break; } if (!key[i]) break; }
        if (!same) { for (int i = 0; i < 80; i++) ui.head_key[i] = key[i]; draw_header(); }
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "env")) *got = set.algo;
    else if (!strcmp(what, "jtravel")) *got = J.on && J.travelling;
    else if (!strcmp(what, "jheld")) *got = J.on && J.held;
    else if (!strcmp(what, "jat")) *got = J.on ? (J.dream ? J.to[L_LANDSCAPE] : J_ROUTE[J.at]) : -1;
    else if (!strcmp(what, "jdream")) *got = J.on && J.dream;
    else if (!strcmp(what, "outdb")) *got = (int)(10.0f * fm_log2f(out_pow + 1e-12f) * 0.30103f - 0.5f);
    else if (!strcmp(what, "jagc")) *got = (int)(J.agc + (J.agc < 0 ? -0.5f : 0.5f));
    else if (!strcmp(what, "sel")) *got = ui.sel;
    else if (!strcmp(what, "nusers")) *got = set.nusers;
    else if (!strcmp(what, "playing")) *got = set.playing;
    else if (!strcmp(what, "dirty")) *got = set_dirty;
    else if (!strncmp(what, "patch", 5)) *got = set.cur.patch[atoi(what + 5) & 3];
    else if (!strncmp(what, "enginepatch", 11)) *got = eng.patch[atoi(what + 11) & 3];
    else if (!strncmp(what, "level", 5)) *got = set.cur.level[atoi(what + 5) & 3];
    else if (!strncmp(what, "dens", 4)) *got = set.cur.dens[atoi(what + 4) & 3];
    else if (!strncmp(what, "mac", 3)) *got = set.cur.mac[atoi(what + 3) & 3];
    else if (!strcmp(what, "sources")) *got = eng.nsrc;
    else if (!strncmp(what, "events", 6)) *got = eng.layer_events[atoi(what + 6) & 3];
    else return 0;
    return 1;
}
#endif

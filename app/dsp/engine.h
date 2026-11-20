/* Outside: the sequencer (behaviours), conductor, mixer and reverb.
 * Plain C, no hardware calls: the same code runs on the FM-1, in the host simulator and in tools/render. */
#ifndef OUTSIDE_ENGINE_H
#define OUTSIDE_ENGINE_H

#include "generators.h"

#ifndef OUT_POOL
#define OUT_POOL                         /* app.h: big buffers into the FM-1's pool RAM */
#endif

#define NUM_LAYERS 4
enum { L_WEATHER, L_ANIMALS, L_LANDSCAPE, L_HUMANS };

/* The four macros, all 0..1.
   WEATHER  steps through its set's weather types, blending neighbours (Earth's, Space's or the alien world's: the
            weather preset's world)
   TIME     dawn -> day -> dusk -> night
   ACTIVITY how much animal and human life
   ENTROPY  how much the scene varies: 0 stays fixed, 1 wanders (weather drifts, states change, songs vary) */
typedef struct { float weather, time, activity, entropy; } Macros;

enum { WX_EARTH, WX_SPACE, WX_ALIEN, WX_SETS };
#define WEATHER_MAX 11                   /* the most types a set has */
int weather_set(int weather_patch);      /* which set a weather preset's world has (-1: Earth's) */
int weather_count(int set);
float weather_value(int set, int type);  /* the knob position of a weather type */
const char *weather_name(int set, float w);
const char *time_name(float t);

typedef enum { G_AIR, G_RAIN, G_WATER, G_VOICE, G_RING, G_HUM, G_CROWD } GenKind;
typedef enum { B_BED, B_SCATTER, B_PHRASE, B_CHORUS, B_SWELL, B_PASSBY, B_RARE } BehKind;

/* what the screen draws for a source's events (app.c) */
enum {
    GL_NONE, GL_BIRD, GL_BIGBIRD, GL_BAT, GL_NOTE, GL_DOT, GL_BIGDOT, GL_RING, GL_DRIP, GL_FLASH, GL_CHIME,
    GL_STEAM, GL_BAR, GL_CAR, GL_PLANE, GL_TRAIN, GL_CRAFT, GL_BOAT, GL_ROCK, GL_SPRAY, GL_BEACON, GL_PRESS,
    GL_GROWL, GL_SIGNAL, GL_GEYSER, GL_SQUARE, GL_FLAT, GL_BEE, GL_SHEEP
};

typedef struct Engine Engine;
typedef struct Source Source;
typedef void (*FireFn)(Engine *e, Source *s, float pan, float dist, float x);
typedef void (*BlockFn)(Engine *e, Source *s);

/* a species: syllables plus phrase patterns ('A'.. = syllable, '-' = pause) */
typedef struct {
    const Syllable *syl; int nsyl;
    const char *pat[6]; int npat;
    float pitch_jit, tempo_jit, gap;
    float rest_lo, rest_hi; int rep_lo, rep_hi;
    float amp;
} Species;

#define MAX_AGENTS 12
typedef struct { float pan, dist, next, pitch, tempo, ph, per; int pat, pos, reps, sp, singing; } Agent;

struct Source {
    const char *name;
    int layer; GenKind gk; BehKind beh;
    union { Air air; Rain rain; Water water; Voice voice; Ring ring; Hum hum; Crowd crowd; } g;
    float rate;                 /* events per second at ACTIVITY 0.5 */
    float pan_lo, pan_hi, dist_lo, dist_hi;
    float tod[4];               /* activity at dawn, day, dusk, night */
    float rain_resp;            /* > 0: loves rain (frogs) */
    int   alive;                /* scales with ACTIVITY (animals, humans) */
    int   regular;              /* SCATTER: near-even spacing */
    float cooldown;             /* RARE: minimum seconds between events */
    int   glyph;                /* GL_*: what the screen draws */
    FireFn fire; BlockFn block; FireFn peak;
    const Species *species[4]; int nspecies; float answer;
    Agent ag[MAX_AGENTS]; int nag;
    float c_p0, c_p1, c_pull; const Syllable *c_syl; int c_drift;
    float sw_ph, sw_rate, sw_lo, sw_hi, *swell_out;
    int pb_on, pb_voice; float pb_t, pb_len, pb_dir, pb_doppler, pb_len_lo, pb_len_hi;
    float t_next, cool; Rng rng; int events; float user[6];
    int novar;                  /* as designed, no variety (SPACE and ALIEN: they are right as they are) */
    float vw_p, vw_r, vt_p, vt_r, vt_left;   /* variety: its pitch and rate wandering over minutes (and where to) */
};

#define MAX_SOURCES 18

/* FDN reverb, 16-bit lines to fit the FM-1's RAM */
#define RV_LINES 8
#define RV_MAX 6144
#define RV_AP 800
typedef struct {
    int16_t buf[RV_LINES][RV_MAX]; int len[RV_LINES], pos[RV_LINES];
    float g[RV_LINES]; OnePole damp[RV_LINES];
    float ap[4][2][RV_AP]; int aplen[4], appos[4];
    float pre[2][2048]; int prelen, prepos;
    float mix;
} Reverb;
void reverb_init(Reverb *rv, float size, float rt60, float damp_hz, float predelay_ms, float mix);
void reverb_block(Reverb *rv, const float *inL, const float *inR, float *outL, float *outR);

/* ---------------- patches: four libraries, one per layer; an environment is one patch from each ---------------- */
#define NUM_ENVS 10
#define ENV_MEADOW 9
typedef struct { const char *name; float dw, da; } StateDef;      /* a scene state: offsets on weather, activity */
typedef struct {
    const char *name;
    void (*build)(Engine *e);           /* adds its sources (layer set by the library) */
    /* landscape patches only: the place */
    float rv_size, rv_rt60, rv_damp, rv_pre, rv_mix;
    int backdrop;                       /* app.c draws it */
    StateDef states[4];
} PatchDef;
extern const PatchDef PATCHES[NUM_LAYERS][NUM_ENVS];
extern const char *const ENV_NAMES[NUM_ENVS];

/* ---------------- what the screen sees ---------------- */
typedef struct { uint8_t layer, glyph, dist, a; int8_t pan, dir; uint16_t dur_ds; } UiEvent;   /* dist 0..100 */
#define UI_EVQ 64

typedef struct { float at; Macros m; const char *name; } StatePoint;   /* tools/render's demo timelines */

#define ENGINE_SUB 8                     /* OUT_BLOCKs an app block holds (fm1-base's 256 samples) */
#define UI_EVQ2 32

struct Engine {
    Source src[MAX_SOURCES]; int nsrc;
    Bus bus[NUM_LAYERS];
    /* the app's block rendered in three steps, so two cores can share the layers (engine_block_*) */
    Bus xb[ENGINE_SUB][NUM_LAYERS];
    /* a light phaser on ALIEN's weather (ION STORM): four all-passes a side, swept slowly, the sides apart */
    struct { float z[2][4], fb[2], ph; } phaser;
    float xg[ENGINE_SUB][NUM_LAYERS];   /* each layer's gain, block by block */
    int nsub;
    uint32_t (*clock)(void);            /* optional: a tick counter, to measure each layer's cost */
    uint32_t (*core)(void);             /* optional: which core runs the caller (0 or 1) */
    float cost[NUM_LAYERS + 1];         /* ticks an app block: each layer's sources, then the mix and the reverb */
    UiEvent uiq2[UI_EVQ2]; volatile uint32_t uiq2_n;   /* the second core's events, handed on after its job */
    /* set by the app */
    Macros user;                        /* the four macros as set */
    float level[NUM_LAYERS];            /* layer volumes 0..1 (knobs 1-4) */
    float dens[NUM_LAYERS];             /* layer densities 0..2, 1 = as designed (SELECT + knobs) */
    float gov;                          /* 0.3..1: the load guard thins every layer's events when the processor is short */
    int playing;
    volatile int oneshot[NUM_LAYERS];  /* a key: fire one event in that layer now */
    /* patches */
    int patch[NUM_LAYERS], want[NUM_LAYERS];
    float fade[NUM_LAYERS];             /* 0..1: a layer being swapped fades out, swaps, fades in */
    float master_fade;
    /* the conductor */
    Macros m;                           /* glides to user + state */
    Macros eff;                         /* after ENTROPY's wandering: what the sound follows */
    float glide_s;
    const StatePoint *timeline; int ntimeline;
    int state; float state_left; const char *state_name;
    Drift wander[3];
    float wind, rain, thunder, actf, ent, tod_w[4];
    float howl, drop, thunder_near, muffle, animals_w, snow;
    float gust, startle, t, swell_ui; int swell_peak;
    int wset;                           /* the weather set in play (the weather preset's world) */
    float shelter;                      /* 0..1: storms and heavy rain send the animals into cover (not the jungle's) */
    float shower, shower_t, shower_len, shower_amp, shower_wait;   /* THUNDER's passing showers */
    float wx_slow, wx_aim, wx_left;     /* Space's and the alien world's weather, wandering over minutes */
    OnePole muf[2][2];
    Reverb rv;
    float dc[2];
    Rng rng;
    int layer_events[NUM_LAYERS];
    float meter[NUM_LAYERS];            /* each layer's level, smoothed, for the screen */
    float layer_ss[NUM_LAYERS];         /* (float: the FM-1 has no double maths) */
    /* events for the screen: written in the render, read by the UI */
    UiEvent uiq[UI_EVQ]; volatile uint32_t uiq_head;
};

void engine_init(Engine *e, uint32_t seed);
void engine_env(Engine *e, int env);                 /* ask for all four layers of an environment */
void engine_want(Engine *e, int layer, int patch);   /* ask for one layer's patch; swapped in the render, faded */
void engine_render(Engine *e, float *outL, float *outR);   /* one OUT_BLOCK, all of it here */
/* An app block of nsub OUT_BLOCKs in three steps: begin (the conductor, the patch swaps: one core), the layers (each
 * layer is independent of the others: any layer on either core, both at once), then the mix (the reverb: one core,
 * after both). engine_block_events, after the second core's job, passes its screen events on. */
void engine_block_begin(Engine *e, int nsub);
void engine_block_layer(Engine *e, int layer);
void engine_block_mix(Engine *e, float *outL, float *outR);   /* nsub x OUT_BLOCK samples each */
void engine_block_events(Engine *e);
void engine_randomise(Engine *e);                    /* REC: new macros (the app keeps them in step) */
void engine_post(Engine *e, int layer, int glyph, float pan, float dist, int a, int dir, float dur_s);

/* for patches */
Source *engine_add(Engine *e, const char *name, int layer, GenKind gk, BehKind beh);
float src_tod(const Engine *e, const Source *s);
/* for an event a patch starts itself (from its block): the variety every event gets */
void engine_var_begin(Engine *e, Source *s);
void engine_var_end(void);
void phrase_setup(Source *s, Rng *r, int nagents);
void chorus_setup(Source *s, Rng *r, int nagents, float p_lo, float p_hi);

#endif

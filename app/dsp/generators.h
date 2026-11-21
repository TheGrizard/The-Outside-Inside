/* Outside: the seven sound generators. Each renders one block, adding into a layer Bus. */
#ifndef OUTSIDE_GENERATORS_H
#define OUTSIDE_GENERATORS_H

#include "dsp.h"

/* ================= AIR: stereo noise bed, gusts, howl, swell, thunder ================= */
typedef struct {
    /* settings */
    float level;        /* 0..1 */
    float colour;       /* 0 white, 1 pink, 2 brown */
    float cutoff, q;    /* filter */
    int   bandpass;     /* 0 low-pass, 1 band-pass */
    float hp;           /* high-pass corner, Hz (0 = off) */
    float gust_depth;   /* how much gusts move level and cutoff */
    float gust_speed;   /* 1 = normal */
    float howl, howl_freq, howl_q, howl2_ratio; /* whistling resonance */
    float width;        /* 0 mono .. 1 independent sides */
    float send;         /* reverb send */
    float swell_amt;    /* 0 = gusts only, 1 = driven by swell */
    float swell;        /* external 0..1, set by a SWELL behaviour */
    float *gust_link;   /* follow another AIR's gust (leaf rustle follows wind) */
    float weather;      /* 0..1, set by the conductor */
    int   no_bed;       /* thunder only */
    int   burst_mode;   /* steam vents: silent until air_burst() */
    float burst_env, burst_hold;
    /* state */
    Pink pk[3]; float br[3];
    Svf f[2], hpf[2], howlf[2], howl2f[2];
    float gust, gust_target; int gust_left;
    float amp_s, cut_s;
    /* thunder */
    int th_on; float th_t, th_len, th_amp, th_crack, th_zap; Spatial th_sp; Svf th_lp, th_zf; float th_br, th_mod, th_modt;
} Air;

void air_init(Air *a);
void air_thunder(Air *a, Rng *r, float pan, float dist, float zap);
void air_burst(Air *a, float hold_s);
void air_render(Air *a, Rng *r, Bus *b);

/* ================= RAIN: particles through resonant surfaces ================= */
#define RAIN_SURF 4
typedef struct {
    float density;      /* particles per second */
    float level;
    float width;        /* pan spread */
    float hiss;         /* diffuse hiss amount */
    float click;        /* direct, unfiltered tick amount */
    float send;
    float size;         /* 0 fine drizzle .. 1 big heavy drops */
    float surf_f[RAIN_SURF], surf_q[RAIN_SURF], surf_w[RAIN_SURF], surf_g[RAIN_SURF];
    /* the wash: most rain is drops too many and too far to hear one by one, a soft band of noise that grows with the
     * rain. 0 = none (static, crackle and shingle are all drops, on purpose) */
    float wash, wash_lp, wash_hp, wash_white;   /* its level, its band (Hz), 0 pink .. 1 white */
    float tone;         /* the drops' top: a gentle low-pass over them (Hz); 0 = open */
    float drops;        /* the drops' level (1 = as the surfaces were designed) */
    float var;          /* how much louder one drop can be than the next: 0 even .. 1 a few very loud (the old rain) */
    /* state */
    Svf sl[RAIN_SURF], sr[RAIN_SURF], hissf[2], clickf[2], washlp[2], washhp[2];
    Pink washpk[2];
    OnePole tonef[2][2];
    float flut, wash_amp;
    float energy;       /* events this block, for the meter */
} Rain;

void rain_init(Rain *r);
void rain_render(Rain *rn, Rng *r, Bus *b);

/* ================= WATER: bubbles ================= */
#define WATER_VOICES 20
typedef struct { int on; float ph, f, df, amp, dec, env; Spatial sp; } Bubble;
typedef struct {
    float rate;         /* bubbles per second (stream) */
    float dmin, dmax, gamma; /* bubble diameter range in mm; gamma > 1 favours small */
    float rise;         /* pitch rise factor */
    float decay_mul;    /* > 1 rings longer */
    float level;
    float pan_lo, pan_hi, dist_lo, dist_hi;
    float rush;         /* noise bed under the bubbles */
    float rush_cut;
    /* state */
    Bubble v[WATER_VOICES];
    int burst_left; float burst_rate; float burst_pan, burst_dist, burst_dmin, burst_dmax;
    Pink rush_pk[2]; Svf rush_f[2];
    float energy;
} Water;

void water_init(Water *w);
void water_spawn(Water *w, Rng *r, float d_mm, float pan, float dist, float amp, float decay_mul);
void water_burst(Water *w, int count, float seconds, float pan, float dist, float dmin, float dmax);
void water_render(Water *w, Rng *r, Bus *b);

/* ================= VOICE: syllables for birds, insects, creatures, beeps ================= */
enum { CURVE_LIN, CURVE_EXP, CURVE_SWOOP, CURVE_HOLD };
typedef struct {
    float f0, f1;       /* start and end pitch, Hz */
    int   curve;
    float dur;          /* seconds */
    float att;          /* attack share of the duration */
    float am_rate, am_depth;  /* trills and insect pulses */
    float h2;           /* second harmonic */
    float fm_ratio, fm_index; /* rasp, metallic tones */
    float vib_rate, vib_depth; /* vibrato, semitones */
    float amp;
} Syllable;

#define VOICE_VOICES 10
typedef struct { int on; float t; Syllable s; float pmul, ph, phm, vph; Spatial sp; } VVoice;
typedef struct { float level; VVoice v[VOICE_VOICES]; float energy; } Voice;

void voice_init(Voice *v);
void voice_play(Voice *v, const Syllable *s, float pitch_mul, float tempo_mul, float pan, float dist, float amp);
void voice_render(Voice *v, Bus *b);

/* ================= RING: modal resonators ================= */
#define RING_MODES 8
/* click: a burst of noise at the strike (its level, its band, and how long it lasts: click_ms, 0 = 3 ms) */
typedef struct { int n; float ratio[RING_MODES], tau[RING_MODES], amp[RING_MODES]; float click, click_cut, click_ms; } Material;
#define RING_VOICES 10
/* each mode a decaying phasor (re, im), turned by (cr, ci) = decay x (cos, sin) every sample: no table, no phase to wrap */
typedef struct { int on; int n; float re[RING_MODES], im[RING_MODES], cr[RING_MODES], ci[RING_MODES]; float clk, clk_dec; Svf clkf; Spatial sp; } RVoice;
typedef struct { float level; RVoice v[RING_VOICES]; float energy; } Ring;

void ring_init(Ring *g);
void ring_strike(Ring *g, Rng *r, const Material *m, float freq, float amp, float bright, float decay_mul, float pan, float dist);
void ring_render(Ring *g, Rng *r, Bus *b);

/* ================= HUM: FM drones, engines, craft ================= */
#define HUM_VOICES 4
typedef struct {
    int on; float f, ratio, index, detune, noise, noise_cut, vib_rate, vib_depth;
    float amp, target, att, rel;  /* envelope: att/rel are per-block slews */
    float ph[2], phm[2], vph, pmul;
    Pink pk[2]; Svf nf[2];
    Spatial sp; int bed; float bed_width;
} HVoice;
typedef struct { float level; HVoice v[HUM_VOICES]; float energy; } Hum;

void hum_init(Hum *h);
int  hum_start(Hum *h, float f, float ratio, float index, float detune, float noise, float noise_cut, float vib_rate, float vib_depth, float amp, float att_s);
void hum_release(Hum *h, int i, float rel_s);
void hum_render(Hum *h, Rng *r, Bus *b);

/* ================= CROWD: formant voices (babble, growls) ================= */
#define CROWD_VOICES 5
/* A voice: either a formant sweep (crowd_say: a howler's call, a growl), or a person talking (crowd_talk): a phrase
 * of syllables, each moving between vowels, breathy, its pitch falling across the phrase, a little hiss between */
typedef struct {
    int on, talk; float t, dur, f0, ph, fa1, fa2, fb1, fb2, amp, jit; Svf f1, f2; Spatial sp;
    float syl_t, syl_dur, gap, fric, stress, fscale, v1, v2, w1, w2, lp, nz; uint32_t seed;
    float trem, tph;    /* a bleat's quaver (Hz; 0 = none) */
    Svf f3;             /* a bleat's third formant: the nasal edge */
} CVoice;
typedef struct { float level; CVoice v[CROWD_VOICES]; float energy; } Crowd;

void crowd_init(Crowd *c);
void crowd_say(Crowd *c, float f0, float dur, float fa1, float fa2, float fb1, float fb2, float pan, float dist, float amp);
/* a sheep: crowd_say's sweep with a quaver in its pitch and its loudness, trem times a second */
void crowd_bleat(Crowd *c, float f0, float dur, float fa1, float fa2, float fb1, float fb2, float trem, float pan, float dist, float amp);
/* a person: a phrase of dur seconds at about pitch f0; bright 1 = as a voice is, more for a radio's thin voice */
void crowd_talk(Crowd *c, Rng *r, float f0, float dur, float bright, float pan, float dist, float amp);
void crowd_render(Crowd *c, Rng *r, Bus *b);

#endif

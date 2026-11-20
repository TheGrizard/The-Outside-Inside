/* Outside: small DSP helpers. Header-only, no allocation, no hardware calls. */
#ifndef OUTSIDE_DSP_H
#define OUTSIDE_DSP_H

#include <stdint.h>
/* memset and memcpy: the C library's on a computer; on the FM-1 there is no string.h, and felucca-bsp's libc.c has them */
#if defined(__has_include) && __has_include(<string.h>)
#include <string.h>
#else
void *memset(void *d, int c, unsigned n);       /* exactly as felucca-bsp's libc.c has them */
void *memcpy(void *d, const void *s, unsigned n);
#endif
#include "fastmath.h"   /* fm1-base: the FM-1 links no libm; the same maths on the desktop */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define OUT_SR 44100.0f
#define OUT_BLOCK 32
#define TWO_PI_F 6.28318530718f

/* ---------- random: xorshift32 ---------- */
typedef struct { uint32_t s; } Rng;

static inline void rng_seed(Rng *r, uint32_t seed) { r->s = seed ? seed : 0x9E3779B9u; }
static inline uint32_t rng_u32(Rng *r) { uint32_t x = r->s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return r->s = x; }
static inline float rng_f(Rng *r) { return (float)(rng_u32(r) >> 8) * (1.0f / 16777216.0f); }
static inline float rng_bi(Rng *r) { return rng_f(r) * 2.0f - 1.0f; }
static inline float rng_range(Rng *r, float a, float b) { return a + (b - a) * rng_f(r); }
static inline float rng_exp(Rng *r) { return -fm_logf(1e-7f + rng_f(r)); }
static inline int rng_int(Rng *r, int n) { return (int)(rng_f(r) * n) % (n > 0 ? n : 1); }

/* ---------- sine table ---------- */
#define SINE_BITS 12
#define SINE_SIZE (1 << SINE_BITS)
extern float g_sine[SINE_SIZE + 1];   /* filled by dsp_init() */
void dsp_init(void);
/* phase in cycles (0..1, wraps) */
static inline float sine_c(float ph) {
    ph -= fm_floorf(ph);
    float x = ph * SINE_SIZE; int i = (int)x; float f = x - i;
    return g_sine[i] + (g_sine[i + 1] - g_sine[i]) * f;
}

/* ---------- filters ---------- */
/* Denormals: a filter left ringing down to nothing reaches numbers so small that some FPUs (the host's, maybe the
 * FM-1's) take a slow path for each one: rain's filters alone cost five times more. A whisper of noise, about -370 dB,
 * keeps every recursive state in the normal range. */
/* Variety: while an event starts, the engine sets how this one differs from the last (its pitch, loudness and
 * brightness, a little each time, and drifting over minutes); the generators' starts (water_spawn, ring_strike,
 * voice_play, hum_start, crowd_*) take it. One for each core: the two render layers at once. */
typedef struct { float pitch, amp, bright; } VarCtx;
extern VarCtx g_var[2];
extern uint32_t (*g_var_core)(void);
static inline VarCtx *var_now(void) { return &g_var[g_var_core ? (g_var_core() & 1u) : 0]; }

static float dn_v = 1e-18f;
static inline float dn(void) { dn_v = -dn_v; return dn_v; }   /* +-1e-18, alternating: cheaper than noise, and as good */

typedef struct { float ic1, ic2, a1, a2, a3, k; } Svf;
static inline void svf_set(Svf *f, float fc, float q) {
    if (fc > OUT_SR * 0.45f) fc = OUT_SR * 0.45f;
    if (fc < 10.0f) fc = 10.0f;
    if (q < 0.3f) q = 0.3f;
    float g = fm_tanf((float)M_PI * fc / OUT_SR);
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}
static inline void svf_run(Svf *f, float v0, float *lp, float *bp, float *hp) {
    float v3 = v0 + dn() - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    if (lp) *lp = v2;
    if (bp) *bp = v1;
    if (hp) *hp = v0 - f->k * v1 - v2;
}
static inline float svf_lp(Svf *f, float x) { float l; svf_run(f, x, &l, 0, 0); return l; }
static inline float svf_bp(Svf *f, float x) { float b; svf_run(f, x, 0, &b, 0); return b; }
static inline float svf_hp(Svf *f, float x) { float h; svf_run(f, x, 0, 0, &h); return h; }

typedef struct { float z, a; } OnePole;
static inline void op_set(OnePole *o, float fc) { if (fc > OUT_SR * 0.49f) fc = OUT_SR * 0.49f; o->a = 1.0f - fm_expf(-TWO_PI_F * fc / OUT_SR); }
static inline float op_run(OnePole *o, float x) { o->z += o->a * (x + dn() - o->z); return o->z; }

/* pink noise, Paul Kellet's economy filter */
typedef struct { float b0, b1, b2; } Pink;
static inline float pink_run(Pink *p, float w) {
    w += dn();
    p->b0 = 0.99765f * p->b0 + w * 0.0990460f;
    p->b1 = 0.96300f * p->b1 + w * 0.2965164f;
    p->b2 = 0.57000f * p->b2 + w * 1.0526913f;
    return (p->b0 + p->b1 + p->b2 + w * 0.1848f) * 0.22f;
}
/* brown noise: leaky integrator */
static inline float brown_run(float *z, float w) { *z = 0.985f * *z + 0.06f * (w + dn()); return *z * 1.6f; }

/* ---------- spatial: pan + distance for one voice ---------- */
typedef struct { float gl, gr, send; OnePole lp; } Spatial;
static inline void spatial_set(Spatial *s, float pan, float dist) {
    pan = pan < -1 ? -1 : pan > 1 ? 1 : pan;
    dist = dist < 0 ? 0 : dist > 1 ? 1 : dist;
    float t = (pan + 1.0f) * 0.25f * (float)M_PI;
    float gain = 1.0f / (1.0f + 2.5f * dist);
    s->gl = fm_cosf(t) * gain;
    s->gr = fm_sinf(t) * gain;
    s->send = 0.12f + 0.75f * dist;
    op_set(&s->lp, 16000.0f * fm_powf(0.1f, dist));
}

/* a layer bus: dry stereo plus reverb send */
typedef struct { float L[OUT_BLOCK], R[OUT_BLOCK], sL[OUT_BLOCK], sR[OUT_BLOCK]; } Bus;
static inline void emit(Bus *b, int i, Spatial *s, float x) {
    x = op_run(&s->lp, x);
    float l = x * s->gl, r = x * s->gr;
    b->L[i] += l; b->R[i] += r;
    b->sL[i] += l * s->send; b->sR[i] += r * s->send;
}
static inline void emit_stereo(Bus *b, int i, float l, float r, float send) {
    b->L[i] += l; b->R[i] += r; b->sL[i] += l * send; b->sR[i] += r * send;
}

/* ---------- drift: three smoothed random walks (seconds, tens of seconds, minutes) ---------- */
typedef struct { float v[3], target[3]; int left[3]; } Drift;
static inline float drift_step(Drift *d, Rng *r, float speed) {
    static const float len_s[3] = { 2.0f, 15.0f, 70.0f };
    static const float weight[3] = { 0.25f, 0.4f, 0.35f };
    float out = 0;
    for (int i = 0; i < 3; i++) {
        if (d->left[i]-- <= 0) {
            d->target[i] = rng_bi(r);
            d->left[i] = (int)(len_s[i] * rng_range(r, 0.5f, 1.5f) * OUT_SR / OUT_BLOCK / (speed > 0.05f ? speed : 0.05f));
        }
        float slew = (float)OUT_BLOCK / (len_s[i] * OUT_SR) * 2.0f * speed;
        d->v[i] += (d->target[i] - d->v[i]) * (slew > 1 ? 1 : slew);
        out += d->v[i] * weight[i];
    }
    return out; /* roughly -1..1 */
}

#endif

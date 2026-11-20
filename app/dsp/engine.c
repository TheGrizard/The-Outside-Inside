/* Outside: sequencer (behaviours), conductor, mixer, reverb. */
#include "engine.h"

static const float BLOCK_S = (float)OUT_BLOCK / OUT_SR;

/* ------------------------------------------------------------------ weather types and names */
/* Three worlds, three sets of weather, each in order calm to stormy (the knob's scale; ENTROPY drifts along it, so
 * a world only ever drifts into its own weather). showers: now and then a light shower passes (THUNDER's) */
typedef struct { const char *name; float wind, howl, rain, drop, thunder, near, muffle, animals, snow, showers; } WeatherType;
static const WeatherType WX_E[] = {
    /* name           wind   howl   rain   drop  thun  near  muffle animals snow showers */
    { "SNOW",         0.25f, 0.1f,  0.0f,  0.0f, 0.0f, 0.0f, 0.75f, 0.35f, 1.0f, 0 },
    { "FOG",          0.05f, 0.0f,  0.04f, 0.9f, 0.0f, 0.0f, 0.5f,  0.7f,  0.0f, 0 },
    { "CALM",         0.08f, 0.0f,  0.0f,  0.5f, 0.0f, 0.0f, 0.0f,  1.0f,  0.0f, 0 },
    { "BREEZE",       0.45f, 0.1f,  0.0f,  0.5f, 0.0f, 0.0f, 0.0f,  1.0f,  0.0f, 0 },
    { "DRIZZLE",      0.3f,  0.05f, 0.35f, 0.1f, 0.0f, 0.0f, 0.1f,  0.75f, 0.0f, 0 },
    { "RAIN",         0.4f,  0.1f,  0.7f,  0.5f, 0.0f, 0.0f, 0.1f,  0.45f, 0.0f, 0 },
    { "DOWNPOUR",     0.5f,  0.15f, 1.0f,  1.0f, 0.05f, 0.0f, 0.15f, 0.2f, 0.0f, 0 },
    { "THUNDERSTORM", 0.8f,  0.35f, 0.9f,  0.8f, 1.0f, 0.8f, 0.1f,  0.12f, 0.0f, 0 },
    { "THUNDER",      0.55f, 0.2f,  0.0f,  0.6f, 1.0f, 0.55f, 0.0f, 0.5f,  0.0f, 1 },   /* dry: the storm without its rain */
    { "GALE",         1.0f,  0.8f,  0.15f, 0.6f, 0.25f, 0.1f, 0.0f, 0.2f,  0.0f, 0 },
    { "WINDY",        0.85f, 0.4f,  0.0f,  0.5f, 0.0f, 0.0f, 0.0f,  0.6f,  0.0f, 0 },
};
static const WeatherType WX_S[] = {   /* the station: no snow, no fog; the solar wind, particles, flares */
    { "STILL VOID",     0.05f, 0.0f,  0.0f,  0.5f, 0.0f, 0.0f, 0, 1.0f,  0.0f, 0 },
    { "ICE DUST",       0.15f, 0.05f, 0.05f, 0.5f, 0.0f, 0.0f, 0, 1.0f,  0.6f, 0 },
    { "SOLAR WIND",     0.5f,  0.15f, 0.05f, 0.5f, 0.0f, 0.0f, 0, 1.0f,  0.0f, 0 },
    { "PARTICLE RAIN",  0.35f, 0.1f,  0.6f,  0.5f, 0.0f, 0.0f, 0, 0.8f,  0.0f, 0 },
    { "SOLAR FLARE",    0.6f,  0.3f,  0.4f,  0.6f, 0.9f, 0.6f, 0, 0.6f,  0.0f, 0 },
    { "MAGNETIC STORM", 1.0f,  0.8f,  0.7f,  0.6f, 0.5f, 0.3f, 0, 0.45f, 0.0f, 0 },
};
static const WeatherType WX_A[] = {   /* the crystal plains: singing winds and ion rain */
    { "STILL AIR",      0.08f, 0.0f,  0.0f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f,  0.0f, 0 },
    { "CRYSTAL FOG",    0.05f, 0.0f,  0.04f, 0.9f, 0.0f, 0.0f, 0.5f, 0.7f,  0.0f, 0 },
    { "SINGING WIND",   0.55f, 0.45f, 0.0f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f,  0.0f, 0 },
    { "ION DRIZZLE",    0.3f,  0.1f,  0.35f, 0.1f, 0.0f, 0.0f, 0.1f, 0.8f,  0.0f, 0 },
    { "ION RAIN",       0.45f, 0.15f, 0.75f, 0.5f, 0.0f, 0.0f, 0.1f, 0.6f,  0.0f, 0 },
    { "ION STORM",      0.8f,  0.4f,  0.9f,  0.8f, 1.0f, 0.8f, 0.1f, 0.35f, 0.0f, 0 },
    { "HOWLING GALE",   1.0f,  0.9f,  0.15f, 0.6f, 0.25f, 0.1f, 0.0f, 0.3f, 0.0f, 0 },
};
static const WeatherType *const WXS[WX_SETS] = { WX_E, WX_S, WX_A };
static const int WXN[WX_SETS] = { sizeof WX_E / sizeof WX_E[0], sizeof WX_S / sizeof WX_S[0], sizeof WX_A / sizeof WX_A[0] };
int weather_set(int p) { return p == 2 ? WX_SPACE : p == 7 ? WX_ALIEN : WX_EARTH; }
int weather_count(int set) { return WXN[set < 0 || set >= WX_SETS ? 0 : set]; }
float weather_value(int set, int type) { return (float)type / (weather_count(set) - 1); }
const char *weather_name(int set, float w) {
    int n = weather_count(set), i = (int)(w * (n - 1) + 0.5f);
    return WXS[set < 0 || set >= WX_SETS ? 0 : set][i < 0 ? 0 : i >= n ? n - 1 : i].name;
}
const char *time_name(float t) { static const char *const n[4] = { "DAWN", "DAY", "DUSK", "NIGHT" }; int i = (int)(t * 3.0f + 0.5f); return n[i < 0 ? 0 : i > 3 ? 3 : i]; }

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

static void derive(Engine *e) {
    e->ent = clamp01(e->m.entropy);
    float sp = 0.15f + 2.0f * e->ent;
    float ww = drift_step(&e->wander[0], &e->rng, sp), wa = drift_step(&e->wander[1], &e->rng, sp), wt = drift_step(&e->wander[2], &e->rng, sp * 0.5f);
    int set = e->wset = weather_set(e->patch[L_WEATHER]), nwx = WXN[set];
    const WeatherType *WX = WXS[set];
    e->eff = e->m;
    if (set == WX_EARTH) e->eff.weather = clamp01(e->m.weather + ww * e->ent * 0.315f);   /* (about three types either way) */
    else {   /* Space, the alien world: their weather wanders slowly through the whole set, a new aim every few minutes */
        if ((e->wx_left -= BLOCK_S * (0.3f + e->ent)) <= 0) { e->wx_aim = rng_bi(&e->rng); e->wx_left = rng_range(&e->rng, 60.0f, 200.0f); }
        e->wx_slow += (e->wx_aim - e->wx_slow) * (BLOCK_S / 45.0f);
        e->eff.weather = clamp01(e->m.weather + (ww * 0.1f + e->wx_slow * 0.65f) * e->ent);
    }
    e->eff.activity = clamp01(e->m.activity + wa * e->ent * 0.4f);
    e->eff.time = clamp01(e->m.time + wt * e->ent * 0.12f);
    {   /* blend the two neighbouring weather types */
        float x = e->eff.weather * (nwx - 1); int i = (int)x; if (i >= nwx - 1) i = nwx - 2; float f = x - i;
        const WeatherType *a = &WX[i], *b = &WX[i + 1];
#define MIXW(k) (a->k + (b->k - a->k) * f)
        e->wind = MIXW(wind); e->howl = MIXW(howl); e->rain = MIXW(rain); e->drop = MIXW(drop);
        e->thunder = MIXW(thunder); e->thunder_near = MIXW(near); e->muffle = MIXW(muffle); e->animals_w = MIXW(animals); e->snow = MIXW(snow);
        float showers = MIXW(showers);
#undef MIXW
        /* THUNDER's light showers: one passes every few minutes (sooner with ENTROPY), a minute or two of it */
        if (showers > 0.01f) {
            if (e->shower_len <= 0) {
                if ((e->shower_wait -= BLOCK_S * (0.4f + 1.6f * e->ent)) <= 0) {
                    e->shower_len = rng_range(&e->rng, 40.0f, 120.0f); e->shower_t = 0; e->shower_amp = rng_range(&e->rng, 0.15f, 0.32f);
                    e->shower_wait = rng_range(&e->rng, 90.0f, 300.0f);
                }
            }
        }
        if (e->shower_len > 0) {
            float u = (e->shower_t += BLOCK_S) / e->shower_len;
            e->shower = u < 1.0f ? e->shower_amp * fm_sin_turns(0.5f * u) : 0;
            if (u >= 1.0f) e->shower_len = 0;
        } else e->shower = 0;
        e->rain += showers * e->shower;
    }
    {   /* into cover: heavy rain, a storm overhead, a gale. Not in the jungle (it sings on), nor Space or the alien world */
        float h = clamp01((e->rain - 0.35f) * 2.0f), t = e->thunder * 0.85f, g = clamp01((e->wind - 0.8f) * 2.5f);
        if (t > h) h = t; if (g > h) h = g;
        int ap = e->patch[L_ANIMALS];
        e->shelter = (ap == 1 || ap == 2 || ap == 7) ? 0 : h;
    }
    e->actf = 0.1f + 1.8f * e->eff.activity;
    for (int i = 0; i < 4; i++) { float d = fm_fabsf(e->eff.time * 3.0f - i); e->tod_w[i] = d < 1 ? 1 - d : 0; }
}

float src_tod(const Engine *e, const Source *s) {
    float a = 0; for (int i = 0; i < 4; i++) a += e->tod_w[i] * s->tod[i];
    return a;
}

/* how strongly a source is active right now: time of day, weather, activity, startle, its layer's density */
VarCtx g_var[2] = { { 1, 1, 1 }, { 1, 1, 1 } };
uint32_t (*g_var_core)(void);

/* variety: every event a little different (pitch +-3 %, loudness +-25 %, brightness +-15 %), on top of the source's
 * own slow drift; more of both with ENTROPY */
static void var_begin(Engine *e, Source *s, int each) {
    VarCtx *v = var_now();
    if (s->novar) { v->pitch = v->amp = v->bright = 1.0f; return; }
    float k = 1.0f + e->ent;
    v->pitch = 1.0f + s->vw_p;
    v->amp = 1.0f; v->bright = 1.0f;
    if (each) {
        v->pitch *= 1.0f + 0.03f * k * (rng_f(&s->rng) + rng_f(&s->rng) - 1.0f);
        v->amp = 1.0f + 0.25f * (rng_f(&s->rng) + rng_f(&s->rng) - 1.0f) * 2.0f;
        v->bright = 1.0f + 0.15f * k * rng_bi(&s->rng);
    }
}
static void var_end(void) { VarCtx *v = var_now(); v->pitch = v->amp = v->bright = 1.0f; }
void engine_var_begin(Engine *e, Source *s) { var_begin(e, s, 1); }
void engine_var_end(void) { var_end(); }
static void var_drift(Engine *e, Source *s) {    /* where its pitch and its pace wander to next, every 20-70 s */
    if (s->novar) return;
    s->vt_left -= BLOCK_S;
    if (s->vt_left <= 0) {
        float k = 0.5f + e->ent;
        s->vt_p = 0.05f * k * rng_bi(&s->rng);
        s->vt_r = 0.45f * rng_bi(&s->rng);
        s->vt_left = rng_range(&s->rng, 20.0f, 70.0f);
    }
    s->vw_p += (s->vt_p - s->vw_p) * (BLOCK_S / 10.0f);
    s->vw_r += (s->vt_r - s->vw_r) * (BLOCK_S / 6.0f);
}

static float src_life(Engine *e, Source *s) {
    float x = src_tod(e, s) * (1.0f + s->vw_r);   /* (busier and quieter spells, so no rate stays even) */
    if (s->layer == L_ANIMALS) x *= e->animals_w * (s->rain_resp > 0 ? 1.0f : 1.0f - e->shelter);   /* (frogs love the rain) */
    if (s->rain_resp > 0) x *= 1.0f + s->rain_resp * e->rain;
    if (s->alive) x *= e->actf;
    if (s->layer == L_ANIMALS && e->startle > 0) x = 0;
    x *= e->dens[s->layer] * e->gov;
    return x > 0 ? x : 0;
}

/* ------------------------------------------------------------------ the screen's events */
void engine_post(Engine *e, int layer, int glyph, float pan, float dist, int a, int dir, float dur_s) {
    if (glyph == GL_NONE) return;
    UiEvent tmp, *u = &tmp;
    int second = e->core && e->core() != 0;
    uint32_t h = e->uiq_head;
    if (!second) u = &e->uiq[h % UI_EVQ];
    u->layer = (uint8_t)layer; u->glyph = (uint8_t)glyph;
    u->pan = (int8_t)(clamp01((pan + 1) * 0.5f) * 200.0f - 100.0f);
    u->dist = (uint8_t)(clamp01(dist) * 100.0f);
    u->a = (uint8_t)a; u->dir = (int8_t)dir; u->dur_ds = (uint16_t)(dur_s * 10.0f);
    if (second) { if (e->uiq2_n < UI_EVQ2) e->uiq2[e->uiq2_n++] = tmp; }
    else e->uiq_head = h + 1;
}

void engine_block_events(Engine *e) {
    for (uint32_t i = 0; i < e->uiq2_n; i++) { e->uiq[e->uiq_head % UI_EVQ] = e->uiq2[i]; e->uiq_head++; }
    e->uiq2_n = 0;
}

/* ------------------------------------------------------------------ setup */
static void apply_patch(Engine *e, int layer, int patch);

void engine_init(Engine *e, uint32_t seed) {
    memset(e, 0, sizeof *e);
    rng_seed(&e->rng, seed);
    e->gov = 1.0f;
    for (int i = 0; i < NUM_LAYERS; i++) { e->level[i] = 0.75f; e->dens[i] = 1.0f; e->fade[i] = 1.0f; e->patch[i] = -1; e->want[i] = -1; }
    e->user = (Macros){ weather_value(WX_EARTH, 3), 0.33f, 0.5f, 0.4f };
    e->m = e->user; e->eff = e->m; e->glide_s = 6.0f;
    e->master_fade = 1.0f; e->playing = 1;
    e->state_left = 120.0f;
    derive(e);
}

void engine_env(Engine *e, int env) { for (int l = 0; l < NUM_LAYERS; l++) e->want[l] = env; }
void engine_want(Engine *e, int layer, int patch) { if (layer >= 0 && layer < NUM_LAYERS) e->want[layer] = patch; }

Source *engine_add(Engine *e, const char *name, int layer, GenKind gk, BehKind beh) {
    if (e->nsrc >= MAX_SOURCES) e->nsrc = MAX_SOURCES - 1;   /* a patch that asks for too much loses its last source */
    Source *s = &e->src[e->nsrc++];
    memset(s, 0, sizeof *s);
    s->name = name; s->layer = layer; s->gk = gk; s->beh = beh;
    s->pan_lo = -0.9f; s->pan_hi = 0.9f; s->dist_lo = 0.1f; s->dist_hi = 0.6f;
    for (int i = 0; i < 4; i++) s->tod[i] = 1.0f;
    s->alive = (layer == L_ANIMALS || layer == L_HUMANS);
    {   /* each source its own sequence: the engine's next number scrambled (a source seeded with it as it is would
         * replay its neighbour's a step behind, and two pass-bys added together would set off together) */
        uint32_t x = rng_u32(&e->rng) + 0x9E3779B9u * (uint32_t)e->nsrc;
        x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
        rng_seed(&s->rng, x);
    }
    switch (gk) {
    case G_AIR: air_init(&s->g.air); s->g.air.gust_link = 0; break;
    case G_RAIN: rain_init(&s->g.rain); break;
    case G_WATER: water_init(&s->g.water); break;
    case G_VOICE: voice_init(&s->g.voice); break;
    case G_RING: ring_init(&s->g.ring); break;
    case G_HUM: hum_init(&s->g.hum); break;
    case G_CROWD: crowd_init(&s->g.crowd); break;
    }
    return s;
}

/* take a layer's sources out (keeping the others' order) and build its new patch */
static void apply_patch(Engine *e, int layer, int patch) {
    int j = 0;
    for (int i = 0; i < e->nsrc; i++) {
        if (e->src[i].layer == layer) continue;
        if (i != j) memcpy(&e->src[j], &e->src[i], sizeof(Source))   /* j < i: two different sources, no overlap */;
        j++;
    }
    e->nsrc = j;
    e->patch[layer] = patch;
    if (patch < 0 || patch >= NUM_ENVS) return;
    const PatchDef *p = &PATCHES[layer][patch];
    int first = e->nsrc;
    if (p->build) p->build(e);
    if (patch == 2 || patch == 7) for (int i = first; i < e->nsrc; i++) e->src[i].novar = 1;   /* SPACE, ALIEN */
    if (layer == L_LANDSCAPE) {
        reverb_init(&e->rv, p->rv_size, p->rv_rt60, p->rv_damp, p->rv_pre, p->rv_mix);
        e->state = 0; e->state_left = 120.0f;
    }
}

void phrase_setup(Source *s, Rng *r, int n) {
    if (n > MAX_AGENTS) n = MAX_AGENTS;
    s->nag = n;
    for (int i = 0; i < n; i++) {
        Agent *a = &s->ag[i];
        a->pan = rng_range(r, s->pan_lo, s->pan_hi); a->dist = rng_range(r, s->dist_lo, s->dist_hi);
        a->sp = i % (s->nspecies > 0 ? s->nspecies : 1);
        a->next = rng_range(r, 0.5f, 6.0f); a->pitch = 1; a->tempo = 1; a->singing = 0;
    }
}

void chorus_setup(Source *s, Rng *r, int n, float p_lo, float p_hi) {
    if (n > MAX_AGENTS) n = MAX_AGENTS;
    s->nag = n;
    for (int i = 0; i < n; i++) {
        Agent *a = &s->ag[i];
        a->pan = rng_range(r, s->pan_lo, s->pan_hi); a->dist = rng_range(r, s->dist_lo, s->dist_hi);
        a->per = rng_range(r, s->c_p0, s->c_p1); a->ph = rng_f(r);
        a->pitch = rng_range(r, p_lo, p_hi);
    }
}

/* ------------------------------------------------------------------ behaviours */
static void fire(Engine *e, Source *s, float pan, float dist, float x) {
    if (s->fire) { var_begin(e, s, 1); s->fire(e, s, pan, dist, x); var_end(); }
    s->events++; e->layer_events[s->layer]++;
    engine_post(e, s->layer, s->glyph, pan, dist, 0, 0, 0);
}

static void beh_scatter(Engine *e, Source *s, int rare) {
    float life = src_life(e, s);
    float rate = s->rate * life * (rare ? 0.3f + 1.4f * e->ent : 1.0f);
    if (rare && s->cool > 0) { s->cool -= BLOCK_S; return; }
    if (rate <= 0) return;
    if (s->regular) {
        s->t_next -= BLOCK_S;
        if (s->t_next <= 0) {
            fire(e, s, rng_range(&s->rng, s->pan_lo, s->pan_hi), rng_range(&s->rng, s->dist_lo, s->dist_hi), 1);
            s->t_next = (1.0f / rate) * rng_range(&s->rng, 0.85f, 1.15f);
        }
        return;
    }
    if (rng_f(&s->rng) < rate * BLOCK_S) {
        fire(e, s, rng_range(&s->rng, s->pan_lo, s->pan_hi), rng_range(&s->rng, s->dist_lo, s->dist_hi), 1);
        if (rare) s->cool = s->cooldown;
    }
}

static void phrase_start(Source *s, Agent *a, Rng *r, float ent) {
    const Species *sp = s->species[a->sp];
    a->singing = 1; a->pos = 0;
    if (a->reps <= 0) {
        /* low entropy: a bird keeps one song; high: it changes song often */
        if (rng_f(r) < 0.15f + 0.85f * ent) a->pat = rng_int(r, sp->npat);
        a->reps = sp->rep_lo + rng_int(r, sp->rep_hi - sp->rep_lo + 1);
    }
    float v = 0.25f + 1.5f * ent;
    a->pitch = 1.0f + rng_bi(r) * sp->pitch_jit * v;
    a->tempo = 1.0f + rng_bi(r) * sp->tempo_jit * v;
}

static void beh_phrase(Engine *e, Source *s) {
    float life = src_life(e, s);
    Rng *r = &s->rng;
    int active = (int)(s->nag * (0.3f + 0.45f * e->actf) * (e->dens[s->layer] > 1 ? 1 : e->dens[s->layer]) + 0.5f);
    if (active > s->nag) active = s->nag;
    for (int i = 0; i < s->nag; i++) {
        Agent *a = &s->ag[i];
        const Species *sp = s->species[a->sp];
        a->next -= BLOCK_S;
        if (a->next > 0) continue;
        if (!a->singing) {
            if (i >= active || life < 0.02f) { a->next = rng_range(r, 1.0f, 3.0f); continue; }
            phrase_start(s, a, r, e->ent);
            engine_post(e, s->layer, s->glyph, a->pan, a->dist, (int)sp->npat, 0, 0);
        }
        const char *pat = sp->pat[a->pat];
        char c = pat[a->pos];
        if (c == 0) {                                  /* phrase finished */
            a->singing = 0; a->reps--;
            if (a->reps > 0) a->next = rng_range(r, 0.6f, 1.8f);
            else a->next = rng_range(r, sp->rest_lo, sp->rest_hi) / (life > 0.05f ? life : 0.05f);
            if (s->answer > 0 && rng_f(r) < s->answer) {   /* another agent answers from elsewhere */
                Agent *b = &s->ag[rng_int(r, active > 0 ? active : 1)];
                if (b != a && !b->singing) b->next = rng_range(r, 0.3f, 1.2f);
            }
            continue;
        }
        a->pos++;
        if (c == '-') { a->next = sp->gap * 2.0f / a->tempo; continue; }
        int idx = c - 'A';
        if (idx < 0 || idx >= sp->nsyl) { a->next = sp->gap; continue; }
        const Syllable *sy = &sp->syl[idx];
        var_begin(e, s, 0);                         /* (a song keeps its own pitches: only the slow drift) */
        voice_play(&s->g.voice, sy, a->pitch, a->tempo, a->pan + rng_bi(r) * 0.03f, a->dist, sp->amp);
        var_end();
        s->events++; e->layer_events[s->layer]++;
        a->next = sy->dur / a->tempo + sp->gap * rng_range(r, 0.7f, 1.3f) / a->tempo;
    }
}

static void beh_chorus(Engine *e, Source *s) {
    float life = src_life(e, s);
    if (life < 0.02f) return;
    Rng *r = &s->rng;
    int active = (int)(s->nag * (life > 1 ? 1 : life) + 0.5f);
    int fired = 0;
    for (int i = 0; i < active; i++) {
        Agent *a = &s->ag[i];
        if (s->c_drift) { a->pan += rng_bi(r) * 0.002f; if (a->pan > 0.95f) a->pan = 0.95f; if (a->pan < -0.95f) a->pan = -0.95f; a->pitch *= 1.0f + rng_bi(r) * 0.0008f; }
        a->ph += BLOCK_S / a->per;
        if (a->ph >= 1.0f) {
            a->ph -= 1.0f; fired = 1;
            var_begin(e, s, 1);
            if (s->c_syl) { voice_play(&s->g.voice, s->c_syl, a->pitch, 1.0f, a->pan, a->dist, 1.0f); s->events++; e->layer_events[s->layer]++; }
            else if (s->fire) s->fire(e, s, a->pan, a->dist, a->pitch);
            var_end();
            engine_post(e, s->layer, s->glyph, a->pan, a->dist, 0, 0, 0);
            a->per += rng_bi(r) * 0.02f * a->per;
            if (a->per < s->c_p0) a->per = s->c_p0; if (a->per > s->c_p1) a->per = s->c_p1;
        }
    }
    float pull = s->c_pull * (1.5f - e->ent);        /* low entropy: choruses lock into step */
    if (fired && pull > 0)
        for (int i = 0; i < active; i++) if (s->ag[i].ph > 0.85f) s->ag[i].ph += (1.0f - s->ag[i].ph) * (pull > 1 ? 1 : pull);
}

static void beh_swell(Engine *e, Source *s) {
    float prev = s->sw_ph;
    if (s->sw_rate <= 0) s->sw_rate = 1.0f / rng_range(&s->rng, s->sw_lo, s->sw_hi);
    s->sw_ph += BLOCK_S * s->sw_rate * (0.8f + 0.5f * e->wind);
    if (s->sw_ph >= 1.0f) { s->sw_ph -= 1.0f; s->sw_rate = 1.0f / rng_range(&s->rng, s->sw_lo, s->sw_hi); }
    /* slow rise to a peak at 0.7, faster fall: a wave building then breaking */
    float p = s->sw_ph, v = p < 0.7f ? fm_sinf(p / 0.7f * 1.5708f) : fm_cosf((p - 0.7f) / 0.3f * 1.5708f);
    v = v * v;
    if (s->gk == G_AIR) s->g.air.swell = v;
    e->swell_ui = v;
    if (prev < 0.7f && s->sw_ph >= 0.7f) {
        e->swell_peak = 1;
        if (s->peak) s->peak(e, s, rng_range(&s->rng, s->pan_lo, s->pan_hi), rng_range(&s->rng, s->dist_lo, s->dist_hi), v);
    }
}

static void beh_passby(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    if (!s->pb_on) {
        float rate = s->rate * src_life(e, s);
        if ((rate > 0 && rng_f(&s->rng) < rate * BLOCK_S) || s->t_next < 0) {
            s->t_next = 0;
            s->pb_voice = -1;
            if (s->fire) { var_begin(e, s, 1); s->fire(e, s, 0, 0, 0); var_end(); }   /* the patch starts a HUM voice, sets pb_voice */
            if (s->pb_voice >= 0) {
                h->v[s->pb_voice].target = 1e-3f;      /* (a HUM voice aimed at nothing switches itself off at once:
                                                        * every pass-by was silent until this) */
                s->pb_on = 1; s->pb_t = 0;
                s->pb_len = rng_range(&s->rng, s->pb_len_lo, s->pb_len_hi);
                s->pb_dir = rng_f(&s->rng) < 0.5f ? -1.0f : 1.0f;
                s->user[0] = rng_range(&s->rng, s->dist_lo, s->dist_hi);   /* closest distance */
                s->events++; e->layer_events[s->layer]++;
                engine_post(e, s->layer, s->glyph, -s->pb_dir, s->user[0], 0, (int)s->pb_dir, s->pb_len);
            }
        }
        return;
    }
    HVoice *v = &h->v[s->pb_voice];
    s->pb_t += BLOCK_S;
    float u = s->pb_t / s->pb_len;
    float x = s->pb_dir * (u * 2.0f - 1.0f) * 1.6f;              /* along the path */
    float near = 0.25f + s->user[0];
    float r = fm_sqrtf(x * x + near * near);
    float pan = x / r * 0.95f;
    float dist = s->user[0] + (1.0f - s->user[0]) * (r - near) / (fm_sqrtf(1.6f * 1.6f + near * near) - near) * 0.8f;
    float vr = (x / r) * s->pb_dir * 3.2f / s->pb_len;           /* radial speed, path units/s */
    spatial_set(&v->sp, pan, dist);
    s->ag[MAX_AGENTS - 1].pan = pan; s->ag[MAX_AGENTS - 1].dist = dist;   /* where it is now, for wheels */
    v->pmul = 1.0f / (1.0f + vr * s->pb_doppler);
    float edge = u < 0.15f ? u / 0.15f : u > 0.85f ? (1.0f - u) / 0.15f : 1.0f;
    v->target = s->user[1] * edge;
    if (u >= 1.0f) { hum_release(h, s->pb_voice, 0.5f); s->pb_on = 0; }
}

/* ------------------------------------------------------------------ render */
static void gen_render(Source *s, Bus *b) {
    switch (s->gk) {
    case G_AIR: air_render(&s->g.air, &s->rng, b); break;
    case G_RAIN: rain_render(&s->g.rain, &s->rng, b); break;
    case G_WATER: water_render(&s->g.water, &s->rng, b); break;
    case G_VOICE: voice_render(&s->g.voice, b); break;
    case G_RING: ring_render(&s->g.ring, &s->rng, b); break;
    case G_HUM: hum_render(&s->g.hum, &s->rng, b); break;
    case G_CROWD: crowd_render(&s->g.crowd, &s->rng, b); break;
    }
}

static void conductor(Engine *e) {
    Macros target = e->user;
    if (e->timeline) {                                  /* tools/render's demo timelines */
        for (int i = 0; i < e->ntimeline; i++) if (e->t >= e->timeline[i].at) { target = e->timeline[i].m; e->state_name = e->timeline[i].name; }
        e->user = target;
    } else {
        const PatchDef *pl = (e->patch[L_LANDSCAPE] >= 0) ? &PATCHES[L_LANDSCAPE][e->patch[L_LANDSCAPE]] : 0;
        if (e->ent > 0.02f) {                           /* ENTROPY: scene states change by themselves */
            e->state_left -= BLOCK_S * (0.2f + 2.0f * e->ent);
            if (e->state_left <= 0) {
                int next = rng_f(&e->rng) < 0.35f ? 0 : 1 + rng_int(&e->rng, 3);
                e->state = next == e->state ? (next + 1) % 4 : next;
                e->state_left = rng_range(&e->rng, 90.0f, 240.0f);
            }
        } else e->state = 0;
        if (pl) {
            const StateDef *st = &pl->states[e->state];
            float k = e->ent * 2.0f > 1.0f ? 1.0f : e->ent * 2.0f;
            target.weather = clamp01(target.weather + st->dw * k * (e->wset == WX_EARTH ? 0.9f : 1.0f));   /* (Earth's offsets were set in types) */
            target.activity = clamp01(target.activity + st->da * k);
            e->state_name = st->name;
        }
    }
    float k = BLOCK_S / e->glide_s;
    e->m.weather += (target.weather - e->m.weather) * k;
    e->m.time += (target.time - e->m.time) * k;
    e->m.activity += (target.activity - e->m.activity) * k;
    e->m.entropy += (target.entropy - e->m.entropy) * k * 4.0f;
    derive(e);
    if (e->startle > 0) e->startle -= BLOCK_S;
}

/* patch swaps: the layer fades out (0.25 s), its sources change, it fades back in (0.4 s) */
static void swaps(Engine *e) {
    for (int l = 0; l < NUM_LAYERS; l++) {
        if (e->want[l] != e->patch[l]) {
            if (e->patch[l] < 0) { apply_patch(e, l, e->want[l]); e->fade[l] = 0; continue; }
            e->fade[l] -= BLOCK_S / 0.25f;
            if (e->fade[l] <= 0) { e->fade[l] = 0; apply_patch(e, l, e->want[l]); }
        } else if (e->fade[l] < 1.0f) {
            e->fade[l] += BLOCK_S / 0.4f; if (e->fade[l] > 1.0f) e->fade[l] = 1.0f;
        }
    }
}

/* a key pressed: one event from the layer, now (a scatter fires, a bird sings, a vehicle sets off) */
static void oneshots(Engine *e) {
    for (int l = 0; l < NUM_LAYERS; l++) {
        if (!e->oneshot[l]) continue;
        e->oneshot[l] = 0;
        int cand[MAX_SOURCES], n = 0;
        for (int i = 0; i < e->nsrc; i++) {
            Source *s = &e->src[i];
            if (s->layer != l) continue;
            if (((s->beh == B_SCATTER || s->beh == B_RARE) && s->fire) || (s->beh == B_PHRASE && s->nag) ||
                (s->beh == B_PASSBY && !s->pb_on) || (s->beh == B_CHORUS && s->nag)) cand[n++] = i;
        }
        if (!n) continue;
        Source *s = &e->src[cand[rng_int(&e->rng, n)]];
        if (s->beh == B_SCATTER || s->beh == B_RARE) fire(e, s, rng_range(&s->rng, s->pan_lo, s->pan_hi), rng_range(&s->rng, s->dist_lo, s->dist_hi), 1);
        else if (s->beh == B_PHRASE) { Agent *a = &s->ag[rng_int(&s->rng, s->nag)]; if (!a->singing) { phrase_start(s, a, &s->rng, e->ent); a->next = 0; engine_post(e, l, s->glyph, a->pan, a->dist, 0, 0, 0); } }
        else if (s->beh == B_CHORUS) for (int i = 0; i < s->nag; i++) s->ag[i].ph = 0.97f + 0.03f * rng_f(&s->rng);
        else s->t_next = -1;                         /* PASS-BY: set off on the next block */
    }
}

/* one layer's sources, one OUT_BLOCK, into its bus */
static void layer_render(Engine *e, int layer, Bus *b) {
    memset(b, 0, sizeof *b);
    for (int i = 0; i < e->nsrc; i++) {
        Source *s = &e->src[i];
        if (s->layer != layer) continue;
        var_drift(e, s);
        if (s->block) s->block(e, s);
        switch (s->beh) {
        case B_SCATTER: beh_scatter(e, s, 0); break;
        case B_RARE: beh_scatter(e, s, 1); break;
        case B_PHRASE: beh_phrase(e, s); break;
        case B_CHORUS: beh_chorus(e, s); break;
        case B_SWELL: beh_swell(e, s); break;
        case B_PASSBY: beh_passby(e, s); break;
        default: break;
        }
        gen_render(s, b);
    }
}

/* the conductor's block: patch swaps, macros, one-shots, PLAY's fade; then each layer's gain for this block */
static void control(Engine *e, float *g) {
    swaps(e);
    conductor(e);
    oneshots(e);
    float target = e->playing ? 1.0f : 0.0f;          /* PLAY: a fade */
    e->master_fade += (target - e->master_fade) * (BLOCK_S / 1.0f);
    for (int l = 0; l < NUM_LAYERS; l++) g[l] = e->level[l] * e->level[l] * e->fade[l] * e->master_fade * 1.3f;
}

static void phaser_run(Engine *e, float *L, float *R) {
    float *io[2] = { L, R };
    e->phaser.ph += BLOCK_S / 8.0f; if (e->phaser.ph >= 1.0f) e->phaser.ph -= 1.0f;   /* one sweep in 8 s */
    for (int c = 0; c < 2; c++) {
        float lfo = 0.5f + 0.5f * fm_sin_turns(e->phaser.ph + c * 0.25f);              /* the sides a quarter apart */
        float f = 300.0f * fm_exp2f(lfo * 3.06f);                                          /* 300 Hz .. 2.5 kHz */
        float t = fm_tanf(3.14159265f * f / OUT_SR), a = (1.0f - t) / (1.0f + t);
        float *z = e->phaser.z[c], fb = e->phaser.fb[c];
        for (int i = 0; i < OUT_BLOCK; i++) {
            float x = io[c][i], y = x + fb * 0.35f;
            for (int k = 0; k < 4; k++) { float o = -a * y + z[k]; z[k] = y + a * o; y = o; }
            fb = y;
            io[c][i] = x * 0.8f + y * 0.45f;                                             /* light: notches, not a wash */
        }
        e->phaser.fb[c] = fb + 1e-18f;
    }
}

static void mix(Engine *e, Bus *bus, const float *gains, float *outL, float *outR) {
    if (e->patch[L_WEATHER] == 7) phaser_run(e, bus[L_WEATHER].L, bus[L_WEATHER].R);   /* ALIEN's ion storm */
    float sL[OUT_BLOCK] = { 0 }, sR[OUT_BLOCK] = { 0 }, rL[OUT_BLOCK], rR[OUT_BLOCK];
    for (int i = 0; i < OUT_BLOCK; i++) { outL[i] = 0; outR[i] = 0; }
    for (int l = 0; l < NUM_LAYERS; l++) {
        Bus *b = &bus[l]; float g = gains[l];
        float ss = 0;
        for (int i = 0; i < OUT_BLOCK; i++) {
            float ol = b->L[i] * g, orr = b->R[i] * g;
            outL[i] += ol; outR[i] += orr;
            sL[i] += b->sL[i] * g; sR[i] += b->sR[i] * g;
            ss += ol * ol + orr * orr;
        }
        e->layer_ss[l] += ss;
        float rms = fm_sqrtf(ss / (2.0f * OUT_BLOCK));
        float lvl = rms > 1e-5f ? (fm_log2f(rms) * 6.02f + 50.0f) / 42.0f : 0.0f;   /* -50 .. -8 dBFS -> 0 .. 1 */
        lvl = clamp01(lvl);
        e->meter[l] += (lvl - e->meter[l]) * (lvl > e->meter[l] ? 0.3f : 0.03f);
    }
    if (e->muffle > 0.01f) {   /* snow and fog: dull the whole scene and push it into the reverb */
        float fc = 16000.0f * fm_powf(0.12f, e->muffle);
        for (int c = 0; c < 2; c++) for (int k = 0; k < 2; k++) op_set(&e->muf[c][k], fc);
        for (int i = 0; i < OUT_BLOCK; i++) {
            outL[i] = op_run(&e->muf[0][1], op_run(&e->muf[0][0], outL[i])) * (1.0f - 0.25f * e->muffle);
            outR[i] = op_run(&e->muf[1][1], op_run(&e->muf[1][0], outR[i])) * (1.0f - 0.25f * e->muffle);
            sL[i] *= 1.0f + e->muffle; sR[i] *= 1.0f + e->muffle;
        }
    } else for (int c = 0; c < 2; c++) for (int k = 0; k < 2; k++) e->muf[c][k].z = c ? outR[OUT_BLOCK - 1] : outL[OUT_BLOCK - 1];
    reverb_block(&e->rv, sL, sR, rL, rR);
    for (int i = 0; i < OUT_BLOCK; i++) {
        float l = outL[i] + rL[i], r = outR[i] + rR[i];
        e->dc[0] += 0.0035f * (l - e->dc[0]); e->dc[1] += 0.0035f * (r - e->dc[1]);   /* sub-sonic blocker */
        outL[i] = l - e->dc[0]; outR[i] = r - e->dc[1];
    }
}

void engine_render(Engine *e, float *outL, float *outR) {
    float g[NUM_LAYERS];
    e->swell_peak = 0;
    control(e, g);
    for (int l = 0; l < NUM_LAYERS; l++) layer_render(e, l, &e->bus[l]);
    mix(e, e->bus, g, outL, outR);
    e->t += BLOCK_S;
}

static uint32_t now(Engine *e) { return e->clock ? e->clock() : 0; }
static void cost_add(Engine *e, int k, uint32_t t0) { if (e->clock) e->cost[k] += ((float)(now(e) - t0) - e->cost[k]) * 0.05f; }

void engine_block_begin(Engine *e, int nsub) {
    g_var_core = e->core;
    e->nsub = nsub > ENGINE_SUB ? ENGINE_SUB : nsub < 1 ? 1 : nsub;
    e->swell_peak = 0;
    for (int k = 0; k < e->nsub; k++) { control(e, e->xg[k]); e->t += BLOCK_S; }
}

void engine_block_layer(Engine *e, int layer) {
    uint32_t t0 = now(e);
    for (int k = 0; k < e->nsub; k++) layer_render(e, layer, &e->xb[k][layer]);
    cost_add(e, layer, t0);
}

void engine_block_mix(Engine *e, float *outL, float *outR) {
    uint32_t t0 = now(e);
    for (int k = 0; k < e->nsub; k++) mix(e, e->xb[k], e->xg[k], outL + k * OUT_BLOCK, outR + k * OUT_BLOCK);
    cost_add(e, NUM_LAYERS, t0);
}

/* REC: new macros. The conductor glides to them, so nothing jumps. */
void engine_randomise(Engine *e) {
    e->timeline = 0;
    e->user.weather = weather_value(e->wset, rng_int(&e->rng, weather_count(e->wset)));
    e->user.time = rng_f(&e->rng);
    e->user.activity = rng_range(&e->rng, 0.2f, 0.9f);
    e->user.entropy = rng_range(&e->rng, 0.1f, 0.8f);
    e->glide_s = 4.0f;
}

/* ------------------------------------------------------------------ reverb */
void reverb_init(Reverb *rv, float size, float rt60, float damp_hz, float predelay_ms, float mix) {
    static const int base[RV_LINES] = { 1031, 1327, 1523, 1801, 2063, 2357, 2731, 3119 };
    static const int apb[4] = { 142, 107, 379, 277 };
    memset(rv, 0, sizeof *rv);
    if (size <= 0) size = 1.0f;
    for (int i = 0; i < RV_LINES; i++) {
        int n = (int)(base[i] * size); if (n >= RV_MAX) n = RV_MAX - 1; if (n < 64) n = 64;
        rv->len[i] = n;
        rv->g[i] = fm_powf(10.0f, -3.0f * n / (rt60 * OUT_SR));
        op_set(&rv->damp[i], damp_hz);
    }
    for (int i = 0; i < 4; i++) { int n = (int)(apb[i] * size); if (n >= RV_AP) n = RV_AP - 1; if (n < 8) n = 8; rv->aplen[i] = n; }
    rv->prelen = (int)(predelay_ms * 0.001f * OUT_SR); if (rv->prelen > 2047) rv->prelen = 2047; if (rv->prelen < 1) rv->prelen = 1;
    rv->mix = mix;
}

static inline int16_t to16(float x) { x *= 16384.0f; if (x > 32767.0f) x = 32767.0f; if (x < -32768.0f) x = -32768.0f; return (int16_t)x; }

void reverb_block(Reverb *rv, const float *inL, const float *inR, float *outL, float *outR) {
    for (int i = 0; i < OUT_BLOCK; i++) {
        float in[2];
        for (int c = 0; c < 2; c++) { in[c] = rv->pre[c][rv->prepos]; rv->pre[c][rv->prepos] = c ? inR[i] : inL[i]; }
        rv->prepos = (rv->prepos + 1) % rv->prelen;
        for (int a = 0; a < 4; a++) {                   /* input diffusion */
            for (int c = 0; c < 2; c++) {
                int p = rv->appos[a];
                float d = rv->ap[a][c][p];
                float y = -0.6f * in[c] + d;
                rv->ap[a][c][p] = in[c] + 0.6f * y;
                in[c] = y;
            }
            rv->appos[a] = (rv->appos[a] + 1) % rv->aplen[a];
        }
        float o[RV_LINES], h[RV_LINES];
        for (int k = 0; k < RV_LINES; k++) { o[k] = op_run(&rv->damp[k], rv->buf[k][rv->pos[k]] * (1.0f / 16384.0f)) * rv->g[k]; h[k] = o[k]; }
        for (int len = 1; len < RV_LINES; len <<= 1)    /* Hadamard mixing */
            for (int j = 0; j < RV_LINES; j += len << 1)
                for (int k = j; k < j + len; k++) { float a = h[k], b = h[k + len]; h[k] = a + b; h[k + len] = a - b; }
        for (int k = 0; k < RV_LINES; k++) {
            float x = h[k] * 0.35355339f + ((k & 1) ? in[1] : in[0]) * ((k & 2) ? -0.5f : 0.5f);
            rv->buf[k][rv->pos[k]] = to16(x);
            rv->pos[k] = (rv->pos[k] + 1) % rv->len[k];
        }
        outL[i] = (o[0] - o[2] + o[4] - o[6]) * rv->mix;
        outR[i] = (o[1] - o[3] + o[5] - o[7]) * rv->mix;
    }
}

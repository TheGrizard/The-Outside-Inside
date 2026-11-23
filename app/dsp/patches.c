/* Outside: the patch libraries. Four layers, nine patches each; an environment is the same index in every layer.
 * Each patch's build() adds its sources to its layer. Sources never point at each other: shared state goes through
 * the Engine (gust, swell_peak, rain ...), so a layer can be swapped on its own. */
#include "engine.h"

#define BLK ((float)OUT_BLOCK / OUT_SR)

/* ======================================================================= shared pieces */
static const Material MAT_WOOD = { 3, { 1, 2.6f, 4.3f }, { 0.03f, 0.02f, 0.012f }, { 1, 0.6f, 0.35f }, 0.8f, 2500 };
static const Material MAT_TWIG = { 4, { 1, 1.9f, 3.3f, 4.7f }, { 0.012f, 0.01f, 0.008f, 0.006f }, { 1, 0.8f, 0.6f, 0.4f }, 1.5f, 4000 };
static const Material MAT_STONE = { 3, { 1, 1.73f, 2.9f }, { 0.05f, 0.03f, 0.02f }, { 1, 0.5f, 0.3f }, 1.0f, 1500 };
static const Material MAT_GLASS = { 6, { 1, 2.32f, 4.25f, 6.63f, 9.38f, 13.1f }, { 3.0f, 1.8f, 1.1f, 0.7f, 0.4f, 0.25f }, { 1, 0.6f, 0.45f, 0.3f, 0.2f, 0.12f }, 0.05f, 6000 };
static const Material MAT_POD = { 3, { 1, 1.5f, 2.76f }, { 0.5f, 0.3f, 0.15f }, { 1, 0.4f, 0.2f }, 0.2f, 800 };
static const Material MAT_GIRDER = { 4, { 1, 2.756f, 5.404f, 8.933f }, { 1.8f, 1.2f, 0.8f, 0.5f }, { 1, 0.7f, 0.5f, 0.3f }, 0.4f, 2000 };
static const Material MAT_CHAIN = { 3, { 1, 1.6f, 2.3f }, { 0.05f, 0.04f, 0.03f }, { 1, 0.7f, 0.5f }, 0.6f, 5000 };
static const Material MAT_RAIL = { 3, { 1, 2.3f, 3.9f }, { 0.06f, 0.04f, 0.03f }, { 1, 0.6f, 0.4f }, 1.2f, 1800 };
/* the factory's metals: dead, heavy, noisy. A press's slam (a bang of noise, a ring gone in a tenth of a second),
 * scrap and sheet (clangy, short), a beam (a creak's ping, not a gong's) */
static const Material MAT_SLAM = { 5, { 1, 1.38f, 1.93f, 2.71f, 3.62f }, { 0.07f, 0.05f, 0.04f, 0.03f, 0.025f }, { 1, 0.8f, 0.6f, 0.45f, 0.3f }, 3.0f, 1400, 35 };
static const Material MAT_SCRAP = { 6, { 1, 1.41f, 2.13f, 2.87f, 3.91f, 5.3f }, { 0.16f, 0.12f, 0.1f, 0.08f, 0.06f, 0.045f }, { 1, 0.9f, 0.8f, 0.6f, 0.5f, 0.35f }, 2.0f, 3500, 15 };
static const Material MAT_BEAM = { 4, { 1, 2.756f, 5.404f, 8.933f }, { 0.35f, 0.25f, 0.18f, 0.12f }, { 1, 0.7f, 0.5f, 0.3f }, 1.0f, 2500, 8 };
/* a church bell (measured from a recording of one tolling): its partials at 0.75, 1, 1.31, 1.53 and 2.04 times the
 * strike note, and the hum an octave down, ringing for seconds */
static const Material MAT_CHURCH = { 6, { 0.5f, 0.745f, 1.0f, 1.31f, 1.53f, 2.04f }, { 4.5f, 2.6f, 3.2f, 2.2f, 2.0f, 1.4f }, { 0.45f, 0.5f, 1, 0.6f, 0.7f, 0.6f }, 0.6f, 2000, 4 };
static const Material MAT_BELL = { 6, { 1, 2.0f, 2.38f, 3.0f, 4.0f, 5.04f }, { 3.0f, 2.2f, 1.8f, 1.4f, 1.0f, 0.7f }, { 1, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f }, 0.3f, 3000 };

static void set_surfaces(Rain *r, const float f[4], const float q[4], const float w[4], const float g[4]) {
    memcpy(r->surf_f, f, 4 * sizeof(float)); memcpy(r->surf_q, q, 4 * sizeof(float));
    memcpy(r->surf_w, w, 4 * sizeof(float)); memcpy(r->surf_g, g, 4 * sizeof(float));
}

/* a short run of strikes (woodpecker, pods, chains, falling stone): user[2..5] and ag[0] */
static void roll_start(Source *s, int hits, float interval, float freq, float pan, float dist, float amp) {
    s->user[2] = (float)hits; s->user[3] = 0; s->user[4] = freq; s->user[5] = interval;
    s->ag[0].pan = pan; s->ag[0].dist = dist; s->ag[0].pitch = amp;
}
static void roll_step(Source *s, const Material *m, float bright, float decay_mul, float amp_fall) {
    if (s->user[2] <= 0) return;
    s->user[3] -= BLK;
    if (s->user[3] > 0) return;
    ring_strike(&s->g.ring, &s->rng, m, s->user[4] * rng_range(&s->rng, 0.98f, 1.02f), s->ag[0].pitch, bright, decay_mul, s->ag[0].pan, s->ag[0].dist);
    s->ag[0].pitch *= amp_fall;
    s->user[2] -= 1;
    s->user[3] = s->user[5] * rng_range(&s->rng, 0.9f, 1.1f);
}

/* rain, drizzle, downpour and snow from one RAIN source */
static void weather_rain(Engine *e, Rain *r, float base) {
    r->density = fm_powf(e->rain + 1e-6f, 1.3f) * base + e->snow * base * 0.12f;
    r->size = e->snow > 0.5f ? 0.0f : e->drop;
}

/* the wind every weather patch starts with: it also sets the engine's gust for everyone else */
static Source *add_wind(Engine *e, const char *name, float colour, int bandpass, float cutoff, float q, float howl_freq, float howl_q, float send) {
    Source *s = engine_add(e, name, L_WEATHER, G_AIR, B_BED);
    Air *a = &s->g.air;
    a->colour = colour; a->bandpass = bandpass; a->cutoff = cutoff; a->q = q; a->gust_depth = 0.8f;
    a->howl_freq = howl_freq; a->howl_q = howl_q; a->width = 0.95f; a->send = send;
    return s;
}
static void wind_follow(Engine *e, Source *s, float lvl0, float lvl1, float howl0, float howl1) {
    Air *a = &s->g.air; a->weather = e->wind; a->level = lvl0 + lvl1 * e->wind; a->howl = howl0 + howl1 * e->howl; e->gust = a->gust;
}

static void thunder_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)x;
    air_thunder(&s->g.air, &s->rng, pan, dist, s->user[0]);
    e->startle = rng_range(&s->rng, 10.0f, 20.0f);   /* coupling: thunder silences the animals */
}
static void thunder_block(Engine *e, Source *s) {
    s->rate = 0.12f * e->thunder / (0.3f + 1.4f * e->ent);   /* a thunderstorm thunders, whatever ENTROPY */
    s->cooldown = 6.0f + 10.0f * (1.0f - e->thunder);
    s->dist_lo = 0.15f + 0.6f * (1.0f - e->thunder_near); s->dist_hi = s->dist_lo + 0.25f;
    if (s->user[1] > 0) { s->dist_lo = s->user[1]; s->dist_hi = s->user[1] + 0.08f; }   /* fixed far (inside a cave) */
}
static void add_thunder(Engine *e, float level, float zap, float far) {
    Source *s = engine_add(e, "thunder", L_WEATHER, G_AIR, B_RARE);
    s->g.air.no_bed = 1; s->g.air.level = level; s->user[0] = zap; s->user[1] = far;
    s->fire = thunder_fire; s->block = thunder_block; s->glyph = GL_FLASH;
}

/* ======================================================================= species */
static const Syllable ROBIN[] = {
    { 3200, 4100, CURVE_LIN, 0.07f, 0.3f, 0, 0, 0.05f, 0, 0, 0, 0, 1 },
    { 4600, 3000, CURVE_EXP, 0.09f, 0.2f, 0, 0, 0.05f, 0, 0, 0, 0, 1 },
    { 2700, 3600, CURVE_SWOOP, 0.12f, 0.3f, 0, 0, 0.08f, 0, 0, 0, 0, 1 },
    { 3800, 3800, CURVE_HOLD, 0.22f, 0.15f, 38, 0.9f, 0.05f, 0, 0, 0, 0, 0.8f },
    { 5200, 4200, CURVE_LIN, 0.05f, 0.3f, 0, 0, 0.0f, 0, 0, 0, 0, 0.8f },
};
static const Syllable BLACKBIRD[] = {
    { 1900, 2400, CURVE_SWOOP, 0.18f, 0.25f, 0, 0, 0.25f, 0, 0, 0, 0, 1 },
    { 2300, 1800, CURVE_EXP, 0.22f, 0.2f, 0, 0, 0.2f, 0, 0, 7, 0.3f, 1 },
    { 2100, 2600, CURVE_LIN, 0.12f, 0.25f, 0, 0, 0.15f, 0, 0, 0, 0, 1 },
    { 2800, 2200, CURVE_EXP, 0.10f, 0.25f, 0, 0, 0.1f, 0, 0, 0, 0, 0.9f },
    { 3400, 3000, CURVE_LIN, 0.06f, 0.3f, 0, 0, 0.0f, 0, 0, 0, 0, 0.6f },
};
static const Syllable CHIFFCHAFF[] = { { 5600, 4800, CURVE_EXP, 0.06f, 0.2f, 0, 0, 0, 0, 0, 0, 0, 1 }, { 4500, 3900, CURVE_EXP, 0.06f, 0.2f, 0, 0, 0, 0, 0, 0, 0, 1 } };
static const Syllable WREN[] = {
    { 4000, 6000, CURVE_LIN, 0.03f, 0.3f, 0, 0, 0, 0, 0, 0, 0, 1 },
    { 6200, 4500, CURVE_LIN, 0.03f, 0.3f, 0, 0, 0, 0, 0, 0, 0, 1 },
    { 5000, 5000, CURVE_HOLD, 0.40f, 0.1f, 55, 1.0f, 0, 0, 0, 0, 0, 0.9f },
};
static const Syllable OWL[] = { { 420, 380, CURVE_LIN, 0.5f, 0.35f, 0, 0, 0.12f, 0, 0, 0, 0, 1 }, { 400, 360, CURVE_LIN, 0.9f, 0.25f, 0, 0, 0.10f, 0, 0, 5, 0.15f, 1 } };
static const Syllable CRICKET = { 4600, 4600, CURVE_HOLD, 0.12f, 0.1f, 32, 1.0f, 0, 0, 0, 0, 0, 0.35f };
static const Species SP_ROBIN = { ROBIN, 5, { "ABCD", "CEBA-D", "AEC", "DBE" }, 4, 0.08f, 0.12f, 0.045f, 2.5f, 6.0f, 1, 3, 0.55f };
static const Species SP_BLACKBIRD = { BLACKBIRD, 5, { "AB-C", "ACB-EE", "BADE", "A-BCEE" }, 4, 0.06f, 0.1f, 0.09f, 4.0f, 9.0f, 1, 2, 0.6f };
static const Species SP_CHIFFCHAFF = { CHIFFCHAFF, 2, { "ABABABBA", "ABBABAB" }, 2, 0.04f, 0.08f, 0.28f, 5.0f, 10.0f, 1, 2, 0.45f };
static const Species SP_WREN = { WREN, 3, { "ABABC", "AABBCAB" }, 2, 0.05f, 0.1f, 0.015f, 6.0f, 12.0f, 1, 1, 0.5f };
static const Species SP_OWL = { OWL, 2, { "A--B", "A-AB" }, 2, 0.03f, 0.05f, 0.4f, 12.0f, 25.0f, 1, 1, 0.5f };

static const Syllable SPARROW[] = { { 3900, 3300, CURVE_LIN, 0.06f, 0.2f, 0, 0, 0.1f, 0, 0, 0, 0, 1 }, { 4300, 4300, CURVE_HOLD, 0.05f, 0.2f, 0, 0, 0.1f, 0, 0, 0, 0, 0.8f } };
static const Species SP_SPARROW = { SPARROW, 2, { "AABA", "ABAB", "AAA-B" }, 3, 0.08f, 0.15f, 0.08f, 1.5f, 5.0f, 1, 3, 0.45f };
static const Syllable PIGEON[] = { { 520, 430, CURVE_EXP, 0.45f, 0.3f, 18, 0.5f, 0.35f, 0, 0, 0, 0, 1 }, { 430, 470, CURVE_LIN, 0.30f, 0.3f, 14, 0.4f, 0.3f, 0, 0, 0, 0, 0.8f } };
static const Species SP_PIGEON = { PIGEON, 2, { "A-B", "AB-B", "A--AB" }, 3, 0.05f, 0.1f, 0.15f, 5.0f, 12.0f, 1, 2, 0.5f };
static const Syllable CROW[] = { { 1100, 950, CURVE_LIN, 0.35f, 0.15f, 0, 0, 0.4f, 1.0f, 3.0f, 0, 0, 0.8f } };
static const Species SP_CROW = { CROW, 1, { "A-A-A", "AA", "A-AA" }, 3, 0.08f, 0.1f, 0.25f, 10.0f, 25.0f, 1, 1, 0.5f };

static const Syllable TROPICAL[] = {
    { 2200, 3200, CURVE_LIN, 0.12f, 0.3f, 0, 0, 0.05f, 0, 0, 0, 0, 1 },
    { 1800, 1800, CURVE_HOLD, 0.05f, 0.05f, 0, 0, 0.5f, 0, 0, 0, 0, 0.9f },
    { 600, 500, CURVE_LIN, 0.18f, 0.15f, 0, 0, 0.3f, 1.0f, 2.5f, 0, 0, 0.8f },
    { 3500, 3500, CURVE_HOLD, 0.30f, 0.1f, 45, 0.9f, 0, 0, 0, 0, 0, 0.7f },
    { 1200, 4200, CURVE_EXP, 0.10f, 0.3f, 0, 0, 0, 0, 0, 0, 0, 0.9f },
};
static const Species SP_TROPICAL = { TROPICAL, 5, { "AAE", "BB-B-B", "CC-C", "DAE", "EEB" }, 5, 0.12f, 0.12f, 0.07f, 1.5f, 5.0f, 1, 3, 0.5f };
static const Syllable CICADA = { 5200, 5200, CURVE_HOLD, 1.5f, 0.3f, 170, 0.8f, 0, 0, 0, 0, 0, 0.18f };
static const Syllable FROG = { 280, 240, CURVE_LIN, 0.14f, 0.1f, 30, 0.9f, 0.45f, 0, 0, 0, 0, 0.7f };
static const Syllable TREEFROG = { 2800, 3000, CURVE_LIN, 0.05f, 0.2f, 0, 0, 0.1f, 0, 0, 0, 0, 0.4f };
static const Syllable HERON[] = { { 850, 650, CURVE_LIN, 0.4f, 0.1f, 0, 0, 0.3f, 1.0f, 3.5f, 0, 0, 0.9f } };
static const Species SP_HERON = { HERON, 1, { "A", "A--A" }, 2, 0.05f, 0.05f, 0.5f, 15.0f, 35.0f, 1, 1, 0.55f };
static const Syllable GULL[] = { { 1500, 1150, CURVE_SWOOP, 0.35f, 0.15f, 0, 0, 0.3f, 1.0f, 2.2f, 0, 0, 1 }, { 1300, 1000, CURVE_LIN, 0.12f, 0.15f, 0, 0, 0.3f, 1.0f, 2.0f, 0, 0, 0.8f } };
static const Species SP_GULL = { GULL, 2, { "ABBBBB", "A-A", "AA-BBB" }, 3, 0.1f, 0.12f, 0.08f, 4.0f, 10.0f, 1, 2, 0.5f };
static const Syllable OYSTER[] = { { 3000, 2800, CURVE_LIN, 0.06f, 0.2f, 0, 0, 0.1f, 0, 0, 0, 0, 1 } };
static const Species SP_OYSTER = { OYSTER, 1, { "AAAAAA", "AAAA-AAAA" }, 2, 0.05f, 0.1f, 0.06f, 6.0f, 15.0f, 1, 2, 0.4f };
static const Syllable WHALE[] = { { 180, 420, CURVE_SWOOP, 2.5f, 0.3f, 0, 0, 0.3f, 0.5f, 1.2f, 3, 0.3f, 1 }, { 400, 150, CURVE_EXP, 3.0f, 0.25f, 0, 0, 0.3f, 0.5f, 1.5f, 2, 0.2f, 1 } };
static const Species SP_WHALE = { WHALE, 2, { "A-B", "AB" }, 2, 0.15f, 0.1f, 1.0f, 30.0f, 60.0f, 1, 1, 0.6f };
static const Syllable SIGNAL[] = { { 200, 600, CURVE_SWOOP, 2.0f, 0.3f, 0, 0, 0.2f, 0.25f, 2.5f, 5, 0.5f, 1 }, { 900, 300, CURVE_EXP, 1.5f, 0.2f, 0, 0, 0, 2.7f, 1.5f, 0, 0, 0.8f } };
static const Species SP_SIGNAL = { SIGNAL, 2, { "A-B", "B-A", "A--A" }, 3, 0.2f, 0.15f, 0.8f, 12.0f, 28.0f, 1, 1, 0.5f };
static const Syllable PULSAR = { 2200, 2200, CURVE_HOLD, 0.03f, 0.3f, 0, 0, 0, 0, 0, 0, 0, 0.35f };
static const Syllable BAT[] = { { 11000, 7000, CURVE_EXP, 0.02f, 0.2f, 0, 0, 0, 0, 0, 0, 0, 1 }, { 9500, 6500, CURVE_EXP, 0.03f, 0.2f, 0, 0, 0, 0, 0, 0, 0, 0.8f } };
static const Species SP_BAT = { BAT, 2, { "AAAAB", "ABAAAA", "AABA" }, 3, 0.1f, 0.15f, 0.03f, 4.0f, 12.0f, 1, 3, 0.35f };
static const Syllable CAVE_CRICKET = { 4200, 4200, CURVE_HOLD, 0.08f, 0.1f, 30, 1.0f, 0, 0, 0, 0, 0, 0.3f };
static const Syllable WARBLER[] = {
    { 500, 2200, CURVE_SWOOP, 0.35f, 0.3f, 0, 0, 0, 1.5f, 1.2f, 6, 0.6f, 1 },
    { 2400, 700, CURVE_EXP, 0.28f, 0.2f, 0, 0, 0, 1.5f, 1.6f, 0, 0, 1 },
    { 900, 1300, CURVE_LIN, 0.5f, 0.3f, 9, 0.8f, 0, 2.01f, 0.8f, 0, 0, 0.9f },
    { 1800, 1800, CURVE_HOLD, 0.12f, 0.2f, 0, 0, 0, 3.3f, 2.0f, 0, 0, 0.8f },
};
static const Syllable CALLER[] = { { 180, 420, CURVE_SWOOP, 0.8f, 0.3f, 0, 0, 0.3f, 0.5f, 1.5f, 0, 0, 1 }, { 380, 160, CURVE_EXP, 1.0f, 0.25f, 0, 0, 0.3f, 0.5f, 2.0f, 3, 0.4f, 1 } };
static const Syllable SPORE = { 1300, 1700, CURVE_EXP, 0.07f, 0.2f, 0, 0, 0, 3.53f, 1.2f, 0, 0, 0.5f };
static const Species SP_WARBLER = { WARBLER, 4, { "AB-C", "DDA-B", "CAD", "A-A-B" }, 4, 0.25f, 0.15f, 0.12f, 3.0f, 8.0f, 1, 2, 0.5f };
static const Species SP_CALLER = { CALLER, 2, { "A-B", "AB" }, 2, 0.15f, 0.1f, 0.5f, 10.0f, 20.0f, 1, 1, 0.6f };
static const Syllable BEACON = { 880, 880, CURVE_HOLD, 0.2f, 0.05f, 0, 0, 0.2f, 1.5f, 0.5f, 0, 0, 0.5f };
static const Syllable BEEPER = { 1150, 1150, CURVE_HOLD, 0.5f, 0.02f, 0, 0, 0.15f, 0, 0, 0, 0, 0.6f };
static const Species SP_BEEPER = { &BEEPER, 1, { "A-A-A-A-A-A-A-A" }, 1, 0, 0, 0.25f, 25.0f, 60.0f, 1, 1, 0.5f };
static const Syllable TELEMETRY = { 1760, 1760, CURVE_HOLD, 0.08f, 0.1f, 0, 0, 0.1f, 0, 0, 0, 0, 0.5f };

/* the meadow's birds: a skylark's endless warble from high up, a yellowhammer's "little bit of bread and no
 * cheese" (a run of short notes and a long one), a wood pigeon's soft five-note coo */
static const Syllable SKYLARK[] = {
    { 3600, 4400, CURVE_LIN, 0.05f, 0.3f, 0, 0, 0.05f, 0, 0, 0, 0, 1 },
    { 4800, 3900, CURVE_EXP, 0.045f, 0.3f, 0, 0, 0.05f, 0, 0, 0, 0, 0.9f },
    { 4200, 4200, CURVE_HOLD, 0.12f, 0.1f, 45, 0.9f, 0, 0, 0, 0, 0, 0.8f },
    { 3100, 5200, CURVE_SWOOP, 0.07f, 0.3f, 0, 0, 0.05f, 0, 0, 0, 0, 0.9f },
    { 5600, 4600, CURVE_LIN, 0.035f, 0.3f, 0, 0, 0, 0, 0, 0, 0, 0.8f },
    { 3900, 3400, CURVE_EXP, 0.06f, 0.3f, 0, 0, 0.05f, 0, 0, 0, 0, 0.9f },
};
static const Species SP_SKYLARK = { SKYLARK, 6, { "ABDCEAFBDEACFBDE", "DEABCFDABECAFDBE", "CABDEFACBDEFAC", "ABCDEFEDCBAFDBCE", "FDCABEDFACEB" }, 5,
                                    0.06f, 0.1f, 0.012f, 0.4f, 1.6f, 3, 6, 0.38f };
static const Syllable YELLOWHAMMER[] = {
    { 5800, 5300, CURVE_EXP, 0.07f, 0.25f, 0, 0, 0.05f, 0, 0, 0, 0, 1 },
    { 4600, 4400, CURVE_LIN, 0.45f, 0.15f, 0, 0, 0.05f, 0, 0, 0, 0, 0.9f },
    { 5200, 4300, CURVE_EXP, 0.4f, 0.15f, 0, 0, 0.05f, 0, 0, 0, 0, 0.9f },
};
static const Species SP_YELLOWHAMMER = { YELLOWHAMMER, 3, { "AAAAAAAB", "AAAAAAAAAC", "AAAAAAB" }, 3, 0.05f, 0.08f, 0.06f, 6.0f, 12.0f, 2, 4, 0.45f };
static const Syllable WOODPIGEON[] = {
    { 470, 440, CURVE_EXP, 0.28f, 0.35f, 0, 0, 0.25f, 0, 0, 0, 0, 0.8f },
    { 510, 455, CURVE_EXP, 0.5f, 0.3f, 0, 0, 0.3f, 0, 0, 0, 0, 1 },
};
static const Species SP_WOODPIGEON = { WOODPIGEON, 2, { "ABA-AA-A", "ABA-AA-AB", "ABA-AA" }, 3, 0.04f, 0.06f, 0.12f, 15.0f, 35.0f, 2, 4, 0.5f };
static const Syllable GRASSHOPPER = { 6800, 6800, CURVE_HOLD, 1.3f, 0.35f, 24, 1.0f, 0, 1.41f, 1.2f, 0, 0, 0.16f };

/* ======================================================================= WEATHER */
static void city_wind(Engine *e, Source *s) { wind_follow(e, s, 0.12f, 0.45f, 0.1f, 0.4f); }
static void city_rain(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 2200.0f); }
static void w_city(Engine *e) {
    Source *s = add_wind(e, "street wind", 1.1f, 1, 800, 0.6f, 600, 10, 0.3f); s->block = city_wind;
    s = engine_add(e, "rain on roofs", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.39f; r->hiss = 0; r->click = 0; r->send = 0.3f; r->width = 0.95f;
      r->wash = 1.0f; r->wash_white = 0.8f; r->wash_hp = 500.0f; r->wash_lp = 9000.0f; r->tone = 6500.0f; r->var = 0.35f;   r->drops = 0.5f; /* streets: smooth white hiss */
      static const float f[4] = { 1400, 2600, 800, 4200 }, q[4] = { 1.8f, 2, 1.5f, 2.5f }, w[4] = { 2, 2, 1.5f, 1 }, g[4] = { 1, 0.9f, 0.7f, 0.6f }; set_surfaces(r, f, q, w, g); }
    s->block = city_rain;
    add_thunder(e, 0.8f, 0, 0);
}

static void jungle_wind(Engine *e, Source *s) { wind_follow(e, s, 0.1f, 0.4f, 0.05f, 0.3f); }
static void jungle_rain(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 3200.0f); }
static void w_jungle(Engine *e) {
    Source *s = add_wind(e, "humid air", 1.3f, 0, 1500, 0.7f, 500, 8, 0.35f); s->g.air.gust_depth = 0.5f; s->block = jungle_wind;
    s = engine_add(e, "rain on leaves", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.328f; r->hiss = 0; r->click = 0; r->send = 0.4f; r->width = 1.0f;
      r->wash = 1.1f; r->wash_white = 0.4f; r->wash_hp = 250.0f; r->wash_lp = 6000.0f; r->tone = 4500.0f; r->var = 0.4f;  r->drops = 0.5f; /* the canopy: soft and dark */
      static const float f[4] = { 1900, 2900, 1100, 600 }, q[4] = { 1.6f, 2, 1.6f, 1.2f }, w[4] = { 3, 2, 2, 1 }, g[4] = { 1, 1, 0.9f, 0.6f }; set_surfaces(r, f, q, w, g); }
    s->block = jungle_rain;
    add_thunder(e, 0.85f, 0, 0);
}

static void space_wind(Engine *e, Source *s) { wind_follow(e, s, 0.08f, 0.3f, 0.0f, 0.2f); }
static void space_static(Engine *e, Source *s) { s->g.rain.density = 20.0f + 400.0f * e->rain + 120.0f * e->wind + 200.0f * e->snow; s->g.rain.size = 0.2f; }
static void w_space(Engine *e) {
    Source *s = add_wind(e, "solar wind", 0.3f, 1, 3000, 0.5f, 1800, 12, 0.4f); s->g.air.gust_speed = 0.3f; s->block = space_wind;
    s = engine_add(e, "static", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.35f; r->hiss = 0.2f; r->click = 0.5f; r->send = 0.3f; r->width = 1.0f;
      static const float f[4] = { 6000, 8000, 3500, 9500 }, q[4] = { 3, 3, 2, 4 }, w[4] = { 1, 1, 1, 1 }, g[4] = { 0.4f, 0.4f, 0.3f, 0.3f }; set_surfaces(r, f, q, w, g); }
    s->block = space_static;
    add_thunder(e, 0.5f, 0.9f, 0);               /* solar flares: a zap through the static */
}

static void cave_air(Engine *e, Source *s) { wind_follow(e, s, 0.18f, 0.5f, 0.3f, 0.9f); }
static void w_caves(Engine *e) {
    Source *s = add_wind(e, "airflow", 1.4f, 1, 250, 0.9f, 180, 10, 0.6f); s->g.air.gust_speed = 0.4f; s->block = cave_air;
    add_thunder(e, 0.7f, 0, 0.88f);              /* outside: always far and muffled */
}

static void forest_breeze(Engine *e, Source *s) { wind_follow(e, s, 0.2f, 0.6f, 0.05f, 0.6f); }
static void forest_rain(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 2500.0f); }
static void w_forest(Engine *e) {
    Source *s = add_wind(e, "breeze", 1.1f, 1, 700, 0.6f, 900, 8, 0.25f); s->block = forest_breeze;
    s = engine_add(e, "rain on leaves", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.429f; r->hiss = 0; r->click = 0; r->send = 0.35f; r->width = 0.95f;
      r->wash = 0.9f; r->wash_white = 0.45f; r->wash_hp = 250.0f; r->wash_lp = 6500.0f; r->tone = 5000.0f; r->var = 0.4f;   r->drops = 0.5f; /* leaves: soft */
      static const float f[4] = { 2200, 3400, 1100, 420 }, q[4] = { 1.8f, 2.2f, 1.8f, 1.2f }, w[4] = { 3, 2, 1, 1.5f }, g[4] = { 1, 1, 1, 0.6f }; set_surfaces(r, f, q, w, g); }
    s->block = forest_rain;
    add_thunder(e, 0.9f, 0, 0);
}

static void swamp_wind(Engine *e, Source *s) { wind_follow(e, s, 0.1f, 0.3f, 0.0f, 0.3f); }
static void swamp_rain(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 2000.0f); }
static void w_swamp(Engine *e) {
    Source *s = add_wind(e, "humid air", 1.5f, 0, 900, 0.7f, 400, 8, 0.3f); s->g.air.gust_depth = 0.5f; s->block = swamp_wind;
    s = engine_add(e, "rain on water", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.39f; r->hiss = 0; r->click = 0; r->send = 0.35f; r->width = 0.95f;
      r->wash = 0.75f; r->wash_white = 0.4f; r->wash_hp = 350.0f; r->wash_lp = 6500.0f; r->tone = 5500.0f; r->var = 0.5f;   r->drops = 0.65f; /* on water: plinks in a soft wash */
      static const float f[4] = { 1800, 700, 3500, 1200 }, q[4] = { 5, 2, 3, 4 }, w[4] = { 2, 1.5f, 1, 1.5f }, g[4] = { 1, 0.8f, 0.6f, 0.9f }; set_surfaces(r, f, q, w, g); }
    s->block = swamp_rain;
    add_thunder(e, 0.8f, 0, 0);
}

static void sea_wind(Engine *e, Source *s) { wind_follow(e, s, 0.15f, 0.5f, 0.1f, 0.5f); }
static void sea_rain(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 2400.0f); }
static void w_ocean(Engine *e) {
    Source *s = add_wind(e, "sea wind", 1.0f, 1, 500, 0.5f, 700, 9, 0.25f); s->block = sea_wind;
    s = engine_add(e, "squall", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.39f; r->hiss = 0; r->click = 0; r->send = 0.3f; r->width = 1.0f;
      r->wash = 1.1f; r->wash_white = 0.6f; r->wash_hp = 300.0f; r->wash_lp = 7000.0f; r->tone = 5500.0f; r->var = 0.4f;   r->drops = 0.5f; /* rain over the sea: a broad wash */
      static const float f[4] = { 1200, 2500, 600, 4000 }, q[4] = { 2.5f, 2, 1.5f, 2 }, w[4] = { 2, 2, 1, 1 }, g[4] = { 1, 0.9f, 0.6f, 0.6f }; set_surfaces(r, f, q, w, g); }
    s->block = sea_rain;
    add_thunder(e, 0.85f, 0, 0);
}

static void alien_wind(Engine *e, Source *s) { wind_follow(e, s, 0.15f, 0.35f, 0.6f, 0.6f); }
static void alien_ion(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 600.0f); s->g.rain.density += 25.0f + 80.0f * e->wind; }
static void w_alien(Engine *e) {
    Source *s = add_wind(e, "singing wind", 1.0f, 1, 600, 0.8f, 620, 18, 0.4f);
    s->g.air.howl2_ratio = 1.414f; s->g.air.gust_depth = 0.7f; s->block = alien_wind;
    s = engine_add(e, "ion crackle", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.5f; r->hiss = 0.2f; r->click = 0.15f; r->send = 0.45f; r->width = 1.0f;
      static const float f[4] = { 2310, 3720, 5150, 7420 }, q[4] = { 22, 25, 20, 18 }, w[4] = { 1, 1, 1, 1 }, g[4] = { 0.11f, 0.1f, 0.09f, 0.08f }; set_surfaces(r, f, q, w, g); }
    s->block = alien_ion;
    add_thunder(e, 0.5f, 0.8f, 0);
}

static void ind_wind(Engine *e, Source *s) { wind_follow(e, s, 0.1f, 0.35f, 0.15f, 0.5f); }
static void ind_roof(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 1800.0f); }
static void w_industrial(Engine *e) {
    Source *s = add_wind(e, "pipe wind", 1.0f, 1, 900, 0.7f, 380, 14, 0.45f);
    s->g.air.howl2_ratio = 2.7f; s->g.air.gust_depth = 0.85f; s->block = ind_wind;
    s = engine_add(e, "rain on metal", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.39f; r->hiss = 0; r->click = 0.12f; r->send = 0.35f; r->width = 0.95f;
      r->wash = 0.7f; r->wash_white = 0.7f; r->wash_hp = 500.0f; r->wash_lp = 8000.0f; r->tone = 7500.0f; r->var = 0.6f;   r->drops = 0.7f; /* on metal: pings over a hiss */
      static const float f[4] = { 900, 1700, 3100, 5200 }, q[4] = { 10, 12, 9, 8 }, w[4] = { 2, 2, 1.5f, 1 }, g[4] = { 0.5f, 0.45f, 0.4f, 0.3f }; set_surfaces(r, f, q, w, g); }
    s->block = ind_roof;
    add_thunder(e, 0.8f, 0, 0);
}

static void meadow_breeze(Engine *e, Source *s) { wind_follow(e, s, 0.15f, 0.55f, 0.03f, 0.4f); }
static void meadow_rain(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 2400.0f); }
static void w_meadow(Engine *e) {
    Source *s = add_wind(e, "summer breeze", 1.0f, 1, 800, 0.6f, 1000, 8, 0.2f); s->block = meadow_breeze;
    s = engine_add(e, "rain on grass", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.4f; r->hiss = 0; r->click = 0; r->send = 0.25f; r->width = 1.0f;
      r->wash = 1.0f; r->wash_white = 0.5f; r->wash_hp = 300.0f; r->wash_lp = 7000.0f; r->tone = 5000.0f; r->var = 0.35f; r->drops = 0.4f;   /* on grass: a soft hiss */
      static const float f[4] = { 2400, 3600, 1300, 800 }, q[4] = { 1.6f, 2, 1.5f, 1.2f }, w[4] = { 3, 2, 1, 1 }, g[4] = { 1, 0.9f, 0.8f, 0.6f }; set_surfaces(r, f, q, w, g); }
    s->block = meadow_rain;
    add_thunder(e, 0.9f, 0, 0);
}

/* ======================================================================= ANIMALS */
static Source *add_phrase(Engine *e, const char *name, float level, float dlo, float dhi, float answer, int agents,
                          const Species *a, const Species *b, const Species *c, const Species *d, const float tod[4], int glyph) {
    Source *s = engine_add(e, name, L_ANIMALS, G_VOICE, B_PHRASE);
    s->species[0] = a; s->species[1] = b ? b : a; s->species[2] = c ? c : a; s->species[3] = d ? d : a;
    s->nspecies = d ? 4 : c ? 3 : b ? 2 : 1;
    s->g.voice.level = level; s->dist_lo = dlo; s->dist_hi = dhi; s->answer = answer; s->glyph = glyph;
    if (tod) memcpy(s->tod, tod, sizeof s->tod);
    phrase_setup(s, &s->rng, agents);
    return s;
}
static Source *add_chorus(Engine *e, int layer, const char *name, const Syllable *syl, float level, float p0, float p1, float pull,
                          int n, float plo, float phi, float dlo, float dhi, const float tod[4], int glyph) {
    Source *s = engine_add(e, name, layer, G_VOICE, B_CHORUS);
    s->c_syl = syl; s->c_p0 = p0; s->c_p1 = p1; s->c_pull = pull; s->g.voice.level = level;
    s->dist_lo = dlo; s->dist_hi = dhi; s->glyph = glyph;
    if (tod) memcpy(s->tod, tod, sizeof s->tod);
    chorus_setup(s, &s->rng, n, plo, phi);
    return s;
}
static const float TOD_DAYBIRD[4] = { 1.6f, 1.0f, 0.8f, 0.0f };
static const float TOD_URBANBIRD[4] = { 1.5f, 1.0f, 0.7f, 0.05f };
static const float TOD_NIGHT[4] = { 0.15f, 0.0f, 0.6f, 1.0f };
static const float TOD_OWL[4] = { 0.3f, 0.0f, 0.4f, 1.0f };

static void a_city(Engine *e) {
    add_phrase(e, "sparrows and pigeons", 0.45f, 0.1f, 0.6f, 0.35f, 6, &SP_SPARROW, &SP_PIGEON, &SP_SPARROW, 0, TOD_URBANBIRD, GL_BIRD);
}

static void howler_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; crowd_say(&s->g.crowd, rng_range(&s->rng, 90, 130), rng_range(&s->rng, 2.5f, 4.0f), 400, 900, 700, 1300, pan, dist, 1.0f);
}
static void a_jungle(Engine *e) {
    static const float tod_b[4] = { 1.5f, 1.0f, 1.0f, 0.1f }, tod_c[4] = { 0.5f, 1.0f, 0.8f, 0.4f }, tod_h[4] = { 1.0f, 0.4f, 1.0f, 0.2f }, tod_f[4] = { 0.3f, 0.0f, 0.7f, 1.0f };
    add_phrase(e, "tropical birds", 0.45f, 0.1f, 0.8f, 0.5f, 8, &SP_TROPICAL, 0, 0, 0, tod_b, GL_BIRD);
    add_chorus(e, L_ANIMALS, "cicadas", &CICADA, 0.35f, 2.5f, 5.0f, 0.2f, 8, 0.85f, 1.15f, 0.2f, 0.7f, tod_c, GL_DOT);
    Source *s = engine_add(e, "howler monkeys", L_ANIMALS, G_CROWD, B_SCATTER);
    s->rate = 0.03f; s->g.crowd.level = 0.6f; s->dist_lo = 0.7f; s->dist_hi = 0.95f; s->fire = howler_fire; s->glyph = GL_GROWL;
    memcpy(s->tod, tod_h, sizeof s->tod);
    s = add_chorus(e, L_ANIMALS, "tree frogs", &TREEFROG, 0.4f, 0.6f, 1.2f, 0.3f, 8, 0.9f, 1.15f, 0.2f, 0.7f, tod_f, GL_DOT);
    s->rain_resp = 1.0f;
}

static void a_space(Engine *e) {
    add_phrase(e, "unknown signals", 0.45f, 0.5f, 0.9f, 0.3f, 3, &SP_SIGNAL, 0, 0, 0, 0, GL_SIGNAL);
    add_chorus(e, L_ANIMALS, "pulsar", &PULSAR, 0.3f, 1.6f, 2.2f, 1.0f, 2, 0.95f, 1.05f, 0.7f, 0.9f, 0, GL_DOT);
}

static void a_caves(Engine *e) {
    static const float tod_bat[4] = { 1.0f, 0.2f, 1.5f, 1.2f };
    add_phrase(e, "bats", 0.7f, 0.2f, 0.7f, 0.0f, 4, &SP_BAT, 0, 0, 0, tod_bat, GL_BAT);
    add_chorus(e, L_ANIMALS, "cave crickets", &CAVE_CRICKET, 0.6f, 1.5f, 3.0f, 0.2f, 5, 0.9f, 1.05f, 0.4f, 0.8f, 0, GL_DOT);
}

static void a_forest(Engine *e) {
    Source *s = add_phrase(e, "songbirds", 0.5f, 0.1f, 0.75f, 0.3f, 8, &SP_ROBIN, &SP_BLACKBIRD, &SP_CHIFFCHAFF, &SP_WREN, TOD_DAYBIRD, GL_BIRD);
    (void)s;
    add_phrase(e, "owl", 0.45f, 0.5f, 0.8f, 0.0f, 1, &SP_OWL, 0, 0, 0, TOD_OWL, GL_BIGBIRD);
    s = add_chorus(e, L_ANIMALS, "night insects", &CRICKET, 0.25f, 0.7f, 1.1f, 0.3f, 10, 0.95f, 1.08f, 0.3f, 0.8f, TOD_NIGHT, GL_DOT);
}

static void a_swamp(Engine *e) {
    static const float tod_frog[4] = { 0.6f, 0.3f, 1.0f, 1.0f };
    Source *s = add_chorus(e, L_ANIMALS, "frogs", &FROG, 0.55f, 0.8f, 1.3f, 0.35f, 10, 0.85f, 1.2f, 0.15f, 0.7f, tod_frog, GL_BIGDOT);
    s->rain_resp = 1.0f;
    add_chorus(e, L_ANIMALS, "crickets", &CRICKET, 0.25f, 0.7f, 1.1f, 0.3f, 8, 0.95f, 1.1f, 0.3f, 0.8f, TOD_NIGHT, GL_DOT);
    add_phrase(e, "heron", 0.5f, 0.3f, 0.8f, 0.0f, 1, &SP_HERON, 0, 0, 0, 0, GL_BIGBIRD);
}

static void a_ocean(Engine *e) {
    static const float tod_g[4] = { 1, 1, 1, 0.1f };
    add_phrase(e, "gulls", 0.5f, 0.15f, 0.8f, 0.4f, 5, &SP_GULL, &SP_GULL, &SP_OYSTER, 0, tod_g, GL_BIGBIRD);
    add_phrase(e, "whale song", 0.45f, 0.85f, 0.95f, 0.0f, 1, &SP_WHALE, 0, 0, 0, 0, GL_FLAT);
}

static void growl_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; crowd_say(&s->g.crowd, rng_range(&s->rng, 60, 95), rng_range(&s->rng, 0.9f, 1.8f), 300, 700, 750, 1250, pan, dist, 1.0f);
}
static void a_alien(Engine *e) {
    static const float tod_w[4] = { 1, 1, 1, 0.6f }, tod_s[4] = { 0.8f, 0.6f, 1, 1 };
    add_phrase(e, "warblers", 0.45f, 0.15f, 0.8f, 0.45f, 5, &SP_WARBLER, &SP_WARBLER, &SP_CALLER, 0, tod_w, GL_BIRD);
    Source *s = add_chorus(e, L_ANIMALS, "spore chorus", &SPORE, 0.3f, 0.35f, 0.7f, 0.5f, 9, 0.7f, 1.5f, 0.2f, 0.8f, tod_s, GL_DOT);
    s->c_drift = 1;
    s = engine_add(e, "growls", L_ANIMALS, G_CROWD, B_SCATTER);
    s->rate = 0.07f; s->g.crowd.level = 0.7f; s->dist_lo = 0.4f; s->dist_hi = 0.85f; s->fire = growl_fire; s->glyph = GL_GROWL;
}

static void a_industrial(Engine *e) {
    static const float tod_p[4] = { 1.2f, 1.0f, 0.8f, 0.05f }, tod_c[4] = { 1.3f, 0.8f, 1.0f, 0.0f };
    add_phrase(e, "pigeons", 0.45f, 0.15f, 0.6f, 0.3f, 4, &SP_PIGEON, 0, 0, 0, tod_p, GL_BIRD);
    add_phrase(e, "crows", 0.4f, 0.5f, 0.85f, 0.5f, 3, &SP_CROW, 0, 0, 0, tod_c, GL_BIGBIRD);
}

/* A bee flies by: a buzz (a honeybee's 200-250 Hz, a bumblebee's lower), wavering as it turns, close and gone */
static void bee_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)pan; (void)dist; (void)x;
    float f = rng_f(&s->rng) < 0.4f ? rng_range(&s->rng, 130, 170) : rng_range(&s->rng, 205, 245);
    s->pb_voice = hum_start(&s->g.hum, f, 1.0f, 1.6f, 0.8f, 0.06f, 2500, rng_range(&s->rng, 4.0f, 8.0f), 0.45f, 0, 0.3f);
}
/* the field's hum: many insects far off, by day; it thins when it rains */
static void insect_hum_block(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    static const float F[2] = { 196.0f, 238.0f }, PAN[2] = { -0.55f, 0.5f };
    for (int k = 0; k < 2; k++)
        if (!h->v[k].on) { hum_start(h, F[k], 1.0f, 1.1f, 1.8f, 0.04f, 1500, 2.5f + k, 0.3f, 0, 3.0f); spatial_set(&h->v[k].sp, PAN[k], 0.8f); }
    float life = src_tod(e, s) * e->animals_w * (1.0f - e->shelter) * (0.4f + 1.2f * e->eff.activity) * e->dens[L_ANIMALS] * e->gov;
    s->user[4] += (rng_bi(&s->rng) * 0.8f - s->user[4]) * BLK * 0.1f;
    for (int k = 0; k < 2; k++) {
        h->v[k].pmul = 1.0f + 0.02f * (k ? -s->user[4] : s->user[4]);
        h->v[k].target = 0.02f + 0.035f * life * (1.0f + 0.5f * (k ? -s->user[4] : s->user[4]));
    }
}
/* sheep across the fields: quiet for a while, then a bout of bleating, the flock calling to each other (2 to 6
 * bleats from here and there, a second or two apart), sometimes just one. A ewe's, now and then a lamb's */
static void sheep_one(Engine *e, Source *s, float pan, float dist) {
    int lamb = rng_f(&s->rng) < 0.25f;
    float f0 = lamb ? rng_range(&s->rng, 330, 460) : rng_range(&s->rng, 225, 260), dur = lamb ? rng_range(&s->rng, 0.3f, 0.6f) : rng_range(&s->rng, 0.6f, 1.3f);
    crowd_bleat(&s->g.crowd, f0, dur, 720, 1950, 660, 1820, lamb ? 15.0f : rng_range(&s->rng, 12.0f, 14.0f), pan, dist, 1.0f);
    (void)e;
}
static void sheep_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)x;
    sheep_one(e, s, pan, dist);
    s->user[1] = pan;                                            /* where the flock is */
    s->user[2] = rng_f(&s->rng) < 0.25f ? 0 : (float)(1 + rng_int(&s->rng, 5));   /* how many answer */
    s->user[0] = rng_range(&s->rng, 0.5f, 2.2f);
}
static void sheep_block(Engine *e, Source *s) {
    if (s->user[2] <= 0 || (s->user[0] -= BLK) > 0) return;
    float pan = s->user[1] + rng_bi(&s->rng) * 0.4f, dist = rng_range(&s->rng, s->dist_lo, s->dist_hi);
    pan = pan < -1 ? -1 : pan > 1 ? 1 : pan;
    engine_var_begin(e, s);
    sheep_one(e, s, pan, dist);
    engine_var_end();
    engine_post(e, L_ANIMALS, GL_SHEEP, pan, dist, 0, 0, 0);
    s->user[2] -= 1;
    s->user[0] = rng_range(&s->rng, 0.5f, 2.5f);
}
static void a_meadow(Engine *e) {
    static const float tod_p[4] = { 1.2f, 1.0f, 0.8f, 0.0f }, tod_bee[4] = { 0.3f, 1.0f, 0.6f, 0.0f }, tod_g[4] = { 0.3f, 1.0f, 0.8f, 0.0f }, tod_sh[4] = { 1.0f, 0.8f, 1.0f, 0.3f };
    add_phrase(e, "songbirds", 0.45f, 0.2f, 0.75f, 0.3f, 7, &SP_SKYLARK, &SP_YELLOWHAMMER, &SP_BLACKBIRD, &SP_ROBIN, TOD_DAYBIRD, GL_BIRD);
    add_phrase(e, "wood pigeon", 0.4f, 0.4f, 0.85f, 0.2f, 2, &SP_WOODPIGEON, 0, 0, 0, tod_p, GL_BIGBIRD);
    Source *s = engine_add(e, "bees", L_ANIMALS, G_HUM, B_PASSBY);
    s->rate = 0.12f; s->pb_len_lo = 2.5f; s->pb_len_hi = 5.0f; s->dist_lo = 0.05f; s->dist_hi = 0.35f; s->pb_doppler = 0.04f; s->user[1] = 0.22f;
    s->g.hum.level = 0.6f; s->fire = bee_fire; s->glyph = GL_BEE; memcpy(s->tod, tod_bee, sizeof s->tod);
    s = engine_add(e, "insect hum", L_ANIMALS, G_HUM, B_BED); s->g.hum.level = 0.6f; s->block = insect_hum_block; memcpy(s->tod, tod_bee, sizeof s->tod);
    add_chorus(e, L_ANIMALS, "grasshoppers", &GRASSHOPPER, 0.4f, 4.0f, 9.0f, 0.1f, 6, 0.9f, 1.1f, 0.15f, 0.6f, tod_g, GL_DOT);
    s = engine_add(e, "sheep", L_ANIMALS, G_CROWD, B_SCATTER);
    s->rate = 0.022f; s->g.crowd.level = 0.07f; s->dist_lo = 0.45f; s->dist_hi = 0.65f; s->fire = sheep_fire; s->block = sheep_block; s->glyph = GL_SHEEP;
    memcpy(s->tod, tod_sh, sizeof s->tod);
    add_chorus(e, L_ANIMALS, "night crickets", &CRICKET, 0.22f, 0.7f, 1.1f, 0.3f, 8, 0.95f, 1.08f, 0.3f, 0.8f, TOD_NIGHT, GL_DOT);
}

/* ======================================================================= LANDSCAPE */
static void rustle_block(Engine *e, Source *s) { s->g.air.level = s->user[0] + s->user[1] * e->wind; }
static Source *add_rustle(Engine *e, const char *name, float hp, float l0, float l1) {
    Source *s = engine_add(e, name, L_LANDSCAPE, G_AIR, B_BED);
    s->g.air.colour = 0.6f; s->g.air.cutoff = 9000; s->g.air.hp = hp; s->g.air.gust_link = &e->gust; s->g.air.gust_depth = 1.0f;
    s->g.air.width = 1.0f; s->g.air.send = 0.2f; s->user[0] = l0; s->user[1] = l1; s->block = rustle_block;
    return s;
}
static void single_drip(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; water_spawn(&s->g.water, &s->rng, rng_range(&s->rng, s->user[0], s->user[1]), pan, dist, rng_range(&s->rng, 0.5f, 0.9f), s->user[2]);
}

static void gutter_block(Engine *e, Source *s) { s->rate = 0.2f + 6.0f * e->rain; }
static void l_city(Engine *e) {
    Source *s = engine_add(e, "gutters", L_LANDSCAPE, G_WATER, B_SCATTER);
    s->g.water.level = 0.45f; s->dist_lo = 0.2f; s->dist_hi = 0.6f; s->user[0] = 3; s->user[1] = 7; s->user[2] = 1.5f;
    s->fire = single_drip; s->block = gutter_block; s->glyph = GL_DRIP;
    add_rustle(e, "street trees", 2200, 0.06f, 0.3f);
}

static void canopy_drip_block(Engine *e, Source *s) { s->rate = 0.4f + 4.0f * e->rain; }
static void l_jungle(Engine *e) {
    add_rustle(e, "canopy rustle", 1800, 0.08f, 0.35f);
    Source *s = engine_add(e, "canopy drips", L_LANDSCAPE, G_WATER, B_SCATTER);
    s->g.water.level = 0.45f; s->dist_lo = 0.1f; s->dist_hi = 0.6f; s->user[0] = 3; s->user[1] = 8; s->user[2] = 1.2f;
    s->fire = single_drip; s->block = canopy_drip_block; s->glyph = GL_DRIP;
    s = engine_add(e, "stream", L_LANDSCAPE, G_WATER, B_BED);
    { Water *w = &s->g.water; w->rate = 180; w->dmin = 1.5f; w->dmax = 10; w->gamma = 2.0f; w->level = 0.3f;
      w->pan_lo = 0.1f; w->pan_hi = 0.85f; w->dist_lo = 0.3f; w->dist_hi = 0.6f; w->rush = 0.2f; w->rush_cut = 1000; }
}

static void creak_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; ring_strike(&s->g.ring, &s->rng, &MAT_GIRDER, rng_range(&s->rng, 90, 200), rng_range(&s->rng, 0.3f, 0.6f), 0.5f, 1.0f, pan, dist);
}
static void tick_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; ring_strike(&s->g.ring, &s->rng, &MAT_CHAIN, rng_range(&s->rng, 2500, 4000), rng_range(&s->rng, 0.2f, 0.5f), 0.8f, 1.0f, pan, dist);
}
static void coolant_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; water_burst(&s->g.water, 8 + rng_int(&s->rng, 8), rng_range(&s->rng, 0.4f, 0.9f), pan, dist, 2, 6);
}
static void l_space(Engine *e) {
    Source *s = engine_add(e, "hull creaks", L_LANDSCAPE, G_RING, B_SCATTER);
    s->rate = 0.25f; s->g.ring.level = 0.5f; s->dist_lo = 0.3f; s->dist_hi = 0.8f; s->fire = creak_fire; s->glyph = GL_BAR;
    s = engine_add(e, "thermal ticks", L_LANDSCAPE, G_RING, B_SCATTER);
    s->rate = 0.3f; s->g.ring.level = 0.3f; s->dist_lo = 0.2f; s->dist_hi = 0.7f; s->fire = tick_fire; s->glyph = GL_SQUARE;
    s = engine_add(e, "coolant", L_LANDSCAPE, G_WATER, B_SCATTER);
    s->rate = 0.2f; s->g.water.level = 0.45f; s->g.water.decay_mul = 1.5f; s->dist_lo = 0.3f; s->dist_hi = 0.6f; s->fire = coolant_fire; s->glyph = GL_RING;
}

static void patter_step(Source *s) {
    if (s->user[3] <= 0) return;                   /* user[3] drips left, user[4] timer, user[5] interval; ag[1] where */
    s->user[4] -= BLK;
    if (s->user[4] > 0) return;
    water_spawn(&s->g.water, &s->rng, rng_range(&s->rng, 7.0f, 13.0f), s->ag[1].pan + rng_bi(&s->rng) * 0.05f, s->ag[1].dist, s->ag[1].pitch, 3.5f);
    s->ag[1].pitch *= 0.8f; s->user[3] -= 1; s->user[4] = s->user[5] * rng_range(&s->rng, 0.6f, 1.4f);
}
static void cave_drip_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x;
    water_spawn(&s->g.water, &s->rng, rng_range(&s->rng, 8.0f, 16.0f), pan, dist, rng_range(&s->rng, 0.6f, 1.0f), 3.5f);   /* big, slow drops: 400-800 Hz */
    if (rng_f(&s->rng) < 0.3f) { s->user[3] = 2 + rng_int(&s->rng, 4); s->user[4] = 0.4f; s->user[5] = rng_range(&s->rng, 0.25f, 0.6f); s->ag[1].pan = pan; s->ag[1].dist = dist; s->ag[1].pitch = 0.7f; }
}
static void cave_drip_block(Engine *e, Source *s) { s->rate = 0.9f * (1.0f + 1.5f * e->rain); patter_step(s); }
static void river_block(Engine *e, Source *s) { Water *w = &s->g.water; w->rate = 120.0f * (0.6f + e->rain); w->level = 0.22f + 0.2f * e->rain; w->rush = 0.3f + 0.3f * e->rain; }
static void rockfall_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x;
    ring_strike(&s->g.ring, &s->rng, &MAT_STONE, rng_range(&s->rng, 180, 260), 1.0f, 0.6f, 1.0f, pan, dist);
    roll_start(s, 4 + rng_int(&s->rng, 5), 0.11f, rng_range(&s->rng, 700, 1300), pan + 0.05f, dist, 0.6f);
}
static void rockfall_block(Engine *e, Source *s) { (void)e; roll_step(s, &MAT_STONE, 0.8f, 1.0f, 0.82f); }
static void l_caves(Engine *e) {
    Source *s = engine_add(e, "drips", L_LANDSCAPE, G_WATER, B_SCATTER);
    s->g.water.level = 0.42f; s->g.water.rise = 0.08f; s->dist_lo = 0.1f; s->dist_hi = 0.8f; s->fire = cave_drip_fire; s->block = cave_drip_block; s->glyph = GL_DRIP;
    s = engine_add(e, "underground river", L_LANDSCAPE, G_WATER, B_BED);
    { Water *w = &s->g.water; w->dmin = 3; w->dmax = 18; w->gamma = 1.8f; w->pan_lo = 0.3f; w->pan_hi = 0.9f; w->dist_lo = 0.7f; w->dist_hi = 0.9f; w->rush_cut = 500; }
    s->block = river_block;
    s = engine_add(e, "falling stone", L_LANDSCAPE, G_RING, B_RARE);
    s->rate = 0.02f; s->cooldown = 20; s->g.ring.level = 0.6f; s->dist_lo = 0.3f; s->dist_hi = 0.8f; s->fire = rockfall_fire; s->block = rockfall_block; s->glyph = GL_ROCK;
}

static void detail_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x;
    if (rng_f(&s->rng) < 0.3f) roll_start(s, 10 + rng_int(&s->rng, 8), 1.0f / rng_range(&s->rng, 15, 20), rng_range(&s->rng, 800, 1100), pan, dist, 0.9f);
    else ring_strike(&s->g.ring, &s->rng, &MAT_TWIG, rng_range(&s->rng, 1300, 2300), rng_range(&s->rng, 0.4f, 0.9f), 0.8f, 1.0f, pan, dist);
}
static void detail_block(Engine *e, Source *s) { (void)e; roll_step(s, &MAT_WOOD, 0.7f, 1.0f, 0.93f); }
static void l_forest(Engine *e) {
    add_rustle(e, "leaf rustle", 2000, 0.12f, 0.45f);
    Source *s = engine_add(e, "brook", L_LANDSCAPE, G_WATER, B_BED);
    { Water *w = &s->g.water; w->rate = 420; w->dmin = 1.0f; w->dmax = 5.5f; w->gamma = 2.6f; w->level = 0.4f; w->rise = 0.04f;
      w->pan_lo = -0.75f; w->pan_hi = 0.2f; w->dist_lo = 0.2f; w->dist_hi = 0.5f; w->rush = 0.55f; w->rush_cut = 2200; }   /* a gentle brook */
    s = engine_add(e, "woodland detail", L_LANDSCAPE, G_RING, B_SCATTER);
    s->rate = 0.12f; s->g.ring.level = 0.5f; s->dist_lo = 0.2f; s->dist_hi = 0.8f; s->glyph = GL_BAR;
    s->tod[0] = 1; s->tod[1] = 1; s->tod[2] = 0.7f; s->tod[3] = 0.1f;
    s->fire = detail_fire; s->block = detail_block;
}

/* marsh gas: big, slow bubbles from the mud (18-40 mm, about 160-360 Hz), now and then a short run of them */
static void gloop_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x;
    water_spawn(&s->g.water, &s->rng, rng_range(&s->rng, 18.0f, 40.0f), pan, dist, rng_range(&s->rng, 0.6f, 1.0f), 2.6f);
    if (rng_f(&s->rng) < 0.35f) { s->user[3] = 1 + rng_int(&s->rng, 3); s->user[4] = rng_range(&s->rng, 0.12f, 0.3f); s->user[0] = pan; s->user[1] = dist; }
}
static void gloop_block(Engine *e, Source *s) {
    (void)e;
    if (s->user[3] <= 0) return;
    s->user[4] -= BLK;
    if (s->user[4] > 0) return;
    water_spawn(&s->g.water, &s->rng, rng_range(&s->rng, 14.0f, 30.0f), s->user[0] + rng_bi(&s->rng) * 0.04f, s->user[1], rng_range(&s->rng, 0.4f, 0.8f), 2.6f);
    s->user[3] -= 1; s->user[4] = rng_range(&s->rng, 0.1f, 0.35f);
}
static void lap_block(Engine *e, Source *s) {
    if (e->swell_peak) water_burst(&s->g.water, 6 + rng_int(&s->rng, 6), 0.6f, rng_range(&s->rng, -0.6f, 0.6f), rng_range(&s->rng, 0.15f, 0.35f), 3, 9);
}
static void l_swamp(Engine *e) {
    Source *s = engine_add(e, "bubbles", L_LANDSCAPE, G_WATER, B_SCATTER);
    s->rate = 0.7f; s->g.water.level = 0.3f; s->g.water.rise = 0.05f; s->dist_lo = 0.15f; s->dist_hi = 0.6f; s->fire = gloop_fire; s->block = gloop_block; s->glyph = GL_RING;
    s = engine_add(e, "lapping water", L_LANDSCAPE, G_AIR, B_SWELL);
    s->g.air.colour = 1.3f; s->g.air.cutoff = 600; s->g.air.gust_depth = 0.9f; s->g.air.swell_amt = 1.0f; s->g.air.level = 0.15f;
    s->g.air.width = 0.9f; s->g.air.send = 0.2f; s->sw_lo = 2.5f; s->sw_hi = 4.5f;
    s = engine_add(e, "lapping bank", L_LANDSCAPE, G_WATER, B_BED);
    s->g.water.level = 0.4f; s->block = lap_block;
    add_rustle(e, "reeds", 2600, 0.04f, 0.25f);
}

static void surf_block(Engine *e, Source *s) { s->g.air.level = 0.35f + 0.25f * e->wind; }
/* A wave breaking: at the swell's crest, a crash of noise rising with it and dying away (its size, length and
 * brightness new each time; now and then a wave that doesn't break), and a sparse fizz of bubbles in it.
 * user[0] size, [1] time since, [2] length, [3] brightness (Hz); the shingle drag follows its size */
static float g_wave_size = 0.6f;
static void breakers_block(Engine *e, Source *s) {
    Water *w = &s->g.water;
    if (e->swell_peak && rng_f(&s->rng) < 0.88f) {
        float size = 0.3f + 0.7f * rng_f(&s->rng);
        size *= 0.7f + 0.6f * e->wind;             /* (rougher with the wind) */
        s->user[0] = size; s->user[1] = 0; s->user[2] = rng_range(&s->rng, 1.6f, 3.8f); s->user[3] = rng_range(&s->rng, 500, 1700);
        g_wave_size = size;
        float pan = rng_range(&s->rng, -0.6f, 0.6f), dist = rng_range(&s->rng, 0.3f, 0.6f);
        water_burst(w, 8 + (int)(22 * size), s->user[2] * 0.5f, pan, dist, 3, 16);
        engine_post(e, L_LANDSCAPE, GL_SPRAY, pan, dist, 0, 0, 0);
        s->events++;
    }
    s->user[1] += BLK;
    float u = s->user[2] > 0 ? s->user[1] / s->user[2] : 2.0f, env;
    if (u < 0.1f) env = u / 0.1f; else if (u < 1.6f) env = fm_expf(-(u - 0.1f) * 3.0f); else env = 0;
    w->rush = env * s->user[0] * 4.0f;   /* (about 10 dB under the swell: it fills the break, it does not lead) */
    w->rush_cut = 250.0f + s->user[3] * (0.45f + 0.55f * env);   /* bright as it crashes, darker as it runs out */
}
static void shingle_block(Engine *e, Source *s) {
    if (e->swell_peak) { s->user[0] = g_wave_size; s->user[1] = rng_range(&s->rng, 0.5f, 1.0f); }   /* the wave runs back */
    if (s->user[1] > 0) s->user[1] -= BLK; else s->user[0] *= 0.9993f;
    s->g.rain.density = (s->user[1] > 0 ? 0 : s->user[0]) * 1300.0f * (0.4f + 0.6f * e->wind);
}
static void l_ocean(Engine *e) {
    Source *s = engine_add(e, "surf swell", L_LANDSCAPE, G_AIR, B_SWELL);
    s->g.air.colour = 1.6f; s->g.air.cutoff = 900; s->g.air.gust_depth = 0.9f; s->g.air.swell_amt = 1.0f;
    s->g.air.width = 0.95f; s->g.air.send = 0.35f; s->sw_lo = 6; s->sw_hi = 12; s->block = surf_block;
    s = engine_add(e, "breakers", L_LANDSCAPE, G_WATER, B_BED);
    s->g.water.level = 0.32f; s->g.water.rush_cut = 900; s->block = breakers_block;
    s = engine_add(e, "shingle drag", L_LANDSCAPE, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.22f; r->hiss = 0.1f; r->click = 0.15f; r->send = 0.3f; r->width = 1.0f; r->size = 0.4f;
      r->tone = 5000.0f; r->var = 0.5f; r->drops = 0.7f;                           /* rounder, quieter, under the wave */
      static const float f[4] = { 2500, 4200, 1600, 6000 }, q[4] = { 3, 3, 2.5f, 3 }, w[4] = { 2, 2, 1, 1 }, g[4] = { 0.6f, 0.6f, 0.5f, 0.4f }; set_surfaces(r, f, q, w, g); }
    s->block = shingle_block;
    s = engine_add(e, "far waves", L_LANDSCAPE, G_AIR, B_BED);
    s->g.air.colour = 2.0f; s->g.air.cutoff = 250; s->g.air.gust_depth = 0.3f; s->g.air.gust_speed = 0.3f; s->g.air.level = 0.25f; s->g.air.width = 0.9f; s->g.air.send = 0.3f;
}

static const float GLASS_SCALE[] = { 1.0f, 1.125f, 1.333f, 1.5f, 1.6875f, 2.0f, 2.25f, 2.667f };
static void spires_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x;
    float f = 440.0f * GLASS_SCALE[rng_int(&s->rng, 8)] * (rng_f(&s->rng) < 0.3f ? 0.5f : 1.0f);
    ring_strike(&s->g.ring, &s->rng, &MAT_GLASS, f, rng_range(&s->rng, 0.3f, 0.8f), 0.7f, 1.0f, pan, dist);
}
static void spires_block(Engine *e, Source *s) { s->rate = 0.6f * (0.4f + 1.4f * e->gust); }   /* wind makes the crystals sing */
static void geyser_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; water_burst(&s->g.water, 50 + rng_int(&s->rng, 30), rng_range(&s->rng, 1.0f, 2.0f), pan, dist, 6, 22);
}
static void pods_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; roll_start(s, 2 + rng_int(&s->rng, 2), 0.28f, rng_range(&s->rng, 110, 190), pan, dist, 0.8f);
}
static void pods_block(Engine *e, Source *s) { (void)e; roll_step(s, &MAT_POD, 0.6f, 1.0f, 0.7f); }
static void l_alien(Engine *e) {
    Source *s = engine_add(e, "glass spires", L_LANDSCAPE, G_RING, B_SCATTER);
    s->g.ring.level = 0.4f; s->dist_lo = 0.2f; s->dist_hi = 0.9f; s->fire = spires_fire; s->block = spires_block; s->glyph = GL_CHIME;
    s = engine_add(e, "geysers", L_LANDSCAPE, G_WATER, B_SCATTER);
    { Water *w = &s->g.water; w->rate = 6; w->dmin = 8; w->dmax = 25; w->gamma = 1.5f; w->rise = 0.25f; w->decay_mul = 2.5f; w->level = 0.5f;
      w->pan_lo = -0.6f; w->pan_hi = 0.6f; w->dist_lo = 0.4f; w->dist_hi = 0.7f; }
    s->rate = 0.09f; s->dist_lo = 0.3f; s->dist_hi = 0.7f; s->fire = geyser_fire; s->glyph = GL_GEYSER;
    s = engine_add(e, "pods", L_LANDSCAPE, G_RING, B_SCATTER);
    s->rate = 0.5f; s->g.ring.level = 0.45f; s->dist_lo = 0.1f; s->dist_hi = 0.6f; s->fire = pods_fire; s->block = pods_block; s->glyph = GL_RING;
}

static void steam_fire(Engine *e, Source *s, float pan, float dist, float x) { (void)e; (void)pan; (void)dist; (void)x; air_burst(&s->g.air, rng_range(&s->rng, 0.8f, 3.0f)); }
static void girder_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; ring_strike(&s->g.ring, &s->rng, &MAT_BEAM, rng_range(&s->rng, 60, 140), rng_range(&s->rng, 0.3f, 0.7f), 0.5f, 1.0f, pan, dist);
}
static void girder_block(Engine *e, Source *s) { s->rate = 0.15f * (0.4f + 1.5f * e->wind); }
static void chain_fire(Engine *e, Source *s, float pan, float dist, float x) { (void)e; (void)x; roll_start(s, 3 + rng_int(&s->rng, 4), 0.09f, rng_range(&s->rng, 1800, 2600), pan, dist, 0.6f); }
static void chain_block(Engine *e, Source *s) { s->rate = 0.08f * (0.2f + 1.5f * e->wind); roll_step(s, &MAT_CHAIN, 0.8f, 1.0f, 0.85f); }
static void pipe_drip_block(Engine *e, Source *s) { s->rate = 0.4f * (0.3f + 2.0f * e->rain); }
static void l_industrial(Engine *e) {
    Source *s = engine_add(e, "steam vent", L_LANDSCAPE, G_AIR, B_SCATTER);
    s->g.air.burst_mode = 1; s->g.air.colour = 0.2f; s->g.air.cutoff = 12000; s->g.air.hp = 2500; s->g.air.gust_depth = 0.3f;
    s->g.air.level = 0.35f; s->g.air.width = 0.3f; s->g.air.send = 0.4f; s->rate = 0.08f; s->fire = steam_fire; s->glyph = GL_STEAM;
    s = engine_add(e, "girders", L_LANDSCAPE, G_RING, B_SCATTER);
    s->g.ring.level = 0.5f; s->dist_lo = 0.4f; s->dist_hi = 0.9f; s->fire = girder_fire; s->block = girder_block; s->glyph = GL_BAR;
    s = engine_add(e, "chains", L_LANDSCAPE, G_RING, B_SCATTER);
    s->g.ring.level = 0.35f; s->dist_lo = 0.3f; s->dist_hi = 0.7f; s->fire = chain_fire; s->block = chain_block; s->glyph = GL_SQUARE;
    s = engine_add(e, "dripping pipes", L_LANDSCAPE, G_WATER, B_SCATTER);
    s->g.water.level = 0.5f; s->dist_lo = 0.2f; s->dist_hi = 0.7f; s->user[0] = 3; s->user[1] = 6; s->user[2] = 2.0f;
    s->fire = single_drip; s->block = pipe_drip_block; s->glyph = GL_DRIP;
}

static void l_meadow(Engine *e) {
    add_rustle(e, "long grass", 2600, 0.16f, 0.45f);
    add_rustle(e, "hedgerow", 1500, 0.08f, 0.3f);
}

/* ======================================================================= HUMANS */
static void air_bed_block(Engine *e, Source *s) { s->g.air.level = (s->user[0] + s->user[1] * e->eff.activity) * (0.4f + 0.6f * src_tod(e, s)); }
static Source *add_far_noise(Engine *e, const char *name, float colour, float cutoff, float l0, float l1) {
    Source *s = engine_add(e, name, L_HUMANS, G_AIR, B_BED);
    s->g.air.colour = colour; s->g.air.cutoff = cutoff; s->g.air.gust_depth = 0.25f; s->g.air.gust_speed = 0.5f; s->g.air.width = 0.7f; s->g.air.send = 0.35f;
    s->user[0] = l0; s->user[1] = l1; s->block = air_bed_block;
    return s;
}
static Source *add_passby(Engine *e, const char *name, FireFn start, float rate, float len_lo, float len_hi, float dlo, float dhi, float doppler, float amp, int glyph) {
    Source *s = engine_add(e, name, L_HUMANS, G_HUM, B_PASSBY);
    s->rate = rate; s->pb_len_lo = len_lo; s->pb_len_hi = len_hi; s->dist_lo = dlo; s->dist_hi = dhi; s->pb_doppler = doppler; s->user[1] = amp;
    s->g.hum.level = 0.6f; s->fire = start; s->glyph = glyph;
    return s;
}
static void jet_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)pan; (void)dist; (void)x; s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 230, 290), 3.1f, 0.5f, 1.2f, 0.85f, 900, 0, 0, 0, 4.0f);
}
static void add_jets(Engine *e, float rate, float dlo, float dhi) {
    Source *s = add_passby(e, "aircraft", jet_fire, rate, 30, 42, dlo, dhi, 0.08f, 0.35f, GL_PLANE);
    s->tod[0] = 0.7f; s->tod[1] = 1.0f; s->tod[2] = 1.0f; s->tod[3] = 0.25f;
}
static void train_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)pan; (void)dist; (void)x;
    /* a rush of air and rails over a low hum: mostly noise, brightening as it nears (wheels_block) */
    s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 60, 75), 1.0f, 0.35f, 1.2f, 0.9f, 2400, 0, 0, 0, 1.2f);
    s->user[4] = rng_range(&s->rng, 0.28f, 0.4f);  /* seconds between bogies: a fast train */
}
static void wheels_block(Engine *e, Source *w) {   /* the wheels ride with the train: the source just before this one */
    (void)e;
    Source *t = w - 1;
    if (t->beh != B_PASSBY || !t->pb_on) { w->user[3] = 0; return; }
    float u = t->pb_t / t->pb_len;
    if (t->pb_voice >= 0) t->g.hum.v[t->pb_voice].noise_cut = 1600.0f + 1600.0f * (u < 0.5f ? 0.4f + 1.2f * u : 1.6f - 1.2f * u);   /* brighter as it nears */
    if (u < 0.1f || u > 0.9f) return;
    w->user[3] -= BLK;
    if (w->user[3] > 0) return;
    float edge = u < 0.25f ? (u - 0.1f) / 0.15f : u > 0.75f ? (0.9f - u) / 0.15f : 1.0f;
    ring_strike(&w->g.ring, &w->rng, &MAT_RAIL, rng_range(&w->rng, 420, 520), 0.5f * edge, 0.6f, 0.8f, t->ag[MAX_AGENTS - 1].pan, t->ag[MAX_AGENTS - 1].dist);
    if (w->user[5] == 0) { w->user[5] = 1; w->user[3] = 0.08f; } else { w->user[5] = 0; w->user[3] = t->user[4] * rng_range(&w->rng, 0.95f, 1.05f); }
}
static void add_trains(Engine *e, float rate, float dlo, float dhi, float len_lo, float len_hi) {
    Source *s = add_passby(e, "trains", train_fire, rate, len_lo, len_hi, dlo, dhi, 0.05f, 0.47f, GL_TRAIN);
    s->tod[0] = 0.8f; s->tod[1] = 1.0f; s->tod[2] = 1.0f; s->tod[3] = 0.3f;
    Source *w = engine_add(e, "train wheels", L_HUMANS, G_RING, B_BED);
    w->g.ring.level = 0.16f; w->block = wheels_block;   /* (in the rush, not over it) */
}

/* The city's people, as heard from a window: distant engines. A low murmur of traffic (two engines idling and
 * cruising, far off, drifting in pitch and loudness), and now and then
 * cars pull away a street or two off, revving through a gear change. Nothing near; nothing that could pass for a voice.
 * user[0] the car's time, [1] its length, [2] its peak (pitch), [3] its voice + 1, [4] [5] the murmur's drift; ag[0]
 * where it set off (pan, dist) and which way (tempo) */
static void engines_block(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    float act = src_tod(e, s) * (0.3f + 1.4f * e->eff.activity) * e->dens[L_HUMANS] * e->gov;
    static const float F[2] = { 32.0f, 43.0f }, PAN[2] = { -0.5f, 0.45f };
    for (int k = 0; k < 2; k++)
        if (!h->v[k].on) { hum_start(h, F[k], 0.5f, 0.5f, 0.3f, 0.75f, 260, 5.5f + k, 0.06f, 0, 3.0f); spatial_set(&h->v[k].sp, PAN[k], 0.85f); }   /* (deep: a rumble, mostly noise: traffic doesn't sit on a note) */
    s->user[4] += (rng_bi(&s->rng) * 0.6f - s->user[4]) * BLK * 0.25f;   /* (traffic: a little busier, a little quieter) */
    s->user[5] += (rng_bi(&s->rng) * 0.6f - s->user[5]) * BLK * 0.15f;
    for (int k = 0; k < 2; k++) {
        h->v[k].pmul = 1.0f + 0.07f * (k ? s->user[5] : s->user[4]);
        h->v[k].target = (0.035f + 0.045f * act) * (1.0f + 0.35f * (k ? s->user[4] : s->user[5]));
    }
    if (s->user[3] <= 0) {                           /* a car pulling away, about one in 7 s when it's busy */
        if (rng_f(&s->rng) < act * 0.14f * BLK) {
            engine_var_begin(e, s);
            int v = hum_start(h, rng_range(&s->rng, 28, 36), 0.5f, 1.3f, 0.4f, 0.35f, 350, 0, 0, 0, 0.3f);
            engine_var_end();
            if (v >= 0) {
                Agent *a = &s->ag[0];
                a->pan = rng_range(&s->rng, -0.7f, 0.7f); a->dist = rng_range(&s->rng, 0.6f, 0.72f);
                a->tempo = a->pan > 0 ? -1.0f : 1.0f;     /* (it drives off across the middle) */
                spatial_set(&h->v[v].sp, a->pan, a->dist);
                h->v[v].target = 0.02f;                  /* (a voice at nothing switches itself off: it starts just above) */
                s->user[0] = 0; s->user[1] = rng_range(&s->rng, 3.5f, 5.5f); s->user[2] = rng_range(&s->rng, 2.4f, 3.2f); s->user[3] = (float)(v + 1);
                engine_post(e, L_HUMANS, GL_CAR, a->pan, a->dist, 0, (int)a->tempo, s->user[1] * 1.5f);
                s->events++; e->layer_events[L_HUMANS]++;
            }
        }
        return;
    }
    int v = (int)s->user[3] - 1;
    Agent *a = &s->ag[0];
    float u = (s->user[0] += BLK) / s->user[1], env;
    /* the revs: up through first gear, a dip at the change, up again through second, then easing off */
    if (u < 0.4f) env = fm_sin_turns(0.25f * u / 0.4f);
    else if (u < 0.47f) env = 1.0f - 0.45f * (u - 0.4f) / 0.07f;
    else if (u < 0.75f) env = 0.55f + 0.35f * fm_sin_turns(0.25f * (u - 0.47f) / 0.28f);
    else if (u < 1.0f) env = 0.9f - 0.6f * (u - 0.75f) / 0.25f;
    else env = 0;
    h->v[v].pmul = 1.0f + (s->user[2] - 1.0f) * env;
    h->v[v].index = 1.3f + 0.9f * env;                          /* harder as it revs, and still dark */
    h->v[v].noise_cut = 350.0f + 350.0f * env;
    h->v[v].target = (0.22f + 0.16f * act) * (0.3f + 0.7f * env) * (1.0f - 0.5f * u);   /* fading as it goes */
    spatial_set(&h->v[v].sp, a->pan + a->tempo * 0.5f * u, a->dist + 0.15f * u);
    if (u >= 1.0f) { hum_release(h, v, 1.0f); s->user[3] = 0; }
}
/* Cars going by on the street (matched to traffic recordings): mostly tyres on the road, an engine low under it;
 * one every few seconds when it's busy, two or three at once at times. Each swells and brightens as it passes and
 * crosses the field; it never sits on a note. Agent k is car k (hum voice k): pan its time, next its length, dist
 * its closest, tempo its way, pitch its loudness, ph its tyres' brightness */
static void cars_block(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    float act = src_tod(e, s) * (0.2f + 1.6f * e->eff.activity) * e->dens[L_HUMANS] * e->gov;
    if (rng_f(&s->rng) < act * 0.22f * BLK) {             /* a car every 4-5 s when it's busy */
        engine_var_begin(e, s);
        int n = hum_start(h, rng_range(&s->rng, 45, 75), 0.5f, 0.4f, 0.3f, 0.9f, 3000, 0, 0, 0, 0.3f);
        engine_var_end();
        if (n >= 3) hum_release(h, n, 0.05f);                /* (three at once at most) */
        else if (n >= 0) {
            Agent *a = &s->ag[n];
            a->singing = 1; a->pan = 0; a->next = rng_range(&s->rng, 3.5f, 7.0f); a->dist = rng_range(&s->rng, 0.4f, 0.7f);
            a->tempo = rng_f(&s->rng) < 0.5f ? -1.0f : 1.0f; a->pitch = rng_range(&s->rng, 0.6f, 1.0f); a->ph = rng_range(&s->rng, 2600, 4200);
            h->v[n].target = 1e-3f;
            engine_post(e, L_HUMANS, GL_CAR, -a->tempo, a->dist, 0, (int)a->tempo, a->next);
            s->events++; e->layer_events[L_HUMANS]++;
        }
    }
    for (int k = 0; k < 3; k++) {
        Agent *a = &s->ag[k];
        HVoice *v = &h->v[k];
        if (!a->singing) continue;
        float u = (a->pan += BLK) / a->next;
        if (u >= 1.0f || !v->on) { hum_release(h, k, 0.4f); a->singing = 0; continue; }
        float x = a->tempo * (u * 2.0f - 1.0f) * 1.6f, near = 0.25f + a->dist, r = fm_sqrtf(x * x + near * near);
        float close = near / r;                                    /* 1 going by, less either side */
        spatial_set(&v->sp, x / r * 0.9f, a->dist + (1.0f - a->dist) * (1.0f - close) * 0.7f);
        v->pmul = 1.0f / (1.0f + (x / r) * a->tempo * 3.2f / a->next * 0.04f);   /* (a touch of Doppler) */
        v->noise_cut = a->ph * (0.45f + 0.55f * close);           /* the tyres' hiss brightest as it passes */
        float edge = u < 0.12f ? u / 0.12f : u > 0.88f ? (1.0f - u) / 0.12f : 1.0f;
        v->target = 0.7f * a->pitch * close * close * edge;      /* (a swell of several dB as it goes by) */
    }
}

static void h_city(Engine *e) {
    static const float tod_t[4] = { 0.6f, 1.0f, 0.9f, 0.25f }, tod_c[4] = { 0.6f, 1.0f, 0.9f, 0.3f };
    Source *s = add_far_noise(e, "traffic roar", 2.0f, 700, 0.05f, 0.17f); memcpy(s->tod, tod_t, sizeof s->tod);
    s = engine_add(e, "distant engines", L_HUMANS, G_HUM, B_BED);
    s->g.hum.level = 0.6f; s->glyph = GL_CAR; s->block = engines_block; memcpy(s->tod, tod_c, sizeof s->tod);
    s = engine_add(e, "passing cars", L_HUMANS, G_HUM, B_BED);
    s->g.hum.level = 0.6f; s->glyph = GL_CAR; s->block = cars_block; memcpy(s->tod, tod_c, sizeof s->tod);
    add_jets(e, 0.025f, 0.35f, 0.6f);
    add_trains(e, 0.018f, 0.5f, 0.72f, 5, 7);      /* fast: across in 5-7 s, quicker than the cars; one a minute or so */
}

static void boat_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)pan; (void)dist; (void)x; s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 85, 100), 1.0f, 1.5f, 0.5f, 0.3f, 900, 6.0f, 0.05f, 0, 2.0f);
}
static void voices_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; crowd_talk(&s->g.crowd, &s->rng, rng_range(&s->rng, 110, 220), rng_range(&s->rng, 0.8f, 2.5f), 1.0f, pan, dist, 0.6f);
}
static void h_jungle(Engine *e) {
    static const float tod_v[4] = { 0.6f, 1.0f, 0.8f, 0.1f };
    add_passby(e, "river boat", boat_fire, 0.012f, 20, 30, 0.4f, 0.7f, 0.06f, 0.24f, GL_BOAT);
    Source *s = engine_add(e, "far voices", L_HUMANS, G_CROWD, B_SCATTER);
    s->rate = 0.15f; s->g.crowd.level = 0.4f; s->dist_lo = 0.85f; s->dist_hi = 0.95f; s->fire = voices_fire; s->glyph = GL_DOT; memcpy(s->tod, tod_v, sizeof s->tod);
}

static void hull_drone_block(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    if (!h->v[0].on) { hum_start(h, 55, 2.0f, 0.8f, 1.2f, 0.05f, 400, 0.1f, 0.1f, 0, 5.0f); spatial_set(&h->v[0].sp, -0.45f, 0.6f); }
    if (!h->v[1].on) { hum_start(h, 82.5f, 1.5f, 0.3f, 1.6f, 0.2f, 700, 0.0f, 0.0f, 0, 5.0f); spatial_set(&h->v[1].sp, 0.5f, 0.5f); }
    s->user[0] += BLK * 0.08f; if (s->user[0] > 1) s->user[0] -= 1;   /* life support breathing */
    float act = 0.3f + 0.7f * e->eff.activity;
    h->v[0].target = (0.07f + 0.13f * act);
    h->v[1].target = (0.04f + 0.12f * act) * (0.5f + 0.5f * fm_sin_turns(s->user[0]));
}
static void telemetry_fire(Engine *e, Source *s, float pan, float dist, float x) {
    static const float P[3] = { 1.0f, 1.189f, 1.498f };
    (void)e; (void)x; voice_play(&s->g.voice, &TELEMETRY, P[rng_int(&s->rng, 3)], 1.0f, pan, dist, 1.0f);
}
static void comms_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x; crowd_talk(&s->g.crowd, &s->rng, rng_range(&s->rng, 130, 200), rng_range(&s->rng, 0.8f, 2.0f), 1.25f, pan, dist, 0.6f);   /* radio chatter */
}
static void h_space(Engine *e) {
    Source *s = engine_add(e, "hull drone", L_HUMANS, G_HUM, B_BED); s->g.hum.level = 0.5f; s->block = hull_drone_block;
    s = engine_add(e, "telemetry", L_HUMANS, G_VOICE, B_SCATTER);
    s->rate = 0.5f; s->g.voice.level = 0.35f; s->dist_lo = 0.3f; s->dist_hi = 0.7f; s->fire = telemetry_fire; s->glyph = GL_SQUARE;
    s = engine_add(e, "comms", L_HUMANS, G_CROWD, B_SCATTER);
    s->rate = 0.04f; s->g.crowd.level = 0.4f; s->dist_lo = 0.55f; s->dist_hi = 0.7f; s->fire = comms_fire; s->glyph = GL_BEACON;
}

static void knock_fire(Engine *e, Source *s, float pan, float dist, float x) { (void)e; (void)x; roll_start(s, 3, 0.45f, rng_range(&s->rng, 160, 220), pan, dist, 0.7f); }
static void knock_block(Engine *e, Source *s) { (void)e; roll_step(s, &MAT_WOOD, 0.5f, 2.0f, 0.95f); }
static void h_caves(Engine *e) {
    Source *s = engine_add(e, "deep rumble", L_HUMANS, G_AIR, B_BED);
    s->g.air.colour = 2.0f; s->g.air.cutoff = 90; s->g.air.gust_depth = 0.3f; s->g.air.gust_speed = 0.3f; s->g.air.level = 0.35f; s->g.air.width = 0.6f; s->g.air.send = 0.3f;
    s = engine_add(e, "distant knocks", L_HUMANS, G_RING, B_SCATTER);
    s->rate = 0.03f; s->g.ring.level = 0.5f; s->dist_lo = 0.88f; s->dist_hi = 0.95f; s->fire = knock_fire; s->block = knock_block; s->glyph = GL_RING;
}

static void prop_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)pan; (void)dist; (void)x; s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 85, 110), 1.0f, 0.4f, 0.6f, 0.75f, 650, 0, 0, 0, 3.0f);
}
static void h_forest(Engine *e) {
    add_far_noise(e, "far road", 2.0f, 260, 0.05f, 0.12f);
    add_passby(e, "aircraft", prop_fire, 0.018f, 22, 32, 0.7f, 0.9f, 0.06f, 0.29f, GL_PLANE);
}

static void airboat_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)pan; (void)dist; (void)x; s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 100, 120), 1.5f, 2.0f, 1.0f, 0.5f, 1600, 0, 0, 0, 1.5f);
}
static void h_swamp(Engine *e) {
    add_passby(e, "airboat", airboat_fire, 0.012f, 12, 18, 0.3f, 0.6f, 0.08f, 0.2f, GL_BOAT);
    add_far_noise(e, "distant hum", 2.0f, 140, 0.05f, 0.06f);
}

static void buoy_block(Engine *e, Source *s) {
    int hit = e->swell_peak || (s->user[0] -= BLK) < 0;   /* rocked by the swell; on its own without one */
    if (!hit) return;
    s->user[0] = rng_range(&s->rng, 5.0f, 10.0f);
    ring_strike(&s->g.ring, &s->rng, &MAT_BELL, 330.0f, rng_range(&s->rng, 0.4f, 0.8f), 0.6f, 1.0f, 0.55f, 0.75f);
    engine_post(e, L_HUMANS, GL_BEACON, 0.55f, 0.75f, 0, 0, 0);
}
static void horn_block(Engine *e, Source *s) {    /* a ship's horn: rare, long blasts */
    Hum *h = &s->g.hum;
    if (s->user[0] > 0) { s->user[0] -= BLK; if (s->user[0] <= 0) hum_release(h, 0, 0.6f); return; }
    if (h->v[0].on) return;
    if (rng_f(&s->rng) < 0.006f * BLK * (0.3f + 1.4f * e->ent) * e->actf) {
        hum_start(h, 75, 2.0f, 1.5f, 0.4f, 0.05f, 300, 0, 0, 0.35f, 0.3f);
        spatial_set(&h->v[0].sp, rng_range(&s->rng, -0.8f, 0.8f), 0.85f);
        s->user[0] = rng_range(&s->rng, 2.5f, 4.0f);
        engine_post(e, L_HUMANS, GL_BOAT, -1, 0.9f, 1, 0, 6.0f);
    }
}
static void rigging_fire(Engine *e, Source *s, float pan, float dist, float x) { (void)e; (void)x; roll_start(s, 2 + rng_int(&s->rng, 3), 0.12f, rng_range(&s->rng, 3000, 4000), pan, dist, 0.4f); }
static void rigging_block(Engine *e, Source *s) { s->rate = 0.1f * (0.2f + 1.5f * e->wind); roll_step(s, &MAT_CHAIN, 0.8f, 1.0f, 0.8f); }
static void h_ocean(Engine *e) {
    Source *s = engine_add(e, "buoy bell", L_HUMANS, G_RING, B_BED); s->g.ring.level = 0.6f; s->block = buoy_block; s->user[0] = 3;
    s = engine_add(e, "ship horn", L_HUMANS, G_HUM, B_BED); s->g.hum.level = 0.5f; s->block = horn_block;
    add_passby(e, "distant boat", boat_fire, 0.025f, 25, 35, 0.55f, 0.8f, 0.05f, 0.35f, GL_BOAT);
    s = engine_add(e, "rigging", L_HUMANS, G_RING, B_SCATTER);
    s->g.ring.level = 0.45f; s->dist_lo = 0.3f; s->dist_hi = 0.6f; s->fire = rigging_fire; s->block = rigging_block; s->glyph = GL_SQUARE;
}

static void craft_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)pan; (void)dist; (void)x; s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 120, 160), 1.414f, 2.5f, 1.5f, 0.15f, 1500, 0.7f, 0.5f, 0, 2.0f);
}
static void beacon_fire(Engine *e, Source *s, float pan, float dist, float x) { (void)e; (void)x; voice_play(&s->g.voice, &BEACON, 1.0f, 1.0f, pan, dist, 1.0f); }
static void alien_drone_block(Engine *e, Source *s) {
    HVoice *v = &s->g.hum.v[0];
    if (!v->on) { hum_start(&s->g.hum, 55, 2.0f, 0.8f, 0.35f, 0.05f, 400, 0.1f, 0.1f, 0, 5.0f); spatial_set(&v->sp, 0, 0.6f); }
    v->target = 0.12f + 0.3f * e->eff.activity;
}
static void h_alien(Engine *e) {
    add_passby(e, "craft", craft_fire, 0.035f, 14, 20, 0.25f, 0.55f, 0.08f, 0.4f, GL_CRAFT);
    Source *s = engine_add(e, "beacon", L_HUMANS, G_VOICE, B_SCATTER);
    s->rate = 0.22f; s->regular = 1; s->pan_lo = -0.65f; s->pan_hi = -0.55f; s->dist_lo = 0.75f; s->dist_hi = 0.85f;
    s->g.voice.level = 0.4f; s->fire = beacon_fire; s->glyph = GL_BEACON;
    s = engine_add(e, "engine drone", L_HUMANS, G_HUM, B_BED); s->g.hum.level = 0.5f; s->block = alien_drone_block;
}

/* The motor hall (matched to a recording of factory machinery): two big motors a little apart (269 and 242 Hz, the
 * first with its octave) beating against each other, a dark rumble of the works under them, all of it rattling
 * and surging every few seconds; and the mains' 100 Hz low under that */
static void factory_hum_block(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    if (!h->v[0].on) { hum_start(h, 50, 2.0f, 0.6f, 0.2f, 0.0f, 400, 0, 0, 0, 3.0f); spatial_set(&h->v[0].sp, -0.2f, 0.5f); }
    if (!h->v[1].on) { hum_start(h, 269, 2.0f, 0.4f, 0.6f, 0.9f, 1300, 0, 0, 0, 3.0f); spatial_set(&h->v[1].sp, 0.35f, 0.45f); h->v[1].rattle = 0.45f; }
    if (!h->v[2].on) { hum_start(h, 242, 1.0f, 0.2f, 0.8f, 0.8f, 1000, 0, 0, 0, 3.0f); spatial_set(&h->v[2].sp, -0.4f, 0.5f); h->v[2].rattle = 0.4f; }
    float act = src_tod(e, s) * (0.2f + 1.6f * e->eff.activity);
    s->user[4] += (rng_bi(&s->rng) * 1.4f - s->user[4]) * BLK * 0.6f;   /* the surges: every few seconds */
    float surge = 1.0f + 0.3f * s->user[4];
    h->v[0].target = 0.1f + 0.06f * act;
    h->v[1].target = (0.08f + 0.1f * act) * surge; h->v[2].target = (0.06f + 0.08f * act) * (2.0f - surge);
}
/* The diesel plant (matched to a building-site recording): a big engine idling, 64-70 Hz with its overtones and
 * the chug of its turning (a quarter of that), a faint whine of hydraulics; now and then it takes the load,
 * revving up for a few seconds as it digs or lifts. user[0] the work left, [1] the next */
static void diesel_block(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    if (!h->v[0].on) {
        float f = rng_range(&s->rng, 64, 70);
        hum_start(h, f, 1.0f, 1.6f, 0.4f, 0.55f, 2200, 0, 0, 0, 3.0f); spatial_set(&h->v[0].sp, 0.5f, 0.55f);
        h->v[0].chug_rate = f / 4.0f; h->v[0].chug_depth = 0.45f;
    }
    if (!h->v[1].on) { hum_start(h, 1276, 1.56f, 0.3f, 1.5f, 0, 2000, 0.4f, 0.1f, 0, 3.0f); spatial_set(&h->v[1].sp, 0.45f, 0.6f); }
    float act = src_tod(e, s) * (0.2f + 1.6f * e->eff.activity) * e->dens[L_HUMANS] * e->gov, work = 0;
    if (s->user[0] > 0) {
        float u = 1.0f - (s->user[0] -= BLK) / s->user[2];
        work = u < 0.2f ? u / 0.2f : u > 0.8f ? (1.0f - u) / 0.2f : 1.0f;
    } else if ((s->user[1] -= BLK * (0.3f + act)) <= 0) {
        s->user[2] = s->user[0] = rng_range(&s->rng, 3.0f, 8.0f); s->user[1] = rng_range(&s->rng, 10.0f, 30.0f);
        engine_post(e, L_HUMANS, GL_SQUARE, 0.5f, 0.55f, 0, 0, 0);
        s->events++; e->layer_events[L_HUMANS]++;
    }
    h->v[0].pmul = h->v[1].pmul = 1.0f + 0.25f * work;
    h->v[0].index = 1.6f + 0.8f * work;
    h->v[0].target = (0.17f + 0.1f * act) * (1.0f + 0.6f * work);
    h->v[1].target = 0.012f + 0.02f * work;
}
/* A press, run as a machine is: spells of stamping (its own pace, a little uneven), then idle; each stamp a metal
 * clank, a lower thud kept short, and now and then the clatter of the part dropping. user[0] stamps left in the
 * spell, [1] to the next, [2] its pace, [3] its pitch, [4] where (pan), [5] how far */
static void press_stamp(Engine *e, Source *s) {
    float f = s->user[3] * rng_range(&s->rng, 0.96f, 1.04f), pan = s->user[4], dist = s->user[5];
    engine_var_begin(e, s);
    ring_strike(&s->g.ring, &s->rng, &MAT_SLAM, f * 0.6f, rng_range(&s->rng, 0.8f, 1.0f), 0.7f, 1.0f, pan, dist);   /* the slam */
    ring_strike(&s->g.ring, &s->rng, &MAT_SCRAP, f * rng_range(&s->rng, 2.4f, 3.1f), rng_range(&s->rng, 0.25f, 0.4f), 0.7f, 0.8f, pan, dist);   /* the die's clank */
    engine_var_end();
    engine_post(e, L_HUMANS, GL_PRESS, pan, dist, 0, 0, 0);
    s->events++; e->layer_events[L_HUMANS]++;
}
static void press_block(Engine *e, Source *s) {
    float life = src_tod(e, s) * (0.3f + 1.4f * e->eff.activity) * e->dens[L_HUMANS] * e->gov;
    if (s->user[0] <= 0) {                           /* idle: a new spell, sooner when it's busy */
        if (rng_f(&s->rng) < life * 0.12f * BLK) {
            s->user[0] = 4 + rng_int(&s->rng, 14); s->user[1] = 0.2f; s->user[2] = rng_range(&s->rng, 0.8f, 1.7f);
            s->user[3] = rng_range(&s->rng, 120, 190); s->user[4] = rng_range(&s->rng, -0.2f, 0.6f); s->user[5] = rng_range(&s->rng, 0.45f, 0.75f);
        }
        return;
    }
    s->user[1] -= BLK;
    if (s->user[1] > 0) return;
    press_stamp(e, s);
    s->user[0] -= 1;
    s->user[1] = s->user[2] * rng_range(&s->rng, 0.9f, 1.12f);   /* (a machine, but an old one) */
}
static void clang_fire(Engine *e, Source *s, float pan, float dist, float x) {   /* the yard: metal dropped, struck, dragged */
    (void)e; (void)x;
    const Material *m = rng_f(&s->rng) < 0.6f ? &MAT_SCRAP : &MAT_CHAIN;
    float f = m == &MAT_SCRAP ? rng_range(&s->rng, 200, 600) : rng_range(&s->rng, 1500, 3500);
    ring_strike(&s->g.ring, &s->rng, m, f, rng_range(&s->rng, 0.3f, 0.8f), 0.6f, 1.0f, pan, dist);
    if (rng_f(&s->rng) < 0.4f) roll_start(s, 2 + rng_int(&s->rng, 4), 0.09f, f * 1.3f, pan, dist, 0.5f);
}
static void clang_block(Engine *e, Source *s) { (void)e; roll_step(s, &MAT_SCRAP, 0.6f, 0.8f, 0.8f); }
static void air_hiss_fire(Engine *e, Source *s, float pan, float dist, float x) {   /* compressed air let go: a hiss */
    (void)e; (void)pan; (void)dist; (void)x; air_burst(&s->g.air, rng_range(&s->rng, 0.2f, 1.1f));
}
static void forklift_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)pan; (void)dist; (void)x; s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 130, 175), 1.0f, 0.35f, 0.9f, 0.6f, 1300, 0.4f, 0.1f, 0, 1.0f);
}
static void freight_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)pan; (void)dist; (void)x;
    s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 65, 80), 1.5f, 1.0f, 0.8f, 0.6f, 500, 1.6f, 0.05f, 0, 4.0f);
    s->user[4] = rng_range(&s->rng, 0.9f, 1.2f);
}
static void h_industrial(Engine *e) {
    static const float tod_m[4] = { 0.8f, 1.0f, 0.8f, 0.6f }, tod_p[4] = { 0.6f, 1.0f, 0.8f, 0.5f }, tod_b[4] = { 0.8f, 1.0f, 0.8f, 0.3f };
    Source *s = engine_add(e, "motor hall", L_HUMANS, G_HUM, B_BED); s->g.hum.level = 0.5f; memcpy(s->tod, tod_m, sizeof s->tod); s->block = factory_hum_block;
    s = engine_add(e, "diesel plant", L_HUMANS, G_HUM, B_BED); s->g.hum.level = 0.5f; memcpy(s->tod, tod_p, sizeof s->tod); s->block = diesel_block;
    /* the press, the yard, the air, the yard's traffic: quieter now, in among the machines */
    s = engine_add(e, "stamping press", L_HUMANS, G_RING, B_BED);
    s->g.ring.level = 0.45f; memcpy(s->tod, tod_p, sizeof s->tod); s->block = press_block; s->glyph = GL_PRESS;
    s = engine_add(e, "metal in the yard", L_HUMANS, G_RING, B_SCATTER);
    s->rate = 0.22f; s->alive = 1; s->g.ring.level = 0.35f; s->dist_lo = 0.5f; s->dist_hi = 0.9f; memcpy(s->tod, tod_p, sizeof s->tod);
    s->fire = clang_fire; s->block = clang_block; s->glyph = GL_BAR;
    s = engine_add(e, "air release", L_HUMANS, G_AIR, B_SCATTER);
    s->g.air.burst_mode = 1; s->g.air.colour = 0.1f; s->g.air.cutoff = 6000; s->g.air.hp = 2000; s->g.air.gust_depth = 0.2f;
    s->g.air.level = 0.14f; s->g.air.width = 0.25f; s->g.air.send = 0.35f; s->rate = 0.12f; s->alive = 1; memcpy(s->tod, tod_p, sizeof s->tod);
    s->fire = air_hiss_fire; s->glyph = GL_STEAM;
    s = add_passby(e, "forklift", forklift_fire, 0.05f, 6, 11, 0.4f, 0.7f, 0.06f, 0.17f, GL_CAR); memcpy(s->tod, tod_b, sizeof s->tod);
    s = engine_add(e, "reversing beeper", L_HUMANS, G_VOICE, B_PHRASE);
    s->species[0] = &SP_BEEPER; s->nspecies = 1; s->g.voice.level = 0.28f; s->dist_lo = 0.6f; s->dist_hi = 0.85f; memcpy(s->tod, tod_b, sizeof s->tod);
    s->glyph = GL_SQUARE; phrase_setup(s, &s->rng, 1);
    s = add_passby(e, "freight train", freight_fire, 0.012f, 25, 35, 0.6f, 0.85f, 0.05f, 0.55f, GL_TRAIN);
    s = engine_add(e, "freight wheels", L_HUMANS, G_RING, B_BED); s->g.ring.level = 0.3f; s->block = wheels_block;
    add_jets(e, 0.02f, 0.4f, 0.7f);
}

static void lorry_fire(Engine *e, Source *s, float pan, float dist, float x) {   /* far off on the main road: a deep drone */
    (void)e; (void)pan; (void)dist; (void)x; s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 55, 70), 0.5f, 0.9f, 0.3f, 0.3f, 300, 0, 0, 0, 3.0f);
}
static void h_meadow(Engine *e) {
    static const float tod_r[4] = { 0.5f, 1.0f, 0.9f, 0.25f };
    Source *s = add_far_noise(e, "distant road", 2.0f, 260, 0.04f, 0.1f); memcpy(s->tod, tod_r, sizeof s->tod);
    s = add_passby(e, "far lorry", lorry_fire, 0.02f, 18, 30, 0.85f, 0.95f, 0.03f, 0.22f, GL_CAR); memcpy(s->tod, tod_r, sizeof s->tod);
    s = add_passby(e, "light aircraft", prop_fire, 0.018f, 25, 40, 0.6f, 0.85f, 0.06f, 0.3f, GL_PLANE);
    s->tod[0] = 0.4f; s->tod[1] = 1.0f; s->tod[2] = 0.8f; s->tod[3] = 0.0f;
    add_jets(e, 0.012f, 0.6f, 0.8f);
}

/* ======================================================================= five more places
 * MOUNTAIN, ARCTIC, DESERT, SAVANNA, VILLAGE (10-14). New sounds made from the generators already here: a buzzard's
 * mew, a raven's croak, marmots' whistles, cowbells, a bell tolling far below, a helicopter (a hum that chugs);
 * ice cracking (the falling "pew" ice makes), the ice's groan, a bearded seal's trill, wolves; booming dunes,
 * a scops owl, jackals, a caravan's bells; a lion's roar and its grunts, hyenas, a hornbill, doves, a fire's
 * crackle; bells ringing changes, rooks, ducks, cows, a dog, a cockerel, a lawnmower. */

/* --------------------------------------------------------------- MOUNTAIN: a pass, the valley far below */
static const Syllable BUZZARD[] = { { 2600, 1900, CURVE_EXP, 0.75f, 0.12f, 0, 0, 0.3f, 0, 0, 6, 0.15f, 1 } };   /* a mewing "pee-yow" */
static const Species SP_BUZZARD = { BUZZARD, 1, { "A", "A--A", "A-A" }, 3, 0.06f, 0.1f, 0.6f, 15.0f, 35.0f, 1, 2, 0.55f };
static const Syllable RAVEN[] = { { 520, 470, CURVE_LIN, 0.22f, 0.15f, 0, 0, 0.5f, 1.0f, 1.8f, 0, 0, 1 },
                                  { 640, 560, CURVE_EXP, 0.12f, 0.2f, 0, 0, 0.4f, 1.0f, 1.4f, 0, 0, 0.8f } };   /* a hollow "kronk" */
static const Species SP_RAVEN = { RAVEN, 2, { "A-A", "AB-A", "B-B-A" }, 3, 0.06f, 0.1f, 0.3f, 12.0f, 30.0f, 1, 2, 0.5f };
static const Syllable CHOUGH[] = { { 3200, 3800, CURVE_SWOOP, 0.14f, 0.2f, 0, 0, 0.1f, 0, 0, 0, 0, 1 } };   /* a high "chree" */
static const Species SP_CHOUGH = { CHOUGH, 1, { "A-A", "AA-A", "A" }, 3, 0.1f, 0.1f, 0.25f, 6.0f, 15.0f, 1, 3, 0.4f };
static const Syllable MARMOT[] = { { 2900, 2700, CURVE_LIN, 0.18f, 0.05f, 0, 0, 0, 0, 0, 0, 0, 0.9f } };   /* the alarm: a sharp whistle */
static const Species SP_MARMOT = { MARMOT, 1, { "A", "A--A", "A--A--A" }, 3, 0.03f, 0.05f, 0.6f, 20.0f, 60.0f, 1, 1, 0.6f };
static const Syllable EAGLEOWL[] = { { 310, 290, CURVE_LIN, 0.4f, 0.3f, 0, 0, 0.15f, 0, 0, 0, 0, 1 }, { 300, 260, CURVE_EXP, 0.25f, 0.2f, 0, 0, 0.12f, 0, 0, 0, 0, 0.8f } };
static const Species SP_EAGLEOWL = { EAGLEOWL, 2, { "A-B", "A-B--A-B" }, 2, 0.03f, 0.05f, 0.5f, 8.0f, 20.0f, 1, 1, 0.5f };
static const Material MAT_COWBELL = { 4, { 1, 1.52f, 2.36f, 3.1f }, { 0.45f, 0.3f, 0.2f, 0.12f }, { 1, 0.7f, 0.45f, 0.3f }, 0.8f, 2500, 6 };

static void mountain_wind(Engine *e, Source *s) { wind_follow(e, s, 0.25f, 0.65f, 0.15f, 0.6f); }
static void mountain_rain(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 2600.0f); }
static void w_mountain(Engine *e) {
    Source *s = add_wind(e, "ridge wind", 1.2f, 1, 600, 0.7f, 520, 12, 0.35f); s->block = mountain_wind;
    s = engine_add(e, "sleet on rock", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.36f; r->hiss = 0.05f; r->click = 0.1f; r->send = 0.35f; r->width = 1.0f;
      r->wash = 0.8f; r->wash_white = 0.6f; r->wash_hp = 400; r->wash_lp = 7500; r->tone = 5500; r->var = 0.4f; r->drops = 0.6f;
      static const float f[4] = { 2600, 4200, 1500, 900 }, q[4] = { 2.5f, 3, 2, 1.5f }, w[4] = { 3, 2, 1, 1 }, g[4] = { 1, 0.9f, 0.8f, 0.6f }; set_surfaces(r, f, q, w, g); }
    s->block = mountain_rain;
    add_thunder(e, 1.0f, 0, 0);
}
static void a_mountain(Engine *e) {
    static const float tod_r[4] = { 1.0f, 1.0f, 0.8f, 0.0f }, tod_m[4] = { 0.8f, 1.0f, 0.5f, 0.0f };
    add_phrase(e, "buzzard", 0.4f, 0.5f, 0.9f, 0.3f, 2, &SP_BUZZARD, 0, 0, 0, tod_r, GL_BIGBIRD);
    add_phrase(e, "ravens and choughs", 0.4f, 0.3f, 0.85f, 0.5f, 5, &SP_RAVEN, &SP_CHOUGH, &SP_CHOUGH, 0, tod_r, GL_BIRD);
    add_phrase(e, "marmots", 0.35f, 0.3f, 0.8f, 0.4f, 2, &SP_MARMOT, 0, 0, 0, tod_m, GL_DOT);
    add_phrase(e, "eagle owl", 0.4f, 0.5f, 0.85f, 0.0f, 1, &SP_EAGLEOWL, 0, 0, 0, TOD_OWL, GL_BIGBIRD);
}
static void scree_fire(Engine *e, Source *s, float pan, float dist, float x) {   /* a stone or two, skittering */
    (void)e; (void)x;
    roll_start(s, 2 + rng_int(&s->rng, 4), rng_range(&s->rng, 0.07f, 0.14f), rng_range(&s->rng, 900, 1700), pan, dist, rng_range(&s->rng, 0.3f, 0.6f));
}
static void scree_block(Engine *e, Source *s) { (void)e; roll_step(s, &MAT_STONE, 0.9f, 0.8f, 0.75f); }
static void l_mountain(Engine *e) {
    Source *s = engine_add(e, "far waterfall", L_LANDSCAPE, G_WATER, B_BED);
    { Water *w = &s->g.water; w->rate = 180; w->dmin = 4; w->dmax = 14; w->gamma = 2.0f; w->level = 0.2f; w->rise = 0.03f;
      w->pan_lo = -0.9f; w->pan_hi = -0.5f; w->dist_lo = 0.85f; w->dist_hi = 0.95f; w->rush = 1.0f; w->rush_cut = 1300; }
    s = engine_add(e, "rockfall", L_LANDSCAPE, G_RING, B_RARE);
    s->rate = 0.015f; s->cooldown = 40; s->g.ring.level = 0.6f; s->dist_lo = 0.5f; s->dist_hi = 0.9f; s->fire = rockfall_fire; s->block = rockfall_block; s->glyph = GL_ROCK;
    s = engine_add(e, "scree", L_LANDSCAPE, G_RING, B_SCATTER);
    s->rate = 0.06f; s->g.ring.level = 0.4f; s->dist_lo = 0.2f; s->dist_hi = 0.6f; s->fire = scree_fire; s->block = scree_block; s->glyph = GL_ROCK;
}
/* cows grazing below: their bells clonk as they move, a few at a time, unevenly. user[0] clonks left, [1] to the next, [2] the bell, [3] where */
static void cowbell_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)dist; (void)x;
    s->user[0] = (float)(2 + rng_int(&s->rng, 6)); s->user[1] = 0; s->user[2] = rng_range(&s->rng, 520, 900); s->user[3] = pan;
}
static void cowbell_block(Engine *e, Source *s) {
    (void)e;
    if (s->user[0] <= 0 || (s->user[1] -= BLK) > 0) return;
    ring_strike(&s->g.ring, &s->rng, &MAT_COWBELL, s->user[2] * rng_range(&s->rng, 0.99f, 1.01f), rng_range(&s->rng, 0.3f, 0.8f), 0.7f, 1.0f, s->user[3], rng_range(&s->rng, 0.75f, 0.9f));
    s->user[0] -= 1; s->user[1] = rng_range(&s->rng, 0.25f, 1.1f);
}
/* a church far down the valley tolls: a slow run of strikes every few minutes. user[0] strikes left, [1] to the next, [2] till the next run */
static void valley_bell_block(Engine *e, Source *s) {
    if (s->user[0] > 0) {
        if ((s->user[1] -= BLK) > 0) return;
        ring_strike(&s->g.ring, &s->rng, &MAT_CHURCH, 285.0f, rng_range(&s->rng, 0.5f, 0.7f), 0.5f, 1.2f, 0.35f, 0.93f);
        engine_post(e, L_HUMANS, GL_BEACON, 0.35f, 0.93f, 0, 0, 0);
        s->user[0] -= 1; s->user[1] = 2.4f;
    } else if ((s->user[2] -= BLK) <= 0) { s->user[0] = (float)(3 + rng_int(&s->rng, 10)); s->user[1] = 0; s->user[2] = rng_range(&s->rng, 180.0f, 420.0f); }
}
static void heli_fire(Engine *e, Source *s, float pan, float dist, float x) {   /* the rotor: a hum that chugs */
    (void)e; (void)pan; (void)dist; (void)x;
    int n = hum_start(&s->g.hum, rng_range(&s->rng, 70, 90), 1.0f, 1.0f, 0.3f, 0.6f, 1800, 0, 0, 0, 3.0f);
    s->pb_voice = n;
    if (n >= 0) { s->g.hum.v[n].chug_rate = rng_range(&s->rng, 17.0f, 22.0f); s->g.hum.v[n].chug_depth = 0.6f; }
}
static void h_mountain(Engine *e) {
    static const float tod_c[4] = { 0.8f, 1.0f, 0.9f, 0.2f };
    Source *s = engine_add(e, "cowbells", L_HUMANS, G_RING, B_SCATTER);
    s->rate = 0.12f; s->alive = 1; s->g.ring.level = 0.45f; s->dist_lo = 0.75f; s->dist_hi = 0.9f; s->fire = cowbell_fire; s->block = cowbell_block; s->glyph = GL_CHIME;
    memcpy(s->tod, tod_c, sizeof s->tod);
    s = engine_add(e, "valley church", L_HUMANS, G_RING, B_BED); s->g.ring.level = 0.5f; s->block = valley_bell_block; s->user[2] = 20.0f;
    add_passby(e, "helicopter", heli_fire, 0.008f, 20, 30, 0.6f, 0.85f, 0.05f, 0.28f, GL_PLANE);
    add_far_noise(e, "valley road", 2.0f, 220, 0.03f, 0.06f);
}

/* --------------------------------------------------------------- ARCTIC: an ice shelf */
/* bearded seals under the ice (matched to a hydrophone recording of them): a buzzing trill, its pulses about 95 a
 * second, high (1.8-2 kHz) for a while, then sweeping down to about 800 Hz in two seconds, rising and falling again;
 * several at once, every few seconds, never quite still */
static const Syllable SEAL[] = {
    { 1850, 1980, CURVE_LIN, 2.4f, 0.1f, 95, 0.9f, 0.1f, 0, 0, 0, 0, 0.55f },    /* the high trill */
    { 1950, 790, CURVE_EXP, 2.1f, 0.05f, 95, 0.9f, 0.15f, 0, 0, 0, 0, 1.0f },    /* the sweep down */
    { 820, 1500, CURVE_LIN, 0.5f, 0.1f, 95, 0.9f, 0.1f, 0, 0, 0, 0, 0.6f },     /* back up */
    { 830, 760, CURVE_LIN, 1.4f, 0.1f, 95, 0.9f, 0.15f, 0, 0, 0, 0, 0.85f },    /* low, held */
};
static const Species SP_SEAL = { SEAL, 4, { "AB", "ABD", "ABCB", "BD", "AABD", "BCBD" }, 6, 0.08f, 0.1f, 0.03f, 0.3f, 2.0f, 2, 5, 0.5f };
static const Syllable WOLF[] = { { 420, 560, CURVE_SWOOP, 3.5f, 0.25f, 0, 0, 0.15f, 0, 0, 5, 0.25f, 1 }, { 580, 380, CURVE_EXP, 2.5f, 0.2f, 0, 0, 0.15f, 0, 0, 5, 0.2f, 0.9f } };
static const Species SP_WOLF = { WOLF, 2, { "A", "AB", "B" }, 3, 0.12f, 0.1f, 0.8f, 25.0f, 60.0f, 1, 2, 0.5f };
static const Syllable TERN[] = { { 3400, 2600, CURVE_EXP, 0.35f, 0.15f, 0, 0, 0.3f, 1.0f, 0.8f, 0, 0, 1 } };   /* a harsh "kee-arr" */
static const Species SP_TERN = { TERN, 1, { "A", "A-A", "AA" }, 3, 0.08f, 0.1f, 0.2f, 5.0f, 14.0f, 1, 2, 0.45f };
static const Material MAT_ICE = { 4, { 1, 1.7f, 2.9f, 4.4f }, { 0.25f, 0.15f, 0.1f, 0.06f }, { 1, 0.6f, 0.4f, 0.3f }, 2.5f, 4000, 4 };   /* a crack: a sharp snap, a short hard ring */

static void polar_wind(Engine *e, Source *s) { wind_follow(e, s, 0.3f, 0.7f, 0.1f, 0.6f); }
static void polar_snow(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 2200.0f); }
static void spindrift_block(Engine *e, Source *s) {   /* snow blown along the ice: a hiss in the gusts */
    Air *a = &s->g.air; a->level = 0.05f + 0.35f * e->wind * e->wind + 0.15f * e->snow; a->weather = e->wind;
}
static void w_arctic(Engine *e) {
    Source *s = add_wind(e, "polar wind", 1.4f, 1, 450, 0.6f, 380, 10, 0.3f); s->block = polar_wind;
    s = engine_add(e, "spindrift", L_WEATHER, G_AIR, B_BED);
    s->g.air.colour = 0.3f; s->g.air.cutoff = 9000; s->g.air.hp = 3000; s->g.air.gust_link = &e->gust; s->g.air.gust_depth = 1.0f; s->g.air.width = 1.0f; s->g.air.send = 0.2f;
    s->block = spindrift_block;
    s = engine_add(e, "snow and sleet", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.3f; r->hiss = 0.05f; r->click = 0.05f; r->send = 0.25f; r->width = 1.0f;
      r->wash = 0.7f; r->wash_white = 0.5f; r->wash_hp = 500; r->wash_lp = 6000; r->tone = 5000; r->var = 0.3f; r->drops = 0.4f;
      static const float f[4] = { 3000, 4500, 1800, 900 }, q[4] = { 2, 2.5f, 2, 1.5f }, w[4] = { 3, 2, 1, 1 }, g[4] = { 1, 0.9f, 0.7f, 0.5f }; set_surfaces(r, f, q, w, g); }
    s->block = polar_snow;
    add_thunder(e, 0.7f, 0, 0);
}
static void a_arctic(Engine *e) {
    static const float tod_t[4] = { 1.0f, 1.0f, 0.8f, 0.2f }, tod_w[4] = { 0.4f, 0.0f, 0.7f, 1.0f };
    add_phrase(e, "bearded seals", 0.4f, 0.45f, 0.8f, 0.7f, 5, &SP_SEAL, 0, 0, 0, 0, GL_RING);
    add_phrase(e, "wolves", 0.45f, 0.6f, 0.9f, 0.85f, 3, &SP_WOLF, 0, 0, 0, tod_w, GL_GROWL);
    add_phrase(e, "terns and ravens", 0.4f, 0.3f, 0.85f, 0.4f, 4, &SP_TERN, &SP_RAVEN, &SP_TERN, 0, tod_t, GL_BIRD);
}
static void crack_fire(Engine *e, Source *s, float pan, float dist, float x) {   /* a snap, and now and then the boom it makes far off */
    (void)e; (void)x;
    ring_strike(&s->g.ring, &s->rng, &MAT_ICE, rng_range(&s->rng, 180, 450), rng_range(&s->rng, 0.5f, 1.0f), 1.0f, 1.0f, pan, dist);
    if (rng_f(&s->rng) < 0.3f) ring_strike(&s->g.ring, &s->rng, &MAT_STONE, rng_range(&s->rng, 45, 80), rng_range(&s->rng, 0.4f, 0.7f), 0.3f, 3.0f, pan, dist);
}
static void crack_block(Engine *e, Source *s) { s->rate = 0.05f + 0.25f * e->eff.activity; }
/* the ice groans: now and then a long low creak, rising and sinking, for seconds. user[0] time left */
static void groan_block(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    if (s->user[0] > 0) { if ((s->user[0] -= BLK) <= 0) hum_release(h, 0, 1.5f); else h->v[0].pmul = 1.0f + 0.15f * sine_c(s->user[0] * 0.13f); return; }
    if (rng_f(&s->rng) < BLK / 50.0f * (0.4f + e->eff.activity)) {
        int n = hum_start(h, rng_range(&s->rng, 45, 80), 1.5f, 2.0f, 0.5f, 0.3f, 500, 0.5f, 1.5f, 0, 1.0f);
        if (n == 0) { h->v[0].target = 0.25f; spatial_set(&h->v[0].sp, rng_bi(&s->rng) * 0.8f, rng_range(&s->rng, 0.4f, 0.8f)); s->user[0] = rng_range(&s->rng, 3.0f, 7.0f); engine_post(e, L_LANDSCAPE, GL_FLAT, 0, 0.6f, 0, 0, 0); }
        else if (n > 0) hum_release(h, n, 0.05f);
    }
}
static void l_arctic(Engine *e) {
    Source *s = engine_add(e, "ice cracks", L_LANDSCAPE, G_RING, B_SCATTER);
    s->g.ring.level = 0.45f; s->dist_lo = 0.2f; s->dist_hi = 0.9f; s->fire = crack_fire; s->block = crack_block; s->glyph = GL_FLASH;
    s = engine_add(e, "ice groans", L_LANDSCAPE, G_HUM, B_BED); s->g.hum.level = 0.5f; s->block = groan_block;
    s = engine_add(e, "ice booms", L_LANDSCAPE, G_RING, B_RARE);
    s->rate = 0.02f; s->cooldown = 25; s->g.ring.level = 0.5f; s->dist_lo = 0.5f; s->dist_hi = 0.9f; s->fire = rockfall_fire; s->block = rockfall_block; s->glyph = GL_ROCK;
}
static void station_block(Engine *e, Source *s) {   /* the station's generator, out across the ice */
    Hum *h = &s->g.hum;
    if (!h->v[0].on) { hum_start(h, 60, 1.0f, 1.2f, 0.3f, 0.3f, 900, 0, 0, 0, 3.0f); spatial_set(&h->v[0].sp, 0.6f, 0.75f); h->v[0].chug_rate = 15.0f; h->v[0].chug_depth = 0.3f; }
    h->v[0].target = 0.15f + 0.08f * e->eff.activity;
}
static void snowmobile_fire(Engine *e, Source *s, float pan, float dist, float x) {   /* a two-stroke's buzz */
    (void)e; (void)pan; (void)dist; (void)x; s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 120, 150), 1.0f, 2.4f, 1.0f, 0.4f, 3000, 0, 0, 0, 1.5f);
}
static void h_arctic(Engine *e) {
    Source *s = engine_add(e, "station generator", L_HUMANS, G_HUM, B_BED); s->g.hum.level = 0.5f; s->block = station_block;
    add_passby(e, "snowmobile", snowmobile_fire, 0.012f, 15, 25, 0.4f, 0.75f, 0.06f, 0.25f, GL_CAR);
    s = engine_add(e, "station flag", L_HUMANS, G_AIR, B_BED);   /* the flag and its halyard slapping in the wind */
    s->g.air.colour = 0.8f; s->g.air.cutoff = 3000; s->g.air.hp = 400; s->g.air.gust_link = &e->gust; s->g.air.gust_depth = 1.0f; s->g.air.width = 0.3f; s->g.air.send = 0.2f;
    s->user[0] = 0.0f; s->user[1] = 0.12f; s->block = rustle_block;
}

/* --------------------------------------------------------------- DESERT: dunes */
/* the desert (matched to a recording of one): mostly stillness; a lark's plaintive "pee-oo" now and then, more often
 * a single short "chip" (1.1-1.4 kHz), and an insect ticking softly near 4.9 kHz, a tick every half second or so */
static const Syllable DESERTLARK[] = { { 2200, 2700, CURVE_LIN, 0.28f, 0.2f, 0, 0, 0.1f, 0, 0, 0, 0, 1 }, { 2900, 2100, CURVE_EXP, 0.45f, 0.15f, 0, 0, 0.1f, 0, 0, 0, 0, 0.9f },
                                       { 1100, 1380, CURVE_LIN, 0.12f, 0.2f, 0, 0, 0.2f, 0, 0, 0, 0, 0.9f } };
static const Species SP_DESERTLARK = { DESERTLARK, 3, { "C", "C-C", "AB", "C", "A-AB" }, 5, 0.06f, 0.08f, 0.35f, 15.0f, 40.0f, 1, 1, 0.4f };
static const Syllable DESERT_TICK = { 4900, 4850, CURVE_LIN, 0.035f, 0.2f, 0, 0, 0, 0, 0, 0, 0, 0.25f };
static const Syllable SCOPS[] = { { 1250, 1200, CURVE_LIN, 0.12f, 0.1f, 0, 0, 0.05f, 0, 0, 0, 0, 1 } };   /* a scops owl: one soft "tyoo", every few seconds */
static const Species SP_SCOPS = { SCOPS, 1, { "AAAA", "AAAAAA" }, 2, 0.02f, 0.05f, 2.4f, 4.0f, 12.0f, 2, 4, 0.5f };
static const Syllable JACKAL[] = { { 700, 1300, CURVE_SWOOP, 0.6f, 0.1f, 0, 0, 0.2f, 0, 0, 7, 0.4f, 1 }, { 1200, 800, CURVE_EXP, 0.35f, 0.1f, 0, 0, 0.2f, 0, 0, 7, 0.3f, 0.8f } };
static const Species SP_JACKAL = { JACKAL, 2, { "AB-AB-A", "ABAB", "A-AB" }, 3, 0.1f, 0.12f, 0.15f, 30.0f, 90.0f, 1, 2, 0.5f };

static void desert_wind(Engine *e, Source *s) { wind_follow(e, s, 0.15f, 0.65f, 0.05f, 0.4f); }
static void desert_rain(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 2000.0f); }
static void sand_block(Engine *e, Source *s) {   /* sand carried on the gusts */
    Air *a = &s->g.air; a->level = 0.03f + 0.45f * e->wind * e->wind; a->weather = e->wind;
}
static void w_desert(Engine *e) {
    Source *s = add_wind(e, "desert wind", 0.9f, 1, 900, 0.5f, 700, 6, 0.15f); s->block = desert_wind;
    s = engine_add(e, "blowing sand", L_WEATHER, G_AIR, B_BED);
    s->g.air.colour = 0.4f; s->g.air.cutoff = 8000; s->g.air.hp = 2500; s->g.air.gust_link = &e->gust; s->g.air.gust_depth = 1.0f; s->g.air.width = 1.0f; s->g.air.send = 0.1f;
    s->block = sand_block;
    s = engine_add(e, "rain on sand", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.35f; r->hiss = 0; r->click = 0.05f; r->send = 0.15f; r->width = 1.0f;
      r->wash = 0.8f; r->wash_white = 0.3f; r->wash_hp = 200; r->wash_lp = 5000; r->tone = 4000; r->var = 0.4f; r->drops = 0.4f;
      static const float f[4] = { 1200, 2200, 700, 3200 }, q[4] = { 1.5f, 2, 1.2f, 2 }, w[4] = { 3, 2, 1, 1 }, g[4] = { 1, 0.8f, 0.7f, 0.5f }; set_surfaces(r, f, q, w, g); }
    s->block = desert_rain;
    add_thunder(e, 0.8f, 0, 0);
}
static void a_desert(Engine *e) {
    static const float tod_d[4] = { 1.4f, 0.8f, 1.0f, 0.0f }, tod_c[4] = { 0.2f, 1.0f, 0.5f, 0.0f }, tod_j[4] = { 0.3f, 0.0f, 1.0f, 0.8f };
    add_phrase(e, "larks and ravens", 0.38f, 0.35f, 0.85f, 0.2f, 3, &SP_DESERTLARK, &SP_DESERTLARK, &SP_RAVEN, 0, tod_d, GL_BIRD);
    add_chorus(e, L_ANIMALS, "ticking insect", &DESERT_TICK, 0.2f, 0.35f, 0.65f, 0.1f, 2, 0.98f, 1.02f, 0.3f, 0.6f, tod_c, GL_DOT);
    add_phrase(e, "scops owl", 0.4f, 0.4f, 0.75f, 0.0f, 1, &SP_SCOPS, 0, 0, 0, TOD_OWL, GL_BIGBIRD);
    add_phrase(e, "jackals", 0.4f, 0.7f, 0.95f, 0.9f, 3, &SP_JACKAL, 0, 0, 0, tod_j, GL_GROWL);
    add_chorus(e, L_ANIMALS, "night crickets", &CRICKET, 0.16f, 0.8f, 1.3f, 0.3f, 3, 0.95f, 1.08f, 0.4f, 0.85f, TOD_NIGHT, GL_DOT);
}
/* a dune booming: a low drone (two notes beating) that swells for tens of seconds as the sand slides. user[0] time left, [1] its length */
static void dune_block(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    if (s->user[0] > 0) {
        float u = 1.0f - (s->user[0] -= BLK) / s->user[1], env = u < 0.4f ? u / 0.4f : (1.0f - u) / 0.6f;
        for (int k = 0; k < 2; k++) h->v[k].target = 0.25f * env * env;
        if (s->user[0] <= 0) { hum_release(h, 0, 2.0f); hum_release(h, 1, 2.0f); }
        return;
    }
    if (!h->v[0].on && !h->v[1].on && rng_f(&s->rng) < BLK / 240.0f * (0.3f + e->wind + e->eff.activity)) {
        float f = rng_range(&s->rng, 75, 105), pan = rng_bi(&s->rng) * 0.6f;
        hum_start(h, f, 2.0f, 0.8f, 2.0f, 0.2f, 600, 0, 0, 0, 2.0f); hum_start(h, f * 1.5f, 2.0f, 0.5f, 1.5f, 0.1f, 600, 0, 0, 0, 2.0f);
        for (int k = 0; k < 2; k++) { h->v[k].target = 1e-3f; spatial_set(&h->v[k].sp, pan + (k ? 0.2f : -0.2f), 0.7f); }
        s->user[1] = s->user[0] = rng_range(&s->rng, 12.0f, 30.0f);
        engine_post(e, L_LANDSCAPE, GL_FLAT, pan, 0.7f, 0, 0, 0);
    }
}
static void heat_tick_fire(Engine *e, Source *s, float pan, float dist, float x) {   /* stones ticking in the heat */
    (void)e; (void)x; ring_strike(&s->g.ring, &s->rng, &MAT_STONE, rng_range(&s->rng, 1500, 3000), rng_range(&s->rng, 0.2f, 0.5f), 1.0f, 0.5f, pan, dist);
}
static void l_desert(Engine *e) {
    static const float tod_h[4] = { 0.4f, 1.0f, 0.6f, 0.1f };
    add_rustle(e, "sand hiss", 4000, 0.1f, 0.45f);
    Source *s = engine_add(e, "booming dunes", L_LANDSCAPE, G_HUM, B_BED); s->g.hum.level = 0.5f; s->block = dune_block;
    s = engine_add(e, "stones in the heat", L_LANDSCAPE, G_RING, B_SCATTER);
    s->rate = 0.08f; s->alive = 1; s->g.ring.level = 0.3f; s->dist_lo = 0.2f; s->dist_hi = 0.6f; s->fire = heat_tick_fire; s->glyph = GL_DOT; memcpy(s->tod, tod_h, sizeof s->tod);
}
/* a caravan going by: its camels' bells, each its own, swinging with the stride, coming near and away over a minute.
 * user[0] time left, [1] its length, [2] which way; ag[k] a camel: pitch its bell's note, ph to its next ring, tempo its stride */
static void caravan_block(Engine *e, Source *s) {
    if (s->user[0] <= 0) {
        if (rng_f(&s->rng) < BLK / 150.0f * (0.3f + e->eff.activity)) {
            s->user[1] = s->user[0] = rng_range(&s->rng, 50.0f, 80.0f); s->user[2] = rng_f(&s->rng) < 0.5f ? -1.0f : 1.0f;
            for (int k = 0; k < 4; k++) { s->ag[k].pitch = rng_range(&s->rng, 1500, 2700); s->ag[k].ph = rng_range(&s->rng, 0, 1.0f); s->ag[k].tempo = rng_range(&s->rng, 0.8f, 1.2f); }
            engine_post(e, L_HUMANS, GL_CHIME, -s->user[2], 0.8f, 0, 0, 0);
        }
        return;
    }
    float u = 1.0f - (s->user[0] -= BLK) / s->user[1], x = s->user[2] * (u * 2.0f - 1.0f), dist = 0.45f + 0.45f * x * x;
    for (int k = 0; k < 4; k++) {
        if ((s->ag[k].ph -= BLK) > 0) continue;
        float pan = x * 0.9f - s->user[2] * 0.08f * k;
        ring_strike(&s->g.ring, &s->rng, &MAT_BELL, s->ag[k].pitch * rng_range(&s->rng, 0.995f, 1.005f), rng_range(&s->rng, 0.25f, 0.5f), 0.8f, 0.25f, pan < -1 ? -1 : pan > 1 ? 1 : pan, dist);
        s->ag[k].ph = s->ag[k].tempo * rng_range(&s->rng, 0.9f, 1.1f);
    }
}
static void jeep_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)pan; (void)dist; (void)x; s->pb_voice = hum_start(&s->g.hum, rng_range(&s->rng, 90, 115), 1.0f, 1.5f, 0.5f, 0.5f, 2200, 0, 0, 0, 2.0f);
}
static void h_desert(Engine *e) {
    Source *s = engine_add(e, "caravan bells", L_HUMANS, G_RING, B_BED); s->g.ring.level = 0.4f; s->block = caravan_block;
    add_passby(e, "four by four", jeep_fire, 0.01f, 15, 25, 0.6f, 0.85f, 0.05f, 0.25f, GL_CAR);
    add_far_noise(e, "far highway", 2.0f, 300, 0.02f, 0.05f);
    add_jets(e, 0.01f, 0.7f, 0.9f);
}

/* --------------------------------------------------------------- SAVANNA: the plains */
static const Syllable HYENA[] = { { 420, 1150, CURVE_EXP, 0.9f, 0.2f, 0, 0, 0.25f, 0, 0, 0, 0, 1 } };   /* "whoo-oop", rising */
static const Species SP_HYENA = { HYENA, 1, { "A-A-A", "A--A", "A-A-A-A" }, 3, 0.1f, 0.1f, 1.5f, 30.0f, 90.0f, 1, 1, 0.5f };
static const Syllable HORNBILL[] = { { 900, 850, CURVE_LIN, 0.06f, 0.1f, 0, 0, 0.4f, 2.0f, 1.5f, 0, 0, 1 } };   /* a hollow "tok", quickening */
static const Species SP_HORNBILL = { HORNBILL, 1, { "A---A--A--A-A-AAAA", "A--A--A-A-AAA" }, 2, 0.04f, 0.05f, 0.12f, 10.0f, 25.0f, 1, 1, 0.45f };
static const Syllable DOVE[] = { { 520, 500, CURVE_LIN, 0.18f, 0.2f, 0, 0, 0.2f, 0, 0, 0, 0, 0.8f }, { 600, 560, CURVE_LIN, 0.3f, 0.2f, 0, 0, 0.2f, 0, 0, 0, 0, 1 }, { 480, 460, CURVE_LIN, 0.22f, 0.2f, 0, 0, 0.2f, 0, 0, 0, 0, 0.8f } };
static const Species SP_DOVE = { DOVE, 3, { "ABC-ABC-ABC", "ABC-ABC" }, 2, 0.04f, 0.05f, 0.1f, 8.0f, 20.0f, 1, 2, 0.4f };

static void plains_wind(Engine *e, Source *s) { wind_follow(e, s, 0.15f, 0.6f, 0.03f, 0.4f); }
static void plains_rain(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 3000.0f); }
static void w_savanna(Engine *e) {
    Source *s = add_wind(e, "plains wind", 1.0f, 1, 800, 0.6f, 900, 8, 0.2f); s->block = plains_wind;
    s = engine_add(e, "storm rain", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.42f; r->hiss = 0; r->click = 0.05f; r->send = 0.3f; r->width = 1.0f;
      r->wash = 1.0f; r->wash_white = 0.45f; r->wash_hp = 200; r->wash_lp = 6500; r->tone = 4500; r->var = 0.4f; r->drops = 0.6f;
      static const float f[4] = { 1800, 2800, 900, 500 }, q[4] = { 1.6f, 2, 1.5f, 1.2f }, w[4] = { 3, 2, 1, 1 }, g[4] = { 1, 0.9f, 0.8f, 0.6f }; set_surfaces(r, f, q, w, g); }
    s->block = plains_rain;
    add_thunder(e, 1.0f, 0, 0);
}
/* a lion (matched to a recording): two or three long roars (2-3.5 s, rough, barely pitched, brightest near 1 kHz,
 * pulsing as it breathes), then the grunts (about a second each, a second and a half to two and a half apart), fading.
 * user[0] calls left, [1] till the next, [2] this call's time left, [3] its length, [4] roars left; ag[0] where */
static void lion_call(Engine *e, Source *s, int roar) {
    Hum *h = &s->g.hum;
    float f = roar ? rng_range(&s->rng, 160, 190) : rng_range(&s->rng, 130, 155);
    engine_var_begin(e, s);
    int n = hum_start(h, f, 1.0f, roar ? 3.2f : 2.6f, 1.5f, 0.85f, roar ? 1600.0f : 1300.0f, 0, 0, 0, 0.05f);
    engine_var_end();
    if (n != 0) { if (n > 0) hum_release(h, n, 0.05f); return; }
    h->v[0].rattle = 0.6f; h->v[0].target = 1e-3f; spatial_set(&h->v[0].sp, s->ag[0].pan, s->ag[0].dist);
    s->user[3] = s->user[2] = roar ? rng_range(&s->rng, 2.3f, 3.5f) : rng_range(&s->rng, 0.8f, 1.1f);
}
static void lion_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)x;
    if (s->user[0] > 0) return;
    s->ag[0].pan = pan; s->ag[0].dist = dist;
    s->user[4] = (float)(2 + rng_int(&s->rng, 2)); s->user[0] = s->user[4] + (float)(5 + rng_int(&s->rng, 6)); s->user[1] = 0;
}
static void lion_block(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    if (s->user[2] > 0) {                /* a call sounding: up fast, pulsing with the breath, down at its end */
        float u = 1.0f - (s->user[2] -= BLK) / s->user[3], roar = s->user[3] > 1.5f;
        float env = (u < 0.08f ? u / 0.08f : u > 0.75f ? (1.0f - u) / 0.25f : 1.0f) * (0.75f + 0.25f * sine_c(u * (roar ? 2.5f : 1.0f)));
        h->v[0].target = 0.5f * env * (0.4f + 0.6f * s->user[0] / 12.0f);
        h->v[0].pmul = 1.0f - 0.25f * u;
        if (s->user[2] <= 0) hum_release(h, 0, 0.15f);
        return;
    }
    if (s->user[0] <= 0 || (s->user[1] -= BLK) > 0) return;
    int roar = s->user[4] > 0;
    lion_call(e, s, roar);
    if (roar) s->user[4] -= 1;
    s->user[0] -= 1;
    s->user[1] = roar ? rng_range(&s->rng, 1.2f, 2.0f) : rng_range(&s->rng, 1.4f, 2.5f);
}
static void a_savanna(Engine *e) {
    static const float tod_b[4] = { 1.4f, 1.0f, 0.8f, 0.0f }, tod_l[4] = { 1.0f, 0.1f, 0.8f, 1.0f }, tod_h[4] = { 0.4f, 0.0f, 0.6f, 1.0f }, tod_c[4] = { 0.3f, 1.0f, 0.6f, 0.1f };
    add_phrase(e, "hornbills, doves, weavers", 0.42f, 0.2f, 0.8f, 0.4f, 6, &SP_HORNBILL, &SP_DOVE, &SP_SPARROW, &SP_DOVE, tod_b, GL_BIRD);
    Source *s = engine_add(e, "lion", L_ANIMALS, G_HUM, B_SCATTER);
    s->rate = 0.006f; s->g.hum.level = 0.6f; s->dist_lo = 0.65f; s->dist_hi = 0.9f; s->fire = lion_fire; s->block = lion_block; s->glyph = GL_GROWL;
    memcpy(s->tod, tod_l, sizeof s->tod);
    add_phrase(e, "hyenas", 0.4f, 0.6f, 0.9f, 0.6f, 2, &SP_HYENA, 0, 0, 0, tod_h, GL_GROWL);
    add_chorus(e, L_ANIMALS, "cicadas", &CICADA, 0.22f, 2.5f, 5.0f, 0.2f, 5, 0.85f, 1.15f, 0.3f, 0.8f, tod_c, GL_DOT);
    add_chorus(e, L_ANIMALS, "night crickets", &CRICKET, 0.22f, 0.7f, 1.1f, 0.3f, 8, 0.95f, 1.08f, 0.3f, 0.8f, TOD_NIGHT, GL_DOT);
}
static void l_savanna(Engine *e) {
    add_rustle(e, "long grass", 2200, 0.2f, 0.5f);
    add_rustle(e, "acacia", 1300, 0.08f, 0.3f);
}
/* the camp's fire: a low roar and its pops and crackles, in the evening and the night */
static void fire_block(Engine *e, Source *s) {
    Rain *r = &s->g.rain; float t = src_tod(e, s);
    r->density = 6.0f + 22.0f * t * (0.6f + 0.6f * e->eff.activity); r->level = 0.25f * t; r->wash_amp = 0;
}
static void h_savanna(Engine *e) {
    static const float tod_f[4] = { 0.3f, 0.15f, 0.9f, 1.0f };
    Source *s = engine_add(e, "camp generator", L_HUMANS, G_HUM, B_BED); s->g.hum.level = 0.35f; s->block = station_block;
    s = engine_add(e, "campfire", L_HUMANS, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.25f; r->hiss = 0; r->click = 1.0f; r->send = 0.15f; r->width = 0.4f; r->size = 0.2f;
      r->wash = 0; r->tone = 6000; r->var = 0.8f; r->drops = 0.9f;
      static const float f[4] = { 2500, 4500, 1200, 6000 }, q[4] = { 3, 3, 2, 3 }, w[4] = { 2, 2, 1, 1 }, g[4] = { 1, 0.8f, 0.6f, 0.5f }; set_surfaces(r, f, q, w, g); }
    s->block = fire_block; memcpy(s->tod, tod_f, sizeof s->tod);
    add_passby(e, "safari jeep", jeep_fire, 0.01f, 15, 25, 0.55f, 0.85f, 0.05f, 0.22f, GL_CAR);
    add_jets(e, 0.008f, 0.75f, 0.9f);
}

/* --------------------------------------------------------------- VILLAGE: the green, the church, the farm */
static const Syllable ROOK[] = { { 700, 600, CURVE_LIN, 0.28f, 0.15f, 0, 0, 0.5f, 1.0f, 1.6f, 0, 0, 1 } };
static const Species SP_ROOK = { ROOK, 1, { "A-A", "AA-A", "A", "A-AA" }, 4, 0.12f, 0.12f, 0.2f, 3.0f, 10.0f, 1, 2, 0.45f };
static const Syllable QUACK[] = { { 420, 360, CURVE_LIN, 0.16f, 0.1f, 0, 0, 0.3f, 1.0f, 2.2f, 0, 0, 1 } };
static const Species SP_DUCK = { QUACK, 1, { "AAA-A", "AAAA", "A-AA" }, 3, 0.05f, 0.1f, 0.12f, 8.0f, 25.0f, 1, 2, 0.45f };
static const Syllable COCKEREL[] = { { 600, 900, CURVE_SWOOP, 0.15f, 0.1f, 0, 0, 0.4f, 1.0f, 1.2f, 0, 0, 0.8f }, { 900, 1000, CURVE_LIN, 0.15f, 0.1f, 0, 0, 0.4f, 1.0f, 1.2f, 0, 0, 0.9f },
                                     { 1000, 1100, CURVE_LIN, 0.15f, 0.1f, 0, 0, 0.4f, 1.0f, 1.2f, 0, 0, 1 }, { 1100, 700, CURVE_EXP, 0.6f, 0.1f, 0, 0, 0.4f, 1.0f, 1.2f, 6, 0.3f, 1 } };
static const Species SP_COCKEREL = { COCKEREL, 4, { "ABCD" }, 1, 0.05f, 0.05f, 0.03f, 20.0f, 60.0f, 1, 1, 0.45f };

static void village_breeze(Engine *e, Source *s) { wind_follow(e, s, 0.2f, 0.6f, 0.05f, 0.5f); }
static void village_rain(Engine *e, Source *s) { weather_rain(e, &s->g.rain, 2400.0f); }
static void w_village(Engine *e) {
    Source *s = add_wind(e, "country breeze", 1.1f, 1, 700, 0.6f, 900, 8, 0.25f); s->block = village_breeze;
    s = engine_add(e, "rain on roofs", L_WEATHER, G_RAIN, B_BED);
    { Rain *r = &s->g.rain; r->level = 0.4f; r->hiss = 0.05f; r->click = 0.1f; r->send = 0.3f; r->width = 1.0f;
      r->wash = 0.85f; r->wash_white = 0.5f; r->wash_hp = 250; r->wash_lp = 7000; r->tone = 5000; r->var = 0.4f; r->drops = 0.7f;
      static const float f[4] = { 2500, 3500, 1300, 700 }, q[4] = { 2, 2.5f, 1.8f, 1.2f }, w[4] = { 3, 2, 1, 1 }, g[4] = { 1, 0.9f, 0.8f, 0.6f }; set_surfaces(r, f, q, w, g); }
    s->block = village_rain;
    add_thunder(e, 0.9f, 0, 0);
}
/* farm animals over the hedge: a cow lowing, now and then a dog barking a few times. user[0] barks left, [1] to the next, [2] where */
static void farm_fire(Engine *e, Source *s, float pan, float dist, float x) {
    (void)e; (void)x;
    if (rng_f(&s->rng) < 0.6f) crowd_say(&s->g.crowd, rng_range(&s->rng, 100, 130), rng_range(&s->rng, 1.4f, 2.2f), 300, 700, 600, 1100, pan, dist, 1.0f);
    else { s->user[0] = (float)(2 + rng_int(&s->rng, 4)); s->user[1] = 0; s->user[2] = pan; s->user[3] = dist; }
}
static void farm_block(Engine *e, Source *s) {
    if (s->user[0] <= 0 || (s->user[1] -= BLK) > 0) return;
    engine_var_begin(e, s);
    crowd_say(&s->g.crowd, rng_range(&s->rng, 350, 450), rng_range(&s->rng, 0.12f, 0.18f), 700, 750, 1400, 1300, s->user[2], s->user[3], 0.8f);
    engine_var_end();
    s->user[0] -= 1; s->user[1] = rng_range(&s->rng, 0.35f, 0.6f);
}
static void a_village(Engine *e) {
    static const float tod_r[4] = { 1.5f, 0.8f, 1.2f, 0.0f }, tod_d[4] = { 1.0f, 1.0f, 0.8f, 0.1f }, tod_k[4] = { 1.0f, 0.1f, 0.0f, 0.0f }, tod_f[4] = { 0.8f, 1.0f, 0.8f, 0.3f };
    add_phrase(e, "rookery", 0.4f, 0.4f, 0.8f, 0.6f, 5, &SP_ROOK, 0, 0, 0, tod_r, GL_BIGBIRD);
    add_phrase(e, "garden birds", 0.45f, 0.1f, 0.6f, 0.3f, 5, &SP_BLACKBIRD, &SP_ROBIN, &SP_WREN, &SP_WOODPIGEON, TOD_DAYBIRD, GL_BIRD);
    add_phrase(e, "ducks on the pond", 0.4f, 0.25f, 0.55f, 0.6f, 3, &SP_DUCK, 0, 0, 0, tod_d, GL_BIRD);
    add_phrase(e, "cockerel", 0.4f, 0.6f, 0.85f, 0.0f, 1, &SP_COCKEREL, 0, 0, 0, tod_k, GL_BIGBIRD);
    Source *s = engine_add(e, "cows and a dog", L_ANIMALS, G_CROWD, B_SCATTER);
    s->rate = 0.025f; s->g.crowd.level = 0.35f; s->dist_lo = 0.7f; s->dist_hi = 0.9f; s->fire = farm_fire; s->block = farm_block; s->glyph = GL_GROWL;
    memcpy(s->tod, tod_f, sizeof s->tod);
    add_phrase(e, "owl", 0.4f, 0.5f, 0.8f, 0.0f, 1, &SP_OWL, 0, 0, 0, TOD_OWL, GL_BIGBIRD);
}
static void l_village(Engine *e) {
    add_rustle(e, "big trees", 1500, 0.14f, 0.45f);
    Source *s = engine_add(e, "pond", L_LANDSCAPE, G_WATER, B_BED);
    { Water *w = &s->g.water; w->rate = 25; w->dmin = 2.0f; w->dmax = 7.0f; w->gamma = 2.4f; w->level = 0.22f; w->rise = 0.04f;
      w->pan_lo = 0.2f; w->pan_hi = 0.6f; w->dist_lo = 0.3f; w->dist_hi = 0.5f; w->rush = 0.1f; w->rush_cut = 900; }
}
/* the church's six bells: now and then a few minutes of changes (rounds, then the plain hunt, pairs swapping row by
 * row, and back to rounds), or one bell tolling, a stroke every two seconds (as recorded). user[0] rows (or strokes)
 * left, [1] place in the row, [2] to the next bell, [3] till the next, [4] which swaps this row; ag[k].pitch the
 * row's order; ag[7].pitch 1 when tolling */
static const float BELL6[6] = { 1.0f, 0.889f, 0.8f, 0.75f, 0.667f, 0.6f };   /* a major scale, down from the treble */
static void ringing_block(Engine *e, Source *s) {
    if (s->user[0] <= 0) {
        if ((s->user[3] -= BLK) > 0) return;
        for (int k = 0; k < 6; k++) s->ag[k].pitch = (float)k;
        s->ag[7].pitch = rng_f(&s->rng) < 0.5f;
        s->user[0] = s->ag[7].pitch > 0 ? (float)(12 + rng_int(&s->rng, 20)) : (float)(30 + rng_int(&s->rng, 40));
        s->user[1] = 0; s->user[2] = 0; s->user[4] = 0;
        s->user[3] = rng_range(&s->rng, 240.0f, 600.0f);
        engine_post(e, L_HUMANS, GL_BEACON, -0.3f, 0.7f, 0, 0, 0);
        return;
    }
    if ((s->user[2] -= BLK) > 0) return;
    if (s->ag[7].pitch > 0) {            /* tolling: the tenor, every two seconds */
        ring_strike(&s->g.ring, &s->rng, &MAT_CHURCH, 592.0f * BELL6[5] / BELL6[2], rng_range(&s->rng, 0.5f, 0.6f), 0.55f, 1.0f, -0.2f, 0.7f);
        s->user[2] = 2.0f; s->user[0] -= 1;
        return;
    }
    int i = (int)s->user[1], b = (int)s->ag[i].pitch;
    ring_strike(&s->g.ring, &s->rng, &MAT_CHURCH, 620.0f * BELL6[b], rng_range(&s->rng, 0.45f, 0.6f), 0.55f, 0.5f, -0.3f + 0.04f * b, 0.7f);
    s->user[2] = i == 5 ? 0.62f : 0.27f;                 /* (the handstroke gap at the row's end) */
    if (++i < 6) { s->user[1] = (float)i; return; }
    s->user[1] = 0; s->user[0] -= 1;
    if (s->user[0] > 6) {                                /* the plain hunt: swap 1-2 3-4 5-6, then 2-3 4-5 */
        int odd = (int)s->user[4] & 1;
        for (int k = odd; k + 1 < 6; k += 2) { float t = s->ag[k].pitch; s->ag[k].pitch = s->ag[k + 1].pitch; s->ag[k + 1].pitch = t; }
        s->user[4] += 1;
    } else for (int k = 0; k < 6; k++) s->ag[k].pitch = (float)k;   /* rounds to finish */
}
/* a lawnmower in someone's garden: on for a few minutes, up and down the lawn. user[0] time left, [1] till next, [2] the phase */
static void mower_block(Engine *e, Source *s) {
    Hum *h = &s->g.hum;
    if (s->user[0] > 0) {
        s->user[0] -= BLK; s->user[2] += BLK / 14.0f;
        float lane = sine_c(s->user[2]);
        h->v[0].target = (0.12f + 0.06f * lane) * (s->user[0] < 3.0f ? s->user[0] / 3.0f : 1.0f);
        h->v[0].pmul = 1.0f + 0.03f * lane; spatial_set(&h->v[0].sp, 0.5f + 0.25f * lane, 0.7f - 0.08f * lane);
        if (s->user[0] <= 0) hum_release(h, 0, 0.5f);
        return;
    }
    if ((s->user[1] -= BLK * (0.3f + e->eff.activity) * src_tod(e, s)) <= 0 && !h->v[0].on) {
        hum_start(h, rng_range(&s->rng, 48, 56), 1.0f, 2.0f, 0.6f, 0.5f, 2500, 0, 0, 0, 2.0f); h->v[0].target = 1e-3f; h->v[0].chug_rate = 25.0f; h->v[0].chug_depth = 0.25f;
        s->user[0] = rng_range(&s->rng, 120.0f, 300.0f); s->user[1] = rng_range(&s->rng, 300.0f, 900.0f);
        engine_post(e, L_HUMANS, GL_CAR, 0.6f, 0.7f, 0, 0, 0);
    }
}
static void h_village(Engine *e) {
    static const float tod_m[4] = { 0.2f, 1.0f, 0.5f, 0.0f };
    Source *s = engine_add(e, "church bells", L_HUMANS, G_RING, B_BED); s->g.ring.level = 0.5f; s->block = ringing_block; s->user[3] = 30.0f;
    add_far_noise(e, "village road", 2.0f, 300, 0.04f, 0.1f);
    s = engine_add(e, "lawnmower", L_HUMANS, G_HUM, B_BED); s->g.hum.level = 0.5f; s->block = mower_block; s->user[1] = 60.0f; memcpy(s->tod, tod_m, sizeof s->tod);
    add_passby(e, "car through the village", jeep_fire, 0.02f, 8, 14, 0.4f, 0.7f, 0.05f, 0.22f, GL_CAR);
}

/* ======================================================================= the libraries */
const char *const ENV_NAMES[NUM_ENVS] = { "CITY", "JUNGLE", "SPACE", "CAVES", "FOREST", "SWAMP", "OCEAN", "ALIEN", "INDUSTRIAL", "MEADOW",
                                            "MOUNTAIN", "ARCTIC", "DESERT", "SAVANNA", "VILLAGE" };

#define NOPLACE 0, 0, 0, 0, 0, 0, { { 0, 0, 0 } }
const PatchDef PATCHES[NUM_LAYERS][NUM_ENVS] = {
    {   /* WEATHER */
        { "STREET WIND", w_city, NOPLACE }, { "TROPICAL RAIN", w_jungle, NOPLACE }, { "SOLAR WIND", w_space, NOPLACE },
        { "CAVE AIRFLOW", w_caves, NOPLACE }, { "WOODLAND BREEZE", w_forest, NOPLACE }, { "SWAMP DRIZZLE", w_swamp, NOPLACE },
        { "SEA WIND", w_ocean, NOPLACE }, { "ION STORM", w_alien, NOPLACE }, { "PIPE WIND", w_industrial, NOPLACE },
        { "SUMMER BREEZE", w_meadow, NOPLACE },
        { "RIDGE WIND", w_mountain, NOPLACE }, { "POLAR WIND", w_arctic, NOPLACE }, { "DESERT WIND", w_desert, NOPLACE },
        { "PLAINS STORM", w_savanna, NOPLACE }, { "COUNTRY BREEZE", w_village, NOPLACE },
    },
    {   /* ANIMALS */
        { "CITY BIRDS", a_city, NOPLACE }, { "RAINFOREST", a_jungle, NOPLACE }, { "DEEP SIGNALS", a_space, NOPLACE },
        { "BATS", a_caves, NOPLACE }, { "SONGBIRDS", a_forest, NOPLACE }, { "FROGS AND HERON", a_swamp, NOPLACE },
        { "GULLS", a_ocean, NOPLACE }, { "ALIEN CREATURES", a_alien, NOPLACE }, { "PIGEONS AND CROWS", a_industrial, NOPLACE },
        { "SKYLARKS AND BEES", a_meadow, NOPLACE },
        { "HIGH CRAGS", a_mountain, NOPLACE }, { "SEALS AND WOLVES", a_arctic, NOPLACE }, { "DESERT NIGHT", a_desert, NOPLACE },
        { "LIONS AND HYENAS", a_savanna, NOPLACE }, { "ROOKS AND DUCKS", a_village, NOPLACE },
    },
    {   /* LANDSCAPE: also the place (reverb, backdrop, states) */
        { "STREETS", l_city, 1.2f, 1.8f, 6000, 20, 0.35f, 0, { { "AFTERNOON", 0, 0 }, { "3 AM", 0, -0.35f }, { "MORNING RUSH", 0, 0.35f }, { "RAIN", 0.25f, 0 } } },
        { "CANOPY STREAM", l_jungle, 1.2f, 1.6f, 5500, 10, 0.4f, 1, { { "MIDDAY HEAT", 0, 0 }, { "MORNING CHORUS", 0, 0.3f }, { "DOWNPOUR", 0.3f, 0 }, { "NIGHT FROGS", 0, -0.2f } } },
        { "STATION HULL", l_space, 1.3f, 2.5f, 8000, 8, 0.45f, 2, { { "DRIFT", 0, 0 }, { "SYSTEMS BUSY", 0, 0.3f }, { "SOLAR STORM", 0.3f, 0 }, { "DEEP SLEEP", -0.1f, -0.3f } } },
        { "CAVERN", l_caves, 2.0f, 5.0f, 3000, 50, 0.6f, 3, { { "STILL", 0, 0 }, { "DRIPPING", 0.2f, 0 }, { "FLOODING", 0.4f, 0 }, { "BREATHING", 0.3f, -0.2f } } },
        { "WOODLAND BROOK", l_forest, 1.0f, 1.4f, 5000, 15, 0.35f, 4, { { "STILL MORNING", 0, 0 }, { "BUSY CHORUS", 0, 0.3f }, { "PASSING SHOWER", 0.3f, 0 }, { "BREEZY", 0.12f, 0 } } },
        { "MARSH", l_swamp, 0.9f, 1.2f, 4000, 12, 0.3f, 5, { { "STILL NOON", 0, 0 }, { "DUSK CHORUS", 0, 0.3f }, { "RAIN", 0.25f, 0.1f }, { "NIGHT", 0, -0.1f } } },
        { "SHORELINE", l_ocean, 0.8f, 1.0f, 5000, 10, 0.25f, 6, { { "CALM", 0, 0 }, { "CHOPPY", 0.15f, 0 }, { "STORM", 0.35f, -0.2f }, { "NIGHT TIDE", -0.05f, -0.2f } } },
        { "CRYSTAL PLAINS", l_alien, 1.4f, 4.0f, 9000, 35, 0.45f, 7, { { "TWIN DAWN", 0, 0 }, { "SPORE BLOOM", 0, 0.35f }, { "ION STORM", 0.35f, -0.1f }, { "LONG NIGHT", -0.1f, -0.1f } } },
        { "FACTORY YARD", l_industrial, 1.6f, 3.0f, 4500, 30, 0.45f, 8, { { "FULL PRODUCTION", 0, 0.2f }, { "NIGHT SHIFT", 0, -0.2f }, { "RAIN ON THE ROOF", 0.3f, 0 }, { "LOCKED UP", 0.2f, -0.45f } } },
        { "WILDFLOWER MEADOW", l_meadow, 0.9f, 0.9f, 6000, 8, 0.18f, 9, { { "SUMMER AFTERNOON", 0, 0 }, { "BUSY DAY", 0, 0.3f }, { "PASSING SHOWER", 0.25f, 0 }, { "HAZY HEAT", -0.08f, -0.15f } } },
        { "MOUNTAIN PASS", l_mountain, 2.0f, 3.5f, 5000, 60, 0.35f, 10, { { "CLEAR DAY", 0, 0 }, { "CLOUD ROLLING IN", 0.25f, -0.1f }, { "STILL DAWN", -0.1f, 0.2f }, { "STORM ON THE PEAKS", 0.4f, -0.2f } } },
        { "ICE SHELF", l_arctic, 1.0f, 1.4f, 7000, 10, 0.2f, 11, { { "STILL COLD", -0.1f, -0.1f }, { "BLIZZARD", 0.4f, -0.2f }, { "MIDNIGHT SUN", 0, 0.2f }, { "ICE ON THE MOVE", 0.1f, 0.15f } } },
        { "SINGING DUNES", l_desert, 0.8f, 0.7f, 7000, 5, 0.1f, 12, { { "MIDDAY HEAT", -0.05f, -0.2f }, { "DESERT NIGHT", 0, 0.1f }, { "SANDSTORM", 0.45f, -0.3f }, { "STILL DAWN", -0.1f, 0.2f } } },
        { "GRASSLANDS", l_savanna, 1.0f, 1.0f, 6000, 6, 0.15f, 13, { { "DRY NOON", -0.05f, -0.15f }, { "GOLDEN EVENING", 0, 0.25f }, { "STORM SEASON", 0.35f, 0 }, { "NIGHT PLAINS", 0, 0 } } },
        { "VILLAGE GREEN", l_village, 1.1f, 1.3f, 5500, 15, 0.3f, 14, { { "SUNDAY MORNING", 0, 0.2f }, { "QUIET AFTERNOON", 0, -0.1f }, { "WET DAY", 0.3f, 0 }, { "MARKET DAY", 0, 0.3f } } },
    },
    {   /* HUMANS */
        { "TRAFFIC", h_city, NOPLACE }, { "RIVER BOAT", h_jungle, NOPLACE }, { "LIFE SUPPORT", h_space, NOPLACE },
        { "DEEP RUMBLE", h_caves, NOPLACE }, { "DISTANT ROAD", h_forest, NOPLACE }, { "AIRBOAT", h_swamp, NOPLACE },
        { "HARBOUR", h_ocean, NOPLACE }, { "VISITORS", h_alien, NOPLACE }, { "MACHINERY", h_industrial, NOPLACE },
        { "COUNTRYSIDE", h_meadow, NOPLACE },
        { "VALLEY BELOW", h_mountain, NOPLACE }, { "RESEARCH STATION", h_arctic, NOPLACE }, { "CARAVAN", h_desert, NOPLACE },
        { "SAFARI CAMP", h_savanna, NOPLACE }, { "VILLAGE LIFE", h_village, NOPLACE },
    },
};

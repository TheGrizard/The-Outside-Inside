/* Render an Outside scene to a stereo 16-bit WAV and print a report.
   usage: render <scene> <seconds> <seed> <out.wav> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../dsp/engine.h"
#include <math.h>

static void put16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, long v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 255, f); }

static float limit(float x) {
    float a = fabsf(x);
    if (a < 0.7f) return x;
    float y = 0.7f + 0.3f * tanhf((a - 0.7f) / 0.3f);
    return x < 0 ? -y : y;
}

/* demo timelines: each environment through four states in two minutes (WEATHER positions: type / 9) */
#define W(t) ((t) / 9.0f)
static const StatePoint DEMO[NUM_ENVS][4] = {
    { { 0, { W(2), 1.0f, 0.15f, 0.2f }, "3 AM" }, { 30, { W(3), 0.05f, 0.9f, 0.4f }, "MORNING RUSH" }, { 62, { W(5), 0.33f, 0.7f, 0.4f }, "RAIN" }, { 95, { W(3), 0.45f, 0.6f, 0.5f }, "AFTERNOON" } },
    { { 0, { W(2), 0.0f, 0.7f, 0.3f }, "MORNING CHORUS" }, { 30, { W(3), 0.33f, 0.6f, 0.4f }, "MIDDAY HEAT" }, { 62, { W(6), 0.5f, 0.5f, 0.4f }, "DOWNPOUR" }, { 95, { W(2), 1.0f, 0.6f, 0.3f }, "NIGHT FROGS" } },
    { { 0, { W(2), 0.5f, 0.4f, 0.2f }, "DRIFT" }, { 30, { W(3), 0.5f, 0.8f, 0.4f }, "SYSTEMS BUSY" }, { 62, { W(7), 0.5f, 0.5f, 0.5f }, "SOLAR STORM" }, { 95, { W(1), 0.9f, 0.2f, 0.3f }, "DEEP SLEEP" } },
    { { 0, { W(2), 0.2f, 0.4f, 0.2f }, "STILL" }, { 30, { W(4), 0.4f, 0.5f, 0.3f }, "DRIPPING" }, { 62, { W(6), 0.5f, 0.4f, 0.4f }, "FLOODING" }, { 95, { W(9), 0.8f, 0.5f, 0.3f }, "BREATHING" } },
    { { 0, { W(2), 0.0f, 0.55f, 0.2f }, "STILL MORNING" }, { 35, { W(3), 0.2f, 0.85f, 0.4f }, "BUSY CHORUS" }, { 66, { W(7), 0.33f, 0.5f, 0.3f }, "THUNDERSTORM" }, { 100, { W(4), 0.4f, 0.6f, 0.5f }, "AFTER THE STORM" } },
    { { 0, { W(2), 0.4f, 0.5f, 0.2f }, "STILL NOON" }, { 30, { W(3), 0.66f, 0.9f, 0.4f }, "DUSK CHORUS" }, { 62, { W(5), 0.75f, 0.7f, 0.4f }, "RAIN" }, { 95, { W(1), 1.0f, 0.5f, 0.3f }, "NIGHT" } },
    { { 0, { W(3), 0.3f, 0.5f, 0.2f }, "CALM" }, { 30, { W(9), 0.4f, 0.6f, 0.4f }, "CHOPPY" }, { 62, { W(7), 0.5f, 0.4f, 0.4f }, "STORM" }, { 95, { W(2), 1.0f, 0.4f, 0.3f }, "NIGHT TIDE" } },
    { { 0, { W(1), 0.0f, 0.5f, 0.2f }, "TWIN DAWN" }, { 30, { W(3), 0.25f, 0.9f, 0.5f }, "SPORE BLOOM" }, { 62, { W(7), 0.5f, 0.4f, 0.4f }, "ION STORM" }, { 95, { W(2), 1.0f, 0.45f, 0.3f }, "LONG NIGHT" } },
    { { 0, { W(2), 1.0f, 0.6f, 0.3f }, "NIGHT SHIFT" }, { 30, { W(5), 0.0f, 0.7f, 0.4f }, "RAIN ON THE ROOF" }, { 62, { W(3), 0.33f, 0.95f, 0.4f }, "FULL PRODUCTION" }, { 95, { W(9), 0.6f, 0.1f, 0.3f }, "LOCKED UP" } },
};
static const StatePoint TOUR[10] = {
    { 0, { W(0), 0.2f, 0.6f, 0 }, "SNOW" }, { 12, { W(1), 0.2f, 0.6f, 0 }, "FOG" }, { 24, { W(2), 0.2f, 0.6f, 0 }, "CALM" },
    { 36, { W(3), 0.2f, 0.6f, 0 }, "BREEZE" }, { 48, { W(4), 0.2f, 0.6f, 0 }, "DRIZZLE" }, { 60, { W(5), 0.2f, 0.6f, 0 }, "RAIN" },
    { 72, { W(6), 0.2f, 0.6f, 0 }, "DOWNPOUR" }, { 84, { W(7), 0.2f, 0.6f, 0 }, "THUNDERSTORM" }, { 96, { W(8), 0.2f, 0.6f, 0 }, "GALE" },
    { 108, { W(9), 0.2f, 0.6f, 0 }, "WINDY" },
};

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: render <scene> <seconds> <seed> <out.wav>\n"); return 1; }
    const char *scene = argv[1]; float secs = (float)atof(argv[2]); uint32_t seed = (uint32_t)strtoul(argv[3], 0, 10);
    dsp_init();
    static Engine e;
    int env = -1;
    for (int i = 0; i < NUM_ENVS; i++) { char lc[16]; int k = 0; for (; ENV_NAMES[i][k] && k < 15; k++) lc[k] = (char)(ENV_NAMES[i][k] | 0x20); lc[k] = 0; if (!strcmp(lc, scene)) env = i; }
    int tour = !strcmp(scene, "weather-tour");
    if (tour) env = 4;
    if (env < 0) { fprintf(stderr, "unknown environment %s\n", scene); return 1; }
    engine_init(&e, seed);
    engine_env(&e, env);
    if (tour) { e.timeline = TOUR; e.ntimeline = 10; }
    else { e.timeline = DEMO[env]; e.ntimeline = 4; }
    e.user = e.timeline[0].m; e.m = e.user; e.glide_s = tour ? 2.5f : 10.0f;
    long nblocks = (long)(secs * OUT_SR / OUT_BLOCK), n = nblocks * OUT_BLOCK;
    float *L = malloc(n * sizeof(float)), *R = malloc(n * sizeof(float));
    const int win = (int)(5 * OUT_SR / OUT_BLOCK);
    int lastev[NUM_LAYERS] = { 0 };
    static double lw[64][NUM_LAYERS], mw[64]; int nw = 0; double lastss[NUM_LAYERS] = { 0 }, mss = 0;
    printf("time  state            weather (now)  time   activity entropy | events W A L H\n");
    for (long b = 0; b < nblocks; b++) {
        engine_render(&e, L + b * OUT_BLOCK, R + b * OUT_BLOCK);
        for (int i = 0; i < OUT_BLOCK; i++) mss += L[b * OUT_BLOCK + i] * L[b * OUT_BLOCK + i] + R[b * OUT_BLOCK + i] * R[b * OUT_BLOCK + i];
        if ((b + 1) % win == 0) {
            printf("%4.0fs %-16s %-5s %.2f      %-5s  %.2f     %.2f   |", e.t, e.state_name ? e.state_name : "",
                   weather_name(e.eff.weather), e.eff.weather, time_name(e.eff.time), e.eff.activity, e.m.entropy);
            for (int l = 0; l < NUM_LAYERS; l++) { printf(" %4d", e.layer_events[l] - lastev[l]); lastev[l] = e.layer_events[l]; }
            printf("%s\n", e.startle > 0 ? "  (animals startled)" : "");
            if (nw < 64) { for (int l = 0; l < NUM_LAYERS; l++) { lw[nw][l] = e.layer_ss[l] - lastss[l]; lastss[l] = e.layer_ss[l]; } mw[nw] = mss; mss = 0; nw++; }
        }
    }
    /* level: set RMS to -20 dBFS, then a soft limiter and -1 dB ceiling */
    double ss = 0, sl = 0, sr = 0, slr = 0;
    for (long i = 0; i < n; i++) { ss += L[i] * L[i] + R[i] * R[i]; sl += L[i] * L[i]; sr += R[i] * R[i]; slr += L[i] * R[i]; }
    double rms = sqrt(ss / (2.0 * n));
    float gain = (float)(0.1 / (rms > 1e-9 ? rms : 1e-9));
    long limited = 0; float peak = 0;
    for (long i = 0; i < n; i++) {
        float a = L[i] * gain, c = R[i] * gain;
        if (fabsf(a) > 0.7f || fabsf(c) > 0.7f) limited++;
        L[i] = limit(a) * 0.89f; R[i] = limit(c) * 0.89f;
        if (fabsf(L[i]) > peak) peak = fabsf(L[i]); if (fabsf(R[i]) > peak) peak = fabsf(R[i]);
    }
    printf("\nraw rms %.4f, gain %.1f dB, limiter touched %.3f%% of samples, peak %.2f dBFS\n",
           rms, 20 * log10(gain), 100.0 * limited / n, 20 * log10(peak));
    printf("\nlevels after normalising, dBFS RMS per 5 s (dry layers, then full mix with reverb):\n time  WTHR  ANML  LAND  HUMN   MIX\n");
    double wn = 2.0 * win * OUT_BLOCK;
    for (int w = 0; w < nw; w++) {
        printf("%4ds", (w + 1) * 5);
        for (int l = 0; l < NUM_LAYERS; l++) printf(" %5.1f", 10 * log10(lw[w][l] / wn * gain * gain + 1e-12));
        printf(" %5.1f\n", 10 * log10(mw[w] / wn * gain * gain + 1e-12));
    }
    printf("stereo correlation %.2f (1 = mono, 0 = fully wide)\n", slr / sqrt(sl * sr + 1e-20));
    printf("\nevents by source:\n");
    for (int i = 0; i < e.nsrc; i++) printf("  %-16s layer %d  %6d\n", e.src[i].name, e.src[i].layer, e.src[i].events);

    FILE *f = fopen(argv[4], "wb");
    long bytes = n * 4;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, (long)OUT_SR); put32(f, (long)OUT_SR * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, bytes);
    for (long i = 0; i < n; i++) {
        int a = (int)lrintf(L[i] * 32767.0f), c = (int)lrintf(R[i] * 32767.0f);
        put16(f, a); put16(f, c);
    }
    fclose(f);
    return 0;
}

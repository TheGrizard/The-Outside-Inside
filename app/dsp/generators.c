/* Outside: the seven sound generators. */
#include "generators.h"

float g_sine[SINE_SIZE + 1];
void dsp_init(void) {
    for (int i = 0; i <= SINE_SIZE; i++) g_sine[i] = fm_sinf(TWO_PI_F * i / SINE_SIZE);
}

/* ===================================== AIR ===================================== */
void air_init(Air *a) {
    memset(a, 0, sizeof *a);
    a->level = 0.5f; a->colour = 1.0f; a->cutoff = 1000.0f; a->q = 0.7f;
    a->gust_depth = 0.6f; a->gust_speed = 1.0f; a->howl_q = 12.0f;
    a->width = 0.9f; a->send = 0.3f; a->weather = 0.5f;
    a->cut_s = a->cutoff;
}

static inline float air_colour(Air *a, int ch, float w) {
    float p = pink_run(&a->pk[ch], w);
    float br = brown_run(&a->br[ch], w);
    float c = a->colour;
    if (c <= 1.0f) return w * 0.55f * (1.0f - c) + p * c;
    return p * (2.0f - c) + br * (c - 1.0f);
}

void air_thunder(Air *a, Rng *r, float pan, float dist, float zap) {
    a->th_on = 1; a->th_t = 0; a->th_len = rng_range(r, 4.0f, 8.0f);
    a->th_amp = rng_range(r, 0.7f, 1.0f); a->th_crack = 1.0f - dist; a->th_zap = zap;
    a->th_mod = 1; a->th_modt = 1;
    spatial_set(&a->th_sp, pan, dist);
    a->th_sp.send = 0.6f;
    svf_set(&a->th_lp, 160.0f, 0.8f);
}

void air_burst(Air *a, float hold_s) { a->burst_hold = hold_s; }

void air_render(Air *a, Rng *r, Bus *b) {
    if (a->gust_left-- <= 0) {
        a->gust_target = rng_f(r) * (0.2f + 0.8f * a->weather);
        a->gust_left = (int)(rng_range(r, 1.5f, 6.0f) / a->gust_speed * OUT_SR / OUT_BLOCK);
    }
    a->gust += (a->gust_target - a->gust) * 0.0016f * a->gust_speed;
    float g = a->gust_link ? *a->gust_link : a->gust;
    float drive = g * (1.0f - a->swell_amt) + a->swell * a->swell_amt;
    float amp = a->level * (1.0f - a->gust_depth + a->gust_depth * drive * 1.5f);
    float cut = a->cutoff * (1.0f - 0.5f * a->gust_depth + 1.3f * a->gust_depth * drive);
    if (a->burst_mode) {
        if (a->burst_hold > 0) { a->burst_hold -= (float)OUT_BLOCK / OUT_SR; a->burst_env += (1.0f - a->burst_env) * 0.06f; }
        else a->burst_env *= 0.993f;
        amp *= a->burst_env;
    }
    a->amp_s += (amp - a->amp_s) * 0.08f;
    a->cut_s += (cut - a->cut_s) * 0.08f;
    svf_set(&a->f[0], a->cut_s, a->q); svf_set(&a->f[1], a->cut_s * 1.03f, a->q);
    if (a->hp > 0) { svf_set(&a->hpf[0], a->hp, 0.7f); svf_set(&a->hpf[1], a->hp, 0.7f); }
    float howl_amt = 0;
    if (a->howl > 0) {
        float hf = a->howl_freq * (0.7f + 0.8f * drive);
        svf_set(&a->howlf[0], hf, a->howl_q); svf_set(&a->howlf[1], hf * 1.01f, a->howl_q);
        if (a->howl2_ratio > 0) { svf_set(&a->howl2f[0], hf * a->howl2_ratio, a->howl_q); svf_set(&a->howl2f[1], hf * a->howl2_ratio * 0.99f, a->howl_q); }
        howl_amt = a->howl * a->level * (0.25f + 0.75f * drive) * 1.5f / fm_sqrtf(a->howl_q);
    }
    if (a->th_on && (a->th_modt -= (float)OUT_BLOCK / OUT_SR) <= 0) { a->th_mod = rng_range(r, 0.35f, 1.0f); a->th_modt = rng_range(r, 0.08f, 0.35f); }
    if (a->th_on && a->th_zap > 0) svf_set(&a->th_zf, 2200.0f * fm_expf(-a->th_t * 1.5f) + 140.0f, 5.0f);
    float k = a->bandpass ? 1.0f / a->q : 1.0f;

    for (int i = 0; i < OUT_BLOCK; i++) {
        if (a->no_bed) goto thunder;
        float ws = rng_bi(r);
        float nl = rng_bi(r) * a->width + ws * (1.0f - a->width);
        float nr = rng_bi(r) * a->width + ws * (1.0f - a->width);
        float cl = air_colour(a, 0, nl), cr = air_colour(a, 1, nr);
        float l, rr;
        if (a->bandpass) { l = svf_bp(&a->f[0], cl) * k; rr = svf_bp(&a->f[1], cr) * k; }
        else { l = svf_lp(&a->f[0], cl); rr = svf_lp(&a->f[1], cr); }
        if (a->hp > 0) { l = svf_hp(&a->hpf[0], l); rr = svf_hp(&a->hpf[1], rr); }
        l *= a->amp_s; rr *= a->amp_s;
        if (howl_amt > 0) {
            l += svf_bp(&a->howlf[0], cl) * howl_amt;
            rr += svf_bp(&a->howlf[1], cr) * howl_amt;
            if (a->howl2_ratio > 0) { l += svf_bp(&a->howl2f[0], cl) * howl_amt * 0.7f; rr += svf_bp(&a->howl2f[1], cr) * howl_amt * 0.7f; }
        }
        emit_stereo(b, i, l, rr, a->send);
    thunder:
        if (a->th_on) {
            float t = a->th_t;
            float w = rng_bi(r);
            float env = t < 0.25f ? t / 0.25f : fm_expf(-(t - 0.25f) / (a->th_len * 0.3f));
            float rumble = svf_lp(&a->th_lp, brown_run(&a->th_br, w) * 3.0f) * env * a->th_mod * 2.2f;
            float crack = w * a->th_crack * fm_expf(-t / 0.04f) * 0.6f;
            float x = (rumble + crack) * a->th_amp;
            if (a->th_zap > 0) x += svf_bp(&a->th_zf, w) * a->th_zap * env * 1.4f;
            emit(b, i, &a->th_sp, x * a->level);
            a->th_t += 1.0f / OUT_SR;
            if (a->th_t > a->th_len) a->th_on = 0;
        }
    }
}

/* ===================================== RAIN ===================================== */
void rain_init(Rain *rn) {
    memset(rn, 0, sizeof *rn);
    rn->level = 0.5f; rn->width = 0.9f; rn->send = 0.25f; rn->size = 0.5f; rn->var = 1.0f; rn->drops = 1.0f;
    rn->wash_lp = 6000.0f; rn->wash_hp = 300.0f; rn->wash_white = 0.5f;
    for (int s = 0; s < RAIN_SURF; s++) { rn->surf_f[s] = 1000.0f * (s + 1); rn->surf_q[s] = 2.0f; rn->surf_w[s] = 1.0f; rn->surf_g[s] = 1.0f; }
}

void rain_render(Rain *rn, Rng *r, Bus *b) {
    float fm = 1.35f - 0.6f * rn->size, am = (0.35f + 1.3f * rn->size) * rn->drops;
    for (int s = 0; s < RAIN_SURF; s++) { svf_set(&rn->sl[s], rn->surf_f[s] * fm, rn->surf_q[s]); svf_set(&rn->sr[s], rn->surf_f[s] * fm * 1.02f, rn->surf_q[s]); }
    svf_set(&rn->hissf[0], 5000.0f, 0.7f); svf_set(&rn->hissf[1], 5200.0f, 0.7f);
    svf_set(&rn->clickf[0], 2500.0f, 0.7f); svf_set(&rn->clickf[1], 2500.0f, 0.7f);
    float wsum = 0; for (int s = 0; s < RAIN_SURF; s++) wsum += rn->surf_w[s];
    float p = rn->density / OUT_SR;
    float hiss = rn->hiss * fm_sqrtf(rn->density / 2000.0f) * 0.25f * (1.4f - rn->size);
    rn->energy = 0;
    /* the wash: its level follows the square root of the drops' rate (many quiet sources add up so), with a slow
     * unsteadiness, as rain comes in waves */
    float wash = 0;
    if (rn->wash > 0) {
        rn->flut += (rng_bi(r) * 0.35f - rn->flut) * 0.02f;
        float target = rn->wash * 0.26f * fm_sqrtf(rn->density / 1000.0f) * (1.0f + rn->flut);
        rn->wash_amp += (target - rn->wash_amp) * 0.05f;
        wash = rn->wash_amp;
        svf_set(&rn->washlp[0], rn->wash_lp, 0.6f); svf_set(&rn->washlp[1], rn->wash_lp * 0.97f, 0.6f);
        svf_set(&rn->washhp[0], rn->wash_hp, 0.6f); svf_set(&rn->washhp[1], rn->wash_hp * 1.03f, 0.6f);
    }
    if (rn->tone > 0) for (int c = 0; c < 2; c++) for (int k = 0; k < 2; k++) op_set(&rn->tonef[c][k], rn->tone * (k ? 1.4f : 1.0f));
    for (int i = 0; i < OUT_BLOCK; i++) {
        float inL[RAIN_SURF] = { 0 }, inR[RAIN_SURF] = { 0 }, ckL = 0, ckR = 0;
        if (rng_f(r) < p) {
            float a = rng_exp(r); a = a * a * 0.4f; if (a > 4) a = 4;
            float even = 0.4f + 0.8f * rng_f(r);      /* (the same average loudness, no loud outliers) */
            a = (even + (a - even) * rn->var) * am;
            float t = (rng_bi(r) * rn->width + 1.0f) * 0.25f * (float)M_PI;
            float gl = fm_cosf(t) * a, gr = fm_sinf(t) * a;
            float pick = rng_f(r) * wsum; int s = 0;
            while (s < RAIN_SURF - 1 && (pick -= rn->surf_w[s]) > 0) s++;
            inL[s] = gl; inR[s] = gr; ckL = gl; ckR = gr;
            rn->energy += 1;
        }
        float l = 0, rr = 0;
        for (int s = 0; s < RAIN_SURF; s++) {
            if (rn->surf_g[s] == 0) continue;
            l += svf_bp(&rn->sl[s], inL[s]) * rn->surf_g[s];
            rr += svf_bp(&rn->sr[s], inR[s]) * rn->surf_g[s];
        }
        if (rn->click > 0) { l += svf_hp(&rn->clickf[0], ckL) * rn->click; rr += svf_hp(&rn->clickf[1], ckR) * rn->click; }
        if (hiss > 0) { l += svf_hp(&rn->hissf[0], rng_bi(r)) * hiss; rr += svf_hp(&rn->hissf[1], rng_bi(r)) * hiss; }
        if (rn->tone > 0) {                          /* the drops rounded off: two gentle poles, a little apart */
            l = op_run(&rn->tonef[0][1], op_run(&rn->tonef[0][0], l));
            rr = op_run(&rn->tonef[1][1], op_run(&rn->tonef[1][0], rr));
        }
        if (wash > 0) {
            for (int c = 0; c < 2; c++) {
                float w = rng_bi(r), n = w * rn->wash_white + pink_run(&rn->washpk[c], w) * 2.2f * (1.0f - rn->wash_white);
                n = svf_lp(&rn->washlp[c], svf_hp(&rn->washhp[c], n)) * wash;
                if (c) rr += n; else l += n;
            }
        }
        emit_stereo(b, i, l * rn->level, rr * rn->level, rn->send);
    }
}

/* ===================================== WATER ===================================== */
void water_init(Water *w) {
    memset(w, 0, sizeof *w);
    w->dmin = 1.0f; w->dmax = 8.0f; w->gamma = 2.0f; w->rise = 0.1f; w->decay_mul = 1.0f;
    w->level = 0.5f; w->pan_lo = -0.8f; w->pan_hi = 0.8f; w->dist_lo = 0.1f; w->dist_hi = 0.5f; w->rush_cut = 900.0f;
}

void water_spawn(Water *w, Rng *r, float d, float pan, float dist, float amp, float decay_mul) {
    Bubble *v = 0; float quiet = 1e9f;
    for (int i = 0; i < WATER_VOICES; i++) {
        if (!w->v[i].on) { v = &w->v[i]; break; }
        float lvl = w->v[i].env * w->v[i].amp;
        if (lvl < quiet) { quiet = lvl; v = &w->v[i]; }
    }
    float f = 6520.0f / d;                       /* Minnaert: about 6.52 kHz / diameter in mm */
    if (f < 120.0f) f = 120.0f; if (f > 14000.0f) f = 14000.0f;
    float damp = (0.043f * f + 0.0014f * fm_powf(f, 1.5f)) / decay_mul; /* van den Doel damping, per second */
    v->on = 1; v->ph = 0; v->f = f;
    v->dec = fm_expf(-damp / OUT_SR);
    v->df = f * w->rise * damp / OUT_SR;
    v->amp = amp * fm_powf(d / 3.0f, 0.6f) * 0.35f;
    v->env = 1.0f;
    spatial_set(&v->sp, pan, dist);
    w->energy += 1;
}

void water_burst(Water *w, int count, float seconds, float pan, float dist, float dmin, float dmax) {
    w->burst_left = count; w->burst_rate = count / (seconds > 0.01f ? seconds : 0.01f);
    w->burst_pan = pan; w->burst_dist = dist; w->burst_dmin = dmin; w->burst_dmax = dmax;
}

static float water_size(Rng *r, float dmin, float dmax, float gamma) {
    return dmin * fm_powf(dmax / dmin, fm_powf(rng_f(r), gamma));
}

void water_render(Water *w, Rng *r, Bus *b) {
    float p = w->rate / OUT_SR, pb = w->burst_rate / OUT_SR;
    if (w->rush > 0) { svf_set(&w->rush_f[0], w->rush_cut, 0.7f); svf_set(&w->rush_f[1], w->rush_cut * 1.05f, 0.7f); }
    w->energy *= 0.0f;
    for (int i = 0; i < OUT_BLOCK; i++) {
        if (p > 0 && rng_f(r) < p)
            water_spawn(w, r, water_size(r, w->dmin, w->dmax, w->gamma), rng_range(r, w->pan_lo, w->pan_hi), rng_range(r, w->dist_lo, w->dist_hi), rng_range(r, 0.35f, 1.0f), w->decay_mul);
        if (w->burst_left > 0 && rng_f(r) < pb) {
            w->burst_left--;
            water_spawn(w, r, water_size(r, w->burst_dmin, w->burst_dmax, 1.2f), w->burst_pan + rng_bi(r) * 0.25f, w->burst_dist + rng_bi(r) * 0.08f, rng_range(r, 0.5f, 1.0f), w->decay_mul);
        }
        if (w->rush > 0) {
            float l = svf_lp(&w->rush_f[0], pink_run(&w->rush_pk[0], rng_bi(r))) * w->rush;
            float rr = svf_lp(&w->rush_f[1], pink_run(&w->rush_pk[1], rng_bi(r))) * w->rush;
            emit_stereo(b, i, l * w->level, rr * w->level, 0.2f);
        }
    }
    for (int k = 0; k < WATER_VOICES; k++) {
        Bubble *v = &w->v[k];
        if (!v->on) continue;
        for (int i = 0; i < OUT_BLOCK; i++) {
            float s = sine_c(v->ph) * v->env * v->amp * w->level;
            v->ph += v->f / OUT_SR; v->f += v->df; v->env *= v->dec;
            emit(b, i, &v->sp, s);
        }
        if (v->ph > 1e6f) v->ph -= fm_floorf(v->ph);
        if (v->env < 1e-4f || v->f > 18000.0f) v->on = 0;
    }
}

/* ===================================== VOICE ===================================== */
void voice_init(Voice *v) { memset(v, 0, sizeof *v); v->level = 0.5f; }

void voice_play(Voice *v, const Syllable *s, float pitch_mul, float tempo_mul, float pan, float dist, float amp) {
    VVoice *x = 0; float oldest = -1;
    for (int i = 0; i < VOICE_VOICES; i++) {
        if (!v->v[i].on) { x = &v->v[i]; break; }
        if (v->v[i].t > oldest) { oldest = v->v[i].t; x = &v->v[i]; }
    }
    x->on = 1; x->t = 0; x->s = *s;
    x->s.dur = s->dur / (tempo_mul > 0.1f ? tempo_mul : 0.1f);
    x->s.amp = s->amp * amp;
    x->pmul = pitch_mul; x->ph = 0; x->phm = 0; x->vph = 0;
    spatial_set(&x->sp, pan, dist);
    v->energy += 1;
}

void voice_render(Voice *v, Bus *b) {
    const float dt = 1.0f / OUT_SR;
    for (int k = 0; k < VOICE_VOICES; k++) {
        VVoice *x = &v->v[k];
        if (!x->on) continue;
        Syllable *s = &x->s;
        float att = s->att > 0.01f ? s->att : 0.01f;
        float f = 0, g = 0;
        for (int i = 0; i < OUT_BLOCK; i++) {
            if ((i & 7) == 0) {                       /* the control rate: every 8 samples (5.5 kHz) */
                float u = x->t / s->dur;
                if (u >= 1.0f) { x->on = 0; break; }
                switch (s->curve) {
                case CURVE_EXP: f = s->f0 * fm_exp2f(fm_log2f(s->f1 / s->f0) * u); break;
                case CURVE_SWOOP: f = s->f0 + (s->f1 - s->f0) * sine_c(u * 0.5f); break;
                case CURVE_HOLD: f = s->f0; break;
                default: f = s->f0 + (s->f1 - s->f0) * u; break;
                }
                f *= x->pmul;
                if (s->vib_depth > 0) { f *= fm_exp2f(s->vib_depth / 12.0f * sine_c(x->vph)); x->vph += s->vib_rate * dt * 8.0f; }
                float e = u < att ? u / att : (1.0f - u) / (1.0f - att);
                e = e * e * (3.0f - 2.0f * e);
                float m = 1.0f;
                if (s->am_depth > 0) m = 1.0f - s->am_depth * (0.5f + 0.5f * sine_c(x->t * s->am_rate + 0.25f));
                g = e * m * s->amp * v->level;
            }
            x->ph += f * dt;
            float carrier = x->ph;
            if (s->fm_index > 0) { x->phm += f * s->fm_ratio * dt; carrier += s->fm_index * sine_c(x->phm) * 0.159f; }
            float o = sine_c(carrier);
            if (s->h2 != 0) o += s->h2 * sine_c(2.0f * x->ph);
            emit(b, i, &x->sp, o * g);
            x->t += dt;
        }
        if (x->ph > 1e5f) x->ph -= fm_floorf(x->ph);
        if (x->phm > 1e5f) x->phm -= fm_floorf(x->phm);
    }
}

/* ===================================== RING ===================================== */
void ring_init(Ring *g) { memset(g, 0, sizeof *g); g->level = 0.5f; }

void ring_strike(Ring *g, Rng *r, const Material *m, float freq, float amp, float bright, float decay_mul, float pan, float dist) {
    RVoice *v = 0; float quiet = 1e9f;
    for (int i = 0; i < RING_VOICES; i++) {
        if (!g->v[i].on) { v = &g->v[i]; break; }
        float e = 0; for (int k = 0; k < g->v[i].n; k++) e += fm_fabsf(g->v[i].re[k]) + fm_fabsf(g->v[i].im[k]);
        if (e < quiet) { quiet = e; v = &g->v[i]; }
    }
    v->on = 1; v->n = 0;
    for (int k = 0; k < m->n; k++) {
        float f = freq * m->ratio[k];
        if (f > 18000.0f) break;
        float dec = fm_expf(-1.0f / (m->tau[k] * decay_mul * OUT_SR));
        float a = amp * m->amp[k] * fm_powf(bright, (float)k) * 0.3f, ph = TWO_PI_F * rng_f(r), w = TWO_PI_F * f / OUT_SR;
        v->re[v->n] = a * fm_cosf(ph); v->im[v->n] = a * fm_sinf(ph);
        v->cr[v->n] = dec * fm_cosf(w); v->ci[v->n] = dec * fm_sinf(w);
        v->n++;
    }
    v->clk = amp * m->click; v->clk_dec = fm_expf(-1.0f / (0.003f * OUT_SR));
    svf_set(&v->clkf, m->click_cut > 0 ? m->click_cut : 3000.0f, 1.2f);
    spatial_set(&v->sp, pan, dist);
    g->energy += 1;
}

void ring_render(Ring *g, Rng *r, Bus *b) {
    for (int k = 0; k < RING_VOICES; k++) {
        RVoice *v = &g->v[k];
        if (!v->on) continue;
        float out[OUT_BLOCK];
        for (int i = 0; i < OUT_BLOCK; i++) out[i] = 0;
        for (int m = 0; m < v->n; m++) {             /* mode by mode: four multiplies a sample each */
            float re = v->re[m], im = v->im[m], cr = v->cr[m], ci = v->ci[m];
            for (int i = 0; i < OUT_BLOCK; i++) { float t = re * cr - im * ci; im = re * ci + im * cr; re = t; out[i] += im; }
            v->re[m] = re; v->im[m] = im;
        }
        if (v->clk > 1e-5f) for (int i = 0; i < OUT_BLOCK; i++) { out[i] += svf_bp(&v->clkf, rng_bi(r)) * v->clk; v->clk *= v->clk_dec; }
        for (int i = 0; i < OUT_BLOCK; i++) emit(b, i, &v->sp, out[i] * g->level);
        float e = v->clk;                             /* the modes that have died away go: the voice gets cheaper as it rings out */
        for (int m = 0; m < v->n; m++) {
            float a = fm_fabsf(v->re[m]) + fm_fabsf(v->im[m]);
            if (a < 2e-5f) { v->n--; v->re[m] = v->re[v->n]; v->im[m] = v->im[v->n]; v->cr[m] = v->cr[v->n]; v->ci[m] = v->ci[v->n]; m--; continue; }
            e += a;
        }
        if (e < 1e-4f || v->n == 0) v->on = 0;
    }
}

/* ===================================== HUM ===================================== */
void hum_init(Hum *h) { memset(h, 0, sizeof *h); h->level = 0.5f; }

int hum_start(Hum *h, float f, float ratio, float index, float detune, float noise, float noise_cut, float vib_rate, float vib_depth, float amp, float att_s) {
    int idx = -1;
    for (int i = 0; i < HUM_VOICES; i++) if (!h->v[i].on) { idx = i; break; }
    if (idx < 0) return -1;
    HVoice *v = &h->v[idx];
    memset(v, 0, sizeof *v);
    v->on = 1; v->f = f; v->ratio = ratio; v->index = index; v->detune = detune; v->noise = noise; v->noise_cut = noise_cut;
    v->vib_rate = vib_rate; v->vib_depth = vib_depth; v->target = amp; v->pmul = 1.0f;
    v->att = 1.0f - fm_expf(-(float)OUT_BLOCK / (att_s * OUT_SR));
    spatial_set(&v->sp, 0, 0.3f);
    h->energy += 1;
    return idx;
}

void hum_release(Hum *h, int i, float rel_s) {
    if (i < 0 || i >= HUM_VOICES) return;
    h->v[i].target = 0;
    h->v[i].rel = 1.0f - fm_expf(-(float)OUT_BLOCK / (rel_s * OUT_SR));
}

void hum_render(Hum *h, Rng *r, Bus *b) {
    const float dt = 1.0f / OUT_SR;
    for (int k = 0; k < HUM_VOICES; k++) {
        HVoice *v = &h->v[k];
        if (!v->on) continue;
        float slew = v->target > v->amp ? v->att : (v->rel > 0 ? v->rel : v->att);
        v->amp += (v->target - v->amp) * slew;
        if (v->target == 0 && v->amp < 1e-4f) { v->on = 0; continue; }
        if (v->noise > 0) { svf_set(&v->nf[0], v->noise_cut, 0.7f); svf_set(&v->nf[1], v->noise_cut * 1.04f, 0.7f); }
        float f = v->f * v->pmul;
        if (v->vib_depth > 0) { f *= fm_exp2f(v->vib_depth / 12.0f * sine_c(v->vph)); v->vph += v->vib_rate * dt * OUT_BLOCK; }   /* once a block */
        for (int i = 0; i < OUT_BLOCK; i++) {
            float out[2];
            for (int c = 0; c < 2; c++) {
                float fc = f + (c ? 0.5f : -0.5f) * v->detune;
                v->ph[c] += fc * dt; v->phm[c] += fc * v->ratio * dt;
                float x = sine_c(v->ph[c] + v->index * sine_c(v->phm[c]) * 0.159f);
                if (v->noise > 0) x += svf_lp(&v->nf[c], pink_run(&v->pk[c], rng_bi(r))) * v->noise * 3.0f;
                out[c] = x * v->amp * h->level;
            }
            float l = out[0] * v->sp.gl, rr = out[1] * v->sp.gr;
            b->L[i] += l; b->R[i] += rr; b->sL[i] += l * v->sp.send; b->sR[i] += rr * v->sp.send;
        }
        for (int c = 0; c < 2; c++) { if (v->ph[c] > 1e5f) v->ph[c] -= fm_floorf(v->ph[c]); if (v->phm[c] > 1e5f) v->phm[c] -= fm_floorf(v->phm[c]); }
    }
}

/* ===================================== CROWD ===================================== */
void crowd_init(Crowd *c) { memset(c, 0, sizeof *c); c->level = 0.5f; }

void crowd_say(Crowd *c, float f0, float dur, float fa1, float fa2, float fb1, float fb2, float pan, float dist, float amp) {
    CVoice *v = 0;
    for (int i = 0; i < CROWD_VOICES; i++) if (!c->v[i].on) { v = &c->v[i]; break; }
    if (!v) v = &c->v[0];
    memset(v, 0, sizeof *v);
    v->on = 1; v->dur = dur; v->f0 = f0; v->fa1 = fa1; v->fa2 = fa2; v->fb1 = fb1; v->fb2 = fb2; v->amp = amp;
    spatial_set(&v->sp, pan, dist);
    c->energy += 1;
}

void crowd_render(Crowd *c, Rng *r, Bus *b) {
    const float dt = 1.0f / OUT_SR;
    for (int k = 0; k < CROWD_VOICES; k++) {
        CVoice *v = &c->v[k];
        if (!v->on) continue;
        float u = v->t / v->dur;
        if (u >= 1.0f) { v->on = 0; continue; }
        v->jit += (rng_bi(r) * 0.04f - v->jit) * 0.2f;
        svf_set(&v->f1, v->fa1 + (v->fb1 - v->fa1) * u, 7.0f);
        svf_set(&v->f2, v->fa2 + (v->fb2 - v->fa2) * u, 9.0f);
        float f = v->f0 * (1.0f + v->jit) * (1.0f - 0.15f * u), e = 0;
        for (int i = 0; i < OUT_BLOCK; i++) {
            if ((i & 7) == 0) { float uu = v->t / v->dur; e = sine_c(uu * 0.5f); e = e > 0 ? fm_exp2f(0.7f * fm_log2f(e + 1e-9f)) : 0; }
            v->ph += f * dt; if (v->ph >= 1.0f) v->ph -= 1.0f;
            float saw = 2.0f * v->ph - 1.0f;
            float x = svf_bp(&v->f1, saw) * 0.5f + svf_bp(&v->f2, saw) * 0.3f;
            emit(b, i, &v->sp, x * e * v->amp * c->level);
            v->t += dt;
        }
    }
}

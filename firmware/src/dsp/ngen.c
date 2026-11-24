/* SPDX-License-Identifier: GPL-3.0-only */
/* ngen: see ngen.h. */
#include "ngen.h"
#include "fastmath.h"

float nfm_sine(float phase);               /* nfm.c's shared table, phase in cycles */

#define NG_FENCE() __asm__ volatile("" ::: "memory")
#define NG_BLOCK 32                        /* frames per control step */
#define NG_INJECT 0.55f                    /* share of what is left to full a full-strength step adds */
#define NG_FLOOR 0.16f                     /* amplitude at no energy, while the bar's hold lasts */
#define NG_CUT 95.0f                       /* Hz: nothing below (kick and rumble own it) */
#define NG_LOW 150.0f                      /* Hz: from NG_CUT up to here partials come in gradually */
#define NG_HIGH 15000.0f
#define NG_RMS 0.2f

/* A chord's tones as multiples of one fundamental, so a period holds all of them (just intonation):
 * calm first, then what complexity adds. Minor over root/10, major over root/4. */
static const uint8_t TONES_MIN[] = {10, 15, 20, 12, 18, 24, 30, 36, 45};
static const uint8_t TONES_MAJ[] = {4, 6, 8, 5, 10, 12, 9, 7, 16};
#define NG_NTONES 9
#define NG_CALM 3

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static float rnd01(uint32_t seed, uint32_t a, uint32_t b) { return (float)(hash32(seed ^ hash32(a * 0x9e3779b9u + b)) >> 8) * (1.0f / 16777216.0f); }

static float pct(const ngen *g, int p) { return (float)g->param[p] * 0.01f; }
static float root_hz(const ngen *g) { return 440.0f * fm_exp2f(((float)g->root - 69.0f) * (1.0f / 12.0f)); }
static float fund_hz(const ngen *g) { return root_hz(g) / (g->minor ? 10.0f : 4.0f); }
static const uint8_t *tones(const ngen *g) { return g->minor ? TONES_MIN : TONES_MAJ; }
static int ntones(const ngen *g, int state)
{
    return state ? NG_CALM + (int)(pct(g, NGEN_COMPLEXITY) * (NG_NTONES - NG_CALM) + 0.5f) : NG_CALM;
}
/* a third of the seeds are dull sounds, the low partials leading and the filters lower: 0 bright .. 1 dull */
static float dull(const ngen *g)
{
    float d = rnd01(g->seed, 9u, 61u);
    return d < 0.34f ? 1.0f - d * (1.0f / 0.34f) : 0.0f;
}
static int nharm(const ngen *g, int state) { return state ? 4 + (int)(pct(g, NGEN_COMPLEXITY) * 20.0f) : 3; }

static int16_t *slot_at(const ngen *g, int slot, int state, int ch)
{
    return g->mem + (uint32_t)slot * NGEN_SLOT_WORDS + ((uint32_t)state * 2u + (uint32_t)ch) * NGEN_LEN;
}

void ngen_init(ngen *g, int16_t *mem, float sr, uint32_t seed)
{
    uint8_t *b = (uint8_t *)g;
    unsigned i;
    for (i = 0; i < sizeof *g; i++)
        b[i] = 0;
    g->mem = mem;
    g->sr = sr;
    g->seed = seed;
    g->param[NGEN_COMPLEXITY] = 70;
    g->param[NGEN_MOVEMENT] = 60;
    g->param[NGEN_NOISE] = 35;
    g->param[NGEN_SPREAD] = 75;
    g->param[NGEN_INERTIA] = 40;
    g->param[NGEN_DECAY] = 10;
    g->root = 45;                           /* A2 */
    g->minor = 1;
    g->len = 16;
    g->job = 1;
    g->wr = 1;
    g->rng = seed | 1u;
    g->k_sp = pct(g, NGEN_SPREAD);
    g->k_mov = pct(g, NGEN_MOVEMENT);
    g->k_nz = pct(g, NGEN_NOISE);
    g->nz_c[0] = g->nz_c[1] = g->nz_t[0] = g->nz_t[1] = 1000.0f;
}

void ngen_set_param(ngen *g, int p, int value)
{
    if ((unsigned)p >= NGEN_NPARAMS)
        return;
    value = value < 0 ? 0 : value > 100 ? 100 : value;
    if (g->param[p] == value)
        return;
    g->param[p] = (uint8_t)value;
    if (p == NGEN_COMPLEXITY || p == NGEN_SPREAD)
        g->req++;                           /* the material changes: made anew, faded in */
    if (p == NGEN_DECAY)
        ngen_pattern(g, g->vel, g->len);
}

void ngen_set_key(ngen *g, int root_note, int minor)
{
    while (root_note < 33)                  /* the key's root, kept in A1..G#2: the chord sits above it */
        root_note += 12;
    while (root_note > 44)
        root_note -= 12;
    root_note += 12;
    if (g->root == root_note && g->minor == (minor != 0))
        return;
    g->root = (int8_t)root_note;
    g->minor = minor != 0;
    g->req++;
}

void ngen_new_sound(ngen *g, uint32_t seed)
{
    if (g->seed == seed)
        return;
    g->seed = seed;
    g->req++;
}

/* energy's decay: from 1 to about 5 % over Decay's half a bar .. 3 bars */
static float decay_bars(const ngen *g) { return 0.5f + 2.5f * pct(g, NGEN_DECAY); }
static float step_keep(const ngen *g)
{
    float bars = decay_bars(g);
    return fm_expf(-3.0f / (16.0f * bars));
}

void ngen_pattern(ngen *g, const uint8_t *vel, int len)
{
    int i, k, loop;
    float e = 0.0f, keep = step_keep(g), sum = 0.0f;
    if (len < 1)
        len = 1;
    if (len > NGEN_STEPS)
        len = NGEN_STEPS;
    if (vel != g->vel)
        for (i = 0; i < len; i++)
            g->vel[i] = vel[i];
    g->len = (uint8_t)len;
    /* the energy the loop settles to at each step: three times round, the last one kept */
    for (loop = 0; loop < 3; loop++)
        for (i = 0; i < len; i++) {
            float v = (float)g->vel[i] * (1.0f / 127.0f);
            e *= keep;
            if (v > 0.0f)
                e += NG_INJECT * v * (1.0f - e);
            g->pred[i] = e;
        }
    for (i = 0; i < len; i++)
        sum += (float)g->vel[i] * (1.0f / 127.0f);
    g->global = sum / (float)len;
    /* what is coming: the next quarter and a bit, the nearer stronger; the timbre leans there */
    for (i = 0; i < len; i++) {
        float f = 0.0f, w = 0.0f;
        for (k = 1; k <= 6; k++) {
            float wk = 1.0f - (float)k * (1.0f / 7.0f);
            f += g->pred[(i + k) % len] * wk;
            w += wk;
        }
        f /= w;
        g->plan[i] = f > g->pred[i] ? f : g->pred[i];
    }
}

void ngen_step(ngen *g, int pos, uint32_t step_len)
{
    float v;
    if (!g->len)
        return;
    g->pos = (uint8_t)((unsigned)pos % g->len);
    g->step_len = step_len ? step_len : 1;
    g->step_t = 0;
    v = (float)g->vel[g->pos] * (1.0f / 127.0f);
    if (v > 0.0f) {
        g->e += NG_INJECT * v * (1.0f - g->e);
        if (v > g->bloom)
            g->bloom = v;
        g->since = 0;
        g->gate = 1.0f;
        g->live = 1;
    }
}

void ngen_stop(ngen *g)
{
    g->e = 0.0f;
    g->gate = 0.0f;
    g->live = 0;
}

int ngen_active(const ngen *g) { return g->live || g->amp > 1e-4f; }

/* --------------------------------------------------------------- the material --- */

/* inharmonic partials: on the period's grid (any whole multiple of the fundamental), not on the chord's */
static int ninharm(const ngen *g, int state) { return state ? 8 + (int)(pct(g, NGEN_COMPLEXITY) * 40.0f) : 4; }

/* one partial of the set being made (tone it_t, harmonic it_n; after the tones, the inharmonic ones) into
 * the scratch; 0 when none is left */
static int next_partial(ngen *g)
{
    const uint8_t *tn = tones(g);
    float f0 = fund_hz(g), width = pct(g, NGEN_SPREAD) * (g->state ? 1.0f : 0.7f);
    float odd = rnd01(g->seed, 7u, 1u);
    int nt = ntones(g, g->state);
    for (;;) {
        uint32_t h, i, q;
        float f, a, pan, pl, pr, gl, gr, dph;
        if (g->it_t > nt)
            return 0;
        if (g->it_t == nt) {                 /* the inharmonic ones: scattered, quiet, wide */
            uint32_t lo = (uint32_t)(NG_CUT / f0) + 1u, span;
            if (g->it_n >= ninharm(g, g->state)) {
                g->it_t++;
                continue;
            }
            span = NGEN_LEN / 2u - lo;
            if (f0 * (float)(lo + span) > NG_HIGH)
                span = (uint32_t)(NG_HIGH / f0) - lo;
            /* the same seed draws the same ones in both states: the intense one has more of them */
            h = lo + (uint32_t)(rnd01(g->seed, (uint32_t)g->it_n, 11u) * (float)span);
            f = f0 * (float)h;
            a = (0.08f + 0.22f * rnd01(g->seed, (uint32_t)g->it_n, 12u)) * (g->state ? 1.0f : 0.5f);
            g->it_n++;
        } else {
            q = tn[g->it_t];
            h = q * (uint32_t)(g->it_n + 1u);
            f = f0 * (float)h;
            if (g->it_n >= nharm(g, g->state) || h >= NGEN_LEN / 2u || f > NG_HIGH) {
                g->it_t++;
                g->it_n = 0;
                continue;
            }
            g->it_n++;
            /* level: calm overtones fall fast, intense ones slowly; odd or even ones favoured by the seed */
            a = fm_powf((float)g->it_n, g->state ? -0.95f : -1.8f);
            if (g->it_n > 1)
                a *= (g->it_n & 1) ? 0.6f + 0.8f * odd : 1.4f - 0.8f * odd;
            a *= g->it_t == 0 ? 1.0f : g->it_t < NG_CALM ? 0.75f : 0.55f;
        }
        if (f < NG_CUT)
            continue;
        if (f < NG_LOW)
            a *= (f - NG_CUT) / (NG_LOW - NG_CUT);
        a *= 0.6f + 0.8f * rnd01(g->seed, h, 2u);
        /* the tilt: calm leans to the low partials, intense to the high ones */
        a *= fm_powf(f * (1.0f / 400.0f), g->state ? 0.45f - 0.75f * dull(g) : -0.6f - 0.4f * dull(g));
        /* stereo: low down one centred core, above it each partial its own place and phase */
        pl = rnd01(g->seed, h, 3u);
        if (f < 200.0f) {
            pan = 0.5f;
            dph = 0.0f;
        } else {
            pan = 0.5f + (rnd01(g->seed, h, 4u) - 0.5f) * width;
            dph = (rnd01(g->seed, h, 5u) - 0.5f) * 0.35f * width;
        }
        pr = pl + dph;
        gl = nfm_sine(0.25f * (1.0f - pan));    /* equal power */
        gr = nfm_sine(0.25f * pan);
        for (i = 0; i < NGEN_LEN; i++) {
            float c = (float)((h * i) & (NGEN_LEN - 1u)) * (1.0f / NGEN_LEN);
            g->scratch[0][i] += a * gl * nfm_sine(c + pl);
            g->scratch[1][i] += a * gr * nfm_sine(c + pr);
        }
        return 1;
    }
}

/* the state just summed, to int16: its loudness matched, its peaks kept below full */
static void finish_state(ngen *g)
{
    float sq = 0.0f, peak = 0.0f, k;
    uint32_t i, c;
    for (c = 0; c < 2u; c++)
        for (i = 0; i < NGEN_LEN; i++) {
            float x = g->scratch[c][i];
            sq += x * x;
            if (fm_fabsf(x) > peak)
                peak = fm_fabsf(x);
        }
    sq /= 2.0f * NGEN_LEN;
    k = sq > 1e-12f ? NG_RMS / fm_expf(0.5f * fm_logf(sq)) : 0.0f;
    if (peak * k > 0.97f)
        k = 0.97f / peak;
    for (c = 0; c < 2u; c++) {
        int16_t *d = slot_at(g, g->wr, g->state, (int)c);
        for (i = 0; i < NGEN_LEN; i++) {
            float x = g->scratch[c][i] * k * 32767.0f;
            d[i] = (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
            g->scratch[c][i] = 0.0f;
        }
    }
}

int ngen_work(ngen *g)
{
    if (g->ready)
        return 0;                           /* the last one has not been taken yet */
    if (g->req != g->req_done) {            /* asked for (again): start over */
        g->req_done = g->req;
        g->job = 1;
    }
    if (g->job == 1) {                      /* start: a slot neither playing nor fading out */
        uint8_t s;
        for (s = 0; s < NGEN_SLOTS; s++)
            if (s != g->cur && s != g->old)
                break;
        g->wr = s;
        g->job_f0 = fund_hz(g);
        g->state = 0;
        g->it_t = g->it_n = 0;
        g->job = 2;
        return 1;
    }
    if (g->job != 2)
        return 0;
    if (next_partial(g))
        return 1;
    finish_state(g);
    g->it_t = g->it_n = 0;
    if (++g->state < NGEN_STATES)
        return 1;
    g->job = 0;
    g->f0[g->wr] = g->job_f0;
    g->dk[g->wr] = dull(g);
    g->made++;
    NG_FENCE();
    g->ready = 1;
    return 1;
}

/* ------------------------------------------------------------------ playback --- */

static inline float rd(const int16_t *t, float ph)
{
    uint32_t i = (uint32_t)ph, j = (i + 1u) & (NGEN_LEN - 1u);
    float fr = ph - (float)i;
    return (float)t[i] + ((float)t[j] - (float)t[i]) * fr;
}

static inline float wrap(float ph) { return ph >= (float)NGEN_LEN ? ph - (float)NGEN_LEN : ph; }

/* the noise's next resonance: one of the chord's partials (any of its tones, overtone 2..9), 300 Hz..5 kHz */
static float noise_centre(ngen *g, int side)
{
    const uint8_t *tn = tones(g);
    uint32_t k = g->nz_next * 2u + (uint32_t)side;
    float f = fund_hz(g) * (float)tn[(uint32_t)(rnd01(g->seed, k, 21u) * (float)ntones(g, 1)) % NG_NTONES] *
              (float)(2u + (uint32_t)(rnd01(g->seed, k, 22u) * 8.0f));
    while (f > 5000.0f)
        f *= 0.5f;
    while (f < 700.0f)
        f *= 2.0f;
    return f;
}

static inline float white(ngen *g)
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

/* slow drifts: each its own length in bars (2..8, from the seed), a sine */
static float drift(const ngen *g, int i) { return nfm_sine(g->lfo[i]); }

void ngen_render(ngen *g, float *l, float *r, int n)
{
    int done = 0;
    if (g->ready && (g->old == g->cur || !g->have)) {      /* a new set: fade it in */
        NG_FENCE();
        g->old = g->have ? g->cur : g->wr;
        g->cur = g->wr;
        g->po[0] = g->ph[0];                     /* the old set reads on where it was */
        g->po[1] = g->ph[1];
        g->po[2] = g->ph[2];
        g->fade = 0;
        g->fade_len = g->step_len ? 4u * g->step_len : (uint32_t)(0.5f * g->sr);
        g->have = 1;
        NG_FENCE();
        g->ready = 0;
    }
    if (!g->have)
        return;
    while (done < n) {
        int m = n - done > NG_BLOCK ? NG_BLOCK : n - done, i, k;
        uint32_t bar = 16u * (g->step_len ? g->step_len : (uint32_t)(0.125f * g->sr));
        float kk = 1.0f - fm_expf(-(float)m / (0.05f * g->sr)), mov, spread;
        float target, mt, k_amp, k_m, inertia, det, c0, c1, p0, p1, dp, depth, a_step;
        float nb, nb_t, mo, dl, kt[2], vb, kf[2], qn, side, bal, hpa, dhp;
        const int16_t *n0l = slot_at(g, g->cur, 0, 0), *n0r = slot_at(g, g->cur, 0, 1);
        const int16_t *n1l = slot_at(g, g->cur, 1, 0), *n1r = slot_at(g, g->cur, 1, 1);
        const int16_t *o0l = slot_at(g, g->old, 0, 0), *o0r = slot_at(g, g->old, 0, 1);
        const int16_t *o1l = slot_at(g, g->old, 1, 0), *o1r = slot_at(g, g->old, 1, 1);
        int fading = g->old != g->cur;
        /* a knob's change glides in over ~50 ms */
        g->k_sp += (pct(g, NGEN_SPREAD) - g->k_sp) * kk;
        g->k_mov += (pct(g, NGEN_MOVEMENT) - g->k_mov) * kk;
        g->k_nz += (pct(g, NGEN_NOISE) - g->k_nz) * kk;
        mov = g->k_mov;
        spread = g->k_sp;
        /* energy: time takes it away; after a bar without steps the hold lets go */
        g->e *= fm_expf(-3.0f * (float)m / ((float)bar * decay_bars(g)));
        g->bloom *= fm_expf(-(float)m / (1.5f * (float)(g->step_len ? g->step_len : 5512u)));
        g->since += (uint32_t)m;
        if (g->since >= bar) {
            g->gate *= fm_expf(-4.0f * (float)m / (float)bar);
            if (g->gate < 1e-5f) {
                g->gate = 0.0f;
                g->live = 0;
            }
        }
        for (k = 0; k < 4; k++) {                    /* the drifts: 2, 3, 5 and 8 bars, scaled by the seed */
            static const float BARS[4] = {2.0f, 3.0f, 5.0f, 8.0f};
            g->lfo[k] += (float)m / ((float)bar * BARS[k] * (0.75f + 0.5f * rnd01(g->seed, (uint32_t)k, 31u)));
            if (g->lfo[k] >= 1.0f)
                g->lfo[k] -= 1.0f;
        }
        /* level: energy squared over a low floor, and each step's flash on top */
        target = g->gate * (NG_FLOOR + (1.0f - NG_FLOOR) * g->e * g->e) * (1.0f + 0.35f * mov * g->bloom);
        /* timbre: the energy, leaning towards what the plan says is coming, followed with inertia */
        mt = g->e;
        if (g->live && g->plan[g->pos] > mt)
            mt += 0.6f * (g->plan[g->pos] - mt);
        inertia = (0.25f + 3.75f * pct(g, NGEN_INERTIA)) * (float)(g->step_len ? g->step_len : 5512u);
        k_m = 1.0f - fm_expf(-(float)m / inertia);
        g->morph += (mt - g->morph) * k_m;
        mo = fm_clampf(g->morph + 0.25f * mov * drift(g, 0) + 0.4f * mov * g->bloom, 0.0f, 1.0f);
        k_amp = 1.0f - fm_expf(-1.0f / (0.012f * g->sr));
        /* the share of noise: half of Noise (default 17.5 %), moved +-7.5 % by its drift, a little up with energy and each step */
        nb_t = fm_clampf(0.5f * g->k_nz + 0.075f * drift(g, 1) + 0.04f * (g->bloom + g->morph) - 0.03f, 0.0f, 0.45f);
        nb = g->param[NGEN_NOISE] ? nb_t : 0.0f;    /* the noise's share of the power; Noise 0: none */
        /* the noise's resonances wander from partial to partial, every half bar a new one, gliding */
        if (g->since % (bar / 2u) < (uint32_t)m || g->nz_t[0] == 1000.0f) {
            g->nz_next++;
            g->nz_t[0] = noise_centre(g, 0);
            g->nz_t[1] = noise_centre(g, 1);
        }
        for (k = 0; k < 2; k++) {
            g->nz_c[k] += (g->nz_t[k] * (0.35f + 1.3f * mo) * (1.0f - 0.45f * g->dl) - g->nz_c[k]) * (1.0f - fm_expf(-(float)m / (0.25f * (float)bar)));
            kf[k] = 2.0f * nfm_sine(fm_minf(g->nz_c[k], 7000.0f) / (2.0f * g->sr));
        }
        qn = 0.08f + 0.5f * (1.0f - mo);             /* calm: a narrow, singing band; intense: wider, rougher */
        side = 1.0f + 0.15f * spread * mo;
        bal = 0.3f * mov * drift(g, 2);              /* the image leans slowly from side to side */
        hpa = 1.0f / (1.0f + FM_TWO_PI * NG_CUT / g->sr);
        dhp = 1.0f / (1.0f + FM_TWO_PI * 350.0f / g->sr);
        /* a pulse on the sixteenths, deeper with energy; raised cosine, so a step's edge does not click */
        depth = mov * g->morph * 0.6f;
        a_step = g->step_len ? 1.0f / (float)g->step_len : 0.0f;
        p0 = (float)g->step_t * a_step;
        p1 = (float)(g->step_t + (uint32_t)m) * a_step;
        if (p1 > 1.0f)
            p1 = 1.0f;
        c0 = 1.0f - depth * 0.5f * (1.0f - nfm_sine(p0 + 0.25f));
        c1 = 1.0f - depth * 0.5f * (1.0f - nfm_sine(p1 + 0.25f));
        dp = (c1 - c0) / (float)m;
        g->step_t += (uint32_t)m;
        /* the readers: one shared (the stronger, the core), one each side detuned apart (more with energy) */
        det = spread * 0.005f * (0.4f + 0.6f * mo) * (1.0f + 0.3f * drift(g, 3));
        /* the tones' low-pass per side: up with energy, each side's cutoff drifting its own way */
        /* how dull: the playing set's, gliding over a quarter as a new one comes in */
        g->dl += (g->dk[g->cur] - g->dl) * (1.0f - fm_expf(-(float)m / (4.0f * (float)(g->step_len ? g->step_len : 5512u))));
        dl = g->dl;
        for (k = 0; k < 2; k++) {
            float dr = nfm_sine(g->lfo[3] + 0.37f * (float)k + 0.11f * rnd01(g->seed, (uint32_t)k, 41u));
            float fc = fm_minf(350.0f * fm_exp2f(4.3f * mo * (1.0f - 0.45f * dl) + 1.1f * mov * dr - 0.8f * dl), 7000.0f);
            kt[k] = 2.0f * nfm_sine(fc / (2.0f * g->sr));
        }
        /* vibrato: a few cents, the core one way and the detuned readers apart, 4-6.5 Hz from the seed */
        g->vib += (float)m * (4.0f + 2.5f * rnd01(g->seed, 5u, 51u)) / g->sr;
        if (g->vib >= 1.0f)
            g->vib -= 1.0f;
        vb = nfm_sine(g->vib) * mov * (0.6f + 0.4f * drift(g, 0));
        {
            float inc0 = g->f0[g->cur] * (float)NGEN_LEN / g->sr * (1.0f + 0.0016f * vb);
            float incl = inc0 * (1.0f + det + 0.003f * vb), incr = inc0 * (1.0f - det - 0.003f * vb);
            float oinc0 = g->f0[g->old] * (float)NGEN_LEN / g->sr * (1.0f + 0.0016f * vb);
            float oincl = oinc0 * (1.0f + det + 0.003f * vb), oincr = oinc0 * (1.0f - det - 0.003f * vb);
            float sc = 1.0f / 65534.0f, pt = 0.0f, pn = 0.0f, gn = g->gn;
            for (i = 0; i < m; i++) {
                float xl, xr, dl, dr, w, a, b, mid, sd, y[2], x[2], wc;
                g->amp += (target - g->amp) * k_amp;
                /* each side: the shared reader (the core) and its own detuned one, whose lows are taken
                 * away, so below a few hundred Hz both sides are the same */
                a = rd(n0l, g->ph[0]); b = rd(n1l, g->ph[0]); xl = a + (b - a) * mo;
                a = rd(n0r, g->ph[0]); b = rd(n1r, g->ph[0]); xr = a + (b - a) * mo;
                a = rd(n0l, g->ph[1]); b = rd(n1l, g->ph[1]); dl = a + (b - a) * mo;
                a = rd(n0r, g->ph[2]); b = rd(n1r, g->ph[2]); dr = a + (b - a) * mo;
                if (fading) {
                    float ol, orr, odl, odr;
                    w = (float)g->fade / (float)g->fade_len;
                    a = rd(o0l, g->po[0]); b = rd(o1l, g->po[0]); ol = a + (b - a) * mo;
                    a = rd(o0r, g->po[0]); b = rd(o1r, g->po[0]); orr = a + (b - a) * mo;
                    a = rd(o0l, g->po[1]); b = rd(o1l, g->po[1]); odl = a + (b - a) * mo;
                    a = rd(o0r, g->po[2]); b = rd(o1r, g->po[2]); odr = a + (b - a) * mo;
                    xl = ol + (xl - ol) * w;
                    xr = orr + (xr - orr) * w;
                    dl = odl + (dl - odl) * w;
                    dr = odr + (dr - odr) * w;
                    g->po[0] = wrap(g->po[0] + oinc0);
                    g->po[1] = wrap(g->po[1] + oincl);
                    g->po[2] = wrap(g->po[2] + oincr);
                    if (++g->fade >= g->fade_len) {
                        g->old = g->cur;
                        fading = 0;
                    }
                }
                a = dhp * (g->dh_y[0] + dl - g->dh_x[0]);
                g->dh_x[0] = dl;
                g->dh_y[0] = a;
                b = dhp * (g->dh_y[1] + dr - g->dh_x[1]);
                g->dh_x[1] = dr;
                g->dh_y[1] = b;
                xl = 1.2f * xl + 0.9f * a;
                xr = 1.2f * xr + 0.9f * b;
                x[0] = xl * sc;
                x[1] = xr * sc;
                /* the tones through their low-pass, the noise through its band-pass (both Chamberlin) */
                wc = white(g);                       /* half of it shared, so the sides stay related */
                for (k = 0; k < 2; k++) {
                    float hi = x[k] - g->tl_lo[k] - 0.8f * g->tl_bd[k], nz;
                    g->tl_bd[k] += kt[k] * hi;
                    g->tl_lo[k] += kt[k] * g->tl_bd[k];
                    hi = 0.6f * wc + 0.8f * white(g) - g->nz_lo[k] - qn * g->nz_bd[k];
                    g->nz_bd[k] += kf[k] * hi;
                    g->nz_lo[k] += kf[k] * g->nz_bd[k];
                    nz = g->nz_bd[k] * qn;
                    pt += g->tl_lo[k] * g->tl_lo[k];
                    pn += nz * nz;
                    y[k] = g->tl_lo[k] + nz * gn;
                }
                /* wider: more side, the middle kept; the image leaning */
                mid = 0.5f * (y[0] + y[1]);
                sd = 0.5f * (y[0] - y[1]) * side;
                y[0] = (mid + sd) * (1.0f - bal);
                y[1] = (mid - sd) * (1.0f + bal);
                /* nothing below ~95 Hz: two poles of high-pass */
                for (k = 0; k < 2; k++) {
                    x[k] = y[k];
                    y[k] = hpa * (g->hp_y[k][0] + x[k] - g->hp_x[k][0]);
                    g->hp_x[k][0] = x[k];
                    g->hp_y[k][0] = y[k];
                    x[k] = y[k];
                    y[k] = hpa * (g->hp_y[k][1] + x[k] - g->hp_x[k][1]);
                    g->hp_x[k][1] = x[k];
                    g->hp_y[k][1] = y[k];
                }
                c0 += dp;
                l[done + i] += y[0] * g->amp * c0;
                r[done + i] += y[1] * g->amp * c0;
                g->ph[0] = wrap(g->ph[0] + inc0);
                g->ph[1] = wrap(g->ph[1] + incl);
                g->ph[2] = wrap(g->ph[2] + incr);
            }
            /* the noise's gain: its power to nb of the whole, measured as it plays (~70 ms) */
            g->pt += (pt / (float)m - g->pt) * 0.02f;
            g->pn += (pn / (float)m - g->pn) * 0.02f;
            if (g->pn > 1e-12f && nb > 0.0f)
                gn = fm_expf(0.5f * fm_logf(nb / (1.0f - nb) * g->pt / g->pn));
            else
                gn = 0.0f;
            g->gn += (fm_minf(gn, 40.0f) - g->gn) * 0.05f;
        }
        done += m;
    }
}

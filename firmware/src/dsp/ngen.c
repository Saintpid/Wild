/* SPDX-License-Identifier: GPL-3.0-only */
/* ngen: see ngen.h. */
#include "ngen.h"
#include "fastmath.h"

float nfm_sine(float phase);               /* nfm.c's shared table, phase in cycles */

#define NG_FENCE() __asm__ volatile("" ::: "memory")
#define NG_BLOCK 32                        /* frames per control step */
#define NG_INJECT 0.55f                    /* share of what is left to full a full-strength step adds */
#define NG_FLOOR 0.3f                      /* amplitude at no energy, while the bar's hold lasts */
#define NG_LOW 110.0f                      /* Hz: below this the overtones fall away (kick and 303 own it) */
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
    g->param[NGEN_COMPLEXITY] = 50;
    g->param[NGEN_MOVEMENT] = 40;
    g->param[NGEN_SPREAD] = 60;
    g->param[NGEN_INERTIA] = 40;
    g->param[NGEN_DECAY] = 10;
    g->root = 45;                           /* A2 */
    g->minor = 1;
    g->len = 16;
    g->job = 1;
    g->wr = 1;
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
        g->job = 1;                         /* the material changes: made anew, faded in */
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
    g->job = 1;
}

void ngen_new_sound(ngen *g, uint32_t seed)
{
    g->seed = seed;
    g->job = 1;
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

/* one partial of the set being made (tone it_t, harmonic it_n) into the scratch; 0 when none is left */
static int next_partial(ngen *g)
{
    const uint8_t *tn = tones(g);
    float f0 = fund_hz(g), width = pct(g, NGEN_SPREAD) * (g->state ? 1.0f : 0.45f);
    float odd = rnd01(g->seed, 7u, 1u);
    for (;;) {
        uint32_t h, i, q;
        float f, a, pan, pl, pr, gl, gr, dph;
        if (g->it_t >= ntones(g, g->state))
            return 0;
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
        a = fm_powf((float)g->it_n, g->state ? -1.15f : -2.2f);
        if (g->it_n > 1)
            a *= (g->it_n & 1) ? 0.6f + 0.8f * odd : 1.4f - 0.8f * odd;
        a *= g->it_t == 0 ? 1.0f : g->it_t < NG_CALM ? 0.75f : 0.55f;
        if (f < NG_LOW)
            a *= (f / NG_LOW) * (f / NG_LOW);
        a *= 0.8f + 0.4f * rnd01(g->seed, h, 2u);
        /* stereo: low down one centred core, above it each partial its own place and phase */
        pl = rnd01(g->seed, h, 3u);
        if (f < 220.0f) {
            pan = 0.5f;
            dph = 0.0f;
        } else {
            pan = 0.5f + (rnd01(g->seed, h, 4u) - 0.5f) * width;
            dph = (rnd01(g->seed, h, 5u) - 0.5f) * 0.5f * width;
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
    if (g->job == 1) {                      /* start: a slot neither playing nor fading out */
        uint8_t s;
        for (s = 0; s < NGEN_SLOTS; s++)
            if (s != g->cur && s != g->old)
                break;
        g->wr = s;
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

void ngen_render(ngen *g, float *l, float *r, int n)
{
    int done = 0;
    if (g->ready && (g->old == g->cur || !g->have)) {      /* a new set: fade it in */
        NG_FENCE();
        g->old = g->have ? g->cur : g->wr;
        g->cur = g->wr;
        g->fade = 0;
        g->fade_len = g->step_len ? 4u * g->step_len : (uint32_t)(0.5f * g->sr);
        g->have = 1;
        NG_FENCE();
        g->ready = 0;
    }
    if (!g->have)
        return;
    while (done < n) {
        int m = n - done > NG_BLOCK ? NG_BLOCK : n - done, i;
        uint32_t bar = 16u * (g->step_len ? g->step_len : (uint32_t)(0.125f * g->sr));
        float target, mt, k_amp, k_m, inertia, det, c0, c1, p0, p1, dp, depth, a_step;
        const int16_t *n0l = slot_at(g, g->cur, 0, 0), *n0r = slot_at(g, g->cur, 0, 1);
        const int16_t *n1l = slot_at(g, g->cur, 1, 0), *n1r = slot_at(g, g->cur, 1, 1);
        const int16_t *o0l = slot_at(g, g->old, 0, 0), *o0r = slot_at(g, g->old, 0, 1);
        const int16_t *o1l = slot_at(g, g->old, 1, 0), *o1r = slot_at(g, g->old, 1, 1);
        int fading = g->old != g->cur;
        /* energy: time takes it away; after a bar without steps the hold lets go */
        g->e *= fm_expf(-3.0f * (float)m / ((float)bar * decay_bars(g)));
        g->since += (uint32_t)m;
        if (g->since >= bar) {
            g->gate *= fm_expf(-4.0f * (float)m / (float)bar);
            if (g->gate < 1e-5f) {
                g->gate = 0.0f;
                g->live = 0;
            }
        }
        target = g->gate * (NG_FLOOR + (1.0f - NG_FLOOR) * g->e * g->e);
        /* timbre: the energy, leaning towards what the plan says is coming, followed with inertia */
        mt = g->e;
        if (g->live && g->plan[g->pos] > mt)
            mt += 0.6f * (g->plan[g->pos] - mt);
        inertia = (0.25f + 3.75f * pct(g, NGEN_INERTIA)) * (float)(g->step_len ? g->step_len : 5512u);
        k_m = 1.0f - fm_expf(-(float)m / inertia);
        g->morph += (mt - g->morph) * k_m;
        k_amp = 1.0f - fm_expf(-1.0f / (0.012f * g->sr));
        /* a pulse on the sixteenths, deeper with energy; raised cosine, so a step's edge does not click */
        depth = pct(g, NGEN_MOVEMENT) * g->morph * 0.7f;
        a_step = g->step_len ? 1.0f / (float)g->step_len : 0.0f;
        p0 = (float)g->step_t * a_step;
        p1 = (float)(g->step_t + (uint32_t)m) * a_step;
        if (p1 > 1.0f)
            p1 = 1.0f;
        c0 = 1.0f - depth * 0.5f * (1.0f - nfm_sine(p0 + 0.25f));
        c1 = 1.0f - depth * 0.5f * (1.0f - nfm_sine(p1 + 0.25f));
        dp = (c1 - c0) / (float)m;
        g->step_t += (uint32_t)m;
        /* the readers: one shared, one each side detuned apart (more with energy) */
        g->inc = fund_hz(g) * (float)NGEN_LEN / g->sr;
        det = pct(g, NGEN_SPREAD) * 0.0035f * (0.4f + 0.6f * g->morph);
        {
            float lpk = 0.06f + 0.92f * g->morph * g->morph, mo = g->morph;
            float inc0 = g->inc, incl = g->inc * (1.0f + det), incr = g->inc * (1.0f - det);
            float sc = 1.0f / 65534.0f;
            for (i = 0; i < m; i++) {
                float xl, xr, w = 1.0f, a, b;
                g->amp += (target - g->amp) * k_amp;
                a = rd(n0l, g->ph[0]) + rd(n0l, g->ph[1]);
                b = rd(n1l, g->ph[0]) + rd(n1l, g->ph[1]);
                xl = a + (b - a) * mo;
                a = rd(n0r, g->ph[0]) + rd(n0r, g->ph[2]);
                b = rd(n1r, g->ph[0]) + rd(n1r, g->ph[2]);
                xr = a + (b - a) * mo;
                if (fading) {
                    float ol, orr;
                    w = (float)g->fade / (float)g->fade_len;
                    a = rd(o0l, g->ph[0]) + rd(o0l, g->ph[1]);
                    b = rd(o1l, g->ph[0]) + rd(o1l, g->ph[1]);
                    ol = a + (b - a) * mo;
                    a = rd(o0r, g->ph[0]) + rd(o0r, g->ph[2]);
                    b = rd(o1r, g->ph[0]) + rd(o1r, g->ph[2]);
                    orr = a + (b - a) * mo;
                    xl = ol + (xl - ol) * w;
                    xr = orr + (xr - orr) * w;
                    if (++g->fade >= g->fade_len) {
                        g->old = g->cur;
                        fading = 0;
                    }
                }
                g->lp[0] += (xl - g->lp[0]) * lpk;
                g->lp[1] += (xr - g->lp[1]) * lpk;
                c0 += dp;
                l[done + i] += g->lp[0] * sc * g->amp * c0;
                r[done + i] += g->lp[1] * sc * g->amp * c0;
                g->ph[0] = wrap(g->ph[0] + inc0);
                g->ph[1] = wrap(g->ph[1] + incl);
                g->ph[2] = wrap(g->ph[2] + incr);
            }
        }
        done += m;
    }
}

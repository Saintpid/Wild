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
#define NG_NTONES 9                        /* a chord's tones at most */

/* The seed's family: what its partials are like. Weighted: the bells are the rarest. */
enum { FAM_SAW, FAM_SQUARE, FAM_ORGAN, FAM_VOWEL, FAM_SOFT, FAM_BUZZ, FAM_GLASS, NG_FAMS };
static const uint8_t FAM_W[NG_FAMS] = {3, 2, 2, 3, 2, 2, 1};

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static float rnd01(uint32_t seed, uint32_t a, uint32_t b) { return (float)(hash32(seed ^ hash32(a * 0x9e3779b9u + b)) >> 8) * (1.0f / 16777216.0f); }

static float pct(const ngen *g, int p) { return (float)g->param[p] * 0.01f; }
static float note_hz(int note) { return 440.0f * fm_exp2f(((float)note - 69.0f) * (1.0f / 12.0f)); }
static int family(const ngen *g)
{
    uint32_t w = (uint32_t)(rnd01(g->seed, 2u, 82u) * 15.0f), f;
    for (f = 0; f < NG_FAMS - 1u && w >= FAM_W[f]; f++)
        w -= FAM_W[f];
    return (int)f;
}
/* a third of the seeds an octave up, airy */
static float reg(const ngen *g) { return rnd01(g->seed, 3u, 83u) < 0.3f ? 2.0f : 1.0f; }

/* THE CHORDS. Each state its own, in the scale: the calm one mostly on the key's root, the intense one
 * on the same chord or a related one (a fourth or a fifth away likeliest, then a third away). The seed
 * picks them and how many notes: one (in octaves), a fifth, a triad, a seventh, a ninth; Complexity how
 * far the intense one grows past the calm one. A degree needs its fifth in the scale; its third, seventh
 * and ninth are the scale's. Each chord in just intonation over its own fundamental (its root / den), so
 * one period holds it; the states are read at their own rates, so a morph between two chords is a
 * crossfade, never a click. */
static int in_scale(unsigned m, int s) { return (int)(m >> (unsigned)((s % 12 + 12) % 12) & 1u); }
static int degrees(unsigned m, int8_t *d)
{
    int s, n = 0;
    for (s = 0; s < 12; s++)
        if (in_scale(m, s) && in_scale(m, s + 7))
            d[n++] = (int8_t)s;
    if (!n)
        d[n++] = 0;
    return n;
}
/* the harmony now: at home (hs 0) the DNA's chords; away, others drawn from hs */
static uint32_t hseed(const ngen *g) { return g->hs ? g->seed ^ g->hs : g->seed; }
static int chord_size(const ngen *g, int state)
{
    uint32_t sd = hseed(g);
    float a = rnd01(sd, 10u, 1u), b = rnd01(sd, 10u, 2u);
    int c = a < 0.15f ? 1 : a < 0.3f ? 2 : a < 0.75f ? 3 : a < 0.95f ? 4 : 5;
    int x = b < 0.3f ? 0 : b < 0.7f ? 1 : 2;
    if (state)
        c += (int)((float)x * pct(g, NGEN_COMPLEXITY) + 0.5f);
    return c > 5 ? 5 : c;
}
/* a degree near c in harmony: the fourth and fifth most, the thirds and sixths, now and then the rest */
static int related(const int8_t *d, int n, int c, float r)
{
    int i, w, tot = 0;
    for (i = 0; i < n; i++) {
        int k = (d[i] - c + 12) % 12;
        tot += !k ? 0 : (k == 5 || k == 7) ? 3 : (k == 3 || k == 4 || k == 8 || k == 9) ? 2 : 1;
    }
    if (!tot)
        return c;
    w = (int)(r * (float)tot);
    for (i = 0; i < n; i++) {
        int k = (d[i] - c + 12) % 12, wk = !k ? 0 : (k == 5 || k == 7) ? 3 : (k == 3 || k == 4 || k == 8 || k == 9) ? 2 : 1;
        if (w < wk)
            return d[i];
        w -= wk;
    }
    return c;
}
static int chord_degree(const ngen *g, int state)
{
    int8_t d[12];
    int n = degrees(g->mask, d), c = d[0];
    uint32_t sd = hseed(g);
    if (rnd01(g->seed, 10u, 3u) >= 0.7f || d[0] != 0)
        c = d[(int)(rnd01(g->seed, 10u, 4u) * (float)n) % n];
    if (g->hs)                               /* away: a chord related to its own */
        c = related(d, n, c, rnd01(sd, 10u, 8u));
    if (!state || rnd01(sd, 10u, 5u) < 0.4f)
        return c;
    return related(d, n, c, rnd01(sd, 10u, 6u));
}
static unsigned gcd_(unsigned a, unsigned b)
{
    while (b) {
        unsigned t = a % b;
        a = b;
        b = t;
    }
    return a;
}
/* a state's chord: its fundamental and its tones as multiples of it (root first, then what adds) */
static void chord(const ngen *g, int state, float *f0, uint8_t *q, uint8_t *nq)
{
    uint8_t num[NG_NTONES], den[NG_NTONES];
    unsigned L = 1, i;
    int d = chord_degree(g, state), s = chord_size(g, state), n = 0, third = 0, sev = 0;
    unsigned m = g->mask;
    if (in_scale(m, d + 3)) third = 1;               /* minor */
    else if (in_scale(m, d + 4)) third = 2;          /* major */
    if (in_scale(m, d + 10)) sev = third == 2 ? 2 : 1;   /* (on a major chord the harmonic seventh) */
    else if (in_scale(m, d + 11)) sev = 3;
#define ADD(a, b) do { num[n] = (a); den[n] = (b); n++; } while (0)
    if (!(s >= 3 && third && rnd01(hseed(g), 10u, 7u + (uint32_t)state) < 0.15f))
        ADD(1, 1);                                   /* (now and then without its root) */
    if (s >= 2)
        ADD(3, 2);
    if (s >= 3 && third == 1)
        ADD(6, 5);
    else if (s >= 3 && third == 2)
        ADD(5, 4);
    ADD(2, 1);
    if (s >= 4 && sev == 1)
        ADD(9, 5);
    else if (s >= 4 && sev == 2)
        ADD(7, 4);
    else if (s >= 4 && sev == 3)
        ADD(15, 8);
    if (s >= 5 && in_scale(m, d + 2))
        ADD(9, 4);
    if (s >= 2)
        ADD(3, 1);
    else
        ADD(4, 1);
#undef ADD
    for (i = 0; i < (unsigned)n; i++)
        L = L / gcd_(L, den[i]) * den[i];
    for (i = 0; i < (unsigned)n; i++)
        q[i] = (uint8_t)(num[i] * (L / den[i]));
    *nq = (uint8_t)n;
    if (d > 6)
        d -= 12;
    *f0 = note_hz(g->root + d) * reg(g) / (float)L;
}
/* a third of the seeds are dull sounds, the low partials leading and the filters lower: 0 bright .. 1 dull */
static float dull(const ngen *g)
{
    float d = rnd01(g->seed, 9u, 61u);
    return d < 0.34f ? 1.0f - d * (1.0f / 0.34f) : 0.0f;
}
static int nharm(const ngen *g, int state)
{
    int f = family(g), n = state ? 4 + (int)(pct(g, NGEN_COMPLEXITY) * 20.0f) : 3;
    if (f == FAM_SOFT)
        return state ? 3 + n / 4 : 2;
    if (f == FAM_ORGAN)
        return state ? 16 : 8;
    if (f == FAM_SAW || f == FAM_BUZZ || f == FAM_VOWEL)
        return state ? n + 8 : 8;
    return n;
}
/* a harmonic's level (n from 1) in the seed's family; the vowel's two formants by its seed */
static float fam_amp(const ngen *g, uint32_t n, float f, int state)
{
    float x = (float)n;
    switch (family(g)) {
    case FAM_SAW: return fm_powf(x, state ? -0.9f : -1.5f);
    case FAM_SQUARE: return (n & 1u) ? fm_powf(x, state ? -0.9f : -1.4f) : 0.03f / x;
    case FAM_ORGAN: {                        /* drawbars: 1 2 3 4 6 8, the intense one 5 10 12 16 too */
        static const uint8_t BAR[16] = {1, 1, 1, 1, 2, 1, 0, 1, 0, 2, 0, 2, 0, 0, 0, 2};
        uint32_t b = n <= 16u ? BAR[n - 1u] : 0u;
        if (!b || (b == 2u && !state))
            return 0.0f;
        return 0.2f + 0.8f * rnd01(g->seed, n, 71u);
    }
    case FAM_VOWEL: {
        static const float F1[4] = {700.0f, 450.0f, 400.0f, 300.0f}, F2[4] = {1200.0f, 800.0f, 2000.0f, 2300.0f};
        uint32_t v = (uint32_t)(rnd01(g->seed, 4u, 84u) * 4.0f) & 3u;
        float d1 = (f - F1[v]) * (1.0f / 180.0f), d2 = (f - F2[v]) * (1.0f / 300.0f);
        return fm_powf(x, -0.7f) * (0.06f + fm_expf(-d1 * d1) + 0.7f * fm_expf(-d2 * d2)) * (state ? 1.0f : 0.8f);
    }
    case FAM_SOFT: return fm_powf(x, state ? -1.6f : -2.6f);
    case FAM_BUZZ: return fm_powf(x, state ? -0.55f : -1.0f) * ((n & 1u) ? 1.0f : 0.6f);
    default: return fm_powf(x, state ? -0.95f : -1.8f);   /* the glass: as the first ones were */
    }
}

static int16_t *slot_at(const ngen *g, int slot, int state, int ch)
{
    return g->mem + (uint32_t)slot * NGEN_SLOT_WORDS + ((uint32_t)state * 2u + (uint32_t)ch) * NGEN_LEN;
}

static void character(const ngen *g, float *c);

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
    g->param[NGEN_NOISE] = 50;
    g->param[NGEN_SPREAD] = 75;
    g->param[NGEN_INERTIA] = 40;
    g->param[NGEN_DECAY] = 10;
    g->param[NGEN_DEVIANCE] = 40;
    g->param[NGEN_CHORDS] = NGEN_STAB;
    g->sb = 0xFF;
    g->root = 45;                           /* A2 */
    g->mask = NGEN_MINOR;
    g->len = 16;
    g->job = 1;
    g->wr = 1;
    g->rng = seed | 1u;
    g->k_sp = pct(g, NGEN_SPREAD);
    g->k_mov = pct(g, NGEN_MOVEMENT);
    g->k_nz = pct(g, NGEN_NOISE);
    g->nz_c[0] = g->nz_c[1] = g->nz_t[0] = g->nz_t[1] = 1000.0f;
    {
        int i, k;
        for (i = 0; i < (int)NGEN_SLOTS; i++)
            character(g, g->chr[i]);
        for (k = 0; k < NGEN_CHARS; k++)
            g->ch[k] = g->chr[0][k];
    }
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

void ngen_set_key(ngen *g, int root_note, unsigned scale)
{
    while (root_note < 33)                  /* the key's root, kept in A1..G#2: the chords sit above it */
        root_note += 12;
    while (root_note > 44)
        root_note -= 12;
    root_note += 12;
    scale &= 0xFFFu;
    if (!(scale & 1u))
        scale = NGEN_MINOR;
    if (g->root == root_note && g->mask == scale)
        return;
    g->root = (int8_t)root_note;
    g->mask = (uint16_t)scale;
    g->req++;
}

void ngen_new_sound(ngen *g, uint32_t seed)
{
    int k;
    if (g->seed == seed)
        return;
    g->seed = seed;
    for (k = 0; k < 3; k++)                 /* (a new sound starts where its DNA says, not where one wandered) */
        g->wm[k] = 0.0f;
    g->hs = 0;
    g->req++;
}

/* DEVIANCE, at each played step. Below 90 the sound as it plays (its filter's cutoff and peak, vibrato,
 * pulse, the noise's share, its place, its brightness, its detune) takes new values around its own, as
 * far as Deviance; from 90 each step goes on from where the last left off, never back, further the
 * nearer 100, and at each bar its material wanders too (the slope of its partials, odd against even,
 * how fast they fall), made anew; below 90 again it stays where it went, until a new sound. */
static void deviance(ngen *g)
{
    static const float R[NGEN_DEVS] = {2.0f, 0.35f, 0.8f, 0.5f, 0.6f, 0.3f, 0.4f, 0.6f};
    int d = g->param[NGEN_DEVIANCE], k;
    float w = 0.1f + 0.05f * (float)(d - 90);
    uint32_t h = 0x64657621u ^ (++g->dn * 0x9e3779b9u);   /* (its own: a new sound does not jump it) */
    for (k = 0; k < NGEN_DEVS; k++) {
        float r = 2.0f * rnd01(h, (uint32_t)k, 101u) - 1.0f;
        g->dt[k] = d < 90 ? (float)d * (1.0f / 89.0f) * R[k] * r : fm_clampf(g->dt[k] + w * R[k] * r, -2.0f * R[k], 2.0f * R[k]);
    }
    if (d >= 90 && g->pos % 16u == 0u) {
        for (k = 0; k < 3; k++)
            g->wm[k] = fm_clampf(g->wm[k] + 0.5f * w * (2.0f * rnd01(h, (uint32_t)k, 102u) - 1.0f), -1.0f, 1.0f);
        g->req++;
    }
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
    /* its stab steps (Chords: Stab): the played ones off the beat that stand out, an offbeat sixteenth
     * most, then an offbeat eighth, the louder, near a bar's end the more; never a bar's one. One, two
     * in a longer loop: a riff that comes round */
    {
        float best[2] = {0.0f, 0.0f};
        g->ns = 0;
        for (i = 0; i < len; i++) {
            float sc;
            if (!g->vel[i] || i % 16 == 0)
                continue;
            sc = (i % 2 ? 2.0f : i % 4 == 2 ? 1.5f : 0.5f) + (i % 16 >= 10 ? 1.0f : 0.0f) +
                 (float)g->vel[i] * (1.0f / 127.0f) + 0.8f * rnd01(g->seed, (uint32_t)i, 77u);
            if (sc > best[0]) {
                best[1] = best[0];
                g->sp[1] = g->sp[0];
                best[0] = sc;
                g->sp[0] = (uint8_t)i;
            } else if (sc > best[1]) {
                best[1] = sc;
                g->sp[1] = (uint8_t)i;
            }
        }
        g->ns = (uint8_t)(best[1] > 0.0f && len > 16 ? 2 : best[0] > 0.0f ? 1 : 0);
    }
}

/* CHORDS at each step (v: its strength). Stab: on its stab steps (most times round, more with Movement;
 * now and then another offbeat) the other chord for a sixteenth, an eighth if the next step is empty,
 * then home. Dub: the two traded on the played steps, by chance; a bar's one is home. */
static void chords(ngen *g, float v)
{
    int mode = g->param[NGEN_CHORDS], k;
    uint32_t h = 0x73746221u ^ (++g->sn * 0x9e3779b9u);
    float mv = pct(g, NGEN_MOVEMENT);
    if (mode != NGEN_POP && g->hs) {         /* (out of pop: its own chords again) */
        g->hs = 0;
        g->req++;
    }
    if ((g->away && --g->away == 0) || g->pos % 16u == 0u)
        g->want = 0;                         /* (a bar's one is home) */
    if (mode == NGEN_STAB && v > 0.0f) {
        int hit = 0;
        for (k = 0; k < g->ns; k++)
            if (g->pos == g->sp[k] && rnd01(h, 2u, 1u) < 0.55f + 0.4f * mv)
                hit = 1;
        if (!hit && g->pos % 2u && rnd01(h, 2u, 2u) < 0.05f * mv)
            hit = 1;
        if (hit) {
            g->want = 1;
            g->away = (uint8_t)(g->vel[(g->pos + 1u) % g->len] ? 1 : 2);
        }
    } else if (mode == NGEN_DUB && v > 0.0f) {
        g->away = 0;
        if (g->pos % 16u == 0u)
            g->want = 0;
        else if (rnd01(h, 3u, 1u) < 0.25f + 0.35f * mv)
            g->want = (uint8_t)!g->want;
    } else if (mode != NGEN_STAB && mode != NGEN_DUB) {
        g->away = 0;
        g->want = 0;
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
    chords(g, v);
    if (g->param[NGEN_CHORDS] == NGEN_POP && g->live && g->pos % 16u == 0u) {   /* a bar: now and then another chord, home again */
        uint32_t h = 0x68726d21u ^ (++g->hn * 0x9e3779b9u);
        float mv = pct(g, NGEN_MOVEMENT);
        if (rnd01(h, 1u, 1u) < (g->hs ? 0.35f + 0.25f * mv : 0.1f + 0.25f * mv)) {
            g->hs = g->hs && rnd01(h, 1u, 2u) < 0.7f ? 0u : (hash32(h) | 1u);   /* (away: mostly home again) */
            g->req++;
        }
    }
    if (v > 0.0f) {
        g->e += NG_INJECT * v * (1.0f - g->e);
        if (v > g->bloom)
            g->bloom = v;
        g->stab = v;
        deviance(g);
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

/* let go as if a bar had passed without steps: the hold and the energy fall away, what rings rings out */
void ngen_release(ngen *g)
{
    uint32_t bar = 16u * (g->step_len ? g->step_len : (uint32_t)(0.125f * g->sr));
    if (g->since < bar)
        g->since = bar;
}

int ngen_active(const ngen *g) { return g->live || g->amp > 1e-4f; }

/* --------------------------------------------------------------- the material --- */

/* inharmonic partials: on the period's grid (any whole multiple of the fundamental), not on the chord's;
 * only the glass family has them (they ring like bells) */
static int ninharm(const ngen *g, int state)
{
    if (family(g) != FAM_GLASS)
        return 0;
    return state ? 8 + (int)(pct(g, NGEN_COMPLEXITY) * 40.0f) : 4;
}

/* one partial of the set being made (tone it_t, harmonic it_n; after the tones, the inharmonic ones) into
 * the scratch; 0 when none is left */
static int next_partial(ngen *g)
{
    const uint8_t *tn = g->jq[g->state];
    float f0 = g->jf0[g->state], width = pct(g, NGEN_SPREAD) * (g->state ? 1.0f : 0.7f);
    float odd = fm_clampf(rnd01(g->seed, 7u, 1u) + 0.5f * g->wm[1], 0.0f, 1.0f);
    int nt = g->jn[g->state];
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
            /* level: by the family, calm overtones falling faster; odd or even ones favoured by the seed */
            a = fam_amp(g, g->it_n, f, g->state) * fm_powf((float)g->it_n, 0.5f * g->wm[2]);
            if (a <= 0.0f)
                continue;
            if (g->it_n > 1)
                a *= (g->it_n & 1) ? 0.8f + 0.4f * odd : 1.2f - 0.4f * odd;
            a *= g->it_t == 0 ? 1.0f : g->it_t < 3 ? 0.75f : 0.55f;
        }
        if (f < NG_CUT)
            continue;
        if (f < NG_LOW)
            a *= (f - NG_CUT) / (NG_LOW - NG_CUT);
        a *= 0.6f + 0.8f * rnd01(g->seed, h, 2u);
        /* the tilt: calm leans to the low partials, intense to the high ones */
        a *= fm_powf(f * (1.0f / 400.0f), (g->state ? 0.25f - 0.75f * dull(g) : -0.4f - 0.4f * dull(g)) + 0.4f * g->wm[0]);
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

/* the seed's character as it plays: the filter's damping (low: a resonant peak), its cutoff's scale, the
 * vibrato's and the pulse's depth */
static void character(const ngen *g, float *c)
{
    int f = family(g);
    c[0] = 0.45f + 0.95f * rnd01(g->seed, 6u, 91u);
    c[1] = fm_exp2f(-0.7f + 1.6f * rnd01(g->seed, 6u, 92u));
    c[2] = 0.2f + 1.6f * rnd01(g->seed, 6u, 93u);
    c[3] = 0.2f + 1.1f * rnd01(g->seed, 6u, 94u);
    if (f == FAM_GLASS || f == FAM_SOFT) {   /* (pure partials: no peak, little vibrato) */
        c[0] = 1.2f;
        c[2] *= 0.3f;
    }
    if (f == FAM_SOFT)
        c[1] *= 0.6f;
    /* how each step is played: held (a drone, a pad), half, or struck (a stab that dies within the step
     * or two, a floor kept under it) */
    {
        float a = rnd01(g->seed, 6u, 95u);
        c[4] = a < 0.4f ? 0.0f : a < 0.7f ? 0.5f : 0.9f;
        c[5] = 0.5f + 2.0f * rnd01(g->seed, 6u, 96u);   /* ...its stab's length, in steps */
    }
}

int ngen_work(ngen *g)
{
    if (g->ready)
        return 0;                           /* the last one has not been taken yet */
    if (g->req != g->req_done) {            /* asked for (again): start over */
        g->req_done = g->req;
        g->job = 1;
        g->jsb = 0;
        g->sbok = 0;                        /* (the other chord goes with its home: made again after) */
    } else if (!g->job && g->have && !g->sbok && g->old == g->cur && !g->in_sb &&
               (g->param[NGEN_CHORDS] == NGEN_STAB || g->param[NGEN_CHORDS] == NGEN_DUB)) {
        g->job = 1;                         /* the other chord, to stab or trade with */
        g->jsb = 1;
    }
    if (g->job == 1) {                      /* start: a slot neither playing nor fading out (nor the other chord) */
        uint8_t s;
        uint32_t hs = g->hs;
        for (s = 0; s < NGEN_SLOTS; s++)
            if (s != g->cur && s != g->old && (s != g->sb || !g->sbok) && (s != g->home || !g->in_sb))
                break;
        if (s >= NGEN_SLOTS) {              /* (all three in use: the other chord gives way) */
            if (g->jsb) {
                g->job = 0;
                return 0;
            }
            g->sbok = 0;
            s = g->sb;
        }
        g->wr = s;
        if (g->jsb) {                       /* a chord near home: the fourth, the fifth most */
            g->sbok = 0;
            g->hs = (hash32(g->seed ^ (g->made * 0x9e3779b9u) ^ hs) | 1u);
        }
        chord(g, 0, &g->jf0[0], g->jq[0], &g->jn[0]);
        chord(g, 1, &g->jf0[1], g->jq[1], &g->jn[1]);
        g->hs = hs;
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
    g->f0[g->wr][0] = g->jf0[0];
    g->f0[g->wr][1] = g->jf0[1];
    {   /* (the noise's resonances: on the intense chord's partials) */
        int i;
        for (i = 0; i < NG_NTONES; i++)
            g->nzq[g->wr][i] = g->jq[1][i];
        g->nzn[g->wr] = g->jn[1];
    }
    g->dk[g->wr] = dull(g);
    character(g, g->chr[g->wr]);
    g->made++;
    NG_FENCE();
    if (g->jsb) {                           /* the other chord: kept aside, ready */
        g->sb = g->wr;
        g->sbok = 1;
        g->jsb = 0;
        return 1;
    }
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
    const uint8_t *tn = g->nzq[g->cur];
    uint32_t k = g->nz_next * 2u + (uint32_t)side, n = g->nzn[g->cur] ? g->nzn[g->cur] : 1u;
    float f = g->f0[g->cur][1] * (float)tn[(uint32_t)(rnd01(g->seed, k, 21u) * (float)n) % n] *
              (float)(2u + (uint32_t)(rnd01(g->seed, k, 22u) * 8.0f));
    if (f < 1.0f)                                /* (no set yet) */
        return 1000.0f;
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
        int j;
        for (j = 0; j < NGEN_STATES * 3; j++)    /* the old set reads on where it was */
            g->po[j / 3][j % 3] = g->ph[j / 3][j % 3];
        g->fade = 0;
        g->fade_len = g->step_len ? 4u * g->step_len : (uint32_t)(0.5f * g->sr);
        g->have = 1;
        g->home = g->cur;
        g->in_sb = 0;
        NG_FENCE();
        g->ready = 0;
    } else if (g->have && g->old == g->cur && g->want != g->in_sb && (g->sbok || g->in_sb)) {
        int j;                                         /* to the other chord and back: ~5 ms */
        g->old = g->cur;
        g->cur = g->want ? g->sb : g->home;
        g->in_sb = g->want;
        for (j = 0; j < NGEN_STATES * 3; j++)
            g->po[j / 3][j % 3] = g->ph[j / 3][j % 3];
        g->fade = 0;
        g->fade_len = (uint32_t)(0.005f * g->sr);
    }
    if (!g->have)
        return;
    while (done < n) {
        int m = n - done > NG_BLOCK ? NG_BLOCK : n - done, i, k;
        uint32_t bar = 16u * (g->step_len ? g->step_len : (uint32_t)(0.125f * g->sr));
        float kk = 1.0f - fm_expf(-(float)m / (0.05f * g->sr)), mov, spread;
        float target, mt, k_amp, k_m, inertia, det, c0, c1, p0, p1, dp, depth, a_step;
        float kc, damp, nb, nb_t, mo, dl, kt[2], vb, kf[2], qn, side, bal, hpa, dhp;
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
        /* the playing set's character (how dull, its filter, vibrato, pulse), gliding over a quarter as a
         * new one comes in */
        kc = 1.0f - fm_expf(-(float)m / (4.0f * (float)(g->step_len ? g->step_len : 5512u)));
        g->dl += (g->dk[g->cur] - g->dl) * kc;
        for (k = 0; k < NGEN_CHARS; k++)
            g->ch[k] += (g->chr[g->cur][k] - g->ch[k]) * kc;
        kc = 1.0f - fm_expf(-(float)m / (0.5f * (float)(g->step_len ? g->step_len : 5512u)));
        for (k = 0; k < NGEN_DEVS; k++)              /* Deviance's, over half a step */
            g->dv[k] += (g->dt[k] - g->dv[k]) * kc;
        /* energy: time takes it away; after a bar without steps the hold lets go */
        g->e *= fm_expf(-3.0f * (float)m / ((float)bar * decay_bars(g)));
        g->bloom *= fm_expf(-(float)m / (1.5f * (float)(g->step_len ? g->step_len : 5512u)));
        g->stab *= fm_expf(-(float)m / (g->ch[5] * (float)(g->step_len ? g->step_len : 5512u)));
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
        target = g->gate * (NG_FLOOR + (1.0f - NG_FLOOR) * g->e * g->e) * (1.0f + 0.35f * mov * g->bloom) *
                 (1.0f - g->ch[4] + g->ch[4] * (0.15f + 1.1f * g->stab));
        /* timbre: the energy, leaning towards what the plan says is coming, followed with inertia */
        mt = g->e;
        if (g->live && g->plan[g->pos] > mt)
            mt += 0.6f * (g->plan[g->pos] - mt);
        inertia = (0.25f + 3.75f * pct(g, NGEN_INERTIA)) * (float)(g->step_len ? g->step_len : 5512u);
        k_m = 1.0f - fm_expf(-(float)m / inertia);
        g->morph += (mt - g->morph) * k_m;
        mo = fm_clampf(g->morph + 0.25f * mov * drift(g, 0) + 0.4f * mov * g->bloom + g->dv[6], 0.0f, 1.0f);
        k_amp = 1.0f - fm_expf(-1.0f / (0.012f * g->sr));
        /* the share of noise: a tenth of Noise cubed (100: 10 %, 50: 1.25 %, 20: 0.08 %: the low end very quiet), moved by its drift, a little up with energy and each step */
        nb_t = 0.1f * g->k_nz * g->k_nz * g->k_nz * fm_clampf(1.0f + 0.5f * drift(g, 1) + 0.3f * (g->bloom + g->morph) - 0.2f, 0.0f, 2.0f);
        nb = g->param[NGEN_NOISE] ? fm_minf(nb_t * fm_maxf(1.0f + g->dv[4], 0.0f), 0.3f) : 0.0f;    /* the noise's share of the power; Noise 0: none */
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
        bal = fm_clampf(0.3f * mov * drift(g, 2) + g->dv[5], -0.6f, 0.6f);   /* the image leans slowly from side to side */
        hpa = 1.0f / (1.0f + FM_TWO_PI * NG_CUT / g->sr);
        dhp = 1.0f / (1.0f + FM_TWO_PI * 350.0f / g->sr);
        /* a pulse on the sixteenths, deeper with energy; raised cosine, so a step's edge does not click */
        depth = fm_minf(mov * g->morph * 0.6f * g->ch[3] * fm_maxf(1.0f + g->dv[3], 0.0f), 0.9f);
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
        det = spread * 0.005f * (0.4f + 0.6f * mo) * (1.0f + 0.3f * drift(g, 3)) * fm_maxf(1.0f + g->dv[7], 0.0f);
        /* the tones' low-pass per side: up with energy, each side's cutoff drifting its own way */
        dl = g->dl;
        for (k = 0; k < 2; k++) {
            float dr = nfm_sine(g->lfo[3] + 0.37f * (float)k + 0.11f * rnd01(g->seed, (uint32_t)k, 41u));
            float fc = fm_minf(fm_minf(350.0f * g->ch[1] * fm_exp2f(4.3f * mo * (1.0f - 0.45f * dl) + 1.1f * mov * dr - 0.8f * dl), 7000.0f) * fm_exp2f(g->dv[0]), 7000.0f);   /* (Deviance's after the ceiling: it moves an open filter too) */
            kt[k] = 2.0f * nfm_sine(fc / (2.0f * g->sr));
        }
        /* vibrato: a few cents, the core one way and the detuned readers apart, 4-6.5 Hz from the seed */
        g->vib += (float)m * (4.0f + 2.5f * rnd01(g->seed, 5u, 51u)) / g->sr;
        if (g->vib >= 1.0f)
            g->vib -= 1.0f;
        vb = nfm_sine(g->vib) * mov * (0.6f + 0.4f * drift(g, 0)) * g->ch[2] * fm_maxf(1.0f + g->dv[2], 0.0f);
        damp = fm_clampf(g->ch[0] + g->dv[1], 0.45f, 1.6f);
        {
            /* each state its own chord, its own rate: the shared reader and the two detuned apart */
            float inc[NGEN_STATES][3], oinc[NGEN_STATES][3];
            for (k = 0; k < NGEN_STATES; k++) {
                float b0 = g->f0[g->cur][k] * (float)NGEN_LEN / g->sr * (1.0f + 0.0016f * vb);
                float o0 = g->f0[g->old][k] * (float)NGEN_LEN / g->sr * (1.0f + 0.0016f * vb);
                inc[k][0] = b0;
                inc[k][1] = b0 * (1.0f + det + 0.003f * vb);
                inc[k][2] = b0 * (1.0f - det - 0.003f * vb);
                oinc[k][0] = o0;
                oinc[k][1] = o0 * (1.0f + det + 0.003f * vb);
                oinc[k][2] = o0 * (1.0f - det - 0.003f * vb);
            }
            float sc = 1.0f / 65534.0f, pt = 0.0f, pn = 0.0f, gn = g->gn;
            for (i = 0; i < m; i++) {
                float xl, xr, dl, dr, w, a, b, mid, sd, y[2], x[2], wc;
                g->amp += (target - g->amp) * k_amp;
                /* each side: the shared reader (the core) and its own detuned one, whose lows are taken
                 * away, so below a few hundred Hz both sides are the same */
                a = rd(n0l, g->ph[0][0]); b = rd(n1l, g->ph[1][0]); xl = a + (b - a) * mo;
                a = rd(n0r, g->ph[0][0]); b = rd(n1r, g->ph[1][0]); xr = a + (b - a) * mo;
                a = rd(n0l, g->ph[0][1]); b = rd(n1l, g->ph[1][1]); dl = a + (b - a) * mo;
                a = rd(n0r, g->ph[0][2]); b = rd(n1r, g->ph[1][2]); dr = a + (b - a) * mo;
                if (fading) {
                    float ol, orr, odl, odr;
                    w = (float)g->fade / (float)g->fade_len;
                    a = rd(o0l, g->po[0][0]); b = rd(o1l, g->po[1][0]); ol = a + (b - a) * mo;
                    a = rd(o0r, g->po[0][0]); b = rd(o1r, g->po[1][0]); orr = a + (b - a) * mo;
                    a = rd(o0l, g->po[0][1]); b = rd(o1l, g->po[1][1]); odl = a + (b - a) * mo;
                    a = rd(o0r, g->po[0][2]); b = rd(o1r, g->po[1][2]); odr = a + (b - a) * mo;
                    xl = ol + (xl - ol) * w;
                    xr = orr + (xr - orr) * w;
                    dl = odl + (dl - odl) * w;
                    dr = odr + (dr - odr) * w;
                    for (k = 0; k < NGEN_STATES * 3; k++)
                        g->po[k / 3][k % 3] = wrap(g->po[k / 3][k % 3] + oinc[k / 3][k % 3]);
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
                    float hi = x[k] - g->tl_lo[k] - damp * g->tl_bd[k], nz;
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
                for (k = 0; k < NGEN_STATES * 3; k++)
                    g->ph[k / 3][k % 3] = wrap(g->ph[k / 3][k % 3] + inc[k / 3][k % 3]);
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

/* nfm: percussive FM engine, see nfm.h. */
#include "nfm.h"

/* ---- maths without libm ------------------------------------------------ */

#define NFM_SINE_BITS 10
#define NFM_SINE_SIZE (1 << NFM_SINE_BITS)
/* A table rather than code that builds one: on the FM1 the audio code may
 * not call out to soft-double helpers or Flash. */
NFM_DATA static const float sine_table[NFM_SINE_SIZE + 1] = {
#include "nfm_sine.inc"
};

/* The empty asm keeps the compiler from turning this back into a memset call. Word stores only where
 * both ends are aligned: the FM-1's CPU traps an unaligned one (a track's steps are bytes). */
NFM_CODE void nfm_zero(void *p, unsigned n)
{
    if ((((uintptr_t)p) | n) & 3u) {
        uint8_t *b = (uint8_t *)p;
        for (; n; --n) {
            *b++ = 0u;
            __asm__ volatile("" ::: "memory");
        }
        return;
    }
    uint32_t *w = (uint32_t *)p;
    for (n >>= 2; n; --n) {
        *w++ = 0u;
        __asm__ volatile("" ::: "memory");
    }
}

NFM_CODE static inline float wrap1(float x)
{
    int i = (int)x;
    if ((float)i > x) --i;
    return x - (float)i;
}

/* sin(2 pi x), x in cycles, any value. */
NFM_CODE static inline float sin_cyc(float x)
{
    float f = wrap1(x) * (float)NFM_SINE_SIZE;
    int i = (int)f;
    float t = f - (float)i;
    i &= NFM_SINE_SIZE - 1;   /* wrap1 of a tiny negative x rounds to 1.0: the table's end */
    return sine_table[i] + (sine_table[i + 1] - sine_table[i]) * t;
}

NFM_CODE static float exp2_approx(float x)
{
    if (x < -126.0f) return 0.0f;
    if (x > 126.0f) x = 126.0f;
    int i = (int)x;
    if ((float)i > x) --i;
    float f = x - (float)i;
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f))));
    union { float f; uint32_t u; } v;
    v.u = (uint32_t)(i + 127) << 23;
    return v.f * p;
}

NFM_CODE static float log2_approx(float x)
{
    union { float f; uint32_t u; } v;
    v.f = x;
    int e = (int)((v.u >> 23) & 255) - 127;
    v.u = (v.u & 0x007fffffu) | 0x3f800000u; /* mantissa in [1, 2) */
    float m = v.f, t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
    float ln = 2.0f * t * (1.0f + t2 * (0.33333333f + t2 * (0.2f + t2 * (0.14285714f + t2 * 0.11111111f))));
    return (float)e + ln * 1.44269504f;
}

NFM_CODE static inline float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

/* Gain per sample that falls 60 dB in t seconds. */
NFM_CODE static float t60_coef(float t, float sr) { return exp2_approx(-9.9657843f / (t * sr)); }

NFM_CODE static inline float soft_clip(float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

NFM_CODE static inline uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
NFM_CODE static inline float rng_uni(uint32_t *s) { return (float)(rng_next(s) >> 8) * (1.0f / 16777216.0f); }
NFM_CODE static inline float rng_range(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * rng_uni(s); }
NFM_CODE static inline float rng_gauss(uint32_t *s)
{
    return (rng_uni(s) + rng_uni(s) + rng_uni(s) + rng_uni(s) - 2.0f) * 1.7320508f;
}

/* ---- parameter table --------------------------------------------------- */

enum { LIN, LOG, SQR };
typedef struct {
    const char *name;
    float lo, hi;
    uint8_t curve;
    float jitter;   /* std dev of the per-hit offset, normalised, at DEVIANCE 0.4 */
} param_desc;

NFM_DATA static const param_desc params[NFM_PARAMS] = {
    [NFM_PITCH]       = { "pitch",       20.0f, 4000.0f, LOG, 0.005f },
    [NFM_PITCH_ENV]   = { "pitch env",    0.0f,   48.0f, SQR, 0.04f },
    [NFM_PITCH_DECAY] = { "pitch decay", 0.001f,  0.4f,  LOG, 0.04f },
    [NFM_SWEEP_MODS]  = { "sweep mods",   0.0f,   2.0f,  LIN, 0.03f },
    [NFM_ALGO]        = { "algorithm",    0.0f,   7.0f,  LIN, 0.0f },
    [NFM_FEEDBACK]    = { "feedback",     0.0f,   1.0f,  LIN, 0.05f },
    [NFM_ATTACK]      = { "attack",    0.0001f,  0.03f,  LOG, 0.03f },
    [NFM_RATIO1]      = { "ratio 1",    0.125f,  16.0f,  LOG, 0.003f },
    [NFM_RATIO2]      = { "ratio 2",    0.125f,  16.0f,  LOG, 0.003f },
    [NFM_RATIO3]      = { "ratio 3",    0.125f,  16.0f,  LOG, 0.003f },
    [NFM_RATIO4]      = { "ratio 4",    0.125f,  16.0f,  LOG, 0.003f },
    [NFM_LEVEL1]      = { "level 1",      0.0f,   1.0f,  LIN, 0.04f },
    [NFM_LEVEL2]      = { "level 2",      0.0f,   1.0f,  LIN, 0.05f },
    [NFM_LEVEL3]      = { "level 3",      0.0f,   1.0f,  LIN, 0.05f },
    [NFM_LEVEL4]      = { "level 4",      0.0f,   1.0f,  LIN, 0.05f },
    [NFM_DECAY1]      = { "decay 1",    0.002f,   4.0f,  LOG, 0.03f },
    [NFM_DECAY2]      = { "decay 2",    0.002f,   4.0f,  LOG, 0.035f },
    [NFM_DECAY3]      = { "decay 3",    0.002f,   4.0f,  LOG, 0.035f },
    [NFM_DECAY4]      = { "decay 4",    0.002f,   4.0f,  LOG, 0.035f },
    [NFM_NOISE]       = { "noise",        0.0f,   1.0f,  SQR, 0.05f },
    [NFM_NOISE_DECAY] = { "noise decay", 0.002f,  0.8f,  LOG, 0.04f },
    [NFM_NOISE_COLOR] = { "noise color", 100.0f, 16000.0f, LOG, 0.04f },
    [NFM_NOISE_BAND]  = { "noise band",   0.0f,   1.0f,  LIN, 0.05f },
    [NFM_NOISE_FM]    = { "noise fm",     0.0f,   1.0f,  SQR, 0.05f },
    [NFM_FOLD]        = { "fold",         0.0f,   1.0f,  LIN, 0.05f },
    [NFM_CRUSH]       = { "crush",        0.0f,   1.0f,  LIN, 0.03f },
    [NFM_RES_FREQ]    = { "res freq",    40.0f, 8000.0f, LOG, 0.012f },
    [NFM_RES_SPREAD]  = { "res spread",   1.0f,   4.0f,  LIN, 0.03f },
    [NFM_RES_DECAY]   = { "res decay",   0.01f,   2.0f,  LOG, 0.04f },
    [NFM_RES_MIX]     = { "res mix",      0.0f,   1.0f,  LIN, 0.05f },
    [NFM_LEVEL]       = { "level",        0.0f,   1.0f,  LIN, 0.03f },
    [NFM_PAN]         = { "pan",          0.0f,   1.0f,  LIN, 0.05f },
};

NFM_CODE static float param_map(int i, float n)
{
    const param_desc *d = &params[i];
    switch (d->curve) {
    case LOG: return d->lo * exp2_approx(n * log2_approx(d->hi / d->lo));
    case SQR: return d->lo + (d->hi - d->lo) * n * n;
    default: return d->lo + (d->hi - d->lo) * n;
    }
}

/* The inverse: a value in the parameter's own unit to 0..1. */
NFM_CODE static float param_unmap(int i, float v)
{
    const param_desc *d = &params[i];
    float t;
    if (d->curve == LOG) return clamp01(log2_approx(v / d->lo) / log2_approx(d->hi / d->lo));
    t = clamp01((v - d->lo) / (d->hi - d->lo));
    return d->curve == SQR && t > 0.0f ? exp2_approx(0.5f * log2_approx(t)) : t;
}

static const char *const macro_names[NFM_MACROS] = { "deviance", "stress", "spike", "bleed", "fluid" };
static const char *const kind_names[NFM_KINDS] = { "kick", "snare", "hat", "perc", "click", "metal", "clap" };

/* Carriers per algorithm, bit k = op k+1 (see nfm.h). */
NFM_DATA static const uint8_t algo_carriers[NFM_ALGOS] = { 1, 1, 1 | 4, 1 | 2 | 4, 1, 1, 1 | 2, 15 };
/* Loudness for 1..4 carriers, so that adding partials does not add level. */
NFM_DATA static const float carrier_gain[5] = { 0.0f, 1.0f, 0.78f, 0.62f, 0.52f };

NFM_CODE int nfm_algo_carriers(int a) { return a >= 0 && a < NFM_ALGOS ? algo_carriers[a] : 1; }

/* ---- recipes ------------------------------------------------------------ */

/* How a kind is drawn, in real units. Each op gets a role from the chosen
 * algorithm (main carrier, extra carrier or modulator) and is drawn from the
 * ranges for that role. Tuned so that any draw is a usable sound. */
typedef struct { float lo, hi; } span;
#define MAX_SET 8
typedef struct {
    uint8_t algos;                  /* allowed algorithms, bit a */
    span pitch, penv, pdecay, sweep, feedback, attack;
    span car_decay, car2_level;     /* op 1 decay; level of extra carriers */
    uint8_t n_car; float car_ratio[MAX_SET];  /* ratios for extra carriers */
    span mod_level, mod_decay;
    uint8_t n_mod; float mod_ratio[MAX_SET];  /* "nice" modulator ratios */
    float free_chance; span mod_free;         /* else a free ratio in this range */
    float click_chance;             /* op 4, if a modulator, becomes a short bright click */
    span noise, ndecay, ncolor, nband, nfm;
    span res_freq, res_spread, res_decay, res_mix;
    span level, pan;
    float fold_chance, crush_chance;
} recipe;

NFM_DATA static const recipe recipes[NFM_KINDS] = {
    [NFM_KIND_KICK] = {
        .algos = 1 << 0 | 1 << 1 | 1 << 4,
        .pitch = { 38, 68 }, .penv = { 16, 40 }, .pdecay = { 0.02f, 0.09f }, .sweep = { 0.6f, 1.2f },
        .feedback = { 0, 0.25f }, .attack = { 0.0001f, 0.002f },
        .car_decay = { 0.25f, 1.1f }, .car2_level = { 0.3f, 0.6f }, .n_car = 2, .car_ratio = { 1, 2 },
        .mod_level = { 0.08f, 0.35f }, .mod_decay = { 0.008f, 0.09f },
        .n_mod = 5, .mod_ratio = { 0.5f, 1, 1.5f, 2, 3 }, .free_chance = 0.15f, .mod_free = { 1.2f, 4 },
        .click_chance = 0.75f,
        .noise = { 0, 0.25f }, .ndecay = { 0.004f, 0.03f }, .ncolor = { 1500, 8000 }, .nband = { 0, 0.6f }, .nfm = { 0, 0.1f },
        .res_freq = { 45, 140 }, .res_spread = { 1, 2 }, .res_decay = { 0.05f, 0.3f }, .res_mix = { 0, 0.2f },
        .level = { 0.82f, 0.92f }, .pan = { 0.5f, 0.5f }, .fold_chance = 0.2f, .crush_chance = 0 },
    [NFM_KIND_SNARE] = {
        .algos = 1 << 1 | 1 << 2 | 1 << 3 | 1 << 5,
        .pitch = { 150, 260 }, .penv = { 4, 16 }, .pdecay = { 0.01f, 0.05f }, .sweep = { 0.5f, 1.5f },
        .feedback = { 0.1f, 0.6f }, .attack = { 0.0001f, 0.001f },
        .car_decay = { 0.06f, 0.22f }, .car2_level = { 0.4f, 0.8f }, .n_car = 5, .car_ratio = { 1.47f, 1.6f, 2.1f, 2.3f, 1.75f },
        .mod_level = { 0.2f, 0.55f }, .mod_decay = { 0.015f, 0.12f },
        .n_mod = 5, .mod_ratio = { 1.5f, 2.76f, 3.5f, 1.414f, 4.2f }, .free_chance = 0.5f, .mod_free = { 1.3f, 5 },
        .click_chance = 0.4f,
        .noise = { 0.55f, 0.95f }, .ndecay = { 0.08f, 0.26f }, .ncolor = { 3000, 12000 }, .nband = { 0, 0.6f }, .nfm = { 0, 0.35f },
        .res_freq = { 180, 800 }, .res_spread = { 1.2f, 2.8f }, .res_decay = { 0.04f, 0.2f }, .res_mix = { 0, 0.35f },
        .level = { 0.72f, 0.85f }, .pan = { 0.45f, 0.55f }, .fold_chance = 0.2f, .crush_chance = 0.05f },
    [NFM_KIND_HAT] = {
        .algos = 1 << 0 | 1 << 3 | 1 << 4 | 1 << 5,
        .pitch = { 300, 1100 }, .penv = { 0, 4 }, .pdecay = { 0.002f, 0.02f }, .sweep = { 0, 1 },
        .feedback = { 0.4f, 1 }, .attack = { 0.0001f, 0.0008f },
        .car_decay = { 0.02f, 0.16f }, .car2_level = { 0.5f, 0.9f }, .n_car = 4, .car_ratio = { 1.41f, 2.76f, 3.3f, 4.7f },
        .mod_level = { 0.5f, 0.95f }, .mod_decay = { 0.02f, 0.4f },
        .n_mod = 0, .free_chance = 1, .mod_free = { 1.3f, 11 },
        .click_chance = 0,
        .noise = { 0.35f, 0.85f }, .ndecay = { 0.02f, 0.16f }, .ncolor = { 7000, 16000 }, .nband = { 0.3f, 1 }, .nfm = { 0.2f, 0.7f },
        .res_freq = { 3000, 8000 }, .res_spread = { 1.2f, 3 }, .res_decay = { 0.02f, 0.15f }, .res_mix = { 0, 0.3f },
        .level = { 0.55f, 0.7f }, .pan = { 0.3f, 0.7f }, .fold_chance = 0.25f, .crush_chance = 0.05f },
    [NFM_KIND_PERC] = {
        .algos = 1 << 0 | 1 << 1 | 1 << 2 | 1 << 4,
        .pitch = { 90, 480 }, .penv = { 2, 14 }, .pdecay = { 0.012f, 0.08f }, .sweep = { 0.5f, 1.2f },
        .feedback = { 0, 0.35f }, .attack = { 0.0001f, 0.004f },
        .car_decay = { 0.07f, 0.5f }, .car2_level = { 0.3f, 0.7f }, .n_car = 4, .car_ratio = { 1.5f, 2, 2.76f, 3 },
        .mod_level = { 0.12f, 0.45f }, .mod_decay = { 0.01f, 0.15f },
        .n_mod = 7, .mod_ratio = { 1, 1.5f, 2, 2.76f, 3, 3.5f, 4 }, .free_chance = 0.3f, .mod_free = { 1.2f, 6 },
        .click_chance = 0.5f,
        .noise = { 0, 0.3f }, .ndecay = { 0.004f, 0.05f }, .ncolor = { 1000, 9000 }, .nband = { 0, 0.8f }, .nfm = { 0, 0.15f },
        .res_freq = { 150, 1500 }, .res_spread = { 1.3f, 2.8f }, .res_decay = { 0.05f, 0.5f }, .res_mix = { 0.1f, 0.55f },
        .level = { 0.7f, 0.85f }, .pan = { 0.25f, 0.75f }, .fold_chance = 0.2f, .crush_chance = 0.05f },
    [NFM_KIND_CLICK] = {
        .algos = 0x7f,
        .pitch = { 400, 4000 }, .penv = { 8, 48 }, .pdecay = { 0.001f, 0.025f }, .sweep = { 0, 2 },
        .feedback = { 0, 0.8f }, .attack = { 0.0001f, 0.0005f },
        .car_decay = { 0.006f, 0.08f }, .car2_level = { 0.3f, 0.8f }, .n_car = 4, .car_ratio = { 1.5f, 2, 3, 0.5f },
        .mod_level = { 0.3f, 0.9f }, .mod_decay = { 0.002f, 0.04f },
        .n_mod = 5, .mod_ratio = { 0.5f, 1, 2, 3, 5 }, .free_chance = 0.5f, .mod_free = { 0.5f, 8 },
        .click_chance = 0.3f,
        .noise = { 0, 0.5f }, .ndecay = { 0.002f, 0.02f }, .ncolor = { 2000, 16000 }, .nband = { 0, 1 }, .nfm = { 0, 0.5f },
        .res_freq = { 800, 7000 }, .res_spread = { 1, 3 }, .res_decay = { 0.01f, 0.12f }, .res_mix = { 0, 0.4f },
        .level = { 0.7f, 0.9f }, .pan = { 0.2f, 0.8f }, .fold_chance = 0.3f, .crush_chance = 0.08f },
    [NFM_KIND_METAL] = {
        .algos = 1 << 7 | 1 << 2 | 1 << 3 | 1 << 1 | 1 << 6,
        .pitch = { 200, 1300 }, .penv = { 0, 3 }, .pdecay = { 0.003f, 0.03f }, .sweep = { 0, 1 },
        .feedback = { 0, 0.35f }, .attack = { 0.0001f, 0.003f },
        .car_decay = { 0.3f, 2.5f }, .car2_level = { 0.25f, 0.7f }, .n_car = 7, .car_ratio = { 2.76f, 5.4f, 1.414f, 3.5f, 4.2f, 6.8f, 2.4f },
        .mod_level = { 0.08f, 0.45f }, .mod_decay = { 0.05f, 1.2f },
        .n_mod = 5, .mod_ratio = { 1.414f, 2.76f, 3.5f, 5.4f, 7 }, .free_chance = 0.5f, .mod_free = { 1.2f, 9 },
        .click_chance = 0.2f,
        .noise = { 0, 0.12f }, .ndecay = { 0.003f, 0.03f }, .ncolor = { 3000, 14000 }, .nband = { 0, 1 }, .nfm = { 0, 0.1f },
        .res_freq = { 700, 6000 }, .res_spread = { 1.5f, 3.5f }, .res_decay = { 0.2f, 1.5f }, .res_mix = { 0.1f, 0.5f },
        .level = { 0.58f, 0.76f }, .pan = { 0.25f, 0.75f }, .fold_chance = 0.1f, .crush_chance = 0.05f },
    /* the noise is the clap (its bursts: nfm_trigger); the FM a short knock under it, the resonator
     * the room or the hands' body */
    [NFM_KIND_CLAP] = {
        .algos = 1 << 0 | 1 << 1 | 1 << 4,
        .pitch = { 180, 420 }, .penv = { 2, 10 }, .pdecay = { 0.004f, 0.02f }, .sweep = { 0.5f, 1 },
        .feedback = { 0, 0.3f }, .attack = { 0.0001f, 0.001f },
        .car_decay = { 0.006f, 0.03f }, .car2_level = { 0.2f, 0.5f }, .n_car = 3, .car_ratio = { 1.5f, 2.1f, 2.76f },
        .mod_level = { 0.2f, 0.6f }, .mod_decay = { 0.003f, 0.02f },
        .n_mod = 4, .mod_ratio = { 1.414f, 2.76f, 3.5f, 5.4f }, .free_chance = 0.4f, .mod_free = { 1.3f, 7 },
        .click_chance = 0.2f,
        .noise = { 0.75f, 1 }, .ndecay = { 0.07f, 0.35f }, .ncolor = { 800, 2600 }, .nband = { 0.45f, 0.9f }, .nfm = { 0, 0.1f },
        .res_freq = { 600, 2200 }, .res_spread = { 1.2f, 2.2f }, .res_decay = { 0.03f, 0.15f }, .res_mix = { 0, 0.3f },
        .level = { 0.76f, 0.9f }, .pan = { 0.4f, 0.6f }, .fold_chance = 0.15f, .crush_chance = 0.05f },
};

/* ---- slots -------------------------------------------------------------- */

NFM_CODE static int slot_ok(int s) { return s >= 0 && s < NFM_SLOTS; }

NFM_CODE void nfm_set_param(nfm_engine *e, int s, int p, float v)
{
    if (slot_ok(s) && p >= 0 && p < NFM_PARAMS) e->slot[s].p[p] = clamp01(v);
}
NFM_CODE float nfm_get_param(const nfm_engine *e, int s, int p)
{
    return slot_ok(s) && p >= 0 && p < NFM_PARAMS ? e->slot[s].p[p] : 0.0f;
}
NFM_CODE void nfm_set_macro(nfm_engine *e, int s, int m, float v)
{
    if (slot_ok(s) && m >= 0 && m < NFM_MACROS) e->slot[s].macro[m] = clamp01(v);
}
NFM_CODE float nfm_get_macro(const nfm_engine *e, int s, int m)
{
    return slot_ok(s) && m >= 0 && m < NFM_MACROS ? e->slot[s].macro[m] : 0.0f;
}
NFM_CODE void nfm_set_choke(nfm_engine *e, int s, int on)
{
    if (slot_ok(s)) e->slot[s].choke = on ? 1 : 0;
}
NFM_CODE int nfm_slot_kind(const nfm_engine *e, int s) { return slot_ok(s) ? e->slot[s].kind : NFM_KIND_ANY; }
NFM_CODE const char *nfm_param_name(int p) { return p >= 0 && p < NFM_PARAMS ? params[p].name : ""; }
NFM_CODE const char *nfm_macro_name(int m) { return m >= 0 && m < NFM_MACROS ? macro_names[m] : ""; }
NFM_CODE const char *nfm_kind_name(int k) { return k >= 0 && k < NFM_KINDS ? kind_names[k] : "any"; }

/* Draw a parameter from a span in real units, evenly on the parameter's own
 * curve (so decays and frequencies are drawn on a log scale). */
NFM_CODE static void draw(nfm_slot *sl, uint32_t *r, int p, span s)
{
    sl->p[p] = rng_range(r, param_unmap(p, s.lo), param_unmap(p, s.hi));
}
NFM_CODE static float pick_ratio(uint32_t *r, const float *set, int n, float free_chance, span free)
{
    if (n == 0 || rng_uni(r) < free_chance)
        return free.lo * exp2_approx(rng_uni(r) * log2_approx(free.hi / free.lo));
    return set[rng_next(r) % (unsigned)n];
}

NFM_CODE void nfm_randomize(nfm_engine *e, int s, int kind)
{
    if (!slot_ok(s)) return;
    nfm_slot *sl = &e->slot[s];
    uint32_t *r = &e->rng;
    if (kind < 0 || kind >= NFM_KINDS) {
        /* Rhythm first: drums are drawn more often than the tonal kinds. */
        NFM_DATA static const uint8_t weights[NFM_KINDS] = { 3, 3, 3, 4, 3, 2, 2 };
        unsigned total = 0, pick;
        for (int k = 0; k < NFM_KINDS; ++k) total += weights[k];
        pick = rng_next(r) % total;
        for (kind = 0; pick >= weights[kind]; ++kind) pick -= weights[kind];
    }
    const recipe *rc = &recipes[kind];
    for (int i = 0; i < NFM_PARAMS; ++i) sl->walk[i] = 0.0f;

    /* Algorithm: one of the kind's, evenly. */
    int algo, n = 0;
    for (int a = 0; a < NFM_ALGOS; ++a) n += (rc->algos >> a) & 1;
    {
        int pick = (int)(rng_next(r) % (unsigned)n);
        for (algo = 0; algo < NFM_ALGOS; ++algo)
            if (((rc->algos >> algo) & 1) && pick-- == 0) break;
    }
    sl->p[NFM_ALGO] = ((float)algo + 0.5f) / (float)NFM_ALGOS;
    int car = algo_carriers[algo];

    draw(sl, r, NFM_PITCH, rc->pitch);
    draw(sl, r, NFM_PITCH_ENV, rc->penv);
    draw(sl, r, NFM_PITCH_DECAY, rc->pdecay);
    draw(sl, r, NFM_SWEEP_MODS, rc->sweep);
    draw(sl, r, NFM_FEEDBACK, rc->feedback);
    draw(sl, r, NFM_ATTACK, rc->attack);

    /* Hats can open up now and then. */
    span car_decay = rc->car_decay, ndecay = rc->ndecay;
    if (kind == NFM_KIND_HAT && rng_uni(r) < 0.15f) {
        car_decay.lo = ndecay.lo = 0.3f;
        car_decay.hi = ndecay.hi = 0.8f;
    }
    for (int k = 0; k < NFM_OPS; ++k) {
        float ratio, level, decay;
        if (k == 0) {
            ratio = kind == NFM_KIND_METAL && rng_uni(r) < 0.25f ? 0.5f : 1.0f;
            level = rng_range(r, 0.85f, 1.0f);
            decay = car_decay.lo * exp2_approx(rng_uni(r) * log2_approx(car_decay.hi / car_decay.lo));
        } else if (car & (1 << k)) {
            ratio = rc->car_ratio[rng_next(r) % rc->n_car];
            level = rng_range(r, rc->car2_level.lo, rc->car2_level.hi);
            /* Higher partials die sooner, like struck metal and skins. */
            decay = (car_decay.lo * exp2_approx(rng_uni(r) * log2_approx(car_decay.hi / car_decay.lo)))
                    * rng_range(r, 0.3f, 1.0f);
        } else {
            ratio = pick_ratio(r, rc->mod_ratio, rc->n_mod, rc->free_chance, rc->mod_free);
            level = rng_range(r, rc->mod_level.lo, rc->mod_level.hi);
            decay = rc->mod_decay.lo * exp2_approx(rng_uni(r) * log2_approx(rc->mod_decay.hi / rc->mod_decay.lo));
            /* Some modulators sit out, so not every sound is four ops thick. */
            if (k != 3 && rng_uni(r) < 0.2f) level = 0.0f;
        }
        if (k == 3 && !(car & 8) && rng_uni(r) < rc->click_chance) {
            ratio = rng_range(r, 4.0f, 12.0f);
            level = rng_range(r, 0.3f, 0.7f);
            decay = rng_range(r, 0.002f, 0.012f);
        }
        sl->p[NFM_RATIO1 + k] = param_unmap(NFM_RATIO1 + k, ratio);
        sl->p[NFM_LEVEL1 + k] = clamp01(level);
        sl->p[NFM_DECAY1 + k] = param_unmap(NFM_DECAY1 + k, decay);
    }

    draw(sl, r, NFM_NOISE, rc->noise);
    draw(sl, r, NFM_NOISE_DECAY, ndecay);
    draw(sl, r, NFM_NOISE_COLOR, rc->ncolor);
    draw(sl, r, NFM_NOISE_BAND, rc->nband);
    draw(sl, r, NFM_NOISE_FM, rc->nfm);
    draw(sl, r, NFM_RES_FREQ, rc->res_freq);
    draw(sl, r, NFM_RES_SPREAD, rc->res_spread);
    draw(sl, r, NFM_RES_DECAY, rc->res_decay);
    draw(sl, r, NFM_RES_MIX, rc->res_mix);
    draw(sl, r, NFM_LEVEL, rc->level);
    draw(sl, r, NFM_PAN, rc->pan);
    /* Occasional character: a folded edge or an aliased crunch. */
    sl->p[NFM_FOLD] = rng_uni(r) < rc->fold_chance ? rng_range(r, 0.15f, 0.5f) : 0.0f;
    sl->p[NFM_CRUSH] = rng_uni(r) < rc->crush_chance ? rng_range(r, 0.15f, 0.6f) : 0.0f;
    sl->kind = (int8_t)kind;
}

NFM_CODE void nfm_mutate(nfm_engine *e, int s, float amount)
{
    if (!slot_ok(s)) return;
    nfm_slot *sl = &e->slot[s];
    for (int i = 0; i < NFM_PARAMS; ++i)
        sl->p[i] = clamp01(sl->p[i] + rng_gauss(&e->rng) * params[i].jitter * amount * 3.0f);
}

/* ---- engine ------------------------------------------------------------- */

NFM_CODE void nfm_init(nfm_engine *e, float sample_rate, uint32_t seed)
{
    nfm_zero(e, sizeof *e);
    e->sr = sample_rate;
    e->inv_sr = 1.0f / sample_rate;
    e->rng = seed ? seed : 0x6d2b79f5u;
    e->master = 0.9f;
    e->ins_g = 0.5f;
    e->ins_k = 1.0f;
    e->voice_limit = NFM_VOICES;
    e->priority_slot = -1;
    for (int v = 0; v < NFM_VOICES; ++v) e->voice[v].slot = -1;
    NFM_DATA static const int8_t start_kit[NFM_SLOTS > 8 ? NFM_SLOTS : 8] = {
        NFM_KIND_KICK, NFM_KIND_SNARE, NFM_KIND_HAT, NFM_KIND_PERC,
        NFM_KIND_CLICK, NFM_KIND_METAL, NFM_KIND_PERC, NFM_KIND_HAT };
    for (int s = 0; s < NFM_SLOTS; ++s) {
        nfm_slot *sl = &e->slot[s];
        sl->macro[NFM_DEVIANCE] = 0.4f;
        sl->macro[NFM_STRESS] = sl->macro[NFM_SPIKE] = 0.5f;
        sl->macro[NFM_BLEED] = sl->macro[NFM_FLUID] = 0.5f;
        sl->choke = 1;
        nfm_randomize(e, s, s < 8 ? start_kit[s] : NFM_KIND_ANY);
    }
}

NFM_CODE void nfm_stop_all(nfm_engine *e)
{
    for (int v = 0; v < NFM_VOICES; ++v) {
        nfm_voice *vo = &e->voice[v];
        if (vo->slot >= 0 && !vo->fading) {
            vo->fading = 1;
            vo->fade = 1.0f;
            vo->fade_k = t60_coef(0.02f, e->sr);
        }
    }
}

NFM_CODE int nfm_active_voices(const nfm_engine *e)
{
    int n = 0;
    for (int v = 0; v < NFM_VOICES; ++v) n += e->voice[v].slot >= 0;
    return n;
}

NFM_CODE static nfm_voice *pick_voice(nfm_engine *e, int s)
{
    nfm_voice *best = 0;
    if (e->slot[s].choke) {
        for (int v = 0; v < NFM_VOICES; ++v) {
            nfm_voice *vo = &e->voice[v];
            if (vo->slot == s && !vo->fading) {
                vo->fading = 1;
                vo->fade = 1.0f;
                vo->fade_k = t60_coef(0.012f, e->sr);
            }
        }
    }
    int lim = e->voice_limit < 1 ? 1 : e->voice_limit > NFM_VOICES ? NFM_VOICES : e->voice_limit;
    for (int v = 0; v < lim; ++v)
        if (e->voice[v].slot < 0) return &e->voice[v];
    /* Steal: a fading voice first, otherwise the oldest; the priority slot's only by its own. */
    for (int v = 0; v < lim; ++v) {
        nfm_voice *vo = &e->voice[v];
        if (vo->slot == e->priority_slot && s != e->priority_slot && !vo->fading) continue;
        if (!best) best = vo;
        else if (vo->fading != best->fading) { if (vo->fading) best = vo; }
        else if (vo->fading ? vo->fade < best->fade : vo->age < best->age) best = vo;
    }
    return best ? best : &e->voice[0];
}

NFM_CODE static float res_coef(nfm_engine *e, float f, float t60, float *k)
{
    float q = t60 * 3.14159265f * f / 6.9077553f;
    if (q < 0.7f) q = 0.7f;
    if (q > 3000.0f) q = 3000.0f;
    *k = 1.0f / q;
    float x = f * e->inv_sr * 0.5f; /* tan(pi f / sr) */
    return sin_cyc(x) / sin_cyc(x + 0.25f);
}

NFM_CODE static int algo_of(float n)
{
    int a = (int)(clamp01(n) * (float)NFM_ALGOS);
    return a >= NFM_ALGOS ? NFM_ALGOS - 1 : a;
}

NFM_CODE static float svf_g(nfm_engine *e, float f) /* tan(pi f / sr) */
{
    float x = f * e->inv_sr * 0.5f;
    if (x > 0.24f) x = 0.24f;
    return sin_cyc(x) / sin_cyc(x + 0.25f);
}

NFM_CODE int nfm_trigger(nfm_engine *e, int s, float velocity, float transpose)
{
    if (!slot_ok(s)) return -1;
    nfm_slot *sl = &e->slot[s];
    float n[NFM_PARAMS], val[NFM_PARAMS];
    velocity = clamp01(velocity);

    /* The per-hit variation: a mean-reverting walk around the base patch.
     * w' = 0.6 w + 0.8 sigma g keeps the long-run spread at sigma. */
    float amount = sl->macro[NFM_DEVIANCE] * 2.5f;
    for (int i = 0; i < NFM_PARAMS; ++i) {
        float sigma = params[i].jitter * amount;
        sl->walk[i] = sl->walk[i] * 0.6f + 0.8f * sigma * rng_gauss(&e->rng);
        n[i] = sl->p[i] + sl->walk[i];
    }
    int algo = algo_of(sl->p[NFM_ALGO]), car = algo_carriers[algo];
    /* What is off in the base stays off: a stray bit of crush, fold or an
     * extra operator changes the character, not just the detail. */
    NFM_DATA static const uint8_t keep_off[] = {
        NFM_FOLD, NFM_CRUSH, NFM_NOISE, NFM_NOISE_FM, NFM_LEVEL2, NFM_LEVEL3, NFM_LEVEL4 };
    for (unsigned i = 0; i < sizeof keep_off; ++i)
        if (sl->p[keep_off[i]] < 0.02f) n[keep_off[i]] = 0.0f;
    float stress = sl->macro[NFM_STRESS] - 0.5f, spike = sl->macro[NFM_SPIKE] - 0.5f;
    float bleed = sl->macro[NFM_BLEED] - 0.5f, fluid = sl->macro[NFM_FLUID] - 0.5f;
    for (int k = 0; k < NFM_OPS; ++k) {
        n[NFM_DECAY1 + k] += stress * 0.35f;
        if (!(car & (1 << k)) && n[NFM_LEVEL1 + k] > 0.0f) n[NFM_LEVEL1 + k] += spike * 0.4f;
    }
    n[NFM_NOISE_DECAY] += stress * 0.4f;
    n[NFM_RES_DECAY] += stress * 0.4f;
    n[NFM_PITCH_DECAY] += stress * 0.2f;
    n[NFM_FEEDBACK] += spike * 0.5f;
    n[NFM_NOISE_COLOR] += spike * 0.5f;
    if (n[NFM_NOISE_FM] > 0.0f) n[NFM_NOISE_FM] += spike * 0.3f;
    if (spike > 0.0f) n[NFM_FOLD] += spike * 0.8f;
    n[NFM_RES_MIX] += bleed;
    n[NFM_RES_DECAY] += fluid * 0.7f;
    n[NFM_RES_SPREAD] += fluid * 0.3f;
    for (int i = 0; i < NFM_PARAMS; ++i) val[i] = param_map(i, clamp01(n[i]));

    nfm_voice *v = pick_voice(e, s);
    float sr = e->sr, tr = exp2_approx((transpose + e->slot[s].tune) * (1.0f / 12.0f));
    float vel_amp = velocity * (0.4f + 0.6f * velocity);
    float vel_idx = 0.55f + 0.45f * velocity;

    v->slot = (int8_t)s;
    v->fading = 0;
    v->fade = 1.0f;
    v->age = e->clock++;
    v->algo = (uint8_t)algo;
    v->carriers = (uint8_t)car;
    {
        int nc = (car & 1) + (car >> 1 & 1) + (car >> 2 & 1) + (car >> 3 & 1);
        float cg = carrier_gain[nc];
        for (int k = 0; k < NFM_OPS; ++k) {
            float l = val[NFM_LEVEL1 + k];
            v->ph[k] = 0.0f;
            v->ratio[k] = val[NFM_RATIO1 + k];
            v->env_k[k] = t60_coef(val[NFM_DECAY1 + k], sr);
            /* Carriers: loudness. Modulators: index, 0..12 rad, in cycles. */
            v->env[k] = (car & (1 << k)) ? l * cg : l * l * 12.0f * vel_idx * 0.15915494f;
        }
    }
    v->feedback = val[NFM_FEEDBACK] * (algo == 5 ? 0.25f : 0.3f);
    v->fb1 = v->fb2 = v->ring = 0.0f;
    v->f0 = val[NFM_PITCH] * tr;
    v->pitch_depth = val[NFM_PITCH_ENV] * (1.0f / 12.0f);
    v->sweep_mods = val[NFM_SWEEP_MODS];
    v->pitch_env = 1.0f;
    v->pitch_k = t60_coef(val[NFM_PITCH_DECAY], sr);
    v->att = 0.0f;
    v->att_inc = 1.0f / (val[NFM_ATTACK] * sr + 1.0f);
    v->noise = val[NFM_NOISE];
    v->noise_k = t60_coef(val[NFM_NOISE_DECAY], sr);
    v->ns1 = v->ns2 = 0.0f;
    v->ng = svf_g(e, val[NFM_NOISE_COLOR] * tr);
    v->nband = val[NFM_NOISE_BAND];
    v->nk = 2.0f - 1.8f * v->nband;  /* damping: gentle lowpass to a ringing band */
    v->noise_fm = val[NFM_NOISE_FM] * 0.6f;
    v->bursts = 0;
    if (e->slot[s].kind == NFM_KIND_CLAP) {
        /* a clap: three or four bursts 6..13 ms apart, each gone in about its gap, drawn per hit (the
         * hands never meet the same way twice); the noise's own decay is the tail after the last */
        float gap = (0.006f + 0.007f * rng_uni(&e->rng)) * sr;
        v->bursts = (uint8_t)(rng_uni(&e->rng) < 0.6f ? 2 : 3);
        v->clap_gap = (uint16_t)gap;
        v->clap_t = v->clap_gap;
        v->noise *= 2.3f;                         /* (a narrow band of noise: as loud as a snare) */
        v->clap_amp = v->noise;
        v->clap_k = t60_coef(gap * 2.5f / sr, sr);
    }
    v->fold = val[NFM_FOLD];
    v->crush = val[NFM_CRUSH];
    v->crush_rate = exp2_approx(-val[NFM_CRUSH] * 5.0f);
    v->crush_q = exp2_approx(14.0f - val[NFM_CRUSH] * 11.0f);
    v->crush_acc = 1.0f;
    v->crush_hold = 0.0f;
    {
        NFM_DATA static const float mode_decay[NFM_MODES] = { 1.0f, 0.75f, 0.55f, 0.4f };
        NFM_DATA static const float mode_amp[NFM_MODES] = { 1.0f, 0.7f, 0.5f, 0.35f };
        float f = val[NFM_RES_FREQ] * tr, spread = val[NFM_RES_SPREAD];
        float ls = log2_approx(spread);
        /* Modes at 1, s, s^1.75, s^2.4: drum-skin-like, never harmonic. */
        float ratio[NFM_MODES] = { 1.0f, spread, exp2_approx(1.75f * ls), exp2_approx(2.4f * ls) };
        for (int m = 0; m < NFM_MODES; ++m) {
            float fm = f * ratio[m];
            v->rs1[m] = v->rs2[m] = 0.0f;
            if (fm > 0.45f * sr || (m > 0 && spread < 1.01f)) {
                v->ramp[m] = 0.0f;
                v->rg[m] = 0.0f;
                v->rk[m] = 1.0f;
                continue;
            }
            v->rg[m] = res_coef(e, fm, val[NFM_RES_DECAY] * mode_decay[m], &v->rk[m]);
            /* sqrt(k) scaling keeps long and short rings at similar loudness. */
            float k = v->rk[m], sk = k;
            for (int it = 0; it < 6; ++it) sk = 0.5f * (sk + k / sk);
            v->ramp[m] = mode_amp[m] * sk * 1.5f;
        }
    }
    v->res_mix = val[NFM_RES_MIX];
    {
        float level = val[NFM_LEVEL] * vel_amp;
        float pan = val[NFM_PAN] * 0.25f;
        v->gain_l = level * sin_cyc(pan + 0.25f);
        v->gain_r = level * sin_cyc(pan);
    }
    v->peak = 1.0f;
    v->ctl = 0;
    v->ins = 0;
    v->send_rev = v->send_dly = 0.0f;
    v->ins_s1 = v->ins_s2 = 0.0f;
    nfm_sat_set(&v->sat, 0.0f);
    {   /* the sound's modulation effect, on a share of its hits */
        const nfm_mod *m = &e->mod[s];
        int need_line = m->type == NFM_MOD_FLANGER || m->type == NFM_MOD_CHORUS;
        v->mod_on = m->type != NFM_MOD_OFF && m->chance > 0.0f && (!need_line || v->ms.line) &&
                    (m->chance >= 1.0f || rng_uni(&e->rng) < m->chance);
        if (v->mod_on) nfm_mod_reset(&v->ms);
    }
    return (int)(v - e->voice);
}

NFM_CODE void nfm_route(nfm_engine *e, int voice, int ins, float send_rev, float send_dly, float drive)
{
    if (voice < 0 || voice >= NFM_VOICES) return;
    nfm_voice *v = &e->voice[voice];
    v->ins = (uint8_t)ins;
    v->send_rev = clamp01(send_rev);
    v->send_dly = clamp01(send_dly);
    nfm_sat_set(&v->sat, ins & NFM_INS_DRIVE ? drive : 0.0f);
    if (v->sat.amount <= 0.0f) v->ins &= (uint8_t)~NFM_INS_DRIVE;
}

/* the drive: pre-gain 0.75..5.25, a bias 0.1..0.35 (the even harmonics), and the curve's slope at the
 * bias divided out, so a quiet signal comes out as loud as it went in */
NFM_CODE void nfm_sat_set(nfm_sat *s, float amount)
{
    amount = clamp01(amount);
    s->amount = amount;
    s->pre = 0.75f + 4.5f * amount;
    s->bias = 0.1f + 0.25f * amount;
    s->off = sat_curve(s->bias);
    s->comp = 1.0f / (s->pre * (1.0f - 0.444444f * s->bias * s->bias));
    s->u1 = s->bias;
    s->dc = 0.0f;
    s->ein = s->eout = 0.0f;
    s->adapt = s->gain = 1.0f;
}

NFM_CODE float nfm_sat_tick(nfm_sat *s, float x)
{
    float u = x * s->pre + s->bias;
    float y = (sat_adaa(s->u1, u) - s->off) * s->comp;
    s->u1 = u;
    s->dc += 0.0015f * (y - s->dc);
    y -= s->dc;
    s->ein += 0.004f * (x * x - s->ein);
    s->eout += 0.004f * (y * y - s->eout);
    s->gain += (s->adapt - s->gain) * 0.003f;   /* glided (~8 ms): a gain set in steps rang like aliasing */
    return y * s->gain;
}

/* loud parts come out of the curve quieter than they went in: the levels followed (~6 ms) and the
 * difference made up, within 0.7..3 (sqrt by two Newton steps from the last value) */
NFM_CODE void nfm_sat_control(nfm_sat *s)
{
    if (s->eout < 1e-9f || s->ein < 1e-9f) return;
    float want = s->ein / s->eout, r = s->adapt;
    if (want > 9.0f) want = 9.0f;
    if (want < 0.49f) want = 0.49f;
    r = 0.5f * (r + want / r);
    r = 0.5f * (r + want / r);
    s->adapt = r;
}

/* ---- the modulation effects -------------------------------------------- */

/* The LFO is a sine, 0..1. Rate 0..1: 0.05..8 Hz (tremolo 0.5..32 Hz); the ring modulator's carrier
 * 30..1920 Hz. A block's values are worked out at its two ends and slid between. */
NFM_CODE static float mod_lfo_hz(const nfm_mod *m)
{
    return m->type == NFM_MOD_TREMOLO ? 0.5f * exp2_approx(m->rate * 6.0f) : 0.05f * exp2_approx(m->rate * 7.3219281f);
}
/* phaser: the allpasses' corner, 200 Hz up to 200 x 2^(1 + 5 amount), as a coefficient */
NFM_CODE static float mod_ap(const nfm_mod *m, float u, float sr)
{
    float f = 200.0f * exp2_approx(u * (1.0f + 5.0f * m->amount));
    float x = f / sr * 0.5f;
    if (x > 0.2f) x = 0.2f;
    float t = sin_cyc(x) / sin_cyc(x + 0.25f);
    return (t - 1.0f) / (t + 1.0f);
}
/* flanger 0.5..(1 + 3 amount) ms, chorus 7..(9 + 5 amount) ms: the delay in frames */
NFM_CODE static float mod_delay(const nfm_mod *m, float u, float sr)
{
    float ms = m->type == NFM_MOD_FLANGER ? 0.5f + u * (0.5f + 3.0f * m->amount)
                                          : 7.0f + u * (2.0f + 5.0f * m->amount);
    return ms * 0.001f * sr;
}

NFM_CODE void nfm_mod_block(nfm_mod *m, float sr, int n)
{
    if (m->type == NFM_MOD_OFF || n <= 0) return;
    float l1, ph1 = m->ph + mod_lfo_hz(m) * (float)n / sr;
    float rn = 1.0f / (float)n;
    m->l0 = 0.5f + 0.5f * sin_cyc(m->ph);
    l1 = 0.5f + 0.5f * sin_cyc(ph1);
    m->dl = (l1 - m->l0) * rn;
    m->ph = wrap1(ph1);
    switch (m->type) {
    case NFM_MOD_PHASER:
        m->a0 = mod_ap(m, m->l0, sr);
        m->da = (mod_ap(m, l1, sr) - m->a0) * rn;
        break;
    case NFM_MOD_FLANGER: case NFM_MOD_CHORUS:
        m->d0 = mod_delay(m, m->l0, sr);
        m->dd = (mod_delay(m, l1, sr) - m->d0) * rn;
        break;
    case NFM_MOD_RING:
        m->car = wrap1(m->car + m->c_inc * m->nprev);
        m->c_inc = 30.0f * exp2_approx(m->rate * 6.0f) / sr;
        break;
    }
    m->nprev = (float)n;
}

NFM_CODE void nfm_mod_reset(nfm_mod_state *s)
{
    for (int k = 0; k < 4; ++k) s->x1[k] = s->y1[k] = 0.0f;
    s->fb = 0.0f;
    s->ein = s->eout = 0.0f;
    s->g = 1.0f;
    s->w = 0;
    if (s->line) nfm_zero(s->line, NFM_MOD_LEN * 2u);
}

/* the levels in and out followed, every 16th frame the gain that matches them (within 0.5..2: sqrt by
 * two Newton steps from the last value) */
NFM_CODE static inline float mod_match(nfm_mod_state *s, float x, float y, int i)
{
    s->ein += 0.001f * (x * x - s->ein);
    s->eout += 0.001f * (y * y - s->eout);
    if ((i & 15) == 0 && s->eout > 1e-10f && s->ein > 1e-10f) {
        float want = s->ein / s->eout, r = s->g;
        want = want > 4.0f ? 4.0f : want < 0.25f ? 0.25f : want;
        r = 0.5f * (r + want / r);
        s->g = 0.5f * (r + want / r);
    }
    return y * s->g;
}

NFM_CODE float nfm_mod_tick(const nfm_mod *m, nfm_mod_state *s, float x, int i)
{
    float fi = (float)i;
    switch (m->type) {
    case NFM_MOD_PHASER: {       /* four first-order allpasses with feedback, mixed with the dry: notches */
        float a = m->a0 + m->da * fi, y = x + s->fb * (0.2f + 0.5f * m->amount);
        for (int k = 0; k < 4; ++k) {
            float o = a * y + s->x1[k] - a * s->y1[k];
            s->x1[k] = y;
            s->y1[k] = o;
            y = o;
        }
        s->fb = y;
        return mod_match(s, x, x + (y - x) * (0.35f + 0.15f * m->amount), i);
    }
    case NFM_MOD_FLANGER: case NFM_MOD_CHORUS: {
        if (!s->line) return x;
        float d = m->d0 + m->dd * fi;
        int di = (int)d;
        float t = d - (float)di;
        uint16_t w = s->w;
        float a = (float)s->line[(w - di) & (NFM_MOD_LEN - 1)], b = (float)s->line[(w - di - 1) & (NFM_MOD_LEN - 1)];
        float del = (a + (b - a) * t) * (1.0f / 8192.0f), in, y;
        if (m->type == NFM_MOD_FLANGER) {
            in = x + del * (0.3f + 0.55f * m->amount);
            y = (x + del) * 0.6f;
        } else {
            float wet = 0.35f + 0.45f * m->amount;
            in = x;
            y = (x + del * wet) / (1.0f + 0.5f * wet);
        }
        in *= 8192.0f;
        s->line[w & (NFM_MOD_LEN - 1)] = (int16_t)(in > 32767.0f ? 32767.0f : in < -32768.0f ? -32768.0f : in);
        s->w = (uint16_t)(w + 1u);
        return mod_match(s, x, y, i);
    }
    case NFM_MOD_RING:
        return mod_match(s, x, x + (x * sin_cyc(m->car + m->c_inc * fi) - x) * (0.3f + 0.7f * m->amount), i);
    case NFM_MOD_TREMOLO:      /* its average loudness made up, nearly */
        return x * (1.0f - m->amount * (m->l0 + m->dl * fi)) * (1.0f + 0.5f * m->amount);
    }
    return x;
}

NFM_CODE void nfm_set_mod_lines(nfm_engine *e, int16_t *lines)
{
    for (int v = 0; v < NFM_VOICES; ++v) {
        e->voice[v].ms.line = lines ? lines + v * NFM_MOD_LEN : 0;
        nfm_mod_reset(&e->voice[v].ms);
    }
}

NFM_CODE int nfm_voice_alive(const nfm_engine *e, int voice, uint32_t age)
{
    return voice >= 0 && voice < NFM_VOICES && e->voice[voice].slot >= 0 &&
           !e->voice[voice].fading && e->voice[voice].age == age;
}

NFM_CODE uint32_t nfm_voice_age(const nfm_engine *e, int voice)
{
    return voice >= 0 && voice < NFM_VOICES ? e->voice[voice].age : 0;
}

#define CTL_STEP 8

NFM_CODE static void voice_control(nfm_engine *e, nfm_voice *v)
{
    float pk = v->pitch_k, k8 = pk * pk;
    k8 *= k8;
    k8 *= k8;
    v->pitch_env *= k8;
    float sweep = v->pitch_depth * v->pitch_env;
    float fc = v->f0 * exp2_approx(sweep) * e->inv_sr;
    float fm = v->f0 * exp2_approx(sweep * v->sweep_mods) * e->inv_sr;
    float loud = 0.0f;
    for (int k = 0; k < NFM_OPS; ++k) {
        int c = (v->carriers >> k) & 1;
        float inc = (c ? fc : fm) * v->ratio[k];
        v->inc[k] = inc > 0.45f ? 0.45f : inc;
        if (c) loud += v->env[k];
    }

    /* A voice ends when every source is silent and the rings have died. */
    if ((v->fading && v->fade < 1e-4f) ||
        (loud < 1e-4f && v->noise < 1e-4f && !v->bursts && v->peak < 3e-5f)) {
        v->slot = -1;
        return;
    }
    v->peak = 0.0f;
    v->ctl = CTL_STEP;
    if (v->ins & NFM_INS_DRIVE) nfm_sat_control(&v->sat);
}

/* One operator: sine of phase plus modulation, times its envelope. */
#define OP(k, m) (sin_cyc(v->ph[k] + (m)) * v->env[k])

NFM_CODE static inline float voice_tick(const nfm_engine *e, nfm_voice *v, uint32_t *rng)
{
    /* Noise first: it is heard, and it can shake op 1's phase. */
    float nz = 0.0f;
    if (v->bursts) {                              /* a clap: a burst falls fast, the next comes */
        v->noise *= v->clap_k;
        if (--v->clap_t == 0) {
            v->noise = v->clap_amp * (0.75f + 0.25f * rng_uni(rng));
            v->clap_t = (uint16_t)(v->clap_gap * (0.8f + 0.4f * rng_uni(rng)));
            if (--v->bursts == 0) v->noise = v->clap_amp;   /* the last: the tail */
        }
    }
    if (v->noise > 1e-5f) {
        float w = (float)(int32_t)rng_next(rng) * (1.0f / 2147483648.0f);
        float g = v->ng, k = v->nk;
        float hp = (w - (k + g) * v->ns1 - v->ns2) / (1.0f + g * (g + k));
        float bp = g * hp + v->ns1;
        v->ns1 = g * hp + bp;
        float lp = g * bp + v->ns2;
        v->ns2 = g * bp + lp;
        nz = (lp + (bp * 2.0f - lp) * v->nband) * v->noise;
        if (!v->bursts) v->noise *= v->noise_k;
    }
    float nfm = nz * v->noise_fm;

    /* Op 4 with averaged self feedback (DX style); in "ring" op 1 feeds it. */
    float s4 = sin_cyc(v->ph[3] + (v->algo == 5 ? v->feedback * v->ring : v->feedback * (v->fb1 + v->fb2)));
    v->fb2 = v->fb1;
    v->fb1 = s4;
    float o4 = s4 * v->env[3], o3, o2, o1, out;
    switch (v->algo) {
    default: /* stack */
    case 0: o3 = OP(2, o4); o2 = OP(1, o3); o1 = OP(0, o2 + nfm); out = o1; break;
    case 1: o3 = OP(2, o4); o2 = OP(1, 0.0f); o1 = OP(0, o2 + o3 + nfm); out = o1; break;
    case 2: o3 = OP(2, o4); o2 = OP(1, 0.0f); o1 = OP(0, o2 + nfm); out = o1 + o3; break;
    case 3: o3 = OP(2, o4); o2 = OP(1, o4); o1 = OP(0, o4 + nfm); out = o1 + o2 + o3; break;
    case 4: o3 = OP(2, 0.0f); o2 = OP(1, 0.0f); o1 = OP(0, o2 + o3 + o4 + nfm); out = o1; break;
    case 5:
        o3 = OP(2, o4); o2 = OP(1, o3);
        {
            float s1 = sin_cyc(v->ph[0] + o2 + nfm);
            v->ring = s1;
            o1 = s1 * v->env[0];
        }
        out = o1;
        break;
    case 6: o3 = OP(2, o4); o2 = OP(1, o3); o1 = OP(0, o3 + nfm); out = o1 + o2; break;
    case 7: o3 = OP(2, 0.0f); o2 = OP(1, 0.0f); o1 = OP(0, nfm); out = o1 + o2 + o3 + o4; break;
    }
    for (int k = 0; k < NFM_OPS; ++k) {
        v->ph[k] = wrap1(v->ph[k] + v->inc[k]);
        v->env[k] *= v->env_k[k];
    }

    if (v->att < 1.0f) {
        v->att += v->att_inc;
        if (v->att > 1.0f) v->att = 1.0f;
    }
    float x = (out + nz) * v->att;

    if (v->fold > 0.01f) {
        float folded = sin_cyc(x * (1.0f + v->fold * 6.0f) * 0.25f);
        x += (folded - x) * v->fold;
    }
    if (v->crush > 0.01f) {
        v->crush_acc += v->crush_rate;
        if (v->crush_acc >= 1.0f) {
            v->crush_acc -= 1.0f;
            v->crush_hold = (float)(int)(x * v->crush_q) / v->crush_q;
        }
        x = v->crush_hold;
    }

    /* Resonator: bandpass modes excited by the voice (TPT SVF). */
    if (v->res_mix > 0.001f) {
        float r = 0.0f;
        for (int m = 0; m < NFM_MODES; ++m) {
            if (v->ramp[m] == 0.0f) continue;
            float g = v->rg[m], k = v->rk[m];
            float hp = (x - (k + g) * v->rs1[m] - v->rs2[m]) / (1.0f + g * (g + k));
            float bp = g * hp + v->rs1[m];
            v->rs1[m] = g * hp + bp;
            float lp = g * bp + v->rs2[m];
            v->rs2[m] = g * bp + lp;
            r += bp * v->ramp[m];
        }
        x = x * (1.0f - 0.6f * v->res_mix) + r * v->res_mix;
    }

    /* Inserts for this hit: drive, then a filter whose settings move slowly. */
    if (v->ins & NFM_INS_DRIVE) x = nfm_sat_tick(&v->sat, x);
    if (v->ins & NFM_INS_FILTER) {
        float g = e->ins_g, k = e->ins_k;
        float hp = (x - (k + g) * v->ins_s1 - v->ins_s2) / (1.0f + g * (g + k));
        float bp = g * hp + v->ins_s1;
        v->ins_s1 = g * hp + bp;
        float lp = g * bp + v->ins_s2;
        v->ins_s2 = g * bp + lp;
        x = lp + (bp * 1.5f - lp) * e->ins_morph;
    }

    if (v->fading) {
        x *= v->fade;
        v->fade *= v->fade_k;
    }
    float a = x < 0.0f ? -x : x;
    if (a > v->peak) v->peak = a;
    return x;
}

NFM_CODE void nfm_render(nfm_engine *e, float *out_l, float *out_r, int n)
{
    nfm_render_bus(e, out_l, out_r, 0, 0, n);
    for (int i = 0; i < n; ++i) {
        out_l[i] = soft_clip(out_l[i]);
        out_r[i] = soft_clip(out_r[i]);
    }
}

NFM_CODE void nfm_render_bus(nfm_engine *e, float *out_l, float *out_r, float *rev, float *dly, int n)
{
    nfm_zero(out_l, (unsigned)n * 4u);
    nfm_zero(out_r, (unsigned)n * 4u);
    if (rev) nfm_zero(rev, (unsigned)n * 4u);
    if (dly) nfm_zero(dly, (unsigned)n * 4u);
    for (int si = 0; si < NFM_SLOTS; ++si) nfm_mod_block(&e->mod[si], e->sr, n);
    for (int vi = 0; vi < NFM_VOICES; ++vi) {
        nfm_voice *v = &e->voice[vi];
        for (int i = 0; i < n && v->slot >= 0; ++i) {
            if (v->ctl == 0) {
                voice_control(e, v);
                if (v->slot < 0) break;
            }
            --v->ctl;
            float x = voice_tick(e, v, &e->rng);
            if (v->mod_on) x = nfm_mod_tick(&e->mod[v->slot], &v->ms, x, i);
            out_l[i] += x * v->gain_l;
            out_r[i] += x * v->gain_r;
            float mono = x * (v->gain_l + v->gain_r) * 0.5f;
            if (rev && v->send_rev > 0.0f) rev[i] += mono * v->send_rev;
            if (dly && v->send_dly > 0.0f) dly[i] += mono * v->send_dly;
        }
    }
    float g = e->master;
    for (int i = 0; i < n; ++i) {
        out_l[i] *= g;
        out_r[i] *= g;
    }
    if (rev) for (int i = 0; i < n; ++i) rev[i] *= g;
    if (dly) for (int i = 0; i < n; ++i) dly[i] *= g;
}

NFM_CODE void nfm_mix_q23(nfm_engine *e, int32_t *io, int frames, float gain)
{
    float l[64], r[64];
    while (frames > 0) {
        int n = frames < 64 ? frames : 64;
        nfm_render(e, l, r, n);
        for (int i = 0; i < n; ++i) {
            int32_t a = io[2 * i] + (int32_t)(l[i] * gain * 8388607.0f);
            int32_t b = io[2 * i + 1] + (int32_t)(r[i] * gain * 8388607.0f);
            io[2 * i] = a > 8388607 ? 8388607 : a < -8388608 ? -8388608 : a;
            io[2 * i + 1] = b > 8388607 ? 8388607 : b < -8388608 ? -8388608 : b;
        }
        io += 2 * n;
        frames -= n;
    }
}

/* nfx: drifting effects, see nfx.h. */
#include "nfx.h"

NFM_CODE static inline uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
NFM_CODE static inline float rndf(uint32_t *s) { return (float)(rnd(s) >> 8) * (1.0f / 16777216.0f); }
NFM_CODE static inline float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

NFM_CODE static float exp2f_(float x)
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
/* tan(pi x) for 0 < x < 0.5, enough for filter coefficients */
NFM_CODE static float tan_pi(float x)
{
    if (x > 0.3f) x = 0.3f;
    float t = x * 3.14159265f, t2 = t * t;
    return t * (1.0f + t2 * (0.33333333f + t2 * (0.13333333f + t2 * (0.05396825f + t2 * 0.02186949f))));
}
NFM_CODE static inline float soft(float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* SPACE's stations: delay times in sixteenths, reverbs (T60 s, damping Hz) from a small room to a big
 * hall; the home one of each first */
#define DL_N 4
#define RV_N 5
NFM_DATA static const float dl_st[DL_N] = { 3.0f, 2.0f, 1.0f, 4.0f };
NFM_DATA static const float rv_st[RV_N][2] = {
    { 2.1f, 3900.0f }, { 0.45f, 3500.0f }, { 0.9f, 5500.0f }, { 3.2f, 5000.0f }, { 4.5f, 7500.0f } };

NFM_CODE void nfx_init(nfx *fx, float sr, uint32_t seed)
{
    nfm_zero(fx, sizeof *fx);
    fx->sr = sr;
    fx->rng = seed ? seed : 0x85ebca6bu;
    fx->bpm = 112.0f;
    fx->chaos = 0.5f;
    fx->motion = 0.3f;
    NFM_DATA static const float amount[NFX_EFFECTS] = { 0.35f, 0.25f, 0.3f, 0.3f };
    for (int i = 0; i < NFX_EFFECTS; ++i) {
        fx->on[i] = 1;
        fx->amount[i] = amount[i];
    }
    /* base, span: where each setting rests and how far CHAOS 1 moves it */
    NFM_DATA static const float ms[NFX_MODS][2] = {
        [NFX_M_CUTOFF] = { 0.55f, 0.40f }, [NFX_M_RESO] = { 0.35f, 0.30f },
        [NFX_M_MORPH] = { 0.25f, 0.35f },  [NFX_M_DRIVE] = { 0.40f, 0.35f },
        [NFX_M_DTIME] = { 0.45f, 0.0f },   [NFX_M_DFEEDBACK] = { 0.45f, 0.30f },
        [NFX_M_DTONE] = { 0.50f, 0.35f },  [NFX_M_RDECAY] = { 0.45f, 0.0f },
        [NFX_M_RDAMP] = { 0.50f, 0.0f },
    };
    for (int m = 0; m < NFX_MODS; ++m) {
        fx->mod[m].base = fx->mod[m].cur = fx->mod[m].target = ms[m][0];
        fx->mod[m].span = ms[m][1];
    }
    NFM_DATA static const uint16_t lens[NFX_REV_LINES] = { 1559, 1867, 2251, 2687 };
    for (int l = 0; l < NFX_REV_LINES; ++l) {
        float n = (float)lens[l] * sr / 44100.0f;
        fx->len[l] = (uint16_t)(n > NFX_REV_LEN - 1 ? NFX_REV_LEN - 1 : n);
    }
    fx->dtime = fx->dtime2 = sr * 60.0f / fx->bpm / 4.0f * 3.0f;
    fx->rv_x = fx->dl_x = 1.0f;
    fx->bar_len = sr * 60.0f / fx->bpm * 4.0f;
}

NFM_CODE void nfx_set_on(nfx *fx, int i, int on) { if (i >= 0 && i < NFX_EFFECTS) fx->on[i] = on ? 1 : 0; }
NFM_CODE void nfx_set_amount(nfx *fx, int i, float a) { if (i >= 0 && i < NFX_EFFECTS) fx->amount[i] = clamp01(a); }
NFM_CODE void nfx_set_chaos(nfx *fx, float c) { fx->chaos = clamp01(c); }
NFM_CODE void nfx_set_motion(nfx *fx, float m) { fx->motion = clamp01(m); }
NFM_CODE void nfx_set_tempo(nfx *fx, float bpm) { fx->bpm = bpm < 20.0f ? 20.0f : bpm; }
NFM_CODE void nfx_set_space(nfx *fx, float s) { fx->space = clamp01(s); }

/* a glide to a station over bars bars; from home anywhere else, elsewhere home now and then */
NFM_CODE static uint8_t next_station(nfx *fx, uint8_t at, int n)
{
    if (at != 0 && rndf(&fx->rng) < 0.4f) return 0;
    return (uint8_t)((at + 1u + rnd(&fx->rng) % (uint32_t)(n - 1)) % (uint32_t)n);
}
NFM_CODE static void shift(nfx *fx, int reverb, uint8_t to, int bars)
{
    float inc = 1.0f / ((float)bars * fx->bar_len);
    if (reverb && fx->rv_x >= 1.0f) {            /* (only from a station: a glide goes on to its end) */
        fx->rv_from = fx->rv_to;
        fx->rv_to = to;
        fx->rv_x = 0.0f;
        fx->rv_inc = inc;
        fx->rv_stay = 0;
    } else if (!reverb && fx->dl_x >= 1.0f) {
        fx->dl_from = fx->dl_to;
        fx->dl_to = to;
        fx->dl_x = 0.0f;
        fx->dl_inc = inc;
        fx->dl_stay = 0;
    }
}

/* A bar: at SPACE 1 a station is left after 4 bars at the least, by a chance of 1 in 4 a bar (about
 * every 8 bars); at 0.5 after 10, 1 in 8. At 0 everything goes home, over 2 bars. */
NFM_CODE void nfx_bar(nfx *fx, float bar_len)
{
    float s = fx->space;
    fx->bar_len = bar_len > 1.0f ? bar_len : fx->bar_len;
    if (fx->rv_stay < 60000u) fx->rv_stay++;
    if (fx->dl_stay < 60000u) fx->dl_stay++;
    if (s <= 0.0f) {
        if (fx->rv_to != 0 && fx->rv_x >= 1.0f) shift(fx, 1, 0, 2);
        if (fx->dl_to != 0 && fx->dl_x >= 1.0f) shift(fx, 0, 0, 2);
        return;
    }
    float stay = 16.0f - 12.0f * s, p = 0.25f * s;
    if (fx->rv_x >= 1.0f && (float)fx->rv_stay >= stay && rndf(&fx->rng) < p)
        shift(fx, 1, next_station(fx, fx->rv_to, RV_N), 1 + (int)(rnd(&fx->rng) % 4u));
    if (fx->dl_x >= 1.0f && (float)fx->dl_stay >= stay && rndf(&fx->rng) < p)
        shift(fx, 0, next_station(fx, fx->dl_to, DL_N), 1 + (int)(rnd(&fx->rng) % 4u));
}

NFM_CODE void nfx_space_shift(nfx *fx)
{
    if (fx->space <= 0.0f || rndf(&fx->rng) >= fx->space) return;
    shift(fx, 1, next_station(fx, fx->rv_to, RV_N), 1);
    shift(fx, 0, next_station(fx, fx->dl_to, DL_N), 1);
}

NFM_CODE float nfx_mod_value(const nfx *fx, int m) { return m >= 0 && m < NFX_MODS ? fx->mod[m].cur : 0.0f; }

NFM_CODE void nfx_set_slot(nfx *fx, int slot, int effect, float amount)
{
    if (slot >= 0 && slot < NFM_SLOTS && effect >= 0 && effect < NFX_EFFECTS)
        fx->slot_amt[slot][effect] = clamp01(amount);
}

NFM_CODE void nfx_route(nfx *fx, const float amt[NFX_EFFECTS], int *filter, float *drive, float send[2])
{
    *filter = fx->on[NFX_FILTER] && rndf(&fx->rng) < amt[NFX_FILTER] * fx->amount[NFX_FILTER];
    *drive = fx->on[NFX_DRIVE] ? amt[NFX_DRIVE] * fx->amount[NFX_DRIVE] : 0.0f;
    /* the sends move: hit by hit (step by step) any level up to the one set, now and then none */
    for (int w = 0; w < 2; ++w) {
        float a = fx->on[w ? NFX_REVERB : NFX_DELAY] ? amt[w ? NFX_REVERB : NFX_DELAY] : 0.0f;
        send[w] = a > 0.0f && rndf(&fx->rng) >= 0.3f ? a * (0.15f + 0.85f * rndf(&fx->rng)) : 0.0f;
    }
}

NFM_CODE int nfx_trigger(nfx *fx, nfm_engine *e, int slot, float vel, float transpose)
{
    int v = nfm_trigger(e, slot, vel, transpose), filter;
    float drive, send[2];
    if (v < 0) return v;
    nfx_route(fx, fx->slot_amt[slot], &filter, &drive, send);
    nfm_route(e, v, (filter ? NFM_INS_FILTER : 0) | (drive > 0.0f ? NFM_INS_DRIVE : 0), send[1], send[0], drive);
    return v;
}

NFM_CODE void nfx_set_source(nfx *fx, void (*src)(void *, float *, float *, float *, float *, int), void *ctx)
{
    fx->src = src;
    fx->src_ctx = ctx;
}

/* Drift: now and then a new target near the base; glide towards it. */
NFM_CODE static void drift(nfx *fx, float dt)
{
    if (fx->motion <= 0.0f) return;
    float period = 12.0f * exp2f_(-fx->motion * 4.0f); /* 12 s .. 0.75 s */
    float glide = 1.0f - exp2f_(-1.4427f * dt / (period * 0.6f));
    for (int m = 0; m < NFX_MODS; ++m) {
        nfx_mod_state *s = &fx->mod[m];
        if (rndf(&fx->rng) < dt / period)
            s->target = clamp01(s->base + (rndf(&fx->rng) * 2.0f - 1.0f) * s->span * fx->chaos);
        s->cur += (s->target - s->cur) * glide;
    }
}

NFM_CODE static void update(nfx *fx, nfm_engine *e, int n)
{
    float sr = fx->sr;
    drift(fx, (float)n / sr);
    const nfx_mod_state *m = fx->mod;

    /* Filter inserts: cutoff 150 Hz..10 kHz, resonance, lowpass->bandpass. */
    float fc = 150.0f * exp2f_(m[NFX_M_CUTOFF].cur * 6.06f);
    e->ins_g = tan_pi(fc / sr);
    e->ins_k = 2.0f - 1.85f * m[NFX_M_RESO].cur;
    e->ins_morph = m[NFX_M_MORPH].cur;
    fc = 150.0f * exp2f_(m[NFX_M_CUTOFF].base * 6.06f);   /* ...and at rest (the direct bus) */
    e->ins_g0 = tan_pi(fc / sr);
    e->ins_k0 = 2.0f - 1.85f * m[NFX_M_RESO].base;
    e->ins_morph0 = m[NFX_M_MORPH].base;

    /* Delay: tempo-synced; SPACE glides the time from one station to the next like an analog delay,
     * so the repeats bend in pitch on the way (smoothly at both ends). The render loop follows the
     * goal like tape, so a new tempo glides too. */
    float six = sr * 60.0f / fx->bpm / 4.0f;
    float t1 = six * dl_st[fx->dl_from], t2 = six * dl_st[fx->dl_to];
    /* Too long for the buffer (slow tempo, small FM1 buffer): halve, so it stays on the grid. */
    while (t1 > NFX_DELAY_LEN - 2) t1 *= 0.5f;
    while (t2 > NFX_DELAY_LEN - 2) t2 *= 0.5f;
    float dx = fx->dl_x * fx->dl_x * (3.0f - 2.0f * fx->dl_x);
    fx->dtime2 = t1 + (t2 - t1) * dx;
    if (fx->dl_x < 1.0f && (fx->dl_x += fx->dl_inc * (float)n) >= 1.0f) {
        fx->dl_x = 1.0f;
        fx->dl_from = fx->dl_to;
    }

    /* Reverb: SPACE's rooms, gliding (smoothly at both ends) */
    float x = fx->rv_x * fx->rv_x * (3.0f - 2.0f * fx->rv_x);
    const float *a = rv_st[fx->rv_from], *b = rv_st[fx->rv_to];
    float t60 = a[0] + (b[0] - a[0]) * x, damp = a[1] + (b[1] - a[1]) * x;
    for (int l = 0; l < NFX_REV_LINES; ++l)
        fx->rgain[l] = exp2f_(-9.9657843f * (float)fx->len[l] / (t60 * sr));
    fx->rdamp = 1.0f - exp2f_(-9.0647202f * damp / sr);
    if (fx->rv_x < 1.0f && (fx->rv_x += fx->rv_inc * (float)n) >= 1.0f) {
        fx->rv_x = 1.0f;
        fx->rv_from = fx->rv_to;
    }
}

NFM_CODE static inline float delay_read(const int16_t *buf, uint32_t pos, float t)
{
    int ti = (int)t;
    float fr = t - (float)ti;
    uint32_t a = (pos - (uint32_t)ti) & (NFX_DELAY_LEN - 1), b = (a - 1u) & (NFX_DELAY_LEN - 1);
    return ((float)buf[a] + ((float)buf[b] - (float)buf[a]) * fr) * (1.0f / 16384.0f);
}
NFM_CODE static inline int16_t to16(float x)
{
    x *= 16384.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32768.0f ? -32768.0f : x);
}

#ifdef NFX_REV16
#define REV_GET(v) ((float)(v) * (1.0f / 8192.0f))
NFM_CODE static inline int16_t rev_put(float x)
{
    x *= 8192.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32768.0f ? -32768.0f : x);
}
#else
#define REV_GET(v) (v)
#define rev_put(x) (x)
#endif

NFM_CODE void nfx_render(nfx *fx, nfm_engine *e, float *out_l, float *out_r, int n)
{
    /* Static, not on the stack: on the FM1 this runs in the audio interrupt. */
    static float rev[64], dly[64];
    while (n > 0) {
        int c = n < 64 ? n : 64;
        float *dl = e->dir_l, *dr = e->dir_r;
        update(fx, e, c);
        nfm_render_bus(e, out_l, out_r, rev, dly, c);
        if (fx->src) fx->src(fx->src_ctx, out_l, out_r, rev, dly, c);

        float fb = 0.15f + 0.5f * fx->mod[NFX_M_DFEEDBACK].cur;
        float kt = 1.0f - exp2f_(-1.4427f / (fx->sr * 0.15f));
        float dtone = 1.0f - exp2f_(-9.0647202f * (1200.0f * exp2f_(fx->mod[NFX_M_DTONE].cur * 2.9f)) / fx->sr);
        float dret = fx->on[NFX_DELAY] ? fx->amount[NFX_DELAY] : 0.0f;    /* the returns */
        float rret = fx->on[NFX_REVERB] ? 1.2f * fx->amount[NFX_REVERB] : 0.0f;
        for (int i = 0; i < c; ++i) {
            /* Ping-pong delay: input to the left, each side feeds the other; the time follows its
             * goal like tape (0.15 s) */
            fx->dtime += (fx->dtime2 - fx->dtime) * kt;
            float yl = delay_read(fx->dl, fx->dpos, fx->dtime);
            float yr = delay_read(fx->dr, fx->dpos, fx->dtime);
            fx->dlp_l += dtone * (yr - fx->dlp_l);
            fx->dlp_r += dtone * (yl - fx->dlp_r);
            fx->dpos = (fx->dpos + 1u) & (NFX_DELAY_LEN - 1);
            fx->dl[fx->dpos] = to16(soft(dly[i] + fx->dlp_l * fb));
            fx->dr[fx->dpos] = to16(soft(fx->dlp_r * fb));

            /* Reverb: two allpasses, then a Hadamard-mixed delay network. */
            float x = rev[i];
            {
                float z = fx->ap1[fx->ap1_pos], u = x + 0.6f * z;
                fx->ap1[fx->ap1_pos] = u;
                x = z - 0.6f * u;
                if (++fx->ap1_pos >= NFX_AP1) fx->ap1_pos = 0;
                z = fx->ap2[fx->ap2_pos];
                u = x + 0.6f * z;
                fx->ap2[fx->ap2_pos] = u;
                x = z - 0.6f * u;
                if (++fx->ap2_pos >= NFX_AP2) fx->ap2_pos = 0;
            }
            float y[NFX_REV_LINES];
            for (int l = 0; l < NFX_REV_LINES; ++l) {
                y[l] = REV_GET(fx->line[l][fx->rpos[l]]);
                fx->lp[l] += fx->rdamp * (y[l] - fx->lp[l]);
            }
            float a = fx->lp[0], b = fx->lp[1], cc = fx->lp[2], d = fx->lp[3];
            float h[NFX_REV_LINES] = { a + b + cc + d, a - b + cc - d, a + b - cc - d, a - b - cc + d };
            for (int l = 0; l < NFX_REV_LINES; ++l) {
                fx->line[l][fx->rpos[l]] = rev_put(x + 0.5f * h[l] * fx->rgain[l]);
                if (++fx->rpos[l] >= fx->len[l]) fx->rpos[l] = 0;
            }
            float rl = (y[0] + y[2]) * 0.5f, rr = (y[1] + y[3]) * 0.5f;

            float ol = out_l[i] + yl * dret + rl * rret, or_ = out_r[i] + yr * dret + rr * rret;
            out_l[i] = soft(ol);
            out_r[i] = soft(or_);
            if (dl) {                            /* the direct bus: its share of the clip of the sum */
                dl[i] = soft(ol + dl[i]) - out_l[i];
                dr[i] = soft(or_ + dr[i]) - out_r[i];
            }
        }
        if (dl) {
            e->dir_l = dl + c;
            e->dir_r = dr + c;
        }
        fx->now += (uint32_t)c;
        out_l += c;
        out_r += c;
        n -= c;
    }
}

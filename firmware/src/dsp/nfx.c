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

/* How likely each kind is to get an effect: kicks stay out of the reverb
 * and delay, hats and percussion go into the delay readily. */
NFM_DATA static const float weight[NFX_EFFECTS][NFM_KINDS] = {
    /*            kick  snare  hat   perc  click  metal */
    [NFX_FILTER] = { 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    [NFX_DRIVE]  = { 0.8f, 1.0f, 0.6f, 0.8f, 1.0f, 0.5f },
    [NFX_DELAY]  = { 0.2f, 0.6f, 1.0f, 1.0f, 1.0f, 0.8f },
    [NFX_REVERB] = { 0.15f, 0.8f, 0.5f, 1.0f, 0.9f, 1.0f },
};

/* Delay times on offer, in sixteenths. */
NFM_DATA static const float dsteps[] = { 2.0f, 3.0f, 4.0f, 6.0f };

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
        [NFX_M_DTIME] = { 0.45f, 0.50f },  [NFX_M_DFEEDBACK] = { 0.45f, 0.30f },
        [NFX_M_DTONE] = { 0.50f, 0.35f },  [NFX_M_RDECAY] = { 0.45f, 0.35f },
        [NFX_M_RDAMP] = { 0.50f, 0.35f },
    };
    for (int m = 0; m < NFX_MODS; ++m) {
        fx->mod[m].base = fx->mod[m].cur = fx->mod[m].target = ms[m][0];
        fx->mod[m].span = ms[m][1];
    }
    for (int w = 0; w < 2; ++w) { fx->owner_voice[w] = -1; fx->owner_slot[w] = -1; }
    NFM_DATA static const uint16_t lens[NFX_REV_LINES] = { 1559, 1867, 2251, 2687 };
    for (int l = 0; l < NFX_REV_LINES; ++l) {
        float n = (float)lens[l] * sr / 44100.0f;
        fx->len[l] = (uint16_t)(n > NFX_REV_LEN - 1 ? NFX_REV_LEN - 1 : n);
    }
    fx->dtime = sr * 60.0f / fx->bpm / 4.0f * 3.0f;
}

NFM_CODE void nfx_set_on(nfx *fx, int i, int on) { if (i >= 0 && i < NFX_EFFECTS) fx->on[i] = on ? 1 : 0; }
NFM_CODE void nfx_set_amount(nfx *fx, int i, float a) { if (i >= 0 && i < NFX_EFFECTS) fx->amount[i] = clamp01(a); }
NFM_CODE void nfx_set_chaos(nfx *fx, float c) { fx->chaos = clamp01(c); }
NFM_CODE void nfx_set_motion(nfx *fx, float m) { fx->motion = clamp01(m); }
NFM_CODE void nfx_set_tempo(nfx *fx, float bpm) { fx->bpm = bpm < 20.0f ? 20.0f : bpm; }
NFM_CODE float nfx_mod_value(const nfx *fx, int m) { return m >= 0 && m < NFX_MODS ? fx->mod[m].cur : 0.0f; }

/* Owner of delay (0) or reverb (1): held while its voice still sounds,
 * but never longer than 1.5 s, so a long ring cannot keep the bus forever. */
NFM_CODE static int owner_held(const nfx *fx, const nfm_engine *e, int w)
{
    if (fx->owner_voice[w] == NFX_EXT) return fx->now - fx->owner_since[w] < fx->owner_hold[w];
    return fx->owner_voice[w] >= 0 &&
           nfm_voice_alive(e, fx->owner_voice[w], fx->owner_age[w]) &&
           fx->now - fx->owner_since[w] < (uint32_t)(fx->sr * 1.5f);
}

NFM_CODE int nfx_owner(const nfx *fx, nfm_engine *e, int w)
{
    return w >= 0 && w < 2 && owner_held(fx, e, w) ? fx->owner_slot[w] : -1;
}

NFM_CODE int nfx_trigger(nfx *fx, nfm_engine *e, int slot, float vel, float transpose)
{
    int v = nfm_trigger(e, slot, vel, transpose);
    if (v < 0) return v;
    int kind = nfm_slot_kind(e, slot);
    if (kind < 0 || kind >= NFM_KINDS) kind = NFM_KIND_PERC;
    int ins = 0;
    float send[2] = { 0.0f, 0.0f };
    if (fx->on[NFX_FILTER] && rndf(&fx->rng) < fx->amount[NFX_FILTER] * weight[NFX_FILTER][kind]) ins |= NFM_INS_FILTER;
    if (fx->on[NFX_DRIVE] && rndf(&fx->rng) < fx->amount[NFX_DRIVE] * weight[NFX_DRIVE][kind]) ins |= NFM_INS_DRIVE;
    for (int w = 0; w < 2; ++w) {
        int effect = w ? NFX_REVERB : NFX_DELAY;
        if (!fx->on[effect] || owner_held(fx, e, w)) continue;
        if (rndf(&fx->rng) >= fx->amount[effect] * weight[effect][kind]) continue;
        send[w] = 0.6f + 0.4f * rndf(&fx->rng);
        fx->owner_voice[w] = v;
        fx->owner_age[w] = nfm_voice_age(e, v);
        fx->owner_since[w] = fx->now;
        fx->owner_slot[w] = (int8_t)slot;
    }
    nfm_route(e, v, ins, send[1], send[0]);
    return v;
}

NFM_CODE void nfx_choose(nfx *fx, nfm_engine *e, const float w8[NFX_EFFECTS], int slot, uint32_t hold,
                         int *ins, float send[2])
{
    *ins = 0;
    send[0] = send[1] = 0.0f;
    if (fx->on[NFX_FILTER] && rndf(&fx->rng) < fx->amount[NFX_FILTER] * w8[NFX_FILTER]) *ins |= NFM_INS_FILTER;
    if (fx->on[NFX_DRIVE] && rndf(&fx->rng) < fx->amount[NFX_DRIVE] * w8[NFX_DRIVE]) *ins |= NFM_INS_DRIVE;
    for (int w = 0; w < 2; ++w) {
        int effect = w ? NFX_REVERB : NFX_DELAY;
        if (!fx->on[effect]) continue;
        if (owner_held(fx, e, w) && !(fx->owner_voice[w] == NFX_EXT && fx->owner_slot[w] == slot)) continue;
        if (rndf(&fx->rng) >= fx->amount[effect] * w8[effect]) continue;
        send[w] = 0.4f + 0.4f * rndf(&fx->rng);
        fx->owner_voice[w] = NFX_EXT;
        fx->owner_since[w] = fx->now;
        fx->owner_hold[w] = hold;
        fx->owner_slot[w] = (int8_t)slot;
    }
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
    /* Drive 1.5..10x, roughly level-matched. */
    float d = 1.5f + 8.5f * m[NFX_M_DRIVE].cur * (0.4f + 0.6f * fx->amount[NFX_DRIVE]);
    e->ins_drive = d;
    e->ins_comp = 1.0f / (0.6f + 0.4f * d * 0.35f);

    /* Delay time: a tempo-synced choice; changes glide like tape. */
    int pick = (int)(m[NFX_M_DTIME].cur * 3.999f);
    float target = sr * 60.0f / fx->bpm / 4.0f * dsteps[pick];
    /* Too long for the buffer (slow tempo, small FM1 buffer): halve, so it stays on the grid. */
    while (target > NFX_DELAY_LEN - 2) target *= 0.5f;
    fx->dtime += (target - fx->dtime) * (1.0f - exp2f_(-1.4427f * (float)n / (sr * 0.15f)));

    /* Reverb: 0.6..4 s, damping 1.5..10 kHz. */
    float t60 = 0.6f + 3.4f * m[NFX_M_RDECAY].cur;
    for (int l = 0; l < NFX_REV_LINES; ++l)
        fx->rgain[l] = exp2f_(-9.9657843f * (float)fx->len[l] / (t60 * sr));
    float damp = 1500.0f * exp2f_(m[NFX_M_RDAMP].cur * 2.74f);
    fx->rdamp = 1.0f - exp2f_(-9.0647202f * damp / sr);
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
        update(fx, e, c);
        nfm_render_bus(e, out_l, out_r, rev, dly, c);
        if (fx->src) fx->src(fx->src_ctx, out_l, out_r, rev, dly, c);

        float fb = 0.15f + 0.5f * fx->mod[NFX_M_DFEEDBACK].cur;
        float dtone = 1.0f - exp2f_(-9.0647202f * (1200.0f * exp2f_(fx->mod[NFX_M_DTONE].cur * 2.9f)) / fx->sr);
        float dret = 0.35f + 0.5f * fx->amount[NFX_DELAY];
        float rret = 0.3f + 0.6f * fx->amount[NFX_REVERB];
        for (int i = 0; i < c; ++i) {
            /* Ping-pong delay: input to the left, each side feeds the other. */
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

            out_l[i] = soft(out_l[i] + yl * dret + rl * rret);
            out_r[i] = soft(out_r[i] + yr * dret + rr * rret);
        }
        fx->now += (uint32_t)c;
        out_l += c;
        out_r += c;
        n -= c;
    }
}

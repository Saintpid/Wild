/* nacid: the acid line, see nacid.h. */
#include "nacid.h"

NFM_CODE static inline uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
NFM_CODE static inline float rndf(uint32_t *s) { return (float)(rnd(s) >> 8) * (1.0f / 16777216.0f); }

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
NFM_CODE static float tan_pi(float x)              /* tan(pi x), up to x 0.3 */
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
/* the step a band-limited saw or square takes at a wrap */
NFM_CODE static inline float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* minor, phrygian, dorian, minor pentatonic, blues, harmonic minor: what acid lines are made of */
NFM_DATA static const int8_t scales[NACID_SCALES][8] = {
    { 7, 0, 2, 3, 5, 7, 8, 10 }, { 7, 0, 1, 3, 5, 7, 8, 10 }, { 7, 0, 2, 3, 5, 7, 9, 10 },
    { 5, 0, 3, 5, 7, 10 },       { 6, 0, 3, 5, 6, 7, 10 },    { 7, 0, 2, 3, 5, 7, 8, 11 },
};

NFM_CODE void nacid_init(nacid *a, float sr, uint32_t seed)
{
    NFM_DATA static const float def[NACID_PARAMS] = {
        [NACID_P_DENSITY] = 0.6f, [NACID_P_SLIDES] = 0.2f, [NACID_P_ACCENTS] = 0.25f,
        [NACID_P_OCTAVES] = 0.15f, [NACID_P_EVOLVE] = 0.15f, [NACID_P_CUTOFF] = 0.3f,
        [NACID_P_RESO] = 0.7f, [NACID_P_ENVMOD] = 0.6f, [NACID_P_DECAY] = 0.4f,
        [NACID_P_ACCENT] = 0.6f, [NACID_P_LEVEL] = 0.7f,
    };
    nfm_zero(a, sizeof *a);
    a->sr = sr;
    a->rng = seed ? seed : 0x2c1b3c6du;
    for (int i = 0; i < NACID_PARAMS; ++i) a->p[i] = def[i];
    a->length = NACID_BAR;
    a->last = -1;
    a->glide = 1.0f - exp2f_(-1.4427f / (0.018f * sr));   /* a slide: ~60 ms to the note */
    a->freq = a->target = 65.4f;
    a->g = 0.1f;
    nacid_generate(a);
}

NFM_CODE static int8_t pick_note(nacid *a)
{
    const int8_t *sc = scales[a->scale < NACID_SCALES ? a->scale : 0];
    int n = 0;
    if (rndf(&a->rng) >= 0.4f) n = sc[1 + rnd(&a->rng) % (uint32_t)sc[0]];   /* else the root */
    if (rndf(&a->rng) < a->p[NACID_P_OCTAVES]) n += rndf(&a->rng) < 0.67f ? 12 : -12;
    return (int8_t)n;
}

NFM_CODE static uint8_t pick_flags(nacid *a)
{
    uint8_t f = NACID_ON;
    if (rndf(&a->rng) < a->p[NACID_P_ACCENTS]) f |= NACID_ACCENT;
    if (rndf(&a->rng) < a->p[NACID_P_SLIDES]) f |= NACID_SLIDE;
    return f;
}

/* Notes where the density says, likelier on the beat and almost always on the one */
NFM_CODE void nacid_generate(nacid *a)
{
    for (int s = 0; s < NACID_STEPS; ++s) {
        float p = a->p[NACID_P_DENSITY];
        if (s % NACID_BAR == 0) p = 0.9f;
        else if (s % 4 == 0) p = p * 1.3f > 0.95f ? 0.95f : p * 1.3f;
        a->step[s].flags = 0;
        a->step[s].note = 0;
        if (rndf(&a->rng) < p) {
            a->step[s].flags = pick_flags(a);
            a->step[s].note = pick_note(a);
        }
    }
}

/* one step changes: a new note mostly, else a note comes or goes, an accent or a slide flips */
NFM_CODE static void evolve(nacid *a)
{
    nacid_note *st = &a->step[rnd(&a->rng) % a->length];
    float r = rndf(&a->rng);
    if (r < 0.4f) {
        if (st->flags & NACID_ON) st->note = pick_note(a);
    } else if (r < 0.65f) {
        if (st->flags & NACID_ON) st->flags = 0;
        else { st->flags = pick_flags(a); st->note = pick_note(a); }
    } else if (r < 0.85f) {
        if (st->flags & NACID_ON) st->flags ^= NACID_ACCENT;
    } else if (st->flags & NACID_ON) {
        st->flags ^= NACID_SLIDE;
    }
}

NFM_CODE void nacid_set_length(nacid *a, int n)
{
    a->length = (uint8_t)(n < 1 ? 1 : n > NACID_STEPS ? NACID_STEPS : n);
    if (a->pos >= a->length) a->pos = 0;
}

NFM_CODE void nacid_set_step(nacid *a, int s, int flags, int note)
{
    if (s < 0 || s >= NACID_STEPS) return;
    a->step[s].flags = (uint8_t)(flags & 7);
    a->step[s].note = (int8_t)(note < -12 ? -12 : note > 24 ? 24 : note);
}

NFM_CODE void nacid_reset(nacid *a)
{
    a->pos = 0;
    a->last = -1;
}

NFM_CODE void nacid_stop(nacid *a)
{
    a->pending = 0;
    a->gate_on = 0;
    a->last = -1;
}

NFM_CODE void nacid_route(nacid *a, int ins, float rev, float dly)
{
    a->ins_f_to = ins & NFM_INS_FILTER ? 1.0f : 0.0f;
    a->ins_d_to = ins & NFM_INS_DRIVE ? 1.0f : 0.0f;
    a->rev_to = rev;
    a->dly_to = dly;
}

NFM_CODE static void play(nacid *a, nacid_note st, int tie)
{
    int legato = a->gate_on && a->sounding && a->gate < 0.0f;   /* the step before slid into this one */
    if (!(st.flags & NACID_ON)) {
        a->gate_on = 0;
        return;
    }
    a->target = 65.406f * exp2f_(((float)(a->root + st.note) + a->tune) * (1.0f / 12.0f));
    a->accent = (st.flags & NACID_ACCENT) != 0;
    if (a->accent) a->acc_env = 1.0f;
    if (!legato) {
        a->freq = a->target;
        a->env = 1.0f;
    }
    a->gate_on = 1;
    a->sounding = 1;
    a->gate = tie ? -1.0f : a->step_len * 0.55f;
}

NFM_CODE void nacid_step(nacid *a, float delay, float step_len)
{
    if (a->pending) play(a, a->next, a->next_tie);           /* (the one before, still waiting) */
    if (a->pos >= a->length) a->pos = 0;
    nacid_note st = a->step[a->pos];
    a->last = (int16_t)a->pos;
    if (++a->pos >= a->length) {
        a->pos = 0;
        if (rndf(&a->rng) < a->p[NACID_P_EVOLVE]) evolve(a);
    }
    if (a->mute) st.flags = 0;
    a->next = st;
    a->next_tie = (st.flags & NACID_SLIDE) && (a->step[a->pos].flags & NACID_ON) && !a->mute;
    a->step_len = step_len;
    a->at = a->now + (uint32_t)(delay < 0.0f ? 0.0f : delay);
    a->pending = 1;
}

NFM_CODE void nacid_hit(nacid *a, int note, int accent)
{
    nacid_note st = { (uint8_t)(NACID_ON | (accent ? NACID_ACCENT : 0)), (int8_t)note };
    a->gate_on = 0;                                           /* never a slide */
    if (a->step_len <= 0.0f) a->step_len = a->sr * 0.134f;
    play(a, st, 0);
}

/* the cutoff and the envelopes' rates, every 8 samples */
NFM_CODE static void control(nacid *a, float *dk)
{
    float base = 30.0f * exp2f_(a->p[NACID_P_CUTOFF] * 7.6f);          /* 30 Hz .. 5.8 kHz */
    float oct = a->p[NACID_P_ENVMOD] * 5.0f * a->env + a->p[NACID_P_ACCENT] * 2.5f * a->acc_env;
    float fc = base * exp2f_(oct);
    a->g = tan_pi(fc / a->sr);
    a->k = a->p[NACID_P_RESO] * 4.1f;
    a->comp = 1.0f + 0.35f * a->k;
    /* the decay: 80 ms .. 2.5 s; an accent cuts it to 200 ms */
    float t = a->accent ? 0.2f : 0.08f * exp2f_(a->p[NACID_P_DECAY] * 5.0f);
    dk[0] = exp2f_(-1.4427f / (t * a->sr));
    dk[1] = exp2f_(-1.4427f / (0.2f * a->sr));
}

NFM_CODE void nacid_render(nacid *a, const nfm_engine *e, float *out_l, float *out_r, float *rev, float *dly, int n)
{
    static float dk[2] = { 0.999f, 0.999f };
    const float att = 1.0f - exp2f_(-1.4427f / (0.002f * a->sr)), rel = 1.0f - exp2f_(-1.4427f / (0.006f * a->sr));
    const float fade = 1.0f - exp2f_(-1.4427f / (0.01f * a->sr));
    float lvl = a->p[NACID_P_LEVEL] * 0.6f * e->master;
    for (int i = 0; i < n; ++i, ++a->now) {
        if (a->pending && (int32_t)(a->now - a->at) >= 0) {
            a->pending = 0;
            play(a, a->next, a->next_tie);
        }
        if (!a->sounding) continue;
        if (a->gate > 0.0f && (a->gate -= 1.0f) <= 0.0f) a->gate_on = 0;
        if (a->ctl == 0) {
            control(a, dk);
            a->ctl = 8;
        }
        --a->ctl;
        a->env *= dk[0];
        a->acc_env *= dk[1];
        a->freq += (a->target - a->freq) * a->glide;
        a->amp += ((a->gate_on ? 1.0f : 0.0f) - a->amp) * (a->gate_on ? att : rel);
        if (!a->gate_on && a->amp < 0.0001f) {
            a->sounding = 0;
            a->amp = 0.0f;
            a->s[0] = a->s[1] = a->s[2] = a->s[3] = 0.0f;
            continue;
        }

        float dt = a->freq / a->sr, t = a->phase + dt, x;
        if (t >= 1.0f) t -= 1.0f;
        a->phase = t;
        if (a->square) {
            float t2 = t + 0.5f;
            if (t2 >= 1.0f) t2 -= 1.0f;
            x = (t < 0.5f ? 1.0f : -1.0f) + blep(t, dt) - blep(t2, dt);
        } else {
            x = 2.0f * t - 1.0f - blep(t, dt);
        }

        /* the ladder: four one-pole stages with the feedback solved for the sample (and a soft
         * clip on the way in, where it sings) */
        float g = a->g, G = g / (1.0f + g), b = 1.0f / (1.0f + g);
        float S = ((G * a->s[0] + a->s[1]) * G + a->s[2]) * G * b + a->s[3] * b;
        float G4 = G * G * G * G;
        float u = soft((x * 0.5f * a->comp - a->k * S) / (1.0f + a->k * G4));
        for (int s = 0; s < 4; ++s) {
            float v = (u - a->s[s]) * G, y = v + a->s[s];
            a->s[s] = y + v;
            u = y;
        }
        float o = u * a->amp * (1.0f + (a->accent ? 0.8f * a->p[NACID_P_ACCENT] * a->acc_env : 0.0f)) * lvl;

        /* the inserts the effects chose, faded in and out */
        a->ins_d += (a->ins_d_to - a->ins_d) * fade;
        a->ins_f += (a->ins_f_to - a->ins_f) * fade;
        if (a->ins_d > 0.001f) o += (soft(o * e->ins_drive) * e->ins_comp - o) * a->ins_d;
        if (a->ins_f > 0.001f) {
            float gg = e->ins_g, k = e->ins_k;
            float hp = (o - (k + gg) * a->is1 - a->is2) / (1.0f + gg * (gg + k));
            float bp = gg * hp + a->is1;
            a->is1 = gg * hp + bp;
            float lp = gg * bp + a->is2;
            a->is2 = gg * bp + lp;
            o += (lp + (bp * 1.5f - lp) * e->ins_morph - o) * a->ins_f;
        }
        out_l[i] += o;
        out_r[i] += o;
        a->send_rev += (a->rev_to - a->send_rev) * fade;
        a->send_dly += (a->dly_to - a->send_dly) * fade;
        if (rev) rev[i] += o * a->send_rev;
        if (dly) dly[i] += o * a->send_dly;
        float m = o < 0.0f ? -o : o;
        a->peak = m > a->peak ? m : a->peak * 0.9997f;
    }
    if (!a->sounding) a->peak *= 0.5f;
}

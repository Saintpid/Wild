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
    int i = (int)(x + 0.5f);                       /* to the nearest: the series is good for |f| <= 0.5 */
    if ((float)i > x + 0.5f) --i;
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

NFM_CODE static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
NFM_CODE static inline float clamp1(float x) { return x < 0.0f ? 0.0f : x > 0.97f ? 0.97f : x; }

/* minor, phrygian, dorian, minor pentatonic, blues, harmonic minor: what acid lines are made of; and
 * major, for the brighter ones */
NFM_DATA static const int8_t scales[NACID_SCALES][8] = {
    { 7, 0, 2, 3, 5, 7, 8, 10 }, { 7, 0, 1, 3, 5, 7, 8, 10 }, { 7, 0, 2, 3, 5, 7, 9, 10 },
    { 5, 0, 3, 5, 7, 10 },       { 6, 0, 3, 5, 6, 7, 10 },    { 7, 0, 2, 3, 5, 7, 8, 11 },
    { 7, 0, 2, 4, 5, 7, 9, 11 },
};

NFM_CODE void nacid_init(nacid *a, float sr, uint32_t seed)
{
    NFM_DATA static const float def[NACID_PARAMS] = {
        [NACID_P_DENSITY] = 0.6f, [NACID_P_SLIDES] = 0.2f, [NACID_P_ACCENTS] = 0.25f,
        [NACID_P_OCTAVES] = 0.3f, [NACID_P_EVOLVE] = 0.15f, [NACID_P_CUTOFF] = 0.3f,
        [NACID_P_RESO] = 0.7f, [NACID_P_ENVMOD] = 0.6f, [NACID_P_DECAY] = 0.4f,
        [NACID_P_ACCENT] = 0.6f, [NACID_P_LEVEL] = 0.7f, [NACID_P_CHAOS] = 0.25f,
    };
    nfm_zero(a, sizeof *a);
    a->sr = sr;
    a->rng = seed ? seed : 0x2c1b3c6du;
    for (int i = 0; i < NACID_PARAMS; ++i) a->p[i] = def[i];
    a->length = NACID_BAR;
    a->last = -1;
    a->glide = 1.0f - exp2f_(-1.4427f / (0.06f * sr));    /* the slide: an RC of 60 ms on the pitch, a 303's */
    a->freq = 65.406f;
    a->ga = 0.1f;
    a->dk_env = a->dk_acc = 0.999f;
    a->att_soft = 1.0f - exp2f_(-1.4427f / (0.003f * sr));  /* the VCA's attack: soft on a plain note */
    a->att_hard = 1.0f - exp2f_(-1.4427f / (0.0003f * sr)); /* and sharp on an accent */
    a->att = a->att_soft;
    a->drng = a->rng ^ 0x9e3779b9u;
    a->db_ph = 1.0f;
    a->d_energy = 1.0f;
    a->dsm = 1.0f - exp2f_(-32.0f * 1.4427f / (0.02f * sr)); /* Deviance's offsets glide: 20 ms */
    nacid_generate(a);
}

/* ------------------------------------------------------------------------------- the generator ---
 * Every chance below is a rule's at Chaos 0 and moves to an even one as Chaos goes to 1. */

/* where in the bar a note is likely: the one, the offbeat eighths, the beats, then the sixteenths */
NFM_CODE static float pos_w(const nacid *a, int s)
{
    int p = s % NACID_BAR;
    float w = p == 0 ? 1.4f : p % 4 == 2 ? 1.2f : p % 4 == 0 ? 1.05f : 0.8f;
    return lerp(w, 1.0f, a->p[NACID_P_CHAOS]);
}

/* a step's note or rest: the density weighed by the place, fewer after three notes in a row */
NFM_CODE static int roll_on(nacid *a, int s)
{
    float p = a->p[NACID_P_DENSITY] * pos_w(a, s), c = a->p[NACID_P_CHAOS];
    if (s % NACID_BAR == 0) p = lerp(0.95f, p, c);            /* the one, nearly always */
    if (s >= 3 && (a->step[s - 1].flags & a->step[s - 2].flags & a->step[s - 3].flags & NACID_ON))
        p *= lerp(0.6f, 1.0f, c);                             /* a rest makes the groove */
    return rndf(&a->rng) < clamp1(p);
}

/* the note before step s (in the bar or the one before), -1 none */
NFM_CODE static int note_before(const nacid *a, int s)
{
    for (int i = s - 1; i >= 0 && i > s - NACID_BAR; --i)
        if (a->step[i].flags & NACID_ON) return a->step[i].note;
    return -100;
}

/* a pitch 0..11: the root and the fifth most, then the third and the seventh, a note outside the scale
 * now and then; near the note before mostly (a repeat likeliest), a leap now and then; the root
 * likelier on the beat */
NFM_CODE static int pick_pitch(nacid *a, int s)
{
    const int8_t *sc = scales[a->scale < NACID_SCALES ? a->scale : 0];
    float c = a->p[NACID_P_CHAOS], w[12], sum = 0.0f;
    int prev = note_before(a, s), in[12] = { 0 };
    for (int i = 0; i < sc[0]; ++i) in[sc[1 + i]] = 1;
    if (prev > -100) prev = ((prev % 12) + 12) % 12;
    for (int n = 0; n < 12; ++n) {
        float d = n == 0 ? 4.0f : n == 7 ? 2.5f : (n == 3 || n == 4) ? 1.6f : (n == 10 || n == 11) ? 1.4f : 1.0f;
        if (!in[n]) d = 0.12f;
        if (n == 0 && s % 4 == 0) d *= s % NACID_BAR == 0 ? 2.0f : 1.4f;
        if (prev > -100) {
            int iv = n > prev ? n - prev : prev - n;
            d *= iv == 0 ? 1.3f : iv <= 2 ? 1.0f : iv <= 4 ? 0.7f : (iv == 5 || iv == 7) ? 0.55f : 0.3f;
        }
        w[n] = lerp(d, 1.0f, c);
        sum += w[n];
    }
    float r = rndf(&a->rng) * sum;
    for (int n = 0; n < 12; ++n)
        if ((r -= w[n]) < 0.0f) return n;
    return 0;
}

/* the octave on a pitch, the 303 way: notes side by side pushed apart, one up, the next down. Next to
 * a note in the low register (right before it) one up is likely; next to one up, back down (now and
 * then below the root); apart from that, up likelier off the beat (the root low on the one). With
 * Chaos two up too, and the neighbour counts less. */
NFM_CODE static int8_t octave(nacid *a, int s, int pitch)
{
    int p = s % NACID_BAR, n = pitch;
    float c = a->p[NACID_P_CHAOS], o = a->p[NACID_P_OCTAVES], r = rndf(&a->rng);
    float w = p == 0 ? 0.3f : p % 4 == 0 ? 0.7f : p % 2 ? 1.25f : 1.35f;
    float up = o * 1.0f * lerp(w, 1.0f, c), down = o * 0.3f;
    if (s > 0 && (a->step[s - 1].flags & NACID_ON)) {
        if (a->step[s - 1].note >= 12) {                       /* after one up: down again */
            up *= lerp(0.15f, 1.0f, c);
            down = o * lerp(0.6f, 0.35f, c);
        } else if (a->step[s - 1].note < 0) {                  /* after one below: up */
            up = lerp(0.3f + o, up, c);
            down *= lerp(0.2f, 1.0f, c);
        } else {                                               /* after one low: push this up */
            up *= lerp(1.7f, 1.0f, c);
        }
    }
    up = clamp1(up * (1.0f + c));
    down = clamp1(down);
    if (r < up)
        n += rndf(&a->rng) < 0.35f * c * c ? 24 : 12;
    else if (r < up + down)
        n -= 12;
    return (int8_t)(n > 24 ? n - 12 : n);
}

/* an accent where it pushes the rhythm: off the beat, after a rest; less on one in a row */
NFM_CODE static int roll_accent(nacid *a, int s)
{
    int p = s % NACID_BAR;
    float w = p % 4 == 2 ? 1.5f : p % 4 == 0 ? 0.8f : 0.9f;
    if (s > 0 && !(a->step[s - 1].flags & NACID_ON)) w *= 1.3f;
    if (s > 0 && (a->step[s - 1].flags & NACID_ACCENT)) w *= 0.6f;
    return rndf(&a->rng) < clamp1(a->p[NACID_P_ACCENTS] * lerp(w, 1.0f, a->p[NACID_P_CHAOS]));
}

/* a slide where it moves: most into another note, an octave above all; into a rest (a long note) seldom */
NFM_CODE static int roll_slide(nacid *a, int s)
{
    const nacid_note *nx = &a->step[(s + 1) % NACID_STEPS];
    float w = 0.35f;
    if (nx->flags & NACID_ON) {
        int iv = nx->note - a->step[s].note;
        if (iv < 0) iv = -iv;
        w = iv == 0 ? 0.5f : iv <= 2 ? 1.0f : iv >= 12 ? 1.8f : 1.25f;
    }
    return rndf(&a->rng) < clamp1(a->p[NACID_P_SLIDES] * lerp(w, 1.0f, a->p[NACID_P_CHAOS]));
}

/* step s drawn anew: its note or rest, then its pitch, octave, accent; the slides of it and the one
 * before decided again (they depend on it) */
NFM_CODE static void draw_rhythm(nacid *a, int s)
{
    a->step[s].flags = roll_on(a, s) ? NACID_ON : 0;
    a->step[s].note = 0;
}
NFM_CODE static void draw_note(nacid *a, int s)
{
    if (!(a->step[s].flags & NACID_ON)) return;
    a->step[s].note = octave(a, s, pick_pitch(a, s));
    a->step[s].flags = NACID_ON | (roll_accent(a, s) ? NACID_ACCENT : 0);
}
NFM_CODE static void draw_slide(nacid *a, int s)
{
    nacid_note *st = &a->step[s];
    st->flags &= (uint8_t)~NACID_SLIDE;
    if ((st->flags & NACID_ON) && roll_slide(a, s)) st->flags |= NACID_SLIDE;
}

/* The rhythm first, as a motif: a bar, or half a bar twice (its second half a little different); the
 * notes on it, then the slides (they look at the next note). The bars after it the same, with some
 * steps drawn anew, the last quarter of bars 2 and 4 most (a turn into the next). Chaos: more steps
 * anew, until no motif is left. */
NFM_CODE void nacid_generate(nacid *a)
{
    NFM_DATA static const float var[NACID_MAX_BARS] = { 0.12f, 0.12f, 0.04f, 0.15f };
    NFM_DATA static const float turn[NACID_MAX_BARS] = { 0.0f, 0.3f, 0.0f, 0.45f };
    float c = a->p[NACID_P_CHAOS];
    int half = rndf(&a->rng) < lerp(0.4f, 0.0f, c), s;
    uint8_t anew[NACID_STEPS] = { 0 };
    for (s = 0; s < NACID_BAR; ++s) draw_rhythm(a, s);
    for (s = 0; s < NACID_BAR; ++s) draw_note(a, s);
    for (s = NACID_BAR / 2; s < NACID_BAR && half; ++s) {
        a->step[s] = a->step[s - NACID_BAR / 2];
        if (rndf(&a->rng) < lerp(var[0], 1.0f, c * c)) {
            anew[s] = 1;
            draw_rhythm(a, s);
        }
    }
    for (s = NACID_BAR / 2; s < NACID_BAR; ++s)
        if (anew[s]) draw_note(a, s);
    a->step[NACID_BAR] = a->step[0];                          /* (the last step's slide looks there) */
    for (s = 0; s < NACID_BAR; ++s) draw_slide(a, s);
    for (s = NACID_BAR; s < NACID_STEPS; ++s) {
        int b = s / NACID_BAR;
        a->step[s] = a->step[s % NACID_BAR];
        float v = (s % NACID_BAR >= 12 ? turn[b] : 0.0f) + var[b];
        if (rndf(&a->rng) < lerp(v, 1.0f, c * c) + c * 0.3f) {
            anew[s] = 1;
            draw_rhythm(a, s);
        }
    }
    for (s = NACID_BAR; s < NACID_STEPS; ++s)
        if (anew[s]) draw_note(a, s);
    for (s = NACID_BAR; s < NACID_STEPS; ++s)
        if (anew[s]) {
            draw_slide(a, s - 1);
            draw_slide(a, s);
        }
}

/* one step changes: a new note mostly, else a note comes or goes, an accent or a slide flips; by the
 * same rules (and Chaos) as a new pattern */
NFM_CODE static void evolve(nacid *a)
{
    int s = (int)(rnd(&a->rng) % a->length);
    nacid_note *st = &a->step[s];
    float r = rndf(&a->rng);
    if (r < 0.4f) {
        if (st->flags & NACID_ON) {
            uint8_t f = st->flags;
            draw_note(a, s);
            st->flags = f;
        }
    } else if (r < 0.65f) {
        if (st->flags & NACID_ON) {
            st->flags = 0;
        } else {
            st->flags = NACID_ON;
            draw_note(a, s);
            draw_slide(a, s);
        }
        if (s > 0) draw_slide(a, s - 1);
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
    a->acc_gate = 0;
    a->last = -1;
}

NFM_CODE void nacid_route(nacid *a, int filter, float drive, float rev, float dly)
{
    a->ins_f_to = filter ? 1.0f : 0.0f;
    if (drive != a->drive_to) {                   /* (a new drive starts its loudness match over, */
        float dc = a->sat.dc, g = a->sat.gain;    /* the DC and the gain gliding on from where they were) */
        a->drive_to = drive;
        nfm_sat_set(&a->sat, drive);
        if (a->sat.amount > 0.0f && dc != 0.0f) a->sat.dc = dc, a->sat.gain = g;
    }
    a->rev_to = rev;
    a->dly_to = dly;
}

/* ----------------------------------------------------------------------------------- the voice ---
 * A step: a rest closes the gate. A note after a step that slid on (its gate still held) glides there:
 * the pitch's RC goes on from where it is, the envelopes and the VCA go on as they are. Its accent
 * starts no envelope either: as a 303's, it takes the cutoff envelope where it has decayed to, so an
 * accent on a slid note is a weak one. Any other note is a new attack: the pitch there at once, the
 * cutoff envelope and the VCA from where they are, the VCA's attack soft (3 ms) on a plain note and
 * sharp on an accent (the 303's click). The oscillator's phase is never touched. */
NFM_CODE void nacid_apply_event(nacid *a, const nacid_event *event)
{
    int legato = a->gate_on && a->sounding && a->gate < 0.0f;
    if (!event->on) {
        a->gate_on = 0;
        a->acc_gate = 0;
        return;
    }
    a->cv_to = event->pitch;
    a->acc_gate = event->accent;
    if (a->acc_gate) a->acc_env = legato ? a->env : 1.0f;
    if (!legato) {
        a->cv = a->cv_to;
        a->freq = 65.406f * exp2f_(a->cv * (1.0f / 12.0f));
        a->env = 1.0f;
        a->accent = a->acc_gate;                  /* (the cutoff envelope's decay: short on an accent) */
        a->attack = 1;
        a->att = a->acc_gate ? a->att_hard : a->att_soft;
        /* the modulation effect: this note's chance (a slid note keeps the one before's) */
        if (a->mod.type == NFM_MOD_OFF || a->mod.chance <= 0.0f ||
            ((a->mod.type == NFM_MOD_FLANGER || a->mod.type == NFM_MOD_CHORUS) && !a->ms.line))
            a->mod_to = 0.0f;
        else
            a->mod_to = a->mod.chance >= 1.0f || rndf(&a->rng) < a->mod.chance ? 1.0f : 0.0f;
    }
    a->gate_on = 1;
    a->sounding = 1;
    a->gate = event->gate_samples;
}

/* The musical scheduler supplies instructions; the first sound model is 303. */
NFM_CODE static void play(nacid *a, nacid_note st, int slide)
{
    nacid_event event = {
        (float)(a->root + st.note) + a->tune,
        slide ? -1.0f : a->step_len * 0.5f,
        (uint8_t)((st.flags & NACID_ON) != 0),
        (uint8_t)((st.flags & NACID_ACCENT) != 0)
    };
    nacid_apply_event(a, &event);
}

NFM_CODE void nacid_step(nacid *a, float delay, float step_len)
{
    if (a->pending) play(a, a->next, a->next_slide);         /* (the one before, still waiting) */
    if (a->pos >= a->length) a->pos = 0;
    nacid_note st = a->step[a->pos];
    a->last = (int16_t)a->pos;
    if (++a->pos >= a->length) {
        a->pos = 0;
        if (rndf(&a->rng) < a->p[NACID_P_EVOLVE]) evolve(a);
    }
    if (a->mute) st.flags = 0;
    a->next = st;
    a->next_slide = (st.flags & (NACID_ON | NACID_SLIDE)) == (NACID_ON | NACID_SLIDE);
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

/* The 303's diode ladder: four capacitors on a ladder of diodes, so each stage loads the one before
 * (not four buffered stages); the bottom one half the size of the others, so twice as fast. Its
 * linear part as trapezoidal integrators, solved for the sample: with G the stages' gains and D the
 * ladder's coupling, y = P s + q x, P = (I - G D)^-1, q = P G e1 (x into the bottom stage). Built
 * here every 8 samples (a tridiagonal solve per column); the feedback around it, its high-pass and
 * the clip on the way in go on in the render. LADDER_F: the coupling pulls the cutoff down, so the
 * stages run higher for the same knob; LADDER_K: the resonance, just short of oscillating. */
#define LADDER_F 0.75f
#define LADDER_K 19.0f
NFM_CODE static void ladder(nacid *a, float g)
{
    NFM_DATA static const float dg[4] = { -2.0f, -2.0f, -2.0f, -1.0f };   /* D: -2 on the diagonal (the last -1), 1 beside */
    float G[4] = { 2.0f * g, g, g, g }, lo[4], di[4], up[4], c[4], d[4];
    for (int i = 0; i < 4; ++i) {                  /* I - G D */
        di[i] = 1.0f - G[i] * dg[i];
        lo[i] = i ? -G[i] : 0.0f;
        up[i] = i < 3 ? -G[i] : 0.0f;
    }
    for (int j = 0; j < 4; ++j) {                  /* column j of P: (I - G D) p = e_j */
        float m = di[0];
        c[0] = up[0] / m;
        d[0] = (j == 0) / m;
        for (int i = 1; i < 4; ++i) {
            m = di[i] - lo[i] * c[i - 1];
            c[i] = up[i] / m;
            d[i] = ((float)(j == i) - lo[i] * d[i - 1]) / m;
        }
        for (int i = 2; i >= 0; --i) d[i] -= c[i] * d[i + 1];
        for (int i = 0; i < 4; ++i) a->P[i * 4 + j] = d[i];
    }
    for (int i = 0; i < 4; ++i) a->q[i] = a->P[i * 4] * G[0];
}

/* ---------------------------------------------------------------------------------- Deviance ---
 * The four knobs (cutoff, resonance, Env Mod, Decay) played by a hand that listens to the drums. Each
 * goes its own slow way to where the drums' intensity puts it: hard drums, the resonance high, Env Mod
 * middle to high and the cutoff in the middle (Env Mod opens it enough); calm ones, the resonance and
 * Env Mod back, the cutoff lower, the decay longer. One way in six goes against that. Each knob takes
 * its own time: one may sweep in half a bar while another takes eight, and then stays a few bars. By
 * chance, on a beat, a quick turn there and back on one or more knobs (two steps to half a bar, once or
 * a few times over), likelier with hard drums, in bars 4 and 8 of the phrase and on the last beat. In
 * some phrases (more with hard drums) the cutoff and Env Mod build over the last four bars and let go on
 * the one. Deviance: how much of it is heard over the knobs as set; at 100, mostly the hand's. Turning
 * one of the four yourself, Deviance lets go of them all for 2 s after the last turn (nacid_touch).
 *
 * The hand tires: playing hard spends its energy (with hard drums about three phrases' worth, give or
 * take), and when it is gone it rests, from a bar on a half phrase: the knobs back to the calm place in
 * a bar or two, whatever the drums do, no turns, no build, for eight bars (now and then four or twelve).
 * Then all of it at once again, from where the drums are. Playing calm, it gets its energy back. */
NFM_DATA static const float dlo[4] = { 0.25f, 0.45f, 0.30f, 0.55f };   /* the rule's place, calm drums */
NFM_DATA static const float dhi[4] = { 0.45f, 0.90f, 0.80f, 0.30f };   /* ...hard drums */

NFM_CODE static float step_samples(const nacid *a) { return a->step_len > 0.0f ? a->step_len : a->sr * 0.134f; }

/* the intensity the hand plays at: the drums', none while it rests */
NFM_CODE static inline float dev_mood(const nacid *a) { return a->d_rest ? 0.0f : a->d_mood; }

/* knob i: a new way, and how long it takes (resting: to the calm place in a bar or two, and it stays;
 * just rested: back in one to four bars) */
NFM_CODE static void dev_way(nacid *a, int i)
{
    uint32_t *r = &a->drng;
    float t = lerp(dlo[i], dhi[i], dev_mood(a)) + (rndf(r) + rndf(r) - 1.0f) * 0.1f;
    if (!a->d_rest && rndf(r) < 0.16f) t = 0.1f + 0.85f * rndf(r);   /* against the rules */
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float bars = a->d_rest ? 1.0f + rndf(r) : a->d_woke ? 1.0f + 3.0f * rndf(r)
                 : rndf(r) < 0.3f ? 0.5f + 0.5f * rndf(r) : 2.0f + 6.0f * rndf(r);
    float d = t - a->dw[i];
    a->dw_to[i] = t;
    a->dw_v[i] = (d < 0.0f ? -d : d) * 32.0f / (bars * (float)NACID_BAR * step_samples(a));
    a->dw_hold[i] = a->d_rest ? 255 : (uint8_t)((int)bars + 1 + (int)(rnd(r) % 4u));
}

/* a bar's energy: spent playing hard (above a third of the way, a phrase of the hardest takes a third
 * of it, by chance more or less), got back playing calm; gone, a rest from the next half phrase. A
 * rest's end: every knob a new way at once, from the drums. */
NFM_CODE static void dev_energy(nacid *a, int bar)
{
    uint32_t *r = &a->drng;
    a->d_woke = 0;
    if (a->d_rest) {
        if (--a->d_rest) return;
        a->d_energy = 1.0f;
        a->d_woke = 1;
        for (int i = 0; i < 4; ++i) a->dw_hold[i] = 0;
        return;
    }
    float m = a->d_mood - 0.33f;
    if (m > 0.0f) a->d_energy -= m * (1.0f / 0.67f) * (1.0f / 24.0f) * (0.5f + rndf(r));
    else a->d_energy += -m * 0.1f;
    a->d_energy = a->d_energy > 1.0f ? 1.0f : a->d_energy;
    if (a->d_energy <= 0.0f && bar % 4 == 0) {
        float x = rndf(r);
        a->d_rest = (uint8_t)(x < 0.2f ? 4 : x < 0.85f ? 8 : 12);
        a->d_arc_on = 0;
        for (int i = 0; i < 4; ++i) a->dw_hold[i] = 0;
    }
}

/* a quick turn there and back: which knobs, which way (the cutoff and Env Mod mostly up), how far, how
 * fast; shorter with hard drums */
NFM_CODE static void dev_turn(nacid *a)
{
    NFM_DATA static const float ch[4] = { 0.6f, 0.35f, 0.5f, 0.4f }, up[4] = { 0.8f, 0.7f, 0.75f, 0.5f };
    uint32_t *r = &a->drng;
    float m = dev_mood(a), x = rndf(r);
    int any = 0, len = x < 0.25f + 0.2f * m ? 2 : x < 0.75f ? 4 : 8;
    for (int i = 0; i < 4; ++i) {
        a->db_amp[i] = 0.0f;
        if (rndf(r) < ch[i]) {
            float amt = (0.1f + 0.15f * rndf(r)) * (0.7f + 0.5f * m);
            a->db_amp[i] = rndf(r) < up[i] ? amt : -amt;
            any = 1;
        }
    }
    if (!any) a->db_amp[0] = 0.2f;
    x = rndf(r);
    a->db_cyc = (uint8_t)(len == 2 || x < 0.6f ? 1 : x < 0.88f ? 2 : 3);
    a->db_ph = 0.0f;
    a->db_inc = 32.0f / ((float)len * step_samples(a));
}

NFM_CODE void nacid_touch(nacid *a)
{
    a->d_hold = 2.0f * a->sr;
    a->d_live = 0;                                 /* (from the knobs as set when it comes back) */
}

NFM_CODE void nacid_feel(nacid *a, float mood, int ps)
{
    if (a->d_hold > 0.0f) a->d_hold -= step_samples(a);
    if (a->p[NACID_P_DEVIANCE] <= 0.0f || a->d_hold > 0.0f) {   /* off or resting: the offsets glide away (at once if silent) */
        a->d_live = 0;
        if (!a->sounding) a->dv[0] = a->dv[1] = a->dv[2] = a->dv[3] = 0.0f;
        return;
    }
    if (!a->d_live) {                              /* from where the knobs are */
        for (int i = 0; i < 4; ++i) {
            a->dw[i] = a->dw_to[i] = a->p[NACID_P_CUTOFF + i];
            a->dw_v[i] = 0.0f;
            a->dw_hold[i] = 0;
        }
        a->d_mood = mood;
        a->d_rest = a->d_woke = 0;                 /* (its energy as it was) */
        a->d_live = 1;
    }
    a->d_mood += (mood - a->d_mood) * 0.15f;       /* (about a bar to follow) */
    int pos = ps % NACID_BAR, bar = (ps / NACID_BAR) % 8;
    if (pos == 0) {
        dev_energy(a, bar);
        for (int i = 0; i < 4; ++i) {
            if (a->dw_hold[i]) a->dw_hold[i] -= a->dw_hold[i] < 255;
            else dev_way(a, i);
        }
        if (bar == 0) a->d_arc_on = !a->d_rest && rndf(&a->drng) < 0.3f + 0.4f * a->d_mood;
    }
    a->d_arc = a->d_arc_on && bar >= 4 ? (float)(ps % 128 - 64) * (1.0f / 64.0f) : 0.0f;
    if (pos % 4 == 0 && a->db_ph >= 1.0f && !a->d_rest) {
        float c = 0.035f + 0.1f * a->d_mood;
        if (bar == 3 || bar == 7) c *= 2.0f;
        if (pos == 12) c *= 1.5f;
        if (rndf(&a->drng) < c) dev_turn(a);
    }
}

/* every 32 samples: the ways on, the turn, the build; the offsets over the knobs glide to them */
NFM_CODE static void dev_control(nacid *a)
{
    if (a->dctl++ & 3) return;
    float D = a->d_live ? a->p[NACID_P_DEVIANCE] : 0.0f, tw = 0.0f;
    if (a->db_ph < 1.0f) {                         /* a turn: there and back, db_cyc times */
        float f = a->db_ph * (float)a->db_cyc;
        f -= (float)(int)f;
        tw = 4.0f * f * (1.0f - f);
        a->db_ph += a->db_inc;
    }
    for (int i = 0; i < 4; ++i) {
        float d = a->dw_to[i] - a->dw[i], v = a->dw_v[i];
        a->dw[i] = d > v ? a->dw[i] + v : d < -v ? a->dw[i] - v : a->dw_to[i];
        float to = D * (0.8f * (a->dw[i] - a->p[NACID_P_CUTOFF + i]) + a->db_amp[i] * tw);
        if (i == 0 || i == 2) to += D * (i ? 0.1f : 0.12f) * a->d_arc;
        a->dv[i] += (to - a->dv[i]) * a->dsm;
        if (!a->d_live && a->dv[i] < 1e-5f && a->dv[i] > -1e-5f) a->dv[i] = 0.0f;
    }
}
NFM_CODE static inline float knob(const nacid *a, int i)
{
    float v = a->p[NACID_P_CUTOFF + i] + a->dv[i];
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

/* the cutoff and the envelopes' rates, every 8 samples. The cutoff: the knob, the envelope, the
 * accent's sweep. The filter: the 303's diode ladder (ladder()), the resonance's feedback through a
 * high-pass at 100 Hz; up to just short of oscillating. */
NFM_CODE static void control(nacid *a)
{
    if (a->d_live || a->dv[0] != 0.0f || a->dv[1] != 0.0f || a->dv[2] != 0.0f || a->dv[3] != 0.0f) dev_control(a);
    float cut = knob(a, 0), reso = knob(a, 1), em = knob(a, 2), dec = knob(a, 3);
    float base = 30.0f * exp2f_(cut * 7.6f);                           /* 30 Hz .. 5.8 kHz */
    /* Env Mod as a 303's: the more of it, the lower the sweep starts (1.5 octaves at full), its top
     * where it was (5 octaves over the knob) */
    float oct = em * (6.5f * a->env - 1.5f) + a->p[NACID_P_ACCENT] * 3.0f * a->acc_cap;
    float fc = base * exp2f_(oct), w = fc * LADDER_F / a->sr;
    float g = tan_pi((w > 0.3f ? 0.3f : w) * 0.5f);                    /* (at 2x; its top where it was at 1x) */
    a->ga = g / (1.0f + g);
    ladder(a, g);
    a->k = reso * LADDER_K;
    /* the resonance takes the passband down by 1 + k above the feedback's high-pass: given back, the
     * more the higher the cutoff, the envelope's too (from 280 Hz up, a fifth of k an octave; low, the
     * resonance sits on the notes themselves) */
    float q = 1.52f * cut - 0.65f + 0.2f * oct;     /* (log2 of fc / 280 Hz, over 5) */
    a->comp = 1.0f + a->k * (q < 0.0f ? 0.0f : q > 0.7f ? 0.7f : q);
    /* the decay: 80 ms .. 2.5 s; an accent cuts it to 200 ms */
    float t = a->accent ? 0.2f : 0.08f * exp2f_(dec * 5.0f);
    a->dk_env = exp2f_(-8.0f * 1.4427f / (t * a->sr));
    a->dk_acc = exp2f_(-1.4427f / (0.2f * a->sr));
    /* the sweep capacitor: it charges from the accent and lets go slowly; the more resonance, the
     * slower both, so accents in a row build up */
    float src = a->acc_gate ? a->acc_env : 0.0f;
    if (src > a->acc_cap) a->acc_cap += (src - a->acc_cap) * a->chg;
    else a->acc_cap += (src - a->acc_cap) * a->dis;
    a->chg = 1.0f - exp2f_(-8.0f * 1.4427f / ((0.01f + 0.06f * reso) * a->sr));
    a->dis = 1.0f - exp2f_(-8.0f * 1.4427f / ((0.08f + 0.5f * reso) * a->sr));
}

/* ----------------------------------------------------------------------------- 2x oversampled ---
 * The oscillator and the ladder run at twice the rate: the clip in the resonance's feedback (and the
 * square's) makes overtones past the top that would fold back between a note's harmonics, most with the
 * resonance up and the cutoff open, and the ladder keeps its shape further up. Back down through a
 * halfband: 27 taps, 7 multiplies a sample, flat to 17 kHz, -57 dB from 27 kHz. */
NFM_DATA static const float hbc[7] = { 0.31402690f, -0.09586685f, 0.04802163f, -0.02583784f, 0.01338052f, -0.00619374f, 0.00246937f };

/* the wave at phase t, dt its step */
NFM_CODE static inline float osc(nacid *a, float t, float dt)
{
    float x;
    switch (a->wave) {
    default:
    case NACID_SAW: x = 2.0f * t - 1.0f - blep(t, dt); break;
    case NACID_SQUARE: {                     /* a 303's: the saw through a transistor that clips it */
        float w = 2.0f * t - 1.0f - blep(t, dt);
        float u = 2.65f * (w + 0.08f);       /* rounded on the way up, a slant on top; anti-aliased */
        x = sat_adaa(a->sq_u1, u) * 0.95f + 0.1f * w;
        a->sq_u1 = u;
        break;
    }
    case NACID_PULSE: {                      /* 20 % */
        float t2 = t + 0.8f;
        if (t2 >= 1.0f) t2 -= 1.0f;
        x = (t < 0.2f ? 1.0f : -1.0f) + blep(t, dt) - blep(t2, dt) + 0.6f;   /* (its DC off) */
        break;
    }
    case NACID_TRI: x = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t; break;
    case NACID_SINE: {                       /* a parabola pair, near enough a sine */
        float u = t < 0.5f ? t * 4.0f - 1.0f : 3.0f - t * 4.0f;
        x = u * (1.5f - 0.5f * u * u);
        break;
    }
    case NACID_DUO: {                        /* two saws, 12 cents apart */
        float dt2 = dt * 1.00696f, t2 = a->phase2 + dt2;
        if (t2 >= 1.0f) t2 -= 1.0f;
        a->phase2 = t2;
        x = (2.0f * t - 1.0f - blep(t, dt) + 2.0f * t2 - 1.0f - blep(t2, dt2)) * 0.6f;
        break;
    }
    }
    return x;
}

NFM_CODE static inline float ladder_tick(nacid *a, float x, float hf, float Gf)
{
    /* the filter: the diode ladder (ladder()), the feedback through its high-pass solved for the
     * sample (y4 = B + q4 u), a soft clip on the way in, where it sings */
    const float *P = a->P, *s = a->s;
    float k = a->k;
    float B = P[12] * s[0] + P[13] * s[1] + P[14] * s[2] + P[15] * s[3];
    float u = 4.0f * soft(0.25f * (x * 0.12f * a->comp - k * hf * (B - a->sf)) / (1.0f + k * hf * a->q[3]));
    float y0 = P[0] * s[0] + P[1] * s[1] + P[2] * s[2] + P[3] * s[3] + a->q[0] * u;
    float y1 = P[4] * s[0] + P[5] * s[1] + P[6] * s[2] + P[7] * s[3] + a->q[1] * u;
    float y2 = P[8] * s[0] + P[9] * s[1] + P[10] * s[2] + P[11] * s[3] + a->q[2] * u;
    float y3 = B + a->q[3] * u;
    a->s[0] = y0 + y0 - s[0];
    a->s[1] = y1 + y1 - s[1];
    a->s[2] = y2 + y2 - s[2];
    a->s[3] = y3 + y3 - s[3];
    float v = (y3 - a->sf) * Gf;             /* the feedback's high-pass: its low-pass state */
    a->sf += v + v;
    return y3;
}

/* two samples at 2x in (x0 first), one out: x1's on the taps, x0's on the centre (6 samples late) */
NFM_CODE static inline float halfband(nacid *a, float x0, float x1)
{
    unsigned i = a->hb_i = (uint8_t)((a->hb_i + 1u) & 15u);
    const float *h = a->hb_a;
    a->hb_a[i] = x1;
    a->hb_b[i & 7u] = x0;
    float y = 0.5f * a->hb_b[(i - 6u) & 7u];
    for (unsigned k = 0; k < 7u; ++k) y += hbc[k] * (h[(i - 6u + k) & 15u] + h[(i - 7u - k) & 15u]);
    return y;
}

NFM_CODE void nacid_render(nacid *a, const nfm_engine *e, float *out_l, float *out_r, float *rev, float *dly, int n)
{
    const float rel = 1.0f - exp2f_(-1.4427f / (0.006f * a->sr));
    const float vdk = exp2f_(-1.4427f / (3.0f * a->sr));          /* the VCA's decay while the gate is held */
    const float fade = 1.0f - exp2f_(-1.4427f / (0.01f * a->sr));
    const float sm = 1.0f - exp2f_(-1.4427f / (0.002f * a->sr));  /* the accent on the VCA, without a click */
    const float gf = tan_pi(50.0f / a->sr), hf = 1.0f / (1.0f + gf), Gf = gf * hf;   /* 100 Hz at 2x */
    const float ho = 1.0f - exp2f_(-1.4427f * 6.2832f * 40.0f / a->sr);   /* the output's coupling: 40 Hz */
    float lvl = a->p[NACID_P_LEVEL] * 0.6f * e->master;
    nfm_mod_block(&a->mod, a->sr, n);
    for (int i = 0; i < n; ++i, ++a->now) {
        if (a->pending && (int32_t)(a->now - a->at) >= 0) {
            a->pending = 0;
            play(a, a->next, a->next_slide);
        }
        /* the pitch: the slide's RC (no other speed for a longer way) */
        if (a->cv != a->cv_to) {
            float d = a->cv_to - a->cv;
            a->cv = d < 0.0005f && d > -0.0005f ? a->cv_to : a->cv + d * a->glide;
            a->freq = 65.406f * exp2f_(a->cv * (1.0f / 12.0f));
        }
        float dh = 0.5f * a->freq / a->sr, t0 = a->phase + dh, t = t0 + dh;   /* (dh: a step at 2x) */
        if (t0 >= 1.0f) t0 -= 1.0f;
        if (t >= 1.0f) t -= 1.0f;
        a->phase = t;
        if (!a->sounding) {                      /* (the oscillator runs on, unheard) */
            if (a->wave == NACID_DUO && (a->phase2 += dh * 2.01392f) >= 1.0f) a->phase2 -= 1.0f;
            continue;
        }
        if (a->gate > 0.0f && (a->gate -= 1.0f) <= 0.0f) a->gate_on = 0;
        if (a->ctl == 0) {
            control(a);
            a->ctl = 8;
            a->env *= a->dk_env;
        }
        --a->ctl;
        a->acc_env *= a->dk_acc;
        a->acc_vca += ((a->acc_gate ? a->acc_env : 0.0f) - a->acc_vca) * sm;
        if (!a->gate_on) {
            a->amp -= a->amp * rel;
        } else if (a->attack) {
            a->amp += (1.02f - a->amp) * a->att;
            if (a->amp >= 1.0f) a->attack = 0;
        } else {
            a->amp *= vdk;
        }
        if (!a->gate_on && a->amp < 0.0001f) {
            a->sounding = 0;
            a->amp = a->acc_vca = 0.0f;
            a->s[0] = a->s[1] = a->s[2] = a->s[3] = a->sf = 0.0f;
            nfm_zero(a->hb_a, sizeof a->hb_a);
            nfm_zero(a->hb_b, sizeof a->hb_b);
            continue;
        }

        /* the oscillator and the ladder twice a sample, back down through the halfband */
        float u = ladder_tick(a, osc(a, t0, dh), hf, Gf);
        u = halfband(a, u, ladder_tick(a, osc(a, t, dh), hf, Gf));
        a->so += (u - a->so) * ho;
        float o = (u - a->so) * 6.7f * a->amp * (1.0f + 0.9f * a->p[NACID_P_ACCENT] * a->acc_vca) * lvl;

        /* the drive, then the moving filter when the effects chose it (faded in and out) */
        a->ins_f += (a->ins_f_to - a->ins_f) * fade;
        if (a->sat.amount > 0.0f) {
            o = nfm_sat_tick(&a->sat, o);
            if (a->ctl == 0) nfm_sat_control(&a->sat);
        }
        if (a->ins_f > 0.001f) {
            float gg = e->ins_g, k = e->ins_k;
            float hp = (o - (k + gg) * a->is1 - a->is2) / (1.0f + gg * (gg + k));
            float bp = gg * hp + a->is1;
            a->is1 = gg * hp + bp;
            float lp = gg * bp + a->is2;
            a->is2 = gg * bp + lp;
            o += (lp + (bp * 1.5f - lp) * e->ins_morph - o) * a->ins_f;
        }
        if (a->mod.type != NFM_MOD_OFF && (a->mod_to > 0.0f || a->mod_mix > 0.001f)) {
            float y = nfm_mod_tick(&a->mod, &a->ms, o, i);
            a->mod_mix += (a->mod_to - a->mod_mix) * fade;
            o += (y - o) * a->mod_mix;
        } else {
            a->mod_mix = 0.0f;
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

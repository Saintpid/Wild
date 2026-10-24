/* nseq: random pattern sequencer, see nseq.h. */
#include "nseq.h"

NFM_CODE static inline uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
NFM_CODE static inline float rndf(uint32_t *s) { return (float)(rnd(s) >> 8) * (1.0f / 16777216.0f); }
NFM_CODE static int track_ok(int t) { return t >= 0 && t < NSEQ_TRACKS; }

/* Where in the bar each kind likes to play, sixteenths 1..16. Downbeats
 * for kicks, 2 and 4 for snares, off-beats for hats; perc and clicks fill
 * the gaps. Weights are relative. */
NFM_DATA static const uint8_t profile[NFM_KINDS][NSEQ_BAR] = {
    [NFM_KIND_KICK]  = { 10, 1, 2, 3,  2, 1, 4, 2,  7, 1, 5, 2,  2, 1, 3, 1 },
    [NFM_KIND_SNARE] = {  1, 1, 1, 1,  9, 1, 1, 2,  1, 1, 2, 1,  9, 1, 2, 3 },
    [NFM_KIND_HAT]   = {  3, 1, 6, 1,  3, 1, 6, 1,  3, 1, 6, 1,  3, 1, 6, 2 },
    [NFM_KIND_PERC]  = {  2, 1, 3, 4,  1, 2, 3, 2,  2, 4, 1, 3,  2, 1, 4, 2 },
    [NFM_KIND_CLICK] = {  1, 2, 1, 3,  1, 3, 2, 1,  1, 2, 3, 1,  2, 1, 3, 2 },
    [NFM_KIND_METAL] = {  4, 1, 2, 1,  2, 1, 3, 1,  3, 1, 2, 1,  2, 1, 3, 1 },
};

/* Minor pentatonic: percussion pitched to it rarely clashes. */
NFM_DATA static const int8_t scale[] = { 0, 3, 5, 7, 10 };

NFM_CODE static const uint8_t *track_profile(nseq *q, int t)
{
    int k = nfm_slot_kind(q->engine, t);
    return profile[k >= 0 && k < NFM_KINDS ? k : NFM_KIND_PERC];
}

NFM_CODE static int8_t pick_note(nseq *q, int range)
{
    if (range <= 0 || rndf(&q->rng) < 0.5f) return 0;
    int8_t cand[64];
    int n = 0;
    for (int oct = -2; oct <= 2; ++oct)
        for (unsigned i = 0; i < sizeof scale; ++i) {
            int v = oct * 12 + scale[i];
            if (v != 0 && v >= -range && v <= range && n < 64) cand[n++] = (int8_t)v;
        }
    return n ? cand[rnd(&q->rng) % (unsigned)n] : 0;
}

/* A new length of 1..len_random bars. Even bar counts are likelier, and
 * sometimes the last bar is cut short so the track drifts against the rest. */
/* Step weights for generate and evolve. Static, not on the stack: on the FM1
 * these run in the audio interrupt. */
static float work[NSEQ_MAX_STEPS];

NFM_CODE static void random_length(nseq *q, nseq_track *tr)
{
    NFM_DATA static const uint8_t bar_weight[NSEQ_MAX_BARS] = { 3, 4, 1, 3, 1, 1, 1, 2 };
    NFM_DATA static const uint8_t tails[] = { 12, 14, 13, 15, 10, 11, 9, 7, 6, 5 };
    int max = tr->len_random > NSEQ_MAX_BARS ? NSEQ_MAX_BARS : tr->len_random;
    unsigned total = 0, pick;
    int bars;
    for (int b = 0; b < max; ++b) total += bar_weight[b];
    pick = rnd(&q->rng) % total;
    for (bars = 0; pick >= bar_weight[bars]; ++bars) pick -= bar_weight[bars];
    ++bars;
    int tail = NSEQ_BAR;
    if (rndf(&q->rng) < 0.4f) {
        tail = tails[rnd(&q->rng) % (unsigned)sizeof tails];
        if (bars == 1 && tail < 7) tail = 7;
    }
    tr->length = (uint8_t)((bars - 1) * NSEQ_BAR + tail);
    if (tr->pos >= tr->length) tr->pos = 0;
}

/* Weighted draw among free steps; returns -1 if none. */
NFM_CODE static int pick_step(nseq *q, const float *w, int len)
{
    float total = 0.0f;
    for (int s = 0; s < len; ++s) total += w[s];
    if (total <= 0.0f) return -1;
    float r = rndf(&q->rng) * total;
    for (int s = 0; s < len; ++s) {
        r -= w[s];
        if (r < 0.0f && w[s] > 0.0f) return s;
    }
    for (int s = len - 1; s >= 0; --s)
        if (w[s] > 0.0f) return s;
    return -1;
}

NFM_CODE void nseq_generate(nseq *q, int t)
{
    if (!track_ok(t)) return;
    nseq_track *tr = &q->track[t];
    const uint8_t *prof = track_profile(q, t);
    if (tr->len_random) random_length(q, tr);
    int len = tr->length, lo = tr->hits_min, hi = tr->hits_max;
    float *w = work;
    if (hi < lo) { int x = hi; hi = lo; lo = x; }
    int count = lo + (int)(rnd(&q->rng) % (unsigned)(hi - lo + 1));
    if (count > len) count = len;

    nfm_zero(tr->step, sizeof tr->step);
    for (int s = 0; s < len; ++s) w[s] = (float)prof[s % NSEQ_BAR] * (0.8f + 0.4f * rndf(&q->rng));
    /* Kicks usually anchor the loop on its first step. */
    int anchor = nfm_slot_kind(q->engine, t) == NFM_KIND_KICK && rndf(&q->rng) < 0.75f;

    for (int k = 0; k < count; ++k) {
        int s = k == 0 && anchor ? 0 : pick_step(q, w, len);
        if (s < 0) break;
        float strength = (float)prof[s % NSEQ_BAR] / 10.0f;
        int vel = 64 + (int)(strength * 45.0f) + (int)(rndf(&q->rng) * 18.0f);
        tr->step[s] = (nseq_step){ 1, (uint8_t)(vel > 127 ? 127 : vel), pick_note(q, tr->pitch_range), 0 };
        w[s] = 0.0f;
        /* Space hits out, which keeps sparse patterns from clumping... */
        if (len > 2) {
            w[(s + 1) % len] *= 0.3f;
            w[(s + len - 1) % len] *= 0.3f;
        }
        /* ...and echo the same spot in other bars, so ideas repeat. */
        for (int b = s % NSEQ_BAR; b < len; b += NSEQ_BAR)
            if (b != s) w[b] *= 1.6f;
    }
}

NFM_CODE void nseq_generate_all(nseq *q)
{
    for (int t = 0; t < NSEQ_TRACKS; ++t)
        if (!q->track[t].lock) nseq_generate(q, t);
}

/* Move one hit to another free step: the pattern changes, the count does not. */
NFM_CODE static void evolve(nseq *q, int t)
{
    nseq_track *tr = &q->track[t];
    int len = tr->length, on[NSEQ_MAX_STEPS], n = 0;
    for (int s = 0; s < len; ++s)
        if (tr->step[s].on) on[n++] = s;
    if (!n || n >= len) return;
    int from = on[rnd(&q->rng) % (unsigned)n];
    const uint8_t *prof = track_profile(q, t);
    float *w = work;
    for (int s = 0; s < len; ++s) w[s] = tr->step[s].on ? 0.0f : (float)prof[s % NSEQ_BAR];
    int to = pick_step(q, w, len);
    if (to < 0) return;
    tr->step[to] = tr->step[from];
    if (tr->pitch_range && rndf(&q->rng) < 0.3f) tr->step[to].note = pick_note(q, tr->pitch_range);
    tr->step[from].on = 0;
}

NFM_CODE void nseq_init(nseq *q, nfm_engine *engine, uint32_t seed)
{
    nfm_zero(q, sizeof *q);
    q->engine = engine;
    q->rng = seed ? seed : 0x9e3779b9u;
    q->bpm = 112.0f;
    q->swing = 0.0f;
    q->loose = 0.15f;
    q->max_poly = 3;
    /* A sparse starting set with different lengths, so it drifts. */
    /* Kick, snare and hat keep fixed lengths as the anchor; the rest draw
     * their length (up to rnd bars) each time a pattern is made. */
    NFM_DATA static const struct { uint8_t len, lo, hi, pitch, evolve, mute, rnd; } start[8] = {
        { 32, 3, 5, 0, 10, 0, 0 },   /* kick, 2 bars */
        { 32, 2, 4, 0, 0, 0, 0 },    /* snare, 2 bars */
        { 16, 3, 6, 0, 20, 0, 0 },   /* hat, 1 bar */
        { 24, 2, 4, 5, 25, 0, 3 },   /* perc */
        { 13, 1, 2, 0, 30, 0, 2 },   /* click */
        { 64, 1, 3, 7, 20, 1, 4 },   /* metal */
        { 20, 1, 3, 5, 25, 1, 2 },   /* perc */
        { 7, 1, 2, 0, 30, 1, 1 },    /* hat */
    };
    for (int t = 0; t < NSEQ_TRACKS; ++t) {
        nseq_track *tr = &q->track[t];
        int i = t & 7;
        tr->length = start[i].len;
        tr->hits_min = start[i].lo;
        tr->hits_max = start[i].hi;
        tr->pitch_range = start[i].pitch;
        tr->evolve = start[i].evolve;
        tr->mute = start[i].mute;
        tr->len_random = start[i].rnd;
        tr->last = -1;
        nseq_generate(q, t);
    }
    for (int e = 0; e < NSEQ_QUEUE; ++e) q->queue[e].used = 0;
}

NFM_CODE void nseq_start(nseq *q)
{
    q->playing = 1;
    q->to_next = 0.0f;
    q->steps = 0;
    for (int t = 0; t < NSEQ_TRACKS; ++t) {
        q->track[t].pos = 0;
        q->track[t].last = -1;
        q->track[t].loops = 0;
    }
}

NFM_CODE void nseq_stop(nseq *q)
{
    q->playing = 0;
    for (int e = 0; e < NSEQ_QUEUE; ++e) q->queue[e].used = 0;
    for (int t = 0; t < NSEQ_TRACKS; ++t) q->track[t].last = -1;
}

NFM_CODE void nseq_set_length(nseq *q, int t, int steps)
{
    if (!track_ok(t)) return;
    if (steps < 1) steps = 1;
    if (steps > NSEQ_MAX_STEPS) steps = NSEQ_MAX_STEPS;
    q->track[t].length = (uint8_t)steps;
    if (q->track[t].pos >= steps) q->track[t].pos = 0;
}

NFM_CODE void nseq_set_hits(nseq *q, int t, int lo, int hi)
{
    if (!track_ok(t)) return;
    lo = lo < 0 ? 0 : lo > NSEQ_MAX_STEPS ? NSEQ_MAX_STEPS : lo;
    hi = hi < lo ? lo : hi > NSEQ_MAX_STEPS ? NSEQ_MAX_STEPS : hi;
    q->track[t].hits_min = (uint8_t)lo;
    q->track[t].hits_max = (uint8_t)hi;
}

NFM_CODE void nseq_set_step(nseq *q, int t, int s, int on, int vel, int note)
{
    if (!track_ok(t) || s < 0 || s >= NSEQ_MAX_STEPS) return;
    nseq_step *st = &q->track[t].step[s];
    st->on = on ? 1 : 0;
    st->vel = (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel);
    st->note = (int8_t)(note < -24 ? -24 : note > 24 ? 24 : note);
}

NFM_CODE void nseq_set_flag(nseq *q, int t, int which, int v)
{
    if (!track_ok(t)) return;
    nseq_track *tr = &q->track[t];
    switch (which) {
    case NSEQ_FLAG_MUTE: tr->mute = v ? 1 : 0; break;
    case NSEQ_FLAG_LOCK: tr->lock = v ? 1 : 0; break;
    case NSEQ_FLAG_EVOLVE: tr->evolve = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); break;
    case NSEQ_FLAG_PITCH_RANGE: tr->pitch_range = (uint8_t)(v < 0 ? 0 : v > 24 ? 24 : v); break;
    case NSEQ_FLAG_LEN_RANDOM: tr->len_random = (uint8_t)(v < 0 ? 0 : v > NSEQ_MAX_BARS ? NSEQ_MAX_BARS : v); break;
    }
}

NFM_CODE int nseq_midi_note(nseq *q, int note, int velocity)
{
    if (velocity <= 0) return 0;
    if (note >= NSEQ_NOTE_TRACK0 && note < NSEQ_NOTE_TRACK0 + NSEQ_TRACKS) {
        nseq_generate(q, note - NSEQ_NOTE_TRACK0);
    } else if (note == NSEQ_NOTE_ALL) {
        nseq_generate_all(q);
    } else if (note >= NSEQ_NOTE_ROOT_LO && note <= NSEQ_NOTE_ROOT_HI) {
        q->root = (int8_t)(note - NSEQ_NOTE_ROOT_LO);
    } else {
        return 0;
    }
    if (!q->playing) nseq_start(q);
    return 1;
}

NFM_CODE void nseq_set_fx(nseq *q, nfx *fx) { q->fx = fx; }

NFM_CODE static void fire(nseq *q, const nseq_event *e)
{
    int transpose = e->note + (q->track[e->track].pitch_range ? q->root : 0);
    float vel = (float)e->vel * (1.0f / 127.0f);
    if (q->fx) nfx_trigger(q->fx, q->engine, e->track, vel, (float)transpose);
    else nfm_trigger(q->engine, e->track, vel, (float)transpose);
}

NFM_CODE static void schedule(nseq *q, int t, const nseq_step *st, float delay)
{
    float v = (float)st->vel * (1.0f - q->loose * 0.25f * rndf(&q->rng));
    nseq_event e = { q->now + (uint32_t)delay, (uint8_t)t, (uint8_t)(v < 1.0f ? 1.0f : v), st->note, 1 };
    if (delay < 1.0f) { fire(q, &e); return; }
    for (int i = 0; i < NSEQ_QUEUE; ++i)
        if (!q->queue[i].used) { q->queue[i] = e; return; }
    fire(q, &e); /* queue full: play it now rather than drop it */
}

NFM_CODE static void global_step(nseq *q, float step_len)
{
    int cand[NSEQ_TRACKS], n = 0;
    float swing = (q->steps & 1u) ? q->swing * 0.5f * step_len : 0.0f;
    for (int t = 0; t < NSEQ_TRACKS; ++t) {
        nseq_track *tr = &q->track[t];
        if (tr->pos >= tr->length) tr->pos = 0;
        int s = tr->pos;
        tr->last = (int16_t)s;
        if (tr->step[s].on && !tr->mute) cand[n++] = t;
        if (++tr->pos >= tr->length) {
            tr->pos = 0;
            ++tr->loops;
            if (tr->evolve && rndf(&q->rng) * 100.0f < (float)tr->evolve) evolve(q, t);
        }
    }
    /* Never too much: keep only the strongest hits on a crowded step. */
    while (n > q->max_poly) {
        int weakest = 0;
        for (int i = 1; i < n; ++i) {
            const nseq_track *a = &q->track[cand[i]], *b = &q->track[cand[weakest]];
            if (a->step[a->last].vel < b->step[b->last].vel) weakest = i;
        }
        cand[weakest] = cand[--n];
    }
    for (int i = 0; i < n; ++i) {
        nseq_track *tr = &q->track[cand[i]];
        float late = q->loose * 0.12f * step_len * rndf(&q->rng);
        schedule(q, cand[i], &tr->step[tr->last], swing + late);
    }
    ++q->steps;
}

NFM_CODE void nseq_render(nseq *q, float *out_l, float *out_r, int n)
{
    while (n > 0) {
        float step_len = q->engine->sr * 60.0f / (q->bpm < 20.0f ? 20.0f : q->bpm) / 4.0f;
        if (q->playing && q->to_next <= 0.0f) {
            global_step(q, step_len);
            q->to_next += step_len;
        }
        int chunk = n;
        for (int i = 0; i < NSEQ_QUEUE; ++i) {
            nseq_event *e = &q->queue[i];
            if (!e->used) continue;
            int32_t d = (int32_t)(e->at - q->now);
            if (d <= 0) {
                e->used = 0;
                if (q->playing) fire(q, e);
            } else if (d < chunk) {
                chunk = d;
            }
        }
        if (q->playing) {
            int to = (int)q->to_next;
            if ((float)to < q->to_next) ++to;
            if (to < 1) to = 1;
            if (to < chunk) chunk = to;
        }
        if (q->fx) {
            nfx_set_tempo(q->fx, q->bpm);
            nfx_render(q->fx, q->engine, out_l, out_r, chunk);
        } else {
            nfm_render(q->engine, out_l, out_r, chunk);
        }
        out_l += chunk;
        out_r += chunk;
        n -= chunk;
        q->now += (uint32_t)chunk;
        if (q->playing) q->to_next -= (float)chunk;
    }
}

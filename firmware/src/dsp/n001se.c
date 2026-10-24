/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se: the instrument (see n001se.h). A separate unit at -O2 (tools/build.py), with nfm.c, nseq.c
 * and nfx.c. */
#include "n001se.h"

#ifdef OM_HOST
#define NE_POOL
#else
#define NE_POOL __attribute__((section(".pool")))
#endif

static nfm_engine fm;
static nseq seq;
static nfx fx NE_POOL;                 /* the delay and reverb lines: 180 kB */

volatile float ne_track_level[NE_TRACKS];
volatile float ne_out_level;
volatile uint32_t ne_dropped;
volatile uint8_t ne_ext;
volatile uint16_t ne_bpm_x10 = 1120;

/* ------------------------------------------------------------ parameters --- */
static const ne_param_t GP[G_NPARAMS] = {
    [G_TEMPO] = {"Tempo", 60, 200, 112},
    [G_SWING] = {"Swing", 0, 100, 0},
    [G_LOOSE] = {"Loose", 0, 100, 15},
    [G_POLY] = {"Max hits", 1, 4, 3},
    [G_CHAOS] = {"Chaos", 0, 100, 40},
    [G_MOTION] = {"Motion", 0, 100, 30},
    [G_REVERB] = {"Reverb", 0, 100, 30},
    [G_DELAY] = {"Delay", 0, 100, 25},
    [G_FILTER] = {"Filter", 0, 100, 30},
    [G_DRIVE] = {"Drive", 0, 100, 20},
    [G_LEVEL] = {"Level", 0, 100, 80},
    [G_MIDICH] = {"MIDI ch", 0, 16, 0},
};
static const ne_param_t TP[T_NPARAMS] = {
    [T_BARS] = {"Bars", 1, 8, 2},
    [T_RANDLEN] = {"Rnd len", 0, 8, 0},
    [T_HITLO] = {"Hits min", 1, 64, 2},
    [T_HITHI] = {"Hits max", 1, 64, 4},
    [T_PITCH] = {"Pitch", 0, 12, 0},
    [T_EVOLVE] = {"Evolve", 0, 100, 10},
    [T_KIND] = {"Kind", 0, NFM_KINDS - 1, 0},
    [T_MUTE] = {"Mute", 0, 1, 0},
    [T_DEV] = {"Deviance", 0, 100, 40},
    [T_STRESS] = {"Length", 0, 100, 50},
    [T_SPIKE] = {"Edge", 0, 100, 50},
    [T_BLEED] = {"Ring", 0, 100, 50},
    [T_FLUID] = {"Ring tone", 0, 100, 50},
    [T_LEVEL] = {"Level", 0, 100, 80},
};
/* the starting set: kick, snare, hat, perc; kick and snare 2 bars, hat 1, perc drifting */
static const int8_t T_DEF[NE_TRACKS][T_NPARAMS] = {
    {2, 0, 3, 5, 0, 10, NFM_KIND_KICK, 0, 40, 50, 50, 50, 50, 80},
    {2, 0, 2, 4, 0, 0, NFM_KIND_SNARE, 0, 40, 50, 50, 50, 50, 75},
    {1, 0, 3, 6, 0, 20, NFM_KIND_HAT, 0, 40, 50, 50, 50, 50, 70},
    {2, 3, 2, 4, 5, 25, NFM_KIND_PERC, 0, 40, 50, 50, 50, 50, 75},
};

const ne_param_t *ne_param_info(int i) { return &GP[(unsigned)i < G_NPARAMS ? i : 0]; }
const ne_param_t *ne_tparam_info(int i) { return &TP[(unsigned)i < T_NPARAMS ? i : 0]; }
int ne_tparam_def(int t, int i) { return T_DEF[t & 3][(unsigned)i < T_NPARAMS ? i : 0]; }

static const char *const KIND_NAME[NFM_KINDS] = {"Kick", "Snare", "Hat", "Perc", "Click", "Metal"};
const char *ne_kind_name(int k) { return (unsigned)k < NFM_KINDS ? KIND_NAME[k] : "?"; }

static void itoa_s(int v, char *b)
{
    char t[12];
    int n = 0;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    if (v < 0)
        *b++ = '-';
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void cpy(char *d, const char *s)
{
    while ((*d++ = *s++))
        ;
}

void ne_param_text(int i, int v, char *b)
{
    switch (i) {
    case G_TEMPO: case G_POLY: itoa_s(v, b); return;
    case G_MIDICH: if (v) itoa_s(v, b); else cpy(b, "All"); return;
    case G_REVERB: case G_DELAY: case G_FILTER: case G_DRIVE: if (!v) { cpy(b, "Off"); return; } break;
    }
    itoa_s(v, b);
}

void ne_tparam_text(int i, int v, char *b)
{
    switch (i) {
    case T_KIND: cpy(b, ne_kind_name(v)); return;
    case T_MUTE: cpy(b, v ? "Muted" : "On"); return;
    case T_RANDLEN: if (v) { b[0] = '1'; b[1] = '-'; itoa_s(v, b + 2); } else cpy(b, "Fixed"); return;
    case T_PITCH: if (v) { b[0] = '+'; b[1] = '-'; itoa_s(v, b + 2); } else cpy(b, "Root"); return;
    }
    itoa_s(v, b);
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_TSET, C_PLAY, C_STEP, C_GEN, C_SOUND, C_HIT, C_NOTE, C_CLOCK, C_VOICES, C_PANIC };
typedef struct {
    uint8_t op;
    int8_t t;
    int16_t a, b, c, d;
} cmd_t;
#define QN 128u                        /* a project_apply() posts ~70 before the audio starts */
static cmd_t q[QN];
static volatile uint32_t q_w, q_r;
static volatile uint8_t hold;          /* ne_restore() is writing: the render stays silent */

static void post(int op, int t, int a, int b, int c, int d)
{
    uint32_t w = q_w;
    if (w - q_r >= QN) {
        ne_dropped++;
        return;
    }
    q[w % QN] = (cmd_t){(uint8_t)op, (int8_t)t, (int16_t)a, (int16_t)b, (int16_t)c, (int16_t)d};
    __asm__ volatile("" ::: "memory");
    q_w = w + 1u;
}

void ne_set(int p, int v) { post(C_SET, 0, p, v, 0, 0); }
void ne_tset(int t, int p, int v) { post(C_TSET, t, p, v, 0, 0); }
void ne_play(int on) { post(C_PLAY, 0, on, 0, 0, 0); }
void ne_step(int t, int s, int on, int vel, int note) { post(C_STEP, t, s, on, vel, note); }
void ne_generate(int t) { post(C_GEN, t, 0, 0, 0, 0); }
void ne_new_sound(int t, int kind) { post(C_SOUND, t, kind, 0, 0, 0); }
void ne_hit(int t, int vel, int note) { post(C_HIT, t, vel, note, 0, 0); }
void ne_midi_note(int note, int vel) { post(C_NOTE, 0, note, vel, 0, 0); }
void ne_clock(int m) { post(C_CLOCK, 0, m, 0, 0, 0); }
void ne_voices(int n) { post(C_VOICES, 0, n, 0, 0, 0); }
void ne_panic(void) { post(C_PANIC, 0, 0, 0, 0, 0); }

/* ------------------------------------------------------------ the state --- */
static int16_t gpar[G_NPARAMS], tpar[NE_TRACKS][T_NPARAMS];
static float lvl_base[NE_TRACKS];      /* each sound's own level (its recipe's): Level trims it */

static float pct(int v) { return (float)v * 0.01f; }

static void apply_level(int t)
{
    nfm_set_param(&fm, t, NFM_LEVEL, lvl_base[t] * pct(tpar[t][T_LEVEL]) * 1.25f);
}

static void apply_fx(int p, int v)
{
    static const int8_t FX_OF[G_NPARAMS] = {[G_REVERB] = NFX_REVERB + 1, [G_DELAY] = NFX_DELAY + 1,
                                            [G_FILTER] = NFX_FILTER + 1, [G_DRIVE] = NFX_DRIVE + 1};
    int e = FX_OF[p] - 1;
    nfx_set_on(&fx, e, v > 0);
    nfx_set_amount(&fx, e, pct(v));
}

static void gset(int p, int v)
{
    if ((unsigned)p >= G_NPARAMS)
        return;
    v = v < GP[p].lo ? GP[p].lo : v > GP[p].hi ? GP[p].hi : v;
    gpar[p] = (int16_t)v;
    switch (p) {
    case G_TEMPO: if (!ne_ext) { seq.bpm = (float)v; ne_bpm_x10 = (uint16_t)(v * 10); } break;
    case G_SWING: seq.swing = pct(v); break;
    case G_LOOSE: seq.loose = pct(v); break;
    case G_POLY: seq.max_poly = (uint8_t)v; break;
    case G_CHAOS: nfx_set_chaos(&fx, pct(v)); break;
    case G_MOTION: nfx_set_motion(&fx, pct(v)); break;
    case G_REVERB: case G_DELAY: case G_FILTER: case G_DRIVE: apply_fx(p, v); break;
    case G_LEVEL: fm.master = pct(v); break;
    }
}

static void tset(int t, int p, int v)
{
    nseq_track *tr;
    if ((unsigned)t >= NE_TRACKS || (unsigned)p >= T_NPARAMS)
        return;
    v = v < TP[p].lo ? TP[p].lo : v > TP[p].hi ? TP[p].hi : v;
    tpar[t][p] = (int16_t)v;
    tr = &seq.track[t];
    switch (p) {
    case T_BARS: nseq_set_length(&seq, t, v * NE_BAR); break;
    case T_RANDLEN: nseq_set_flag(&seq, t, NSEQ_FLAG_LEN_RANDOM, v); break;
    case T_HITLO: nseq_set_hits(&seq, t, v, tpar[t][T_HITHI] < v ? v : tpar[t][T_HITHI]); break;
    case T_HITHI: nseq_set_hits(&seq, t, tpar[t][T_HITLO] > v ? v : tpar[t][T_HITLO], v); break;
    case T_PITCH: nseq_set_flag(&seq, t, NSEQ_FLAG_PITCH_RANGE, v); break;
    case T_EVOLVE: nseq_set_flag(&seq, t, NSEQ_FLAG_EVOLVE, v); break;
    case T_MUTE: nseq_set_flag(&seq, t, NSEQ_FLAG_MUTE, v); break;
    case T_DEV: nfm_set_macro(&fm, t, NFM_DEVIANCE, pct(v)); break;
    case T_STRESS: nfm_set_macro(&fm, t, NFM_STRESS, pct(v)); break;
    case T_SPIKE: nfm_set_macro(&fm, t, NFM_SPIKE, pct(v)); break;
    case T_BLEED: nfm_set_macro(&fm, t, NFM_BLEED, pct(v)); break;
    case T_FLUID: nfm_set_macro(&fm, t, NFM_FLUID, pct(v)); break;
    case T_LEVEL: apply_level(t); break;
    case T_KIND: break;                /* takes effect with the next new sound */
    }
    (void)tr;
}

static void new_sound(int t, int kind)
{
    if ((unsigned)t >= NE_TRACKS)
        return;
    if (kind < 0)
        kind = tpar[t][T_KIND];
    tpar[t][T_KIND] = (int16_t)kind;
    nfm_randomize(&fm, t, kind);
    lvl_base[t] = nfm_get_param(&fm, t, NFM_LEVEL);
    apply_level(t);
    /* the macros are the track's, not the sound's */
    tset(t, T_DEV, tpar[t][T_DEV]);
    tset(t, T_STRESS, tpar[t][T_STRESS]);
    tset(t, T_SPIKE, tpar[t][T_SPIKE]);
    tset(t, T_BLEED, tpar[t][T_BLEED]);
    tset(t, T_FLUID, tpar[t][T_FLUID]);
}

void ne_init(void)
{
    int t, p;
    nfm_init(&fm, NE_SR, 0x6e303031u);
    nfx_init(&fx, NE_SR, 0x6e736521u);
    nseq_init(&seq, &fm, 0x73657121u);
    nseq_set_fx(&seq, &fx);
    for (p = 0; p < G_NPARAMS; p++)
        gset(p, GP[p].def);
    for (t = 0; t < NE_TRACKS; t++) {
        for (p = 0; p < T_NPARAMS; p++)
            tset(t, p, T_DEF[t][p]);
        new_sound(t, T_DEF[t][T_KIND]);
        nseq_generate(&seq, t);
    }
    ne_ext = 0;
    q_r = q_w = 0;
}

/* ------------------------------------------------------------ save, load --- */
static uint16_t u16(float x) { return (uint16_t)(x <= 0.0f ? 0 : x >= 1.0f ? 65535 : (int)(x * 65535.0f + 0.5f)); }

void ne_snapshot(ne_state_t *s)
{
    int t, i;
    for (t = 0; t < NE_TRACKS; t++) {
        const nseq_track *tr = &seq.track[t];
        s->len[t] = tr->length;
        for (i = 0; i < NE_STEPS; i++) {
            const nseq_step *st = &tr->step[i];
            s->step[t][i] = st->on ? (uint16_t)((st->vel ? st->vel : 1) | (uint16_t)(st->note + 32) << 7) : 0u;
        }
        for (i = 0; i < NFM_PARAMS; i++)
            s->patch[t][i] = u16(i == NFM_LEVEL ? lvl_base[t] : fm.slot[t].p[i]);
        s->kind[t] = fm.slot[t].kind;
    }
}

void ne_restore(const ne_state_t *s)
{
    int t, i;
    hold = 1;
    __asm__ volatile("" ::: "memory");
    nfm_stop_all(&fm);
    for (t = 0; t < NE_TRACKS; t++) {
        nseq_track *tr = &seq.track[t];
        tr->length = (uint8_t)(s->len[t] < 1 ? 1 : s->len[t] > NE_STEPS ? NE_STEPS : s->len[t]);
        if (tr->pos >= tr->length)
            tr->pos = 0;
        for (i = 0; i < NE_STEPS; i++) {
            uint16_t v = s->step[t][i];
            int note = (int)(v >> 7 & 63u) - 32;
            tr->step[i].on = v != 0;
            tr->step[i].vel = (uint8_t)(v & 127u ? v & 127u : 1u);
            tr->step[i].note = (int8_t)(v ? (note < -24 ? -24 : note > 24 ? 24 : note) : 0);
        }
        for (i = 0; i < NFM_PARAMS; i++) {
            float x = (float)s->patch[t][i] * (1.0f / 65535.0f);
            if (i == NFM_LEVEL)
                lvl_base[t] = x;
            else
                fm.slot[t].p[i] = x;
            fm.slot[t].walk[i] = 0.0f;
        }
        fm.slot[t].kind = (int8_t)((unsigned)s->kind[t] < NFM_KINDS ? s->kind[t] : NFM_KIND_PERC);
        tpar[t][T_KIND] = fm.slot[t].kind;
        apply_level(t);
    }
    __asm__ volatile("" ::: "memory");
    hold = 0;
}

const nseq *ne_seq(void) { return &seq; }
const nfm_engine *ne_fm(void) { return &fm; }
int ne_fx_owner(int which) { return nfx_owner(&fx, &fm, which); }

/* ------------------------------------------------------------ MIDI clock --- */
/* 24 ticks a quarter, 6 a step. The tempo is the ticks' (smoothed); while they come (the last within
 * ~0.5 s) G_TEMPO waits. Start and continue start the pattern from its top, stop stops it. */
static uint32_t clk_last, clk_n, clk_seen;
static float clk_avg;
static void clock_msg(int m, uint32_t now)
{
    if (m == NE_CLK_TICK) {
        if (clk_n && now - clk_last < 22050u) {
            float d = (float)(now - clk_last);
            clk_avg = clk_n < 3u ? d : clk_avg + (d - clk_avg) * 0.1f;
            if (clk_n >= 3u && clk_avg > 1.0f) {
                float bpm = NE_SR * 60.0f / (clk_avg * 24.0f);
                seq.bpm = bpm < 20.0f ? 20.0f : bpm > 300.0f ? 300.0f : bpm;
                ne_bpm_x10 = (uint16_t)(seq.bpm * 10.0f + 0.5f);
                ne_ext = 1;
            }
        }
        clk_n++;
        clk_last = now;
        clk_seen = now;
    } else if (m == NE_CLK_START || m == NE_CLK_CONTINUE) {
        nseq_start(&seq);
    } else if (m == NE_CLK_STOP) {
        nseq_stop(&seq);
    }
}

/* ------------------------------------------------------------ the render --- */
static void drain(void)
{
    while (q_r != q_w) {
        cmd_t c = q[q_r % QN];
        __asm__ volatile("" ::: "memory");
        q_r = q_r + 1u;
        switch (c.op) {
        case C_SET: gset(c.a, c.b); break;
        case C_TSET: tset(c.t, c.a, c.b); break;
        case C_PLAY:
            if (c.a && !seq.playing)
                nseq_start(&seq);
            else if (!c.a && seq.playing)
                nseq_stop(&seq);
            break;
        case C_STEP: nseq_set_step(&seq, c.t, c.a, c.b, c.c, c.d); break;
        case C_GEN:
            if (c.t < 0) {
                int t;
                for (t = 0; t < NE_TRACKS; t++)
                    if (!seq.track[t].mute)
                        nseq_generate(&seq, t);
            } else if (c.t < NE_TRACKS) {
                nseq_generate(&seq, c.t);
            }
            break;
        case C_SOUND: new_sound(c.t, c.a); break;
        case C_HIT:
            if ((unsigned)c.t < NE_TRACKS)
                nfx_trigger(&fx, &fm, c.t, (float)c.a * (1.0f / 127.0f), (float)c.b);
            break;
        case C_NOTE: nseq_midi_note(&seq, c.a, c.b); break;
        case C_CLOCK: clock_msg(c.a, seq.now); break;
        case C_VOICES: fm.voice_limit = c.a < 2 ? 2 : c.a > NFM_VOICES ? NFM_VOICES : c.a; break;
        case C_PANIC: nseq_stop(&seq); nfm_stop_all(&fm); break;
        }
    }
}

#define BLK 64
void ne_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    static float l[BLK], r[BLK];
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 8388607.0f;
    float pk = 0.0f;
    uint32_t j;
    int t, v;
    if (hold) {
        for (j = 0; j < 2u * n; j++)
            out[j] = 0;
        return;
    }
    drain();
    if (ne_ext && seq.now - clk_seen > 22050u) {   /* the clock stopped coming: our own tempo again */
        ne_ext = 0;
        clk_n = 0;
        seq.bpm = (float)gpar[G_TEMPO];
        ne_bpm_x10 = (uint16_t)(gpar[G_TEMPO] * 10);
    }
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        nseq_render(&seq, l, r, (int)k);
        for (j = 0; j < k; j++) {
            float a = l[j], b = r[j];
            float m = (a < 0.0f ? -a : a) > (b < 0.0f ? -b : b) ? (a < 0.0f ? -a : a) : (b < 0.0f ? -b : b);
            if (m > pk)
                pk = m;
            out[2u * j] = (int32_t)(a * gain);
            out[2u * j + 1u] = (int32_t)(b * gain);
        }
        out += 2u * k;
        n -= k;
    }
    ne_out_level = pk * (float)gain_q12 * (1.0f / 4096.0f);
    for (t = 0; t < NE_TRACKS; t++) {
        float lv = 0.0f;
        for (v = 0; v < NFM_VOICES; v++)
            if (fm.voice[v].slot == t && fm.voice[v].peak > lv)
                lv = fm.voice[v].peak;
        ne_track_level[t] = lv;
    }
}

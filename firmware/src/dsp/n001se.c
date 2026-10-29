/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se: the instrument (see n001se.h). A separate unit at -O2 (tools/build.py), with nfm.c, nseq.c,
 * nfx.c and nacid.c. */
#include "n001se.h"

#ifdef OM_HOST
#define NE_POOL
#else
#define NE_POOL __attribute__((section(".pool")))
#endif

static nfm_engine fm;
static nseq seq;
static nfx fx NE_POOL;                 /* the delay and reverb lines: 180 kB */
static nacid acid;
static void acid_route(int bar);
/* the modulation effects' delay lines (flanger, chorus): one per drum voice, one for the acid line */
static int16_t mod_lines[NFM_VOICES + 1][NFM_MOD_LEN] NE_POOL;

volatile float ne_track_level[NE_TRACKS];
volatile float ne_out_level;
volatile float ne_acid_level;
volatile uint32_t ne_glitches;
volatile uint32_t ne_mutades;
volatile uint32_t ne_limited;
volatile uint32_t ne_dropped;
volatile uint8_t ne_ext;
volatile uint8_t ne_usb_audio = 1;
volatile uint16_t ne_bpm_x10 = 1120;

/* ------------------------------------------------------------ parameters --- */
static const ne_param_t GP[G_NPARAMS] = {
    [G_TEMPO] = {"Tempo", 60, 200, 112},
    [G_SWING] = {"Swing", 0, 100, 0},
    [G_LOOSE] = {"Loose", 0, 100, 15},
    [G_POLY] = {"Max hits", 1, 4, 4},
    [G_CHAOS] = {"Chaos", 0, 100, 40},
    [G_MOTION] = {"Motion", 0, 100, 30},
    [G_REVERB] = {"Reverb", 0, 100, 70},             /* the returns; the sends are per track */
    [G_DELAY] = {"Delay", 0, 100, 70},
    [G_FILTER] = {"Filter", 0, 100, 100},            /* scales the tracks' filter and drive */
    [G_DRIVE] = {"Drive", 0, 100, 100},
    [G_LEVEL] = {"Level", 0, 100, 80},
    [G_MIDICH] = {"MIDI ch", 0, 16, 0},
    [G_HICUT] = {"Hi cut", 0, 100, 100},
    [G_LOCUT] = {"Lo cut", 0, 100, 0},
    [G_RESO] = {"Reso", 0, 100, 0},
    [G_GLUE] = {"Glue", 0, 100, 30},
    [G_PKNOB] = {"Presets", 0, 0, 0},                 /* (hi: NE_PKNOBS - 1, below) */
    [G_USBAUDIO] = {"USB audio", 0, 1, 1},
    [G_MUTADE] = {"Mutade", 0, 100, 0},
    [G_SELMODE] = {"SEL mode", 0, NE_SEL_MODES - 1, NE_SEL_CHANCE},
    [G_SPACE] = {"Space", 0, 100, 0},
    [G_GMIX] = {"Glue mix", 0, 100, 50},             /* the Glue page: its mastering chain */
    [G_GEQ] = {"Glue EQ", 0, 100, 50},
    [G_GCOLOR] = {"Color", 0, 100, 50},
    [G_GWIDTH] = {"Width", 0, 100, 60},
};
static const ne_param_t TP[T_NPARAMS] = {
    [T_BARS] = {"Bars", 1, 8, 2},
    [T_RANDLEN] = {"Rnd len", 0, 8, 0},
    [T_HITLO] = {"Hits min", 1, 64, 2},
    [T_HITHI] = {"Hits max", 1, 64, 4},
    [T_PITCH] = {"Pitch", 0, 24, 0},
    [T_EVOLVE] = {"Evolve", 0, 100, 10},
    [T_KIND] = {"Kind", 0, NFM_KINDS - 1, 0},
    [T_MUTE] = {"Mute", 0, 1, 0},
    [T_DEV] = {"Deviance", 0, 100, 40},
    [T_STRESS] = {"Length", 0, 100, 50},
    [T_SPIKE] = {"Edge", 0, 100, 50},
    [T_BLEED] = {"Ring", 0, 100, 50},
    [T_FLUID] = {"Ring tone", 0, 100, 50},
    [T_LEVEL] = {"Level", 0, 100, 80},
    [T_TUNE] = {"Tune", -24, 24, 0},
    [T_REVERB] = {"Reverb", 0, 100, 0},
    [T_DELAY] = {"Delay", 0, 100, 0},
    [T_FILTER] = {"Filter", 0, 100, 0},
    [T_DRIVE] = {"Drive", 0, 100, 0},
    [T_MOD] = {"Mod", 0, NFM_MODS - 1, 0},
    [T_MODAMT] = {"Amount", 0, 100, 50},
    [T_MODRATE] = {"Rate", 0, 100, 30},
    [T_MODCHANCE] = {"Chance", 0, 100, 60},
};
static const ne_param_t AP[A_NPARAMS] = {
    [A_BARS] = {"Bars", 1, 4, 1},
    [A_NOTES] = {"Notes", 10, 100, 60},
    [A_SLIDES] = {"Slides", 0, 100, 20},
    [A_ACCENTS] = {"Accents", 0, 100, 25},
    [A_OCTAVES] = {"Octaves", 0, 100, 30},
    [A_SCALE] = {"Scale", 0, NACID_SCALES - 1, 0},
    [A_EVOLVE] = {"Evolve", 0, 100, 15},
    [A_MUTE] = {"Mute", 0, 1, 0},
    [A_CUTOFF] = {"Cutoff", 0, 100, 30},
    [A_RESO] = {"Reso", 0, 100, 70},
    [A_ENVMOD] = {"Env mod", 0, 100, 60},
    [A_DECAY] = {"Decay", 0, 100, 40},
    [A_ACCENT] = {"Accent", 0, 100, 60},
    [A_WAVE] = {"Wave", 0, NACID_WAVES - 1, 0},
    [A_TUNE] = {"Tune", -24, 24, 0},
    [A_LEVEL] = {"Level", 0, 100, 70},
    [A_REVERB] = {"Reverb", 0, 100, 10},
    [A_DELAY] = {"Delay", 0, 100, 25},
    [A_FILTER] = {"Filter", 0, 100, 0},
    [A_DRIVE] = {"Drive", 0, 100, 35},
    [A_MOD] = {"Mod", 0, NFM_MODS - 1, 0},
    [A_MODAMT] = {"Amount", 0, 100, 50},
    [A_MODRATE] = {"Rate", 0, 100, 30},
    [A_MODCHANCE] = {"Chance", 0, 100, 60},
    [A_CHAOS] = {"Chaos", 0, 100, 25},
};
const ne_param_t *ne_aparam_info(int i) { return &AP[(unsigned)i < A_NPARAMS ? i : 0]; }

/* the starting set: kick, snare, hat, perc; kick and snare 2 bars, hat 1, perc drifting */
static const int8_t T_DEF[NE_TRACKS][T_NPARAMS] = {
    {2, 0, 3, 5, 0, 10, NFM_KIND_KICK, 0, 40, 50, 50, 50, 50, 80, 0, 0, 0, 0, 25, 0, 50, 30, 60},
    {2, 0, 2, 4, 0, 0, NFM_KIND_SNARE, 0, 40, 50, 50, 50, 50, 75, 0, 25, 10, 20, 20, 0, 50, 30, 60},
    {1, 0, 3, 6, 0, 20, NFM_KIND_HAT, 0, 40, 50, 50, 50, 50, 70, 0, 10, 30, 30, 0, 0, 50, 30, 60},
    {2, 3, 2, 4, 5, 25, NFM_KIND_PERC, 0, 40, 50, 50, 50, 50, 75, 0, 30, 35, 40, 10, 0, 50, 30, 60},
};

/* the PRESETS encoder: any knob's value (Glue first, the default) */
static const uint8_t PKNOB[] = {
    G_GLUE, G_TEMPO, G_SWING, G_LOOSE, G_POLY, G_CHAOS, G_MOTION, G_MUTADE, G_REVERB, G_DELAY, G_FILTER, G_DRIVE, G_LEVEL,
    G_HICUT, G_LOCUT, G_RESO,
    NE_PK_TRACK | T_HITLO, NE_PK_TRACK | T_HITHI, NE_PK_TRACK | T_PITCH, NE_PK_TRACK | T_EVOLVE,
    NE_PK_TRACK | T_DEV, NE_PK_TRACK | T_STRESS, NE_PK_TRACK | T_SPIKE, NE_PK_TRACK | T_BLEED,
    NE_PK_TRACK | T_FLUID, NE_PK_TRACK | T_LEVEL, NE_PK_TRACK | T_TUNE,
    NE_PK_TRACK | T_MODAMT, NE_PK_TRACK | T_MODRATE, NE_PK_TRACK | T_MODCHANCE,
    NE_PK_ACID | A_CUTOFF, NE_PK_ACID | A_RESO, NE_PK_ACID | A_ENVMOD, NE_PK_ACID | A_DECAY,
    NE_PK_ACID | A_ACCENT, NE_PK_ACID | A_TUNE, NE_PK_ACID | A_LEVEL,
    NE_PK_ACID | A_MODAMT, NE_PK_ACID | A_MODRATE,
};
#define NE_PKNOBS ((int)sizeof PKNOB)
int ne_pknob_target(int v) { return PKNOB[(unsigned)v < (unsigned)NE_PKNOBS ? v : 0]; }

const ne_param_t *ne_param_info(int i)
{
    static ne_param_t pk;                        /* G_PKNOB's range is the table's */
    if (i == G_PKNOB) {
        pk = GP[G_PKNOB];
        pk.hi = NE_PKNOBS - 1;
        return &pk;
    }
    return &GP[(unsigned)i < G_NPARAMS ? i : 0];
}
const ne_param_t *ne_tparam_info(int i) { return &TP[(unsigned)i < T_NPARAMS ? i : 0]; }
int ne_tparam_def(int t, int i) { return T_DEF[t & 3][(unsigned)i < T_NPARAMS ? i : 0]; }

static const char *const KIND_NAME[NFM_KINDS] = {"Kick", "Snare", "Hat", "Perc", "Click", "Metal", "Clap"};
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
static const char *const MOD_NAME[NFM_MODS] = {"Off", "Phaser", "Flanger", "Chorus", "Ringmod", "Tremolo"};

void ne_param_text(int i, int v, char *b)
{
    switch (i) {
    case G_TEMPO: case G_POLY: itoa_s(v, b); return;
    case G_MIDICH: if (v) itoa_s(v, b); else cpy(b, "All"); return;
    case G_USBAUDIO: cpy(b, v ? "On" : "Off"); return;
    case G_SELMODE: cpy(b, v == NE_SEL_AB ? "A/B" : v == NE_SEL_COPY ? "Copy" : "Chance"); return;
    case G_PKNOB: {
        int k = ne_pknob_target(v);
        if (k & NE_PK_ACID) {                    /* "303 Cutoff" */
            cpy(b, "303 ");
            cpy(b + 4, AP[k & ~NE_PK_ACID].name);
        } else {
            cpy(b, k & NE_PK_TRACK ? TP[k & ~NE_PK_TRACK].name : GP[k].name);
        }
        return;
    }
    case G_HICUT: if (v == 100) { cpy(b, "Open"); return; } break;
    case G_LOCUT: if (!v) { cpy(b, "Open"); return; } break;
    case G_GWIDTH: if (!v) { cpy(b, "Mono"); return; } break;
    case G_REVERB: case G_DELAY: case G_FILTER: case G_DRIVE: case G_GLUE: case G_MUTADE: case G_GEQ: case G_GCOLOR: if (!v) { cpy(b, "Off"); return; } break;
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
    case T_TUNE: if (v > 0) { b[0] = '+'; itoa_s(v, b + 1); } else itoa_s(v, b); return;
    case T_MOD: cpy(b, MOD_NAME[(unsigned)v < NFM_MODS ? v : 0]); return;
    }
    itoa_s(v, b);
}

void ne_aparam_text(int i, int v, char *b)
{
    static const char *const SCALE[NACID_SCALES] = {"Minor", "Phrygian", "Dorian", "Penta", "Blues", "Harm min"};
    switch (i) {
    case A_SCALE: cpy(b, SCALE[(unsigned)v < NACID_SCALES ? v : 0]); return;
    case A_MUTE: cpy(b, v ? "Muted" : "On"); return;
    case A_WAVE: {
        static const char *const WAVE[NACID_WAVES] = {"Saw", "Square", "Pulse", "Tri", "Sine", "Duo"};
        cpy(b, WAVE[(unsigned)v < NACID_WAVES ? v : 0]);
        return;
    }
    case A_TUNE: if (v > 0) { b[0] = '+'; itoa_s(v, b + 1); } else itoa_s(v, b); return;
    case A_MOD: cpy(b, MOD_NAME[(unsigned)v < NFM_MODS ? v : 0]); return;
    }
    itoa_s(v, b);
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_TSET, C_PLAY, C_STEP, C_GEN, C_SOUND, C_HIT, C_NOTE, C_CLOCK, C_VOICES, C_PANIC, C_ASET, C_ASTEP, C_SPACE };
typedef struct {
    uint8_t op;
    int8_t t;
    int16_t a, b, c, d;
} cmd_t;
#define QN 256u                        /* a project_apply() posts ~120 before the audio starts */
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
void ne_space_shift(void) { post(C_SPACE, 0, 0, 0, 0, 0); }
void ne_aset(int p, int v) { post(C_ASET, 0, p, v, 0, 0); }
void ne_astep(int s, int flags, int note) { post(C_ASTEP, 0, s, flags, note, 0); }

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

/* the master filter: a resonant low-pass (Hi cut, 100 Hz..20 kHz) and a high-pass (Lo cut, 20 Hz..
 * 2 kHz), TPT state variable filters, per channel. Both open: not run. */
static float mf_g[2], mf_k, mf_s[2][2][2];       /* [lp, hp][channel][state] */
static uint8_t mf_on[2];
static float exp2_(float x)                      /* (nfx.c's) */
{
    union { float f; uint32_t u; } v;
    int i;
    float f, p;
    if (x < -126.0f)
        return 0.0f;
    if (x > 126.0f)
        x = 126.0f;
    i = (int)x;
    if ((float)i > x)
        --i;
    f = x - (float)i;
    p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f))));
    v.u = (uint32_t)(i + 127) << 23;
    return v.f * p;
}
static float tan_pi(float x)                     /* tan(pi x), up to x 0.3 (13 kHz): nfx.c's */
{
    float t, t2;
    if (x > 0.3f)
        x = 0.3f;
    t = x * 3.14159265f;
    t2 = t * t;
    return t * (1.0f + t2 * (0.33333333f + t2 * (0.13333333f + t2 * (0.05396825f + t2 * 0.02186949f))));
}
static void master_filter(void)
{
    float hc = 100.0f * exp2_(7.6438562f * pct(gpar[G_HICUT])), lc = 20.0f * exp2_(6.6438562f * pct(gpar[G_LOCUT]));
    mf_on[0] = gpar[G_HICUT] < 100;
    mf_on[1] = gpar[G_LOCUT] > 0;
    mf_g[0] = tan_pi(hc / NE_SR);
    mf_g[1] = tan_pi(lc / NE_SR);
    mf_k = 2.0f - 1.9f * pct(gpar[G_RESO]);       /* 2: no peak .. 0.1: a sharp one */
}
static float log2_(float x)                      /* x > 0; to about 1e-4 */
{
    union { float f; uint32_t u; } v;
    float e, m;
    v.f = x;
    e = (float)((int)(v.u >> 23 & 255u) - 127);
    v.u = (v.u & 0x7FFFFFu) | 0x3F800000u;
    m = v.f;
    return e - 1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m;
}

/* GLUE: a light mastering chain, all of it off at 0 (Glue's amount, the Master page; the rest on the Glue
 * page):
 *   COMPRESSOR  both channels together (the louder one leads). Attack 25 ms, release 120 ms; the amount
 *               lowers the threshold (-8 .. -28 dB) and raises the ratio (2:1 .. 6:1). A transient
 *               EXPANDER in it (a fast level over a slow one, up to +1.2 dB on a hit at 100) gives back
 *               the punch. Glue mix: how much of it, in parallel with what goes in (100: all of it).
 *               Over 60 it also warms (nfm_sat, more towards 100: over the top).
 *   EQ          five bands that sum back to the signal (one-pole splits at 90, 700, 2000, 9000 Hz). The
 *               mix's balance over and under 700 Hz is followed (~1.5 s) and leaned towards a slope of
 *               -4.5 dB an octave ("pinky": a share of the way, up to 3 dB each side); a little more
 *               foundation (under 90 Hz), less box (90 .. 700), more air (over 9 kHz); de-harsh: 2 .. 9 kHz
 *               turned down while it stands out (followed ~15 ms in, 100 ms out). Glue EQ: 50 is the set
 *               it was tuned to (pinky 54 %, air +2 dB, foundation +1.1, box -0.8, de-harsh 12 %), 100
 *               twice that, 0 flat.
 *   WIDTH       the sides (the difference) under 120 Hz taken out, the lows in mono; Width scales the rest
 *               (0 mono, 50 as it was, 100 twice as wide).
 *   COLOR       an inflator (its curve 0: a little louder at low levels, denser near the top), blended in.
 * The loudness of it all is matched to what goes in (both followed over ~300 ms): it glues and colours,
 * the volume stays (that is the MASTER's).
 * Then the LIMITER, always on: it takes any peak over the ceiling (-0.3 dBFS; -1 dBFS with the Glue on,
 * room for what a DAC makes between samples) at once and lets go over 60 ms, so the output never passes
 * 0 dBFS. (Levels in log2.) */
#define GL_ATT 0.000907f                         /* 1 - exp(-1 / (0.025 s * 44100)) */
#define GL_REL 0.000189f                         /* 120 ms */
#define GL_FATT 0.0443f                          /* the expander's fast level: 0.5 ms in, 20 ms out */
#define GL_FREL 0.00113f
#define GL_SATT 0.00113f                         /* ...its slow level: 20 ms in, 200 ms out */
#define GL_SREL 0.000113f
#define GL_MATCH 0.0048f                         /* per 64-frame block: ~300 ms */
#define GQ_TILT 0.00097f                         /* per block: 1.5 s */
#define GQ_HATT 0.092f                           /* per block: 15 ms, 100 ms */
#define GQ_HREL 0.0144f
#define GQ_PINK 0.29f                            /* energy over / under 700 Hz at -4.5 dB an octave */
#define GQ_HARSH 0.11f                           /* 2 .. 9 kHz's share of it: twice the slope's */
#define GW_LOW 0.01695f                          /* the sides' lows: 120 Hz */
#define LIM_REL 0.99962f                         /* 60 ms */
static float gl_env, gl_thr, gl_slope, gl_mix, gl_xamt, lim_env, lim_ceil = 0.966f;   /* -0.3 dBFS */
static float gl_ein, gl_eout, gl_mk = 1.0f, gl_mk_to = 1.0f;   /* the loudness match */
static float gl_fast, gl_slow;
static nfm_sat gl_sat[2];
static uint8_t gl_on;
static const float GQ_C[4] = {0.01274f, 0.09492f, 0.24795f, 0.72260f};   /* 90, 700, 2000, 9000 Hz */
static float gq_lp[2][4], gq_g[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, gq_dg[5];
static float gq_set[5], gq_pink, gq_harsh;       /* the static bands (log2), the pinky's and de-harsh's amounts */
static float gq_elo = 1e-6f, gq_ehi = 0.29e-6f, gq_eh = 1e-9f, gq_e = 1e-8f;   /* followed */
static float gw_side, gw_lp[2], gc_wet;
static uint8_t gq_on;
static void glue_set(void)
{
    float a = pct(gpar[G_GLUE]), ratio = 2.0f + 4.0f * a * a, q = pct(gpar[G_GEQ]) * 2.0f;
    gl_on = gpar[G_GLUE] > 0;
    gl_thr = (-8.0f - 20.0f * a) * (1.0f / 6.0206f);
    gl_slope = 1.0f - 1.0f / ratio;
    gl_mix = pct(gpar[G_GMIX]);
    gl_xamt = 0.1f * a;                          /* log2 per octave of transient: 0.2 at most (+1.2 dB) */
    nfm_sat_set(&gl_sat[0], a > 0.6f ? (a - 0.6f) * 2.0f : 0.0f);   /* 0 .. 0.8 */
    nfm_sat_set(&gl_sat[1], gl_sat[0].amount);
    gq_on = gpar[G_GEQ] > 0;
    gq_set[0] = 0.18f * q;                       /* foundation +1.1 dB (log2 x 6.02) */
    gq_set[1] = -0.13f * q;                      /* box -0.8 dB */
    gq_set[2] = gq_set[3] = 0.0f;
    gq_set[4] = 0.33f * q;                       /* air +2 dB */
    gq_pink = 0.54f * q;
    gq_harsh = 0.24f * q;
    if (gq_pink > 1.0f)
        gq_pink = 1.0f;
    gw_side = pct(gpar[G_GWIDTH]) * 2.0f;
    gc_wet = pct(gpar[G_GCOLOR]);
    lim_ceil = gl_on ? 0.891f : 0.966f;          /* -1 / -0.3 dBFS */
}
static inline float absf(float x) { return x < 0.0f ? -x : x; }
/* the compressor's gain for one frame: the levels followed every frame, the gain worked out every 8th
 * (log and exp are dear in the audio interrupt) and slid to between them */
static float gl_g = 1.0f, gl_dg;
static uint8_t gl_ctl;
static inline float glue_gain(float a, float b)
{
    float x = absf(a) > absf(b) ? absf(a) : absf(b);
    gl_env += (x > gl_env ? GL_ATT : GL_REL) * (x - gl_env);
    gl_fast += (x > gl_fast ? GL_FATT : GL_FREL) * (x - gl_fast);
    gl_slow += (x > gl_slow ? GL_SATT : GL_SREL) * (x - gl_slow);
    if (gl_ctl == 0u) {
        float over = gl_env > 1e-6f ? log2_(gl_env) - gl_thr : -1.0f, l = over > 0.0f ? -over * gl_slope : 0.0f, to;
        if (gl_slow > 1e-5f && gl_fast > gl_slow) {   /* a hit: up by how far it stands over the slow level */
            float t = log2_(gl_fast) - log2_(gl_slow);
            l += gl_xamt * (t > 2.0f ? 2.0f : t);
        }
        to = l != 0.0f ? exp2_(l) : 1.0f;
        to = 1.0f - gl_mix + gl_mix * to;
        gl_dg = (to - gl_g) * 0.125f;
        gl_ctl = 8u;
    }
    gl_ctl--;
    gl_g += gl_dg;
    return gl_g;
}
/* the EQ's band gains for the next block, from the levels followed (before it: no loop), slid to */
static void glue_eq_block(float elo, float ehi, float eh, float e, uint32_t k)
{
    float l[5], r;
    int i;
    gq_elo += GQ_TILT * (elo - gq_elo);
    gq_ehi += GQ_TILT * (ehi - gq_ehi);
    gq_e += (e > gq_e ? GQ_HATT : GQ_HREL) * (e - gq_e);
    gq_eh += (eh > gq_eh ? GQ_HATT : GQ_HREL) * (eh - gq_eh);
    for (i = 0; i < 5; i++)
        l[i] = gq_set[i];
    if (gq_elo > 1e-7f && gq_ehi > 1e-9f) {      /* pinky: a quarter of the energy's log2 each side */
        float t = 0.25f * gq_pink * (log2_(GQ_PINK * gq_elo) - log2_(gq_ehi));
        t = t < -0.5f ? -0.5f : t > 0.5f ? 0.5f : t;
        l[0] -= t;
        l[1] -= t;
        l[2] += t;
        l[3] += t;
        l[4] += t;
    }
    r = gq_e > 1e-8f ? gq_eh / gq_e : 0.0f;
    if (r > GQ_HARSH) {                          /* de-harsh: towards the share, down 6 dB at most */
        float t = 0.5f * gq_harsh * (log2_(r) - log2_(GQ_HARSH));
        l[3] -= t > 1.0f ? 1.0f : t;
    }
    for (i = 0; i < 5; i++)
        gq_dg[i] = ((l[i] != 0.0f ? exp2_(l[i]) : 1.0f) - gq_g[i]) / (float)k;
}
/* the EQ, one channel: the bands (what lies between the splits) each at its gain */
static inline float glue_eq(float x, float *s, float *elo, float *ehi, float *eh)
{
    float b0, b1, b2, b3;
    s[0] += GQ_C[0] * (x - s[0]);
    s[1] += GQ_C[1] * (x - s[1]);
    s[2] += GQ_C[2] * (x - s[2]);
    s[3] += GQ_C[3] * (x - s[3]);
    b0 = s[0];
    b1 = s[1] - s[0];
    b2 = s[2] - s[1];
    b3 = s[3] - s[2];
    *elo += s[1] * s[1];
    *ehi += (x - s[1]) * (x - s[1]);
    *eh += b3 * b3;
    return gq_g[0] * b0 + gq_g[1] * b1 + gq_g[2] * b2 + gq_g[3] * b3 + gq_g[4] * (x - s[3]);
}
/* the inflator (its curve 0: 1.5 s - 0.5 s^3 - (s^2 - 2 s^3 + s^4) / 16 on s = |x| up to 1, flat at 1), its 1
 * at the limiter's ceiling, blended in by Color; as a gain on x, so no sign to carry */
#define GC_IN 1.122f                             /* 1 / the ceiling with the Glue on (-1 dBFS) */
static inline float inflate(float x)
{
    float s = absf(x) * GC_IN;
    if (s > 1.0f) {
        x = x < 0.0f ? -0.891f : 0.891f;
        s = 1.0f;
    }
    return x * (1.0f + gc_wet * (0.5f - s * (0.0625f + s * (0.375f + s * 0.0625f))));
}
/* a block's energy in and out (out as if before the makeup): the makeup that brings it back (-12 .. +12 dB) */
static void glue_match(float ein, float eout)
{
    float want, r = gl_mk_to;
    gl_ein += GL_MATCH * (ein - gl_ein);
    gl_eout += GL_MATCH * (eout - gl_eout);
    if (gl_ein < 1e-8f || gl_eout < 1e-9f)
        return;
    want = gl_ein / gl_eout;
    want = want < 0.0625f ? 0.0625f : want > 16.0f ? 16.0f : want;
    r = 0.5f * (r + want / r);                   /* sqrt, from the last value */
    gl_mk_to = 0.5f * (r + want / r);
}
static inline float lim_gain(float a, float b)   /* the limiter's gain for one frame */
{
    float y = absf(a) > absf(b) ? absf(a) : absf(b);
    lim_env = y > lim_env ? y : lim_env * LIM_REL;
    return lim_env > lim_ceil ? lim_ceil / lim_env : 1.0f;
}

/* GLITCHES, by Chaos: a beat repeat (the last sixteenth, eighth or thirty-second again and again), a
 * stretch played backwards, or a TAPE STOP (the mix slowing down to nothing). In the phrase's bars 4 and
 * 8 they come on the beat, and most in its last quarter, the way into the next bar: never past the bar's
 * end. Elsewhere only now and then, on an eighth off the beat (the "and"), an eighth long.
 * The mix goes into a ring of 16384 frames (int16, 0.37 s: an eighth down to 41 BPM, a beat down to 82;
 * longer ones are shortened), which stops taking while a repeat or a reverse plays from it (a tape stop
 * goes on recording: it plays what is coming, slower and slower).
 * Chances, Chaos squared x: 0.9 each eighth of bar 4's and 8's last quarter, 0.3 each eighth of their
 * third quarter, 0.15 each of their first two beats, 0.08 each offbeat eighth elsewhere (40: one fill
 * in three, elsewhere one in about three phrases).
 * A beat repeat is an eighth long at most, and an eighth goes by before the next glitch: a repeat, a
 * pause, a repeat. With swing its sixteenths and thirty-seconds come on the swung grid (long, short):
 * it repeats the sixteenth before the last, from its start, for each of them. */
#define GX_LEN 16384u
#define GX_FADE 96u                              /* frames: the edges of a slice, the way in and out */
enum { GX_REPEAT, GX_REVERSE, GX_TAPE };
static int16_t gx_buf[GX_LEN][2] NE_POOL;
static uint32_t gx_w;                            /* the next frame written (counts on) */
static uint32_t gx_at, gx_start, gx_t, gx_len, gx_slice;   /* when, from where, how far, how long */
static uint8_t gx_pending, gx_on, gx_mode;
static uint32_t gx_seg[4], gx_nseg, gx_si, gx_p, gx_back;  /* a repeat: its pieces' lengths, where it is, how far back it reads */
static uint32_t gx_quiet;                        /* the step from which a glitch may come again */
volatile uint32_t ne_repeat_max;                 /* frames: the longest repeat (tests) */
static float gx_rp, gx_dr;                       /* the tape: how far it has played, its slowing per frame */
static uint32_t gx_rng = 0x676c6974u;
static float gx_rnd(void)
{
    gx_rng ^= gx_rng << 13;
    gx_rng ^= gx_rng >> 17;
    gx_rng ^= gx_rng << 5;
    return (float)(gx_rng >> 8) * (1.0f / 16777216.0f);
}
static uint32_t gx_clock;                        /* frames, in step with the sequencer's clock */
static void glitch_step(uint32_t step, float delay, float step_len)
{
    float c = pct(gpar[G_CHAOS]), chance, r;
    uint32_t ps = step % (8u * NE_BAR), pos = ps % NE_BAR, left = NE_BAR - pos, n, sl, len;
    int fill = ps / NE_BAR == 3u || ps / NE_BAR == 7u;
    if (gx_on || gx_pending || (int32_t)(step - gx_quiet) < 0)
        return;
    if (fill)                                    /* bars 4 and 8: on the beat, the second half by eighths */
        chance = pos & 1u ? 0.0f : pos >= 12u ? 0.9f : pos >= 8u ? 0.3f : pos & 3u ? 0.0f : 0.15f;
    else                                         /* elsewhere: the eighth off the beat */
        chance = pos % 4u == 2u ? 0.08f : 0.0f;
    if (chance <= 0.0f || gx_rnd() >= c * c * chance)
        return;
    n = fill ? (left < 4u ? left : 4u) : 2u;     /* its length in steps: to the bar's end at most */
    if (fill && left > 4u && gx_rnd() < 0.4f)
        n = left < 8u ? left : 8u;
    r = gx_rnd();
    gx_mode = r < 0.3f ? GX_REVERSE : r < 0.55f ? GX_TAPE : GX_REPEAT;
    if (gx_mode == GX_REPEAT && n > 2u)          /* a repeat: an eighth at most */
        n = 2u;
    len = (uint32_t)(step_len * (float)n);
    if (gx_mode == GX_REVERSE) {                 /* the last stretch as long as it, backwards */
        while (len > GX_LEN - 2u * GX_FADE && n > 1u)
            len = (uint32_t)(step_len * (float)(n >>= 1));
        sl = len;
    } else if (gx_mode == GX_TAPE) {             /* it falls behind by half its length: within the ring */
        while (len / 2u > GX_LEN - 4u * GX_FADE && n > 1u)
            len = (uint32_t)(step_len * (float)(n >>= 1));
        sl = len;
        gx_rp = 0.0f;
        gx_dr = 1.0f / (float)len;
    } else {                                     /* a sixteenth or an eighth; at the end of a fill a 32nd */
        float k = fill && pos >= 12u && gx_rnd() < 0.5f ? 0.5f : gx_rnd() < 0.5f || !fill ? 1.0f : 2.0f;
        float sw = seq.swing * 0.5f * step_len;  /* (as nseq: the off-beat sixteenths this much later) */
        uint32_t i;
        if (k < 2.0f && sw >= 1.0f && 2.0f * step_len <= (float)(GX_LEN - 2u * GX_FADE)) {
            float l1 = step_len + sw, l2 = step_len - sw;   /* the swung pair: long, short */
            gx_back = (uint32_t)(2.0f * step_len);
            if (k < 1.0f) {
                gx_seg[0] = gx_seg[1] = (uint32_t)(l1 * 0.5f);
                gx_seg[2] = gx_seg[3] = (uint32_t)(l2 * 0.5f);
                gx_nseg = 4u;
            } else {
                gx_seg[0] = (uint32_t)l1;
                gx_seg[1] = (uint32_t)l2;
                gx_nseg = 2u;
            }
        } else {
            gx_back = gx_seg[0] = (uint32_t)(step_len * k);
            gx_nseg = 1u;
        }
        sl = gx_back;
        for (i = 0; i < gx_nseg; i++)
            if (gx_seg[i] < 4u * GX_FADE)
                return;
        gx_si = gx_p = 0;
        gx_quiet = step + n + 2u;                /* an eighth without */
    }
    if (sl < 4u * GX_FADE || sl > GX_LEN - 2u * GX_FADE || len < 4u * GX_FADE)
        return;
    if (gx_mode == GX_REPEAT && len > ne_repeat_max)
        ne_repeat_max = len;
    gx_slice = sl;
    gx_len = len;
    gx_at = seq.now + (uint32_t)delay;
    gx_pending = 1;
}
static inline void gx_put(float a, float b)
{
    uint32_t i = gx_w++ & (GX_LEN - 1u);
    float x = a * 16384.0f, y = b * 16384.0f;
    gx_buf[i][0] = (int16_t)(x > 32767.0f ? 32767.0f : x < -32768.0f ? -32768.0f : x);
    gx_buf[i][1] = (int16_t)(y > 32767.0f ? 32767.0f : y < -32768.0f ? -32768.0f : y);
}
/* one frame through: recorded, or replaced by the glitch */
static inline void glitch(float *a, float *b)
{
    uint32_t i;
    float w, x, y;
    if (gx_pending && (int32_t)(gx_clock - gx_at) >= 0) {
        gx_pending = 0;
        gx_on = 1;
        gx_t = 0;
        ne_glitches++;
        gx_start = gx_w;                         /* the glitch reads what came before this frame */
    }
    gx_clock++;
    if (!gx_on) {
        gx_put(*a, *b);
        return;
    }
    if (gx_mode == GX_TAPE) {                    /* the speed falls from 1 to 0: read behind, in between */
        float sp = 1.0f - (float)gx_t * gx_dr, f;
        uint32_t j;
        gx_put(*a, *b);
        i = (uint32_t)gx_rp;
        f = gx_rp - (float)i;
        j = (gx_start + i + 1u) & (GX_LEN - 1u);
        i = (gx_start + i) & (GX_LEN - 1u);
        w = (1.0f / 16384.0f) * (sp < 0.25f ? sp * 4.0f : 1.0f);   /* quieter as it stops */
        x = ((float)gx_buf[i][0] + ((float)gx_buf[j][0] - (float)gx_buf[i][0]) * f) * w;
        y = ((float)gx_buf[i][1] + ((float)gx_buf[j][1] - (float)gx_buf[i][1]) * f) * w;
        gx_rp += sp > 0.0f ? sp : 0.0f;
    } else {
        uint32_t p = gx_mode == GX_REVERSE ? gx_t % gx_slice : gx_p;
        uint32_t e = (gx_mode == GX_REVERSE ? gx_slice : gx_seg[gx_si]) - p;
        i = gx_mode == GX_REVERSE ? gx_start - 1u - gx_t : gx_start - gx_back + p;
        if (gx_mode == GX_REPEAT && ++gx_p >= gx_seg[gx_si]) {   /* the next piece, from the start */
            gx_p = 0;
            gx_si = (gx_si + 1u) % gx_nseg;
        }
        i &= GX_LEN - 1u;
        w = p < GX_FADE ? (float)p / GX_FADE : e < GX_FADE ? (float)e / GX_FADE : 1.0f;
        if (gx_mode == GX_REVERSE)               /* backwards: whole, faded only at its ends */
            w = gx_t < GX_FADE ? (float)gx_t / GX_FADE : gx_len - gx_t < GX_FADE ? (float)(gx_len - gx_t) / GX_FADE : 1.0f;
        else if (gx_t < GX_FADE || gx_len - gx_t < GX_FADE)
            w = 1.0f;                            /* (the way in and out: the crossfade below) */
        x = (float)gx_buf[i][0] * (1.0f / 16384.0f) * w;
        y = (float)gx_buf[i][1] * (1.0f / 16384.0f) * w;
    }
    {   /* in and out: from the mix to the glitch and back */
        float m = gx_t < GX_FADE ? (float)gx_t / GX_FADE : gx_len - gx_t < GX_FADE ? (float)(gx_len - gx_t) / GX_FADE : 1.0f;
        *a += (x - *a) * m;
        *b += (y - *b) * m;
    }
    if (++gx_t >= gx_len)
        gx_on = 0;
}

static inline float svf(float x, float g, float k, float *s, int hp)
{
    float hpo = (x - (g + k) * s[0] - s[1]) / (1.0f + g * (g + k));
    float bp = g * hpo + s[0], lp;
    s[0] = g * hpo + bp;
    lp = g * bp + s[1];
    s[1] = g * bp + lp;
    return hp ? hpo : lp;
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
    case G_SPACE: nfx_set_space(&fx, pct(v)); break;
    case G_REVERB: case G_DELAY: case G_FILTER: case G_DRIVE: apply_fx(p, v); acid_route(0); break;
    case G_LEVEL: fm.master = pct(v); break;
    case G_HICUT: case G_LOCUT: case G_RESO: master_filter(); break;
    case G_GLUE: case G_GMIX: case G_GEQ: case G_GCOLOR: case G_GWIDTH: glue_set(); break;
    case G_USBAUDIO: ne_usb_audio = (uint8_t)v; break;
    case G_MUTADE: break;                        /* (read at the quarters) */
    case G_SELMODE: break;                       /* (the UI's) */
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
    case T_TUNE: fm.slot[t].tune = (float)v; break;
    case T_REVERB: nfx_set_slot(&fx, t, NFX_REVERB, pct(v)); break;
    case T_DELAY: nfx_set_slot(&fx, t, NFX_DELAY, pct(v)); break;
    case T_FILTER: nfx_set_slot(&fx, t, NFX_FILTER, pct(v)); break;
    case T_DRIVE: nfx_set_slot(&fx, t, NFX_DRIVE, pct(v)); break;
    case T_MOD: fm.mod[t].type = (uint8_t)v; break;
    case T_MODAMT: fm.mod[t].amount = pct(v); break;
    case T_MODRATE: fm.mod[t].rate = pct(v); break;
    case T_MODCHANCE: fm.mod[t].chance = pct(v); break;
    case T_KIND: break;                /* takes effect with the next new sound */
    }
    (void)tr;
}

/* ------------------------------------------------------------ the acid line --- */
static int16_t apar[A_NPARAMS];

static void aset(int p, int v)
{
    if ((unsigned)p >= A_NPARAMS)
        return;
    v = v < AP[p].lo ? AP[p].lo : v > AP[p].hi ? AP[p].hi : v;
    apar[p] = (int16_t)v;
    switch (p) {
    case A_BARS: nacid_set_length(&acid, v * NACID_BAR); break;
    case A_NOTES: acid.p[NACID_P_DENSITY] = pct(v); break;
    case A_SLIDES: acid.p[NACID_P_SLIDES] = pct(v); break;
    case A_ACCENTS: acid.p[NACID_P_ACCENTS] = pct(v); break;
    case A_OCTAVES: acid.p[NACID_P_OCTAVES] = pct(v); break;
    case A_SCALE: acid.scale = (uint8_t)v; break;
    case A_EVOLVE: acid.p[NACID_P_EVOLVE] = pct(v); break;
    case A_MUTE: acid.mute = (uint8_t)v; break;
    case A_CUTOFF: acid.p[NACID_P_CUTOFF] = pct(v); break;
    case A_RESO: acid.p[NACID_P_RESO] = pct(v); break;
    case A_ENVMOD: acid.p[NACID_P_ENVMOD] = pct(v); break;
    case A_DECAY: acid.p[NACID_P_DECAY] = pct(v); break;
    case A_ACCENT: acid.p[NACID_P_ACCENT] = pct(v); break;
    case A_WAVE: acid.wave = (uint8_t)v; break;
    case A_TUNE: acid.tune = (float)v; break;
    case A_LEVEL: acid.p[NACID_P_LEVEL] = pct(v); break;
    case A_REVERB: case A_DELAY: case A_FILTER: case A_DRIVE: acid_route(0); break;
    case A_MOD: acid.mod.type = (uint8_t)v; break;
    case A_MODAMT: acid.mod.amount = pct(v); break;
    case A_MODRATE: acid.mod.rate = pct(v); break;
    case A_MODCHANCE: acid.mod.chance = pct(v); break;
    case A_CHAOS: acid.p[NACID_P_CHAOS] = pct(v); break;
    }
}

/* the acid line's effects: its drive as set, its sends by chance step by step (up to the levels set),
 * the moving filter by chance a bar at a time (bar: a new chance for the filter, else it stays) */
static void acid_route(int bar)
{
    float amt[NFX_EFFECTS], drive, send[2];
    int filter;
    amt[NFX_FILTER] = pct(apar[A_FILTER]);
    amt[NFX_DRIVE] = pct(apar[A_DRIVE]);
    amt[NFX_DELAY] = pct(apar[A_DELAY]);
    amt[NFX_REVERB] = pct(apar[A_REVERB]);
    nfx_route(&fx, amt, &filter, &drive, send);
    nacid_route(&acid, bar ? filter : acid.ins_f_to > 0.5f && amt[NFX_FILTER] > 0.0f, drive, send[1], send[0]);
}

/* MUTADE: in the phrase's bars 4 and 8, a quarter at a time, by chance a different set of tracks plays:
 * each track's mute flipped with a chance of 0.45 (at least one changes, at least one plays). At the
 * next quarter it is back to the mutes as set (tpar, apar), or another set. A quarter's chance: Mutade
 * x 0.4, the last quarter x 0.8. Decided at the step before (the sequencer has picked a step's hits by
 * the time it tells us of it). */
static uint8_t mx_on;
static float gx_rnd(void);
static void mute_set(int t, int m)
{
    if (t == NE_ACID)
        acid.mute = (uint8_t)m;
    else
        nseq_set_flag(&seq, t, NSEQ_FLAG_MUTE, m);
}
static void mutade_step(uint32_t step)
{
    uint32_t ps = (step + 1u) % (8u * NE_BAR), pos = ps % NE_BAR;
    float m = pct(gpar[G_MUTADE]);
    int t, flip = 0, plays = 0;
    uint8_t mu[NE_TRACKS + 1];
    if (pos % 4u)
        return;
    if (mx_on) {                                 /* back to the mutes as set */
        for (t = 0; t < NE_TRACKS; t++)
            mute_set(t, tpar[t][T_MUTE]);
        mute_set(NE_ACID, apar[A_MUTE]);
        mx_on = 0;
    }
    if ((ps / NE_BAR != 3u && ps / NE_BAR != 7u) || m <= 0.0f || gx_rnd() >= m * (pos == 12u ? 0.8f : 0.4f))
        return;
    for (t = 0; t <= NE_TRACKS; t++) {
        mu[t] = (uint8_t)(t == NE_ACID ? apar[A_MUTE] : tpar[t][T_MUTE]);
        if (gx_rnd() < 0.45f) {
            mu[t] ^= 1u;
            flip = 1;
        }
    }
    if (!flip) {
        t = (int)(gx_rnd() * (float)(NE_TRACKS + 1)) % (NE_TRACKS + 1);
        mu[t] ^= 1u;
    }
    for (t = 0; t <= NE_TRACKS; t++)
        plays += !mu[t];
    if (!plays)                                  /* never silence: one plays */
        mu[(int)(gx_rnd() * (float)(NE_TRACKS + 1)) % (NE_TRACKS + 1)] = 0;
    for (t = 0; t <= NE_TRACKS; t++)
        mute_set(t, mu[t]);
    mx_on = 1;
    ne_mutades++;
}

/* the drums' clock steps the acid line (and the glitches: below) */
static void glitch_step(uint32_t step, float delay, float step_len);
static void on_step(void *ctx, uint32_t step, float delay, float step_len)
{
    (void)ctx;
    if (step == 0u)
        nacid_reset(&acid);
    acid.root = seq.root;
    acid_route(acid.pos % NACID_BAR == 0 || acid.pos >= acid.length);
    nacid_step(&acid, delay, step_len);
    glitch_step(step, delay, step_len);
    mutade_step(step);
    if (step % NE_BAR == 0u)
        nfx_bar(&fx, step_len * (float)NE_BAR);
}
static void acid_render(void *ctx, float *l, float *r, float *rev, float *dly, int n)
{
    (void)ctx;
    nacid_render(&acid, &fm, l, r, rev, dly, n);
}

static void new_sound(int t, int kind)
{
    if ((unsigned)t >= NE_TRACKS)
        return;
    if (kind < 0)
        kind = tpar[t][T_KIND];
    tpar[t][T_KIND] = (int16_t)kind;
    tset(t, T_TUNE, 0);                          /* a new sound starts at its own pitch */
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
    nacid_init(&acid, NE_SR, 0x61636964u);
    nfm_set_mod_lines(&fm, mod_lines[0]);
    acid.ms.line = mod_lines[NFM_VOICES];
    nfm_mod_reset(&acid.ms);
    seq.on_step = on_step;
    fm.priority_slot = 0;                        /* track 1: never a hit lost (nseq keeps it too) */
    nfx_set_source(&fx, acid_render, 0);
    for (p = 0; p < A_NPARAMS; p++)
        aset(p, AP[p].def);
    nacid_generate(&acid);
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
const nacid *ne_acid(void) { return &acid; }

void ne_acid_snapshot(ne_acid_t *s)
{
    int i;
    s->len = acid.length;
    s->rsv[0] = s->rsv[1] = s->rsv[2] = 0;
    for (i = 0; i < NE_ASTEPS; i++) {
        s->flags[i] = acid.step[i].flags;
        s->note[i] = acid.step[i].note;
    }
}

void ne_acid_restore(const ne_acid_t *s)
{
    int i;
    hold = 1;
    __asm__ volatile("" ::: "memory");
    nacid_stop(&acid);
    nacid_set_length(&acid, s->len);
    for (i = 0; i < NE_ASTEPS; i++)
        nacid_set_step(&acid, i, s->flags[i], s->note[i]);
    __asm__ volatile("" ::: "memory");
    hold = 0;
}

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
                if (!acid.mute)
                    nacid_generate(&acid);
            } else if (c.t < NE_TRACKS) {
                nseq_generate(&seq, c.t);
            } else if (c.t == NE_ACID) {
                nacid_generate(&acid);
            }
            break;
        case C_ASET: aset(c.a, c.b); break;
        case C_ASTEP: nacid_set_step(&acid, c.a, c.b, c.c); break;
        case C_SOUND: new_sound(c.t, c.a); break;
        case C_HIT:
            if ((unsigned)c.t < NE_TRACKS)
                nfx_trigger(&fx, &fm, c.t, (float)c.a * (1.0f / 127.0f), (float)c.b);
            break;
        case C_NOTE:
            if (c.a == NSEQ_NOTE_TRACK0 + NE_ACID && c.b) {   /* E1: a new acid pattern */
                nacid_generate(&acid);
                if (!seq.playing)
                    nseq_start(&seq);
            } else {
                nseq_midi_note(&seq, c.a, c.b);
            }
            break;
        case C_CLOCK: clock_msg(c.a, seq.now); break;
        case C_VOICES: fm.voice_limit = c.a < 2 ? 2 : c.a > NFM_VOICES ? NFM_VOICES : c.a; break;
        case C_PANIC: nseq_stop(&seq); nfm_stop_all(&fm); nacid_stop(&acid); break;
        case C_SPACE: nfx_space_shift(&fx); break;
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
    if (!seq.playing && acid.last >= 0)            /* stopped (by a command, MIDI or its clock) */
        nacid_stop(&acid);
    if (!seq.playing) {
        gx_on = gx_pending = 0;
        if (mx_on)
            mutade_step(NE_BAR - 1u);            /* (a step before a first beat outside bars 4 and 8) */
    }
    if (ne_ext && seq.now - clk_seen > 22050u) {   /* the clock stopped coming: our own tempo again */
        ne_ext = 0;
        clk_n = 0;
        seq.bpm = (float)gpar[G_TEMPO];
        ne_bpm_x10 = (uint16_t)(gpar[G_TEMPO] * 10);
    }
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        float ein = 0.0f, eout = 0.0f, elo = 0.0f, ehi = 0.0f, eh = 0.0f, imk2 = 1.0f / (gl_mk * gl_mk);
        gx_clock = seq.now;
        nseq_render(&seq, l, r, (int)k);
        if (mf_on[0] | mf_on[1])
            for (j = 0; j < k; j++) {
                float a = l[j], b = r[j];
                if (mf_on[0]) {
                    a = svf(a, mf_g[0], mf_k, mf_s[0][0], 0);
                    b = svf(b, mf_g[0], mf_k, mf_s[0][1], 0);
                }
                if (mf_on[1]) {
                    a = svf(a, mf_g[1], 2.0f - 0.6f * (2.0f - mf_k), mf_s[1][0], 1);
                    b = svf(b, mf_g[1], 2.0f - 0.6f * (2.0f - mf_k), mf_s[1][1], 1);
                }
                l[j] = a < -1.0f ? -1.0f : a > 1.0f ? 1.0f : a;
                r[j] = b < -1.0f ? -1.0f : b > 1.0f ? 1.0f : b;
            }
        for (j = 0; j < k; j++) {
            float a = l[j], b = r[j], g, m;
            glitch(&a, &b);
            if (gl_on) {
                ein += a * a + b * b;
                g = glue_gain(a, b);
                a *= g;
                b *= g;
                if (gl_sat[0].amount > 0.0f) {
                    a = nfm_sat_tick(&gl_sat[0], a);
                    b = nfm_sat_tick(&gl_sat[1], b);
                    if ((j & 7u) == 0u) {
                        nfm_sat_control(&gl_sat[0]);
                        nfm_sat_control(&gl_sat[1]);
                    }
                }
                if (gq_on) {
                    for (v = 0; v < 5; v++)
                        gq_g[v] += gq_dg[v];
                    a = glue_eq(a, gq_lp[0], &elo, &ehi, &eh);
                    b = glue_eq(b, gq_lp[1], &elo, &ehi, &eh);
                }
                m = 0.5f * (a - b);              /* the sides: their lows out, the rest by Width */
                gw_lp[0] += GW_LOW * (m - gw_lp[0]);   /* (12 dB an octave: two one-pole high-passes) */
                m -= gw_lp[0];
                gw_lp[1] += GW_LOW * (m - gw_lp[1]);
                m = (m - gw_lp[1]) * gw_side;
                a = 0.5f * (a + b);
                b = a - m;
                a += m;
                gl_mk += (gl_mk_to - gl_mk) * 0.002f;
                a *= gl_mk;
                b *= gl_mk;
                if (gc_wet > 0.0f) {
                    a = inflate(a);
                    b = inflate(b);
                }
                eout += (a * a + b * b) * imk2;
            }
            g = lim_gain(a, b);
            if (g < 0.891f)
                ne_limited++;
            a *= g;
            b *= g;
            m = absf(a) > absf(b) ? absf(a) : absf(b);
            if (m > pk)
                pk = m;
            out[2u * j] = (int32_t)(a * gain);
            out[2u * j + 1u] = (int32_t)(b * gain);
        }
        if (gl_on) {
            glue_match(ein, eout);
            if (gq_on)
                glue_eq_block(elo, ehi, eh, elo + ehi, k);
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
    ne_acid_level = acid.peak * 1.5f;
}

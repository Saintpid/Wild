/* nfm: a small percussive FM engine where every hit varies a little.
 *
 * Each slot holds a fixed base patch. A trigger copies the base, nudges each
 * parameter by a small random amount (scaled by DEVIANCE) and plays that copy.
 * The nudges follow a mean-reverting random walk, so a sound wanders around
 * its base but never drifts away from it.
 *
 * Portable C99, no libc, no malloc, float maths. Safe to call from one audio
 * thread; parameter calls belong to the same thread (or between renders). */
#ifndef NFM_H
#define NFM_H

#include <stdint.h>

/* Placement. On the FM1 everything the audio interrupt touches must run from
 * RAM (the Flash may be busy being written), so code and the tables it reads
 * go to the firmware's RAM sections. Elsewhere these are empty. */
#if defined(NFM_FM1_OPEN)
#define NFM_CODE __attribute__((section(".volatile_ram_code")))
#define NFM_DATA __attribute__((section(".volatile_ram.fm1"), aligned(4)))
#else
#define NFM_CODE
#define NFM_DATA
#endif
/* The audio's hot loops and the tables they read, in RAM rather than run from the flash through its
 * cache (Wild: build.py sets NFM_RAM_HOT; app.ld puts .ram_dsp with .data, copied there at boot).
 * The same code either way: only where it lies. */
#if defined(NFM_RAM_HOT) && !defined(NFM_FM1_OPEN)
#define NFM_HOT __attribute__((section(".ram_dsp")))
#define NFM_HOT_DATA __attribute__((section(".ram_dsp.rodata"), aligned(4)))
#else
#define NFM_HOT
#define NFM_HOT_DATA
#endif

#ifndef NFM_VOICES
#define NFM_VOICES 6  /* one per sound, plus two for the short fade of a retrigger */
#endif
#ifndef NFM_SLOTS
#define NFM_SLOTS 4
#endif
#define NFM_OPS 4     /* FM operators per voice */
#define NFM_ALGOS 8
#define NFM_MODES 4   /* resonator modes per voice */

/* Patch parameters. All values are stored normalised 0..1; nfm.c maps them
 * to Hz, seconds and indices (see the table there).
 *
 * Four sine operators in one of eight algorithms (op 1 is always a carrier):
 *   0 stack   4>3>2>1             4 branch  2,3,4 > 1
 *   1 y       4>3>1, 2>1          5 ring    4>3>2>1 and 1 back into 4
 *   2 pairs   2>1 + 4>3           6 split   4>3, 3>1 and 3>2 > 1 + 2
 *   3 fan     4 > 1, 2, 3         7 partials 1 + 2 + 3 + 4
 * Op 4 has self feedback (in ring: feedback from op 1). Every operator has its
 * own level and decay: a modulator's level is its index, a carrier's its
 * loudness, so the tone can change through a hit. */
enum nfm_param {
    NFM_PITCH,        /* 20..4000 Hz, log */
    NFM_PITCH_ENV,    /* 0..48 semitones of downward sweep at the hit */
    NFM_PITCH_DECAY,  /* 1..400 ms */
    NFM_SWEEP_MODS,   /* how much the sweep moves the modulators, 0..2 */
    NFM_ALGO,         /* 0..7, see above; never varied per hit */
    NFM_FEEDBACK,     /* op 4 feedback */
    NFM_ATTACK,       /* carriers, 0.1..30 ms */
    NFM_RATIO1,       /* op ratios to the pitch, 0.125..16, log */
    NFM_RATIO2, NFM_RATIO3, NFM_RATIO4,
    NFM_LEVEL1,       /* op level: index 0..12 rad (squared) or loudness */
    NFM_LEVEL2, NFM_LEVEL3, NFM_LEVEL4,
    NFM_DECAY1,       /* op T60, 2 ms..4 s */
    NFM_DECAY2, NFM_DECAY3, NFM_DECAY4,
    NFM_NOISE,        /* noise burst level */
    NFM_NOISE_DECAY,  /* 2..800 ms */
    NFM_NOISE_COLOR,  /* noise filter 100 Hz..16 kHz */
    NFM_NOISE_BAND,   /* lowpass -> resonant bandpass */
    NFM_NOISE_FM,     /* noise into op 1's phase: grit */
    NFM_FOLD,         /* wavefolder drive */
    NFM_CRUSH,        /* sample-rate and bit reduction */
    NFM_RES_FREQ,     /* resonator first mode, 40..8000 Hz */
    NFM_RES_SPREAD,   /* ratio between modes, 1..4 */
    NFM_RES_DECAY,    /* resonator ring T60, 10 ms..2 s */
    NFM_RES_MIX,      /* how much resonator is heard */
    NFM_LEVEL,
    NFM_PAN,
    NFM_PARAMS
};

/* Performance macros, 0..1, 0.5 is neutral (DEVIANCE: 0 = identical hits).
 * Named after the controls of Beatsurfing RANDOM, which inspired this. */
enum nfm_macro {
    NFM_DEVIANCE,     /* how much each hit differs from the base */
    NFM_STRESS,       /* shorter <-> longer decays */
    NFM_SPIKE,        /* darker <-> edgier (index, feedback, fold, crush) */
    NFM_BLEED,        /* resonator amount */
    NFM_FLUID,        /* resonator ring and spread */
    NFM_MACROS
};

/* Archetypes for nfm_randomize: ranges chosen so a new sound is usable. */
enum nfm_kind {
    NFM_KIND_ANY = -1,
    NFM_KIND_KICK,
    NFM_KIND_SNARE,
    NFM_KIND_HAT,
    NFM_KIND_PERC,    /* toms, congas, wood */
    NFM_KIND_CLICK,   /* blips, ticks, zaps */
    NFM_KIND_METAL,   /* bells, cowbells, clangs: the tonal end */
    NFM_KIND_CLAP,    /* hand claps: a few quick bursts of noise, then its tail */
    NFM_KIND_RUMBLE,  /* techno rumble: a kick's tail, smeared, driven and kept low, ducked by the kicks */
    NFM_KINDS
};

typedef struct {
    float p[NFM_PARAMS];
    float macro[NFM_MACROS];
    float walk[NFM_PARAMS];   /* current per-hit offset (the random walk) */
    int8_t kind;
    uint8_t choke;            /* 1: a new hit fades out the previous one */
    float tune;               /* semitones added to every hit (a drawn sound, retuned) */
} nfm_slot;

/* Saturation that warms rather than distorts: a cubic curve (only the 2nd and 3rd harmonics until it
 * flattens), biased so it adds even harmonics, anti-aliased (its antiderivative's slope from one sample
 * to the next, ADAA), DC removed, and its loudness matched to what goes in (statically for quiet
 * signals, then by following both levels, the gain glided sample by sample), so turning it up changes
 * the colour, not the volume. */
typedef struct {
    float amount;             /* 0 off .. 1 */
    float pre, bias, off, comp;
    float dc, ein, eout, adapt;
    float u1, gain;           /* the curve's last input; the loudness match as it glides to adapt */
    float x1, thru;           /* the last sample in; 1: the DC followed in what the curve added only */
} nfm_sat;
/* A sound's modulation effect: phaser, flanger, chorus, ring modulator or tremolo, on a share of its
 * hits (chance), with its own LFO that runs on between hits. The settings and the LFO are the sound's
 * (nfm_mod, stepped once a block by nfm_mod_block); what each hit needs to remember is its own
 * (nfm_mod_state: the allpasses, a delay line for the flanger and chorus). Like the drive, it changes
 * the colour, not the loudness: what comes out is matched to what goes in (the tremolo by its depth). */
enum { NFM_MOD_OFF, NFM_MOD_PHASER, NFM_MOD_FLANGER, NFM_MOD_CHORUS, NFM_MOD_RING, NFM_MOD_TREMOLO, NFM_MODS };
#define NFM_MOD_LEN 1024      /* a delay line's frames (int16): 23 ms */
typedef struct {
    uint8_t type;             /* NFM_MOD_* */
    float amount, rate, chance;   /* 0..1 each */
    float ph, car;            /* the LFO's and the ring modulator's phases (cycles) */
    /* this block's, per frame i: value = v0 + dv * i */
    float l0, dl;             /* the LFO, 0..1 */
    float a0, da;             /* phaser: the allpass coefficient */
    float d0, dd;             /* flanger, chorus: the delay in frames */
    float c_inc;              /* ring: the carrier, cycles a frame */
    float nprev;              /* the last block's frames (the carrier's phase moves on by them) */
} nfm_mod;
typedef struct {
    float x1[4], y1[4], fb;   /* the phaser's four allpasses and its feedback */
    float ein, eout, g;       /* the loudness match: what goes in and out, followed (~20 ms); the gain */
    int16_t *line;            /* NFM_MOD_LEN frames (16-bit floats), or 0 (no flanger, chorus) */
    uint16_t w;
    float gt, dg;             /* the gain's target, and its slide a frame */
    float ein2, eout2;        /* the levels' second pole */
} nfm_mod_state;
void nfm_mod_block(nfm_mod *m, float sr, int n);
void nfm_mod_reset(nfm_mod_state *s);
float nfm_mod_tick(const nfm_mod *m, nfm_mod_state *s, float x, int i);

/* nfm_sat's curve: x - 4x^3/27, flat from +-1.5 (slope 1 at 0, 0 at the ends), and its integral */
NFM_CODE NFM_HOT static inline float sat_curve(float x) { return x > 1.5f ? 1.0f : x < -1.5f ? -1.0f : x - 0.148148f * x * x * x; }
NFM_CODE NFM_HOT static inline float sat_int(float x)
{
    if (x > 1.5f) return x - 0.5625f;
    if (x < -1.5f) return -x - 0.5625f;
    x *= x;
    return 0.5f * x - 0.037037f * x * x;
}
/* the curve's mean between two samples, (F(u1) - F(u0)) / (u1 - u0): what it makes over the Nyquist
 * mostly gone instead of folded back (as a sample later). On the cubic part in closed form, no divide. */
NFM_CODE NFM_HOT static inline float sat_adaa(float u0, float u1)
{
    if (u0 <= 1.5f && u0 >= -1.5f && u1 <= 1.5f && u1 >= -1.5f) {
        float m = u0 + u1;
        return 0.5f * m - 0.037037f * m * (u0 * u0 + u1 * u1);
    }
    if (u0 > 1.5f && u1 > 1.5f) return 1.0f;
    if (u0 < -1.5f && u1 < -1.5f) return -1.0f;
    float d = u1 - u0;
    if (d < 1e-4f && d > -1e-4f) return sat_curve(0.5f * (u0 + u1));
    return (sat_int(u1) - sat_int(u0)) / d;
}

void nfm_sat_set(nfm_sat *s, float amount);
/* (here, inline: it runs a sample per voice, the 303 and the Glue's two sides, a call each from
 * other units) */
NFM_CODE NFM_HOT static inline float nfm_sat_tick(nfm_sat *s, float x)
{
    float u = x * s->pre + s->bias;
    float y = (sat_adaa(s->u1, u) - s->off) * s->comp;
    s->u1 = u;
    /* the DC the bias makes. With thru, followed in what the curve added only (the input as it came
     * through, half a sample late by the ADAA, taken out): followed in all of it, the DC filter takes a
     * share of a bass note itself, and lets it out when the note stops */
    s->dc += 0.0015f * (y - s->thru * 0.5f * (x + s->x1) - s->dc);
    s->x1 = x;
    y -= s->dc;
    s->ein += 0.004f * (x * x - s->ein);
    s->eout += 0.004f * (y * y - s->eout);
    s->gain += (s->adapt - s->gain) * 0.003f;   /* glided (~8 ms): a gain set in steps rang like aliasing */
    return y * s->gain;
}
void nfm_sat_control(nfm_sat *s);   /* every few samples: the loudness match */

typedef struct {
    int8_t slot;              /* -1 idle */
    uint8_t fading;
    uint8_t algo, carriers;   /* carriers: bit k = op k+1 is heard */
    uint32_t age;
    /* operators, phases in cycles */
    float ph[NFM_OPS], inc[NFM_OPS], ratio[NFM_OPS];
    float env[NFM_OPS], env_k[NFM_OPS]; /* index in cycles, or loudness */
    float feedback, fb1, fb2, ring;
    float f0, pitch_env, pitch_k, pitch_depth, sweep_mods;
    float att, att_inc;
    float noise, noise_k, ns1, ns2, ng, nk, nband, noise_fm;
    float nkg, nh;            /* the noise filter's k + g and 1 / (1 + g (g + k)), per hit */
    uint8_t bursts;           /* a clap: bursts still to come, each clap_gap apart, then the tail */
    uint16_t clap_t, clap_gap;
    float clap_amp, clap_k;
    /* a rumble: high-passed into its own drive, low-passed after, the sub held off, ducked by a kick */
    uint8_t rumble, r_down;   /* r_down: a kick (or a step's breath) came, the duck falls to r_floor */
    uint8_t r_routed;         /* its effects are set: a breath leaves them (a reset drive clicked) */
    float r_pre, r_comp, r_hpk, r_hp, r_x1, r_g, r_kg, r_h, r_s1, r_s2, r_dc, r_dck, r_duck, r_duck_k;
    /* it plays on (a drone): its floor, the note it glides to, its low-pass at rest, and its drift
     * (Deviance): level and colour gliding to targets drawn every 0.3..1.2 s */
    float r_floor, r_f0t, r_fl, r_dev, r_lvl, r_col, r_lvt, r_cot, r_mk, r_nz0, r_nzm, r_nzt;
    float r_lv, r_lvg, r_lvk;   /* its level, gliding (~50 ms) to the last step's strength */
    float r_pu;               /* a step's lift: up in ~8 ms, down over ~80 ms, 0.6..1 of its level */
    uint8_t r_pup;            /* the lift rising */
    float r_pre0, r_dr, r_drt, r_gr, r_grt, r_mb[NFM_OPS];   /* drift of the drive and the FM grit (the modulators' r_mb) */
    uint16_t r_mn;
    float fold, crush, crush_rate, crush_acc, crush_hold, crush_q;
    float fade, fade_k;
    /* resonator: TPT state variable bandpass per mode */
    float rg[NFM_MODES], rk[NFM_MODES], ramp[NFM_MODES];
    float rkg[NFM_MODES], rh[NFM_MODES];   /* k + g, 1 / (1 + g (g + k)): no divide a sample */
    float rs1[NFM_MODES], rs2[NFM_MODES];
    uint8_t modes;            /* the modes worked out: up to the last one on */
    float res_mix;
    float gain_l, gain_r;
    float peak;
    uint16_t ctl;            /* samples until next control update */
    /* Effects for this hit, chosen at trigger (see nfm_route). */
    uint8_t ins;             /* NFM_INS_* bits */
    float send_rev, send_dly;
    float ins_s1, ins_s2;    /* insert filter state */
    nfm_sat sat;             /* the drive: warm, even, loudness-matched (nfm_sat_*) */
    uint8_t mod_on;          /* the sound's modulation effect plays on this hit */
    nfm_mod_state ms;
} nfm_voice;

typedef struct {
    float sr, inv_sr;
    uint32_t rng;
    uint32_t clock;
    nfm_slot slot[NFM_SLOTS];
    nfm_voice voice[NFM_VOICES];
    float master;
    /* Shared insert settings, written by the effects module per block:
     * filter coefficient g, damping k, lowpass->bandpass morph. */
    float ins_g, ins_k, ins_morph;
    float ins_g0, ins_k0, ins_morph0;   /* ...and the same at rest, for the slots in direct */
    float ins_kg, ins_h, ins_kg0, ins_h0;   /* k + g and 1 / (1 + g (g + k)) of both (nfm_ins_set) */
    /* The DIRECT bus: the slots whose bits are set here play into dir_l / dir_r (n frames from there,
     * overwritten like out_l) instead of the main bus, their insert filter at rest; their sends as
     * ever. NULL: everything on the main bus. */
    uint8_t direct;
    float *dir_l, *dir_r;
    /* New hits use only voices below this (1..NFM_VOICES). A CPU guard can
     * lower it; voices above it still finish what they are playing. */
    int voice_limit;
    /* This slot's hits are never stolen by another's (-1: none): a kick that never drops out. */
    int priority_slot;
    nfm_mod mod[NFM_SLOTS];  /* each sound's modulation effect (type 0: none) */
    /* The key a rumble sings in (the acid line's): its root, a pitch class above C (-1: none, the
     * rumble at its own pitch), and its scale, bit n for each pitch class n above the root. */
    int key;
    uint16_t key_scale;
} nfm_engine;
/* Delay lines for the flanger and chorus, NFM_MOD_LEN frames for each voice (int16[NFM_VOICES *
 * NFM_MOD_LEN]); without them those two are not played. */
void nfm_set_mod_lines(nfm_engine *e, int16_t *lines);
/* Audio Load's split (the device's diagnostics): a tick counter, and the ticks the voices took */
extern uint32_t (*nfm_clock)(void);
extern uint32_t nfm_voice_ticks;
/* after ins_g / ins_k (or their rest's) change: the insert filter's sums and reciprocal */
void nfm_ins_set(nfm_engine *e);

enum { NFM_INS_FILTER = 1, NFM_INS_DRIVE = 2 };

void nfm_init(nfm_engine *e, float sample_rate, uint32_t seed);
/* Zero n bytes without calling libc memset (words when aligned). */
void nfm_zero(void *p, unsigned n);

/* The base patch. value is 0..1; out-of-range values are clamped. */
void nfm_set_param(nfm_engine *e, int slot, int param, float value);
float nfm_get_param(const nfm_engine *e, int slot, int param);
void nfm_set_macro(nfm_engine *e, int slot, int macro, float value);
float nfm_get_macro(const nfm_engine *e, int slot, int macro);
void nfm_set_choke(nfm_engine *e, int slot, int on);

/* The "wheel": a brand-new base patch of the given kind (or any kind). */
void nfm_randomize(nfm_engine *e, int slot, int kind);
/* Nudge the base patch itself by amount (0..1): a permanent small step. */
void nfm_mutate(nfm_engine *e, int slot, float amount);
/* One step of the base patch's own random walk, sigma per parameter (normalised): it goes and stays. */
void nfm_wander(nfm_engine *e, int slot, float sigma);
int nfm_slot_kind(const nfm_engine *e, int slot);

/* Play slot. velocity 0..1, transpose in semitones (0 for drums).
 * Returns the voice used, for nfm_route. */
int nfm_trigger(nfm_engine *e, int slot, float velocity, float transpose);
/* Effects for a voice just triggered: insert bits, send levels 0..1 and the drive (with
 * NFM_INS_DRIVE: 0..1). */
void nfm_route(nfm_engine *e, int voice, int ins, float send_rev, float send_dly, float drive);
/* 1 while the voice still sounds and was started by the trigger that
 * returned it (age is the value nfm_voice_age gave then). */
int nfm_voice_alive(const nfm_engine *e, int voice, uint32_t age);
uint32_t nfm_voice_age(const nfm_engine *e, int voice);
void nfm_stop_all(nfm_engine *e);
/* A rumble plays on, a drone: its voice (-1: none playing), and its end (faded out over t s). A
 * trigger on its slot while it plays is a breath (a soft dip, deeper by velocity) and a glide to the note. */
int nfm_drone(const nfm_engine *e, int slot);
void nfm_release(nfm_engine *e, int slot, float t);

/* Render n frames, adding nothing: out_l/out_r are overwritten. */
void nfm_render(nfm_engine *e, float *out_l, float *out_r, int n);
/* Same before the final clip, plus mono effect sends (either may be NULL). */
void nfm_render_bus(nfm_engine *e, float *out_l, float *out_r, float *rev, float *dly, int n);
/* Same, mixed into interleaved signed 24-bit (Q23) stereo, as the FM1 uses. */
void nfm_mix_q23(nfm_engine *e, int32_t *interleaved, int frames, float gain);

int nfm_active_voices(const nfm_engine *e);
const char *nfm_param_name(int param);
const char *nfm_macro_name(int macro);
const char *nfm_kind_name(int kind);
/* Which ops are carriers in an algorithm (bit k = op k+1). */
int nfm_algo_carriers(int algo);

#endif

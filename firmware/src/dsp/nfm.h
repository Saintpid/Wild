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
    float fold, crush, crush_rate, crush_acc, crush_hold, crush_q;
    float fade, fade_k;
    /* resonator: TPT state variable bandpass per mode */
    float rg[NFM_MODES], rk[NFM_MODES], ramp[NFM_MODES];
    float rs1[NFM_MODES], rs2[NFM_MODES];
    float res_mix;
    float gain_l, gain_r;
    float peak;
    uint16_t ctl;            /* samples until next control update */
    /* Effects for this hit, chosen at trigger (see nfm_route). */
    uint8_t ins;             /* NFM_INS_* bits */
    float send_rev, send_dly;
    float ins_s1, ins_s2;    /* insert filter state */
} nfm_voice;

typedef struct {
    float sr, inv_sr;
    uint32_t rng;
    uint32_t clock;
    nfm_slot slot[NFM_SLOTS];
    nfm_voice voice[NFM_VOICES];
    float master;
    /* Shared insert settings, written by the effects module per block:
     * filter coefficient g, damping k, lowpass->bandpass morph, drive gain
     * and the loudness compensation that goes with it. */
    float ins_g, ins_k, ins_morph, ins_drive, ins_comp;
    /* New hits use only voices below this (1..NFM_VOICES). A CPU guard can
     * lower it; voices above it still finish what they are playing. */
    int voice_limit;
} nfm_engine;

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
int nfm_slot_kind(const nfm_engine *e, int slot);

/* Play slot. velocity 0..1, transpose in semitones (0 for drums).
 * Returns the voice used, for nfm_route. */
int nfm_trigger(nfm_engine *e, int slot, float velocity, float transpose);
/* Effects for a voice just triggered: insert bits and send levels 0..1. */
void nfm_route(nfm_engine *e, int voice, int ins, float send_rev, float send_dly);
/* 1 while the voice still sounds and was started by the trigger that
 * returned it (age is the value nfm_voice_age gave then). */
int nfm_voice_alive(const nfm_engine *e, int voice, uint32_t age);
uint32_t nfm_voice_age(const nfm_engine *e, int voice);
void nfm_stop_all(nfm_engine *e);

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

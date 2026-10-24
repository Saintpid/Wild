/* nfx: effects for nfm that drift slowly and pick which hits they touch.
 *
 * Filter and drive are inserts on single hits; delay and reverb are sends.
 * At each trigger, every effect that is on decides by chance (its AMOUNT,
 * weighted by the kind of sound) whether this hit gets it, so many hits stay
 * dry. Delay and reverb each take one hit at a time: while one sound owns the
 * reverb, the others stay out of it.
 *
 * The effect settings themselves (cutoff, drive, delay time and feedback,
 * reverb length and tone) wander around fixed base values. CHAOS sets how
 * far they wander, MOTION how fast. Both 0 means fixed settings.
 *
 * Portable C99 like nfm; no libc, no malloc. Memory: two 32768-sample int16
 * delay lines (128 kB) and a four-line reverb (about 48 kB at 44.1 kHz).
 * On the FM1 the delay is 16384 samples (64 kB, 0.37 s) and the reverb lines
 * are int16 (24 kB), to fit next to the stock firmware in RAM. */
#ifndef NFX_H
#define NFX_H

#include "nfm.h"

#if defined(NFM_FM1_OPEN)
#ifndef NFX_DELAY_LEN
#define NFX_DELAY_LEN 16384
#endif
#define NFX_REV16 1
#endif
#ifndef NFX_DELAY_LEN
#define NFX_DELAY_LEN 32768   /* per channel; 0.74 s at 44.1 kHz */
#endif
#ifdef NFX_REV16
typedef int16_t nfx_rev_t;    /* scaled by 8192 */
#else
typedef float nfx_rev_t;
#endif
#define NFX_REV_LINES 4
#define NFX_REV_LEN 3072
#define NFX_AP1 142
#define NFX_AP2 379

enum nfx_effect { NFX_FILTER, NFX_DRIVE, NFX_DELAY, NFX_REVERB, NFX_EFFECTS };

/* Settings that drift. Values are 0..1; nfx.c maps them. */
enum nfx_mod {
    NFX_M_CUTOFF, NFX_M_RESO, NFX_M_MORPH, NFX_M_DRIVE,
    NFX_M_DTIME, NFX_M_DFEEDBACK, NFX_M_DTONE,
    NFX_M_RDECAY, NFX_M_RDAMP,
    NFX_MODS
};

typedef struct {
    float cur, target, base, span;
} nfx_mod_state;

typedef struct {
    float sr;
    uint32_t rng;
    uint32_t now;                  /* sample clock */
    uint8_t on[NFX_EFFECTS];
    float amount[NFX_EFFECTS];     /* 0..1: share of hits and how wet */
    float chaos, motion;           /* 0..1 */
    float bpm;
    nfx_mod_state mod[NFX_MODS];

    /* One owner each for delay and reverb. */
    int owner_voice[2];
    uint32_t owner_age[2], owner_since[2];
    int8_t owner_slot[2];

    /* delay: ping-pong, int16 storage */
    int16_t dl[NFX_DELAY_LEN], dr[NFX_DELAY_LEN];
    uint32_t dpos;
    float dtime;                   /* current delay in samples, glides */
    float dlp_l, dlp_r;

    /* reverb: allpass diffusion into a 4-line feedback delay network */
    float ap1[NFX_AP1], ap2[NFX_AP2];
    uint16_t ap1_pos, ap2_pos;
    nfx_rev_t line[NFX_REV_LINES][NFX_REV_LEN];
    uint16_t len[NFX_REV_LINES], rpos[NFX_REV_LINES];
    float lp[NFX_REV_LINES];
    float rgain[NFX_REV_LINES], rdamp;
} nfx;

void nfx_init(nfx *fx, float sample_rate, uint32_t seed);
void nfx_set_on(nfx *fx, int effect, int on);
void nfx_set_amount(nfx *fx, int effect, float amount);
void nfx_set_chaos(nfx *fx, float chaos);
void nfx_set_motion(nfx *fx, float motion);
void nfx_set_tempo(nfx *fx, float bpm);

/* Trigger a slot and choose this hit's effects. Returns the voice. */
int nfx_trigger(nfx *fx, nfm_engine *e, int slot, float velocity, float transpose);

/* Render the engine through the effects, clipped to -1..1. */
void nfx_render(nfx *fx, nfm_engine *e, float *out_l, float *out_r, int n);

/* For displays: a drifting setting 0..1, or the slot that owns delay (0)
 * or reverb (1), -1 for none. */
float nfx_mod_value(const nfx *fx, int mod);
int nfx_owner(const nfx *fx, nfm_engine *e, int which);

#endif

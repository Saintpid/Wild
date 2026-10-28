/* nfx: effects for nfm, set per sound, whose settings drift slowly.
 *
 * Each sound (slot) has its own four amounts: FILTER, the share of its hits
 * that go through a moving filter (chosen by chance, hit by hit); DRIVE, how
 * much warm saturation all its hits get (an insert per voice, loudness
 * matched); DELAY and REVERB, the most it sends to the two shared effects:
 * each hit sends any level up to it, by chance, and now and then none. The
 * effects' global amounts scale all of it: the filter's and drive's share and
 * strength, the delay's and reverb's return.
 *
 * The insert and delay-colour settings (cutoff, resonance, drive, the delay's
 * feedback and tone) wander around fixed base values. CHAOS sets how far they
 * wander, MOTION how fast. Both 0 means fixed settings.
 *
 * SPACE moves the room: the reverb between a few rooms and halls (its length
 * and tone), the delay between a sixteenth, an eighth, a dotted eighth and a
 * quarter. It stays at each for bars on end and glides to the next over 1-4
 * bars (the delay crossfades between two taps, so no pitch bends). 0: the
 * home room and a dotted eighth, still; 1: a shift every eight bars or so.
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
    NFX_M_DTIME, NFX_M_DFEEDBACK, NFX_M_DTONE,   /* (DTIME, RDECAY, RDAMP: SPACE's, they stay put) */
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
    /* SPACE: a station each for the reverb and the delay, the glide from one to the next */
    float space;                   /* 0..1 */
    uint8_t rv_from, rv_to, dl_from, dl_to;
    float rv_x, rv_inc, dl_x, dl_inc;  /* 0..1 on the way, per sample */
    uint16_t rv_stay, dl_stay;     /* bars at the station */
    float bar_len;                 /* samples, the last bar's */

    /* each slot's own amounts, 0..1 (filter share, drive, delay send, reverb send) */
    float slot_amt[NFM_SLOTS][NFX_EFFECTS];

    /* delay: ping-pong, int16 storage */
    int16_t dl[NFX_DELAY_LEN], dr[NFX_DELAY_LEN];
    uint32_t dpos;
    float dtime, dtime2;           /* the two taps in samples (SPACE crossfades from the first to the second) */
    float dlp_l, dlp_r;

    /* reverb: allpass diffusion into a 4-line feedback delay network */
    float ap1[NFX_AP1], ap2[NFX_AP2];
    uint16_t ap1_pos, ap2_pos;
    nfx_rev_t line[NFX_REV_LINES][NFX_REV_LEN];
    uint16_t len[NFX_REV_LINES], rpos[NFX_REV_LINES];
    float lp[NFX_REV_LINES];
    float rgain[NFX_REV_LINES], rdamp;

    /* another source on the bus (the acid line): adds to the dry bus and the sends */
    void (*src)(void *ctx, float *l, float *r, float *rev, float *dly, int n);
    void *src_ctx;
} nfx;

void nfx_init(nfx *fx, float sample_rate, uint32_t seed);
void nfx_set_on(nfx *fx, int effect, int on);
void nfx_set_amount(nfx *fx, int effect, float amount);
void nfx_set_chaos(nfx *fx, float chaos);
void nfx_set_motion(nfx *fx, float motion);
void nfx_set_tempo(nfx *fx, float bpm);
void nfx_set_space(nfx *fx, float space);
/* A bar starts (bar_len samples long): SPACE may move on. */
void nfx_bar(nfx *fx, float bar_len);
/* A shift now, by SPACE's chance (1: always), over a bar. */
void nfx_space_shift(nfx *fx);

/* A slot's own amount of an effect, 0..1. */
void nfx_set_slot(nfx *fx, int slot, int effect, float amount);
/* Trigger a slot with its effects. Returns the voice. */
int nfx_trigger(nfx *fx, nfm_engine *e, int slot, float velocity, float transpose);
/* The effects for another source with amounts amt (filter share, drive, delay, reverb): whether its
 * filter is in (by chance), its drive and its sends, all scaled by the global amounts. */
void nfx_route(nfx *fx, const float amt[NFX_EFFECTS], int *filter, float *drive, float send[2]);
/* Another source on the bus, rendered after the voices into the dry bus and the sends. */
void nfx_set_source(nfx *fx, void (*src)(void *, float *, float *, float *, float *, int), void *ctx);

/* Render the engine through the effects, clipped to -1..1. */
void nfx_render(nfx *fx, nfm_engine *e, float *out_l, float *out_r, int n);

/* For displays: a drifting setting 0..1. */
float nfx_mod_value(const nfx *fx, int mod);

#endif

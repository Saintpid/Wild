/* SPDX-License-Identifier: GPL-3.0-only */
/* ngen: a generative stereo voice for one drum track. The steps are energy, not hits.
 *
 * Material is made ahead, in the background (ngen_work, small pieces): a set of stereo tables of one
 * period, a calm and an intense state of the same sound (same period, same phases low down, so a morph
 * between them cannot click). Playback (ngen_render) only reads, morphs and crossfades them. A new set
 * is written into a free slot and faded in over a quarter; if it is late, the old one plays on.
 *
 * The tones go through a low-pass per side, each cutoff drifting its own way, with a little vibrato.
 * Beside the tones, a little noise (5 % of the power by default) through a resonant band-pass per side whose
 * centre wanders between the chord's partials; the share of noise and tone keeps moving (slow drifts, energy, each step's flash).
 * Nothing below ~95 Hz: kick and rumble own that.
 *
 * Energy: a played step adds vel/127 of what is left to full (it saturates at 1); time takes it away,
 * tempo-synced. The sound does not go silent before a bar has passed without a played step. The
 * planner reads the whole pattern ahead: the timbre leans towards what is coming (a build-up before a
 * dense part), the energy itself never rises without a step.
 *
 * Portable C99, float, no libm, no allocation. ngen_work belongs to the foreground, the rest to the audio
 * side; they share only the slot hand-over and a request count (a new set asked for). */
#ifndef NGEN_H
#define NGEN_H
#include <stdint.h>

#define NGEN_LEN 1024u        /* frames in one period */
#define NGEN_SLOTS 3u         /* playing, fading out, being made */
#define NGEN_STEPS 128
#define NGEN_STATES 2         /* calm, intense */
#define NGEN_DEVS 8           /* what Deviance moves as it plays (ngen.c's deviance()) */
#define NGEN_CHARS 6          /* a set's character as it plays: filter damping and cutoff, vibrato, pulse, struck or held */
/* one slot: two states, two channels */
#define NGEN_SLOT_WORDS (NGEN_STATES * 2u * NGEN_LEN)
#define NGEN_MEM_WORDS (NGEN_SLOTS * NGEN_SLOT_WORDS)   /* int16: 24 KB */

/* the percentages a player sets (0..100) */
enum {
    NGEN_COMPLEXITY,   /* how many tones and overtones the intense state has */
    NGEN_MOVEMENT,     /* the pulse and drift energy brings */
    NGEN_SPREAD,       /* stereo width and detune */
    NGEN_INERTIA,      /* how slowly the timbre follows the energy */
    NGEN_DECAY,        /* how long energy lasts (half a bar .. 3 bars) */
    NGEN_NOISE,        /* noise's share of the power: a tenth of this cubed (100: 10 %, 50: 1.25 %), moving with it */
    NGEN_DEVIANCE,     /* how far each step strays: 0..89 around its own sound, 90..100 wandering on */
    NGEN_NPARAMS
};

typedef struct {
    int16_t *mem;                        /* NGEN_MEM_WORDS, owned by the caller (the pool) */
    float sr;
    uint32_t seed;
    uint8_t param[NGEN_NPARAMS];
    int8_t root;                         /* MIDI note of the key's root */
    uint16_t mask;                       /* the scale: bit n, n semitones above the root, is in it */
    /* the pattern and its plan */
    uint8_t vel[NGEN_STEPS];
    float plan[NGEN_STEPS];              /* what the timbre leans to at each step */
    float pred[NGEN_STEPS];              /* the energy the pattern settles to there */
    uint8_t len;
    float global;                        /* the pattern's mean strength */
    /* audio side */
    float e;                             /* energy 0..1 */
    float gate;                          /* 1 until a bar without steps, then away */
    float amp, morph, bloom;             /* bloom: a played step's flash, brighter and noisier */
    float stab;                          /* a struck sound's envelope (its character: how struck) */
    float dv[NGEN_DEVS], dt[NGEN_DEVS];  /* Deviance: its offsets now, and where they glide to */
    float wm[3];                         /* ...its material's, wandered (from 90) */
    uint32_t dn;                         /* ...steps it has drawn for */
    float tl_lo[2], tl_bd[2];            /* the tones' low-pass per side (each its own drifting cutoff) */
    float vib;                           /* vibrato, in cycles */
    float pt, pn, gn;                    /* the tones' and the noise's power, the noise's gain to its share */
    float lfo[4];                        /* slow drifts, in cycles: timbre, noise share, place, brightness */
    float nz_lo[2], nz_bd[2], nz_c[2], nz_t[2];   /* the noise's band-pass per side: state, centre, target */
    float hp_x[2][2], hp_y[2][2];        /* two poles of high-pass per side (nothing below ~95 Hz) */
    float dh_x[2], dh_y[2];              /* the detuned readers' high-pass (the lows stay shared) */
    uint32_t rng, nz_next;
    float k_sp, k_mov, k_nz;             /* Spread, Movement and Noise as they glide to a knob's value */
    float ph[NGEN_STATES][3], po[NGEN_STATES][3];   /* readers per state (shared, left detuned, right
                                                     * detuned): the playing set's, the old's */
    float f0[NGEN_SLOTS][NGEN_STATES];   /* each slot's chords' fundamentals, as made (a key comes with its set) */
    uint8_t nzq[NGEN_SLOTS][9], nzn[NGEN_SLOTS];     /* ...its intense chord's tones (the noise's resonances) */
    float dk[NGEN_SLOTS], dl;            /* each slot's dullness (from its seed), and the one heard now */
    float chr[NGEN_SLOTS][NGEN_CHARS], ch[NGEN_CHARS];   /* ...and its character (ngen.c's character()) */
    float jf0[NGEN_STATES];              /* the set being made: its chords */
    uint8_t jq[NGEN_STATES][9], jn[NGEN_STATES];
    uint32_t since, step_len, step_t;
    uint8_t pos, live;
    volatile uint8_t cur, old, ready;    /* slots: playing, fading out (old == cur: none), ready to play */
    uint32_t fade, fade_len;
    /* foreground: the set being made */
    uint8_t wr, job, state, it_t, it_n, have;
    volatile uint8_t req;                /* a new set asked for (either side may ask), and the last begun */
    uint8_t req_done;
    float scratch[2][NGEN_LEN];          /* one state, both channels, before it is scaled to int16 */
    uint32_t made;                       /* sets made (tests) */
} ngen;

void ngen_init(ngen *g, int16_t *mem, float sr, uint32_t seed);
void ngen_set_param(ngen *g, int p, int value);
/* the key: its root (MIDI) and its scale (bit n: n semitones up is in it; 0: natural minor) */
#define NGEN_MINOR 0x5ADu                /* 0 2 3 5 7 8 10 */
#define NGEN_MAJOR 0xAB5u                /* 0 2 4 5 7 9 11 */
void ngen_set_key(ngen *g, int root_note, unsigned scale);
/* a new sound: a new seed; the set is remade and faded in */
void ngen_new_sound(ngen *g, uint32_t seed);
/* the pattern, len steps of velocities (0: empty); replans from the next step */
void ngen_pattern(ngen *g, const uint8_t *vel, int len);
/* step pos begins now; it lasts step_len samples */
void ngen_step(ngen *g, int pos, uint32_t step_len);
/* add n frames to l and r */
void ngen_render(ngen *g, float *l, float *r, int n);
/* one small piece of background work; 0: nothing to do */
int ngen_work(ngen *g);
/* 1 while it sounds or something is coming */
int ngen_active(const ngen *g);
void ngen_stop(ngen *g);
void ngen_release(ngen *g);               /* the same, rung out: the gate falls as after a bar without steps */
#endif

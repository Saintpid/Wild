/* nseq: a random pattern sequencer for nfm.
 *
 * Eight tracks, one per nfm slot. Each track loops over its own length,
 * 1..128 sixteenth steps (1..8 bars of 16), so tracks of different lengths
 * drift against each other. A track may run in triplets instead: eighth
 * triplets, three steps a quarter, 12 a bar (1..96 steps), on the first,
 * second and third sixteenth of each quarter, the second and third played a
 * third and two thirds of a sixteenth late (the fourth plays none). A generator places a random number of hits,
 * drawn from a min..max range for the whole track, weighted towards the
 * positions that suit the slot's kind of sound. Swing and loose timing
 * delay hits within their step.
 *
 * Portable C99 like nfm. All calls belong to the audio thread or happen
 * between renders. */
#ifndef NSEQ_H
#define NSEQ_H

#include "nfm.h"
#include "nfx.h"

#define NSEQ_TRACKS NFM_SLOTS
#define NSEQ_BAR 16
#define NSEQ_MAX_BARS 8
#define NSEQ_MAX_STEPS (NSEQ_BAR * NSEQ_MAX_BARS)
#define NSEQ_TBAR 12           /* a bar of triplets */
#define NSEQ_TMAX_STEPS (NSEQ_TBAR * NSEQ_MAX_BARS)
#define NSEQ_QUEUE 32

/* MIDI notes that drive the generator (see nseq_midi_note). */
#define NSEQ_NOTE_TRACK0 36    /* C1 and up: new pattern for track 1, 2, ... */
#define NSEQ_NOTE_ALL 48       /* C2: new patterns for every unlocked track */
#define NSEQ_NOTE_ROOT_LO 60   /* C3 and up: transpose tonal tracks, C3 = 0 */
#define NSEQ_NOTE_ROOT_HI 84

typedef struct {
    uint8_t on;
    uint8_t vel;      /* 1..127 */
    int8_t note;      /* semitones from the sound's own pitch */
    uint8_t reserved;
} nseq_step;

typedef struct {
    nseq_step step[NSEQ_MAX_STEPS];
    uint8_t length;      /* steps, 1..128 */
    uint8_t hits_min;    /* hits for the whole track, drawn at generate */
    uint8_t hits_max;
    uint8_t pitch_range; /* semitones up and down, 0 = always the root */
    uint8_t evolve;      /* 0..100: % chance per loop that one hit moves */
    uint8_t mute;
    uint8_t lock;        /* skipped by "all tracks" generation */
    uint8_t pos;         /* next step to play */
    int16_t last;        /* step last played, -1 before start (for displays) */
    uint16_t loops;
    uint8_t len_random;  /* 0: fixed length; 1..8: generate picks 1..N bars */
    uint8_t triplet;     /* its steps are eighth triplets (12 a bar), not sixteenths */
} nseq_track;

typedef struct {
    uint32_t at;         /* sample clock time */
    uint8_t track, vel;
    int8_t note;
    uint8_t used;
} nseq_event;

typedef struct {
    nfm_engine *engine;
    nfx *fx;             /* optional: hits and audio go through the effects */
    nseq_track track[NSEQ_TRACKS];
    float bpm;
    float swing;         /* 0..1: off-beat sixteenths later, 1 = half a step */
    float loose;         /* 0..1: random lateness and velocity spread */
    uint8_t max_poly;    /* most hits that may sound on one step (track 1 always plays) */
    uint8_t playing;
    int8_t root;         /* transpose for tonal tracks, set by MIDI */
    uint32_t now;        /* sample clock */
    float to_next;       /* samples until the next global step */
    uint32_t steps;      /* global steps since start */
    uint32_t rng;
    nseq_event queue[NSEQ_QUEUE];
    /* told of each global step (another line clocked by this one): its number, the swing's delay
     * in samples, the step's length */
    void (*on_step)(void *ctx, uint32_t step, float delay, float step_len);
    void *step_ctx;
} nseq;

void nseq_init(nseq *q, nfm_engine *engine, uint32_t seed);
/* Route hits and rendering through an effects module (NULL: none). */
void nseq_set_fx(nseq *q, nfx *fx);
void nseq_start(nseq *q);
void nseq_stop(nseq *q);

/* New random pattern for one track, using its length, hit range, pitch
 * range and the kind of sound in its slot. */
void nseq_generate(nseq *q, int track);
void nseq_generate_all(nseq *q);   /* unlocked tracks only */

/* Live edits. Length changes keep the steps beyond the new end. */
void nseq_set_length(nseq *q, int track, int steps);
void nseq_set_hits(nseq *q, int track, int lo, int hi);
void nseq_set_step(nseq *q, int track, int step, int on, int vel, int note);
void nseq_set_flag(nseq *q, int track, int which, int value); /* NSEQ_FLAG_* */
enum { NSEQ_FLAG_MUTE, NSEQ_FLAG_LOCK, NSEQ_FLAG_EVOLVE, NSEQ_FLAG_PITCH_RANGE, NSEQ_FLAG_LEN_RANDOM,
       NSEQ_FLAG_TRIPLET };  /* (the flag alone: the steps are taken as they are) */

/* A track into triplets (1) or sixteenths (0), its pattern moved along: each hit to the nearest step of
 * the other grid in time (the offbeat eighth to the third triplet, a shuffle; two on one step: the
 * louder), the length the same time (bars stay bars), the playhead where it is. */
void nseq_regrid(nseq *q, int track, int triplet);
/* the grids' step counts for the same time: sixteenths -> triplets and back */
static inline int nseq_to_triplets(int n) { return n / 4 * 3 + (n % 4 < 3 ? n % 4 : 3); }
static inline int nseq_to_sixteenths(int n) { return n / 3 * 4 + n % 3; }
static inline int nseq_bar(const nseq_track *tr) { return tr->triplet ? NSEQ_TBAR : NSEQ_BAR; }
/* Note-on from MIDI. Returns 1 if the note was used. */
int nseq_midi_note(nseq *q, int note, int velocity);

/* Render n frames of the engine with the sequencer driving it. */
void nseq_render(nseq *q, float *out_l, float *out_r, int n);

#endif

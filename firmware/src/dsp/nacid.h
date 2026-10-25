/* nacid: an acid bass line, the fifth track. One voice in the manner of a TB-303: a saw or square
 * oscillator, a resonant four-pole low-pass with a decaying envelope on its cutoff, accent and slide;
 * and its own random sequencer, the same idea as nseq's: a pattern drawn from a scale, with notes,
 * rests, accents, slides and octave jumps by chance, that evolves a little each loop.
 *
 * The drums' sequencer clocks it (nacid_step, from nseq's step hook), so swing and tempo are shared.
 * Its sound goes through the drums' effects: nacid_render adds to the dry bus and to the reverb and
 * delay sends, with its own drive and the moving filter (by chance, a bar at a time: nacid_route).
 * Besides a 303's saw and square: a narrow pulse, a triangle, a sine and two detuned saws.
 *
 * Portable C99 like nfm; no libc, no malloc. All calls belong to the audio thread or happen between
 * renders. */
#ifndef NACID_H
#define NACID_H

#include "nfm.h"

#define NACID_BAR 16
#define NACID_MAX_BARS 4
#define NACID_STEPS (NACID_BAR * NACID_MAX_BARS)
#define NACID_SCALES 6
enum { NACID_SAW, NACID_SQUARE, NACID_PULSE, NACID_TRI, NACID_SINE, NACID_DUO, NACID_WAVES };

enum { NACID_ON = 1, NACID_ACCENT = 2, NACID_SLIDE = 4 };

typedef struct {
    uint8_t flags;                 /* NACID_* */
    int8_t note;                   /* semitones from the root, -12..24 */
} nacid_note;

/* the settings, 0..1 unless said */
enum {
    NACID_P_DENSITY,               /* share of steps with a note */
    NACID_P_SLIDES, NACID_P_ACCENTS, NACID_P_OCTAVES,
    NACID_P_EVOLVE,                /* chance per loop that one step changes */
    NACID_P_CUTOFF, NACID_P_RESO, NACID_P_ENVMOD, NACID_P_DECAY, NACID_P_ACCENT,
    NACID_P_LEVEL,
    NACID_PARAMS
};

typedef struct {
    float sr;
    uint32_t rng;
    /* the pattern */
    nacid_note step[NACID_STEPS];
    uint8_t length, pos, scale, wave, mute;   /* wave: NACID_* */
    int16_t last;                  /* the step last played, -1 stopped (displays) */
    float p[NACID_PARAMS];
    float tune;                    /* semitones on the whole line */
    int8_t root;                   /* the sequencer's root (MIDI) */
    /* the step to come: its time and what it plays */
    uint32_t at;
    uint8_t pending;
    nacid_note next;
    uint8_t next_tie;              /* the step after it slides on: hold the gate */
    float step_len;
    /* the voice */
    uint32_t now;
    float phase, phase2, freq, target, glide;
    float env, acc_env, amp;       /* cutoff envelope, accent envelope, the gate's amplitude */
    float gate;                    /* samples until the gate closes, < 0 held */
    uint8_t gate_on, accent, sounding;
    float s[4];                    /* the ladder */
    float g, k, comp;              /* its coefficients (every 8 samples) */
    uint8_t ctl;
    /* the effects: the filter faded in and out, the drive, sends, from nacid_route */
    float ins_f, ins_f_to;
    float send_rev, send_dly, rev_to, dly_to;
    float is1, is2;
    nfm_sat sat;
    float drive_to;
    float peak;                    /* the level now, for a meter */
} nacid;

void nacid_init(nacid *a, float sr, uint32_t seed);
void nacid_generate(nacid *a);
void nacid_set_length(nacid *a, int steps);
void nacid_set_step(nacid *a, int step, int flags, int note);
void nacid_reset(nacid *a);        /* the sequencer starts: from the top */
void nacid_stop(nacid *a);         /* the gate closes */

/* The drums' sequencer's step: this one plays delay samples from now (swing, lateness), steps
 * step_len apart. */
void nacid_step(nacid *a, float delay, float step_len);
/* The effects from now: the moving filter in or out, the drive (0..1), the send levels. */
void nacid_route(nacid *a, int filter, float drive, float send_rev, float send_dly);
/* Play one note now, outside the pattern (accent: 0 / 1). */
void nacid_hit(nacid *a, int note, int accent);

/* Add n frames to the bus and the sends; the inserts take the engine's settings. */
void nacid_render(nacid *a, const nfm_engine *e, float *out_l, float *out_r, float *rev, float *dly, int n);

#endif

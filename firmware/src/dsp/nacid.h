/* nacid: an acid bass line, the fifth track. One voice in the manner of a TB-303: an oscillator that
 * never stops, the 303's filter (a diode ladder whose first stage sits an octave lower, with a
 * high-pass in its resonance feedback: about 18 dB/oct in the octave above the cutoff, bass that stays
 * when the resonance goes up), a decaying envelope on the cutoff, accent on the VCA and the cutoff
 * through a sweep capacitor that builds up over accents in a row, and the 303's slide: an RC on the
 * pitch (60 ms), the same speed whatever the interval, so a big slide may not get there before the next
 * note. Env Mod lowers where the sweep starts, as on a 303; a plain note's attack is soft, an accent's
 * sharp; an accent on a slid note takes the envelope as it is, so it is a weak one.
 *
 * The gate as a 303's: a note's gate is half a step; a step with SLIDE holds it to the next step, which
 * then glides to its note without a new attack (no VCA or filter envelope again), and a rest after it
 * closes the gate. The SLIDE flag sits on the note that slides on, into the next (also from the last
 * step into the first).
 *
 * Its own random sequencer: the rhythm first (likelier on the one, the beats and the offbeat eighths,
 * rests between), as a motif that repeats over the bars with variations; then the notes (the root and
 * the fifth most, small steps mostly, now and then a leap or a note outside the scale), the octaves,
 * accents where they push the rhythm, and slides where they move: most where the next note is another,
 * an octave above all. Chaos (0..1) moves every one of those chances towards none: at 1 the scale, the
 * beat and the motif are gone.
 *
 * The drums' sequencer clocks it (nacid_step, from nseq's step hook), so swing and tempo are shared.
 * Its sound goes through the drums' effects: nacid_render adds to the dry bus and to the reverb and
 * delay sends, with its own drive and the moving filter (by chance, a bar at a time: nacid_route).
 * Besides a 303's saw and square (the saw clipped, as the 303 makes it): a narrow pulse, a triangle, a sine and two detuned saws.
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
    NACID_P_CHAOS,                 /* the generator: 0 by the rules, 1 by chance */
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
    uint8_t next_slide;            /* it slides on: its gate is held into the next step */
    float step_len;
    /* the voice */
    uint32_t now;
    float phase, phase2, freq;     /* the oscillator: it runs on, notes or not */
    float cv, cv_to, glide;        /* the pitch in semitones over C2 and where it goes, the slide's RC */
    float env, amp;                /* the cutoff envelope, the VCA */
    float att, att_soft, att_hard; /* the VCA's attack: this note's, a plain note's, an accent's */
    float acc_env, acc_vca, acc_cap;   /* the accent: its envelope, on the VCA, the sweep capacitor */
    float gate;                    /* samples until the gate closes, < 0 held */
    uint8_t gate_on, accent, acc_gate, attack, sounding;
    float s[4], sf, so;            /* the ladder, the feedback's high-pass, the output's */
    float ga, gb, k, comp;         /* its coefficients (every 8 samples) */
    float dk_env, dk_acc, chg, dis;
    uint8_t ctl;
    /* the effects: the filter faded in and out, the drive, sends, from nacid_route */
    float ins_f, ins_f_to;
    float send_rev, send_dly, rev_to, dly_to;
    float is1, is2;
    nfm_sat sat;
    float drive_to;
    /* the modulation effect (nfm's), on a share of the notes: faded in and out note by note */
    nfm_mod mod;
    nfm_mod_state ms;              /* ms.line: set by the owner (NFM_MOD_LEN frames), else no flanger / chorus */
    float mod_mix, mod_to;
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
/* Play one note now, outside the pattern (accent: 0 / 1): a new attack, never a slide. */
void nacid_hit(nacid *a, int note, int accent);

/* Add n frames to the bus and the sends; the inserts take the engine's settings. */
void nacid_render(nacid *a, const nfm_engine *e, float *out_l, float *out_r, float *rev, float *dly, int n);

#endif

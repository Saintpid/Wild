/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se: the instrument. Four FM percussion sounds (nfm), a random pattern sequencer over them (nseq),
 * an acid bass line with its own random sequencer (nacid, the fifth track) and effects that drift and
 * pick which hits they touch (nfx), behind one command queue.
 *
 *   SOUNDS    four slots, one per track: a 4-operator FM voice with noise, fold, crush and a resonator.
 *             Every hit varies a little around the slot's base patch (Deviance); a new sound is drawn
 *             from a kind (kick, snare, hat, perc, click, metal).
 *   PATTERN   each track loops over its own length, 1..8 bars of 16 steps, so tracks drift. A new
 *             pattern places a random number of hits (Hits low..high) where the kind likes them.
 *   FX        per sound: reverb and delay sends, the share of its hits in a moving filter, a warm drive
 *             (loudness matched). Globally: the reverb's and delay's returns, the filter's and drive's
 *             scale; Chaos how far the settings wander, Motion how fast. A modulation effect per sound
 *             (phaser, flanger, chorus, ring modulator, tremolo) on a share of its hits. Chaos also
 *             brings GLITCHES: a beat repeat, a stretch played backwards or a tape stop, mostly at the
 *             end of bars 4 and 8 of the phrase, elsewhere only an eighth long, off the beat. MUTADE, in
 *             the same bars, mutes and unmutes tracks by chance a quarter at a time, and always goes
 *             back to the mutes as set.
 *   MASTER    a filter (hi cut, lo cut, resonance), the GLUE (a light mastering chain: a compressor with
 *             a transient expander, mixed in parallel, an EQ that leans the mix towards a pink slope,
 *             an inflator, the width with the lows in mono; loudness matched) and a limiter at -0.3 dBFS
 *             (-1 dBFS with the Glue on). Track 1 and the acid line never lose a hit to the others.
 *
 * Threads: ne_render() runs in the audio interrupt. Everything else posts commands, which the render
 * drains at the start of a block. The pattern and the sounds are read by the UI through ne_seq() and
 * ne_fm() (bytes and floats: a display may see one step stale, never a torn value). ne_restore() writes
 * them directly while the render is held silent. Float only, no libm. */
#pragma once
#include <stdint.h>
#include "nfm.h"
#include "nseq.h"
#include "nfx.h"
#include "nacid.h"

#define NE_SR 44100.0f
#define NE_TRACKS NSEQ_TRACKS          /* 4 */
#define NE_BAR NSEQ_BAR                /* 16 steps */
#define NE_BARS NSEQ_MAX_BARS          /* 8 */
#define NE_STEPS NSEQ_MAX_STEPS        /* 128 */
#define NE_ACID NE_TRACKS              /* the acid line's track number: 4 (the fifth) */
#define NE_ASTEPS NACID_STEPS          /* 64: four bars */

/* global parameters, value ranges in ne_param_info() */
enum {
    G_TEMPO, G_SWING, G_LOOSE, G_POLY,
    G_CHAOS, G_MOTION, G_REVERB, G_DELAY,
    G_FILTER, G_DRIVE, G_LEVEL, G_MIDICH,
    G_HICUT, G_LOCUT, G_RESO,                        /* the master filter, on everything */
    G_GLUE,                                          /* the bus compressor's amount (a limiter after it) */
    G_PKNOB,                                         /* what the PRESETS encoder turns (ne_pknob_*) */
    G_USBAUDIO,                                      /* the master output over USB: on / off (CPU) */
    G_MUTADE,                                        /* bars 4 and 8: tracks muted / unmuted by chance */
    G_SELMODE,                                       /* what SEL does (the UI's): NE_SEL_* */
    G_SPACE,                                         /* the reverb's room and the delay's time move */
    G_GMIX, G_GEQ, G_GCOLOR, G_GWIDTH,               /* the Glue's mastering: parallel mix, EQ, inflator, width */
    G_NPARAMS
};
enum { NE_SEL_CHANCE, NE_SEL_AB, NE_SEL_COPY, NE_SEL_MODES };
/* per-track parameters */
enum {
    T_BARS, T_RANDLEN, T_HITLO, T_HITHI,
    T_PITCH, T_EVOLVE, T_KIND, T_MUTE,
    T_DEV, T_STRESS, T_SPIKE, T_BLEED,
    T_FLUID, T_LEVEL,
    T_TUNE,                                          /* semitones on the sound (reset by a new sound) */
    T_REVERB, T_DELAY, T_FILTER, T_DRIVE,            /* its effects: sends, the filter's share, drive */
    T_MOD, T_MODAMT, T_MODRATE, T_MODCHANCE,         /* its modulation effect (NFM_MOD_*), on a share of hits */
    T_NPARAMS
};
/* the acid line's parameters */
enum {
    A_BARS, A_NOTES, A_SLIDES, A_ACCENTS,
    A_OCTAVES, A_SCALE, A_EVOLVE, A_MUTE,
    A_CUTOFF, A_RESO, A_ENVMOD, A_DECAY,
    A_ACCENT, A_WAVE, A_TUNE, A_LEVEL,
    A_REVERB, A_DELAY, A_FILTER, A_DRIVE,
    A_MOD, A_MODAMT, A_MODRATE, A_MODCHANCE,
    A_CHAOS,                                         /* the generator: by the rules (0) .. by chance (100) */
    A_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} ne_param_t;
const ne_param_t *ne_aparam_info(int i);             /* A_* */
void ne_aparam_text(int i, int v, char *buf);
const ne_param_t *ne_param_info(int i);              /* G_* */
const ne_param_t *ne_tparam_info(int i);             /* T_* (defaults: ne_tparam_def) */
int ne_tparam_def(int track, int i);
void ne_param_text(int i, int v, char *buf);         /* the value as the screen shows it (12 bytes) */
void ne_tparam_text(int i, int v, char *buf);
const char *ne_kind_name(int kind);                  /* "Kick" .. "Metal" */
/* the PRESETS encoder's choices (G_PKNOB): 0..NE_PKNOBS-1, each a G_* or, with NE_PK_TRACK, a T_* of
 * the selected track */
#define NE_PK_TRACK 0x80
#define NE_PK_ACID 0x40                              /* ...or an A_* of the acid line */
int ne_pknob_target(int v);

/* ---- commands (main loop -> render). The main loop is the only producer. */
void ne_init(void);
void ne_set(int param, int value);                   /* G_* */
void ne_tset(int track, int param, int value);       /* T_* */
void ne_play(int on);
void ne_step(int track, int step, int on, int vel, int note);
void ne_generate(int track);                         /* a new pattern; -1: every unmuted track */
void ne_new_sound(int track, int kind);              /* a new base patch of that kind (-1: the track's own) */
void ne_hit(int track, int vel, int note);           /* play the sound now (vel 1..127, note in semitones) */
void ne_midi_note(int note, int vel);                /* nseq's map: C1.. new pattern, C2 all, C3..C5 root */
enum { NE_CLK_TICK, NE_CLK_START, NE_CLK_CONTINUE, NE_CLK_STOP };
void ne_clock(int msg);                              /* MIDI clock in: ticks set the tempo, start / stop */
void ne_voices(int limit);                           /* the CPU guard: new hits use this many voices */
void ne_panic(void);
void ne_space_shift(void);                           /* SPACE moves the room now, by its chance (SEL) */
void ne_aset(int param, int value);                  /* A_* */
void ne_astep(int step, int flags, int note);        /* NACID_* flags, note -12..24 */
/* (ne_generate(NE_ACID): a new acid pattern; ne_generate(-1) includes it unless muted) */

/* ---- the whole state, for saving: the patterns, lengths and base patches (normalised x 65535) */
typedef struct {
    uint8_t len[NE_TRACKS];                          /* steps, 1..128 */
    uint16_t step[NE_TRACKS][NE_STEPS];              /* 0 off; else vel (1..127) | (note + 32) << 7 */
    uint16_t patch[NE_TRACKS][NFM_PARAMS];
    int8_t kind[NE_TRACKS];
} ne_state_t;
void ne_snapshot(ne_state_t *s);                     /* main loop: what plays now */
void ne_restore(const ne_state_t *s);                /* main loop: the render is silent meanwhile */
typedef struct {
    uint8_t len;                                     /* steps, 1..64 */
    uint8_t rsv[3];
    uint8_t flags[NE_ASTEPS];                        /* NACID_* */
    int8_t note[NE_ASTEPS];
} ne_acid_t;
void ne_acid_snapshot(ne_acid_t *s);
void ne_acid_restore(const ne_acid_t *s);

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void ne_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
const nseq *ne_seq(void);
const nfm_engine *ne_fm(void);
const nacid *ne_acid(void);
extern volatile float ne_acid_level;
extern volatile uint32_t ne_glitches;                /* glitches played since boot */
extern volatile uint32_t ne_repeat_max;              /* frames: the longest beat repeat since boot */
extern volatile uint32_t ne_mutades;                 /* Mutade's quarters since boot */
extern volatile uint32_t ne_limited;                 /* frames the limiter took more than 1 dB off */
extern volatile float ne_track_level[NE_TRACKS];     /* each sound's level now, 0..~1 */
extern volatile float ne_out_level;                  /* the output's peak over the last block (tails too) */
extern volatile uint32_t ne_dropped;                 /* commands lost to a full queue */
extern volatile uint8_t ne_ext;
extern volatile uint8_t ne_usb_audio;                /* G_USBAUDIO: the USB audio input is fed */                      /* following MIDI clock */
extern volatile uint16_t ne_bpm_x10;                 /* the tempo playing now (MIDI clock's, or G_TEMPO) */

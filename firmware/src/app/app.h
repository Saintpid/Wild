/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se app: the project (everything that is saved) and the app's entry points. Included by the unity
 * build after gfx.c (drawing helpers in scope) and by the host simulator. (FiMba-1's app.h, from FoMni's.) */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../dsp/n001se.h"

#ifndef OM_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define OM_VERSION "Wild 0.3.1"
#endif
#define PROJ_MAGIC 0x53313031u           /* "101S": never a FiMba-1 or FoMni project */
/* The format: a newer version reads an older project and gives what it lacks its defaults. Add a value
 * at the end of G_* / T_*, bump the format and say here how many the older one had. */
#define PROJ_FORMAT 16u                    /* 2: master filter, PRESETS knob, Tune, the preset number;
                                            3: the acid line (appended);
                                            4: effects per track (T_ and A_ REVERB..DRIVE);
                                            5: USB audio on / off (G_USBAUDIO);
                                            6: a modulation effect per track (T_ and A_ MOD..MODCHANCE);
                                            7: Mutate (G_MUTATE);
                                            8: what SEL does (G_SELMODE);
                                            9: the acid line's Chaos (A_CHAOS);
                                            10: Space (G_SPACE);
                                            11: the Glue's mastering (G_GMIX..G_GWIDTH);
                                            12: a new 303 sound's waves (G_ACIDWAVES);
                                            13: the acid line's Deviance (A_DEV);
                                            14: triplets per track (T_GRID, A_GRID);
                                            15: Drift in / past, per track (T_DRIFT, A_DRIFT);
                                            16: Dynamics per drum track (T_DYN) */
#define PROJ_G_OF(f) ((f) == 1u ? 12 : (f) <= 4u ? 17 : (f) <= 6u ? 18 : (f) <= 7u ? 19 : (f) <= 9u ? 20 : (f) <= 10u ? 21 : (f) <= 11u ? 25 : G_NPARAMS)   /* the values each format holds */
#define PROJ_T_OF(f) ((f) == 1u ? 14 : (f) <= 3u ? 15 : (f) <= 5u ? 19 : (f) <= 13u ? 23 : (f) <= 14u ? 24 : (f) <= 15u ? 25 : T_NPARAMS)
#define PROJ_A_OF(f) ((f) <= 2u ? 0 : (f) == 3u ? 16 : (f) <= 5u ? 20 : (f) <= 8u ? 24 : (f) <= 12u ? 25 : (f) <= 13u ? 26 : (f) <= 14u ? 27 : A_NPARAMS)

typedef struct {
    uint32_t magic, format;
    int16_t g[G_NPARAMS];                /* n001se.h G_* */
    int16_t t[NE_TRACKS][T_NPARAMS];     /* n001se.h T_*, per track */
    ne_state_t st;                       /* the patterns and the sounds, as saved */
    uint8_t preset;                      /* the preset slot it came from / went to last (0..63) */
    uint8_t bank;                        /* the bank it is (0..PROJ_BANKS - 1; older: 0) */
    uint8_t rsv[30];                     /* room to grow */
    ne_acid_t acid;                      /* the acid line's pattern, as saved */
    int16_t a[A_NPARAMS];                /* n001se.h A_* */
} project_t;

extern project_t proj;
/* The banks: whole projects (patterns, sounds, settings) to switch between, SEL + a white key. The one
 * playing is proj (proj.bank); its own place here is stale meanwhile, and the UI uses it as scratch. Two to
 * a flash sector, saved like the project: once stopped and quiet. */
#define PROJ_BANKS 12
_Static_assert(PROJ_BANKS <= 2 * (PLAT_BANK_SECTORS - 1), "two banks a sector (the last: the automation)");
_Static_assert(2 * sizeof(project_t) <= PLAT_STORE_MAX, "two banks must fit a sector");
extern project_t proj_bank[PROJ_BANKS];
extern uint8_t proj_bank_dirty;          /* the sectors to save (bits) */
int proj_bank_has(int n);                /* 1: bank n holds a project (the one playing always) */
void project_bank_keep(int n);           /* the state now into bank n's place, saved when quiet */
int project_banks_save(void);            /* the changed sectors (and the automation): 0 = all saved */
/* AUTOMATION (REC): a knob's moves, AUTO_PTS values over its lane's bars (1, 2, 4 or 8), looped in step with
 * the phrase, for one bank. k: the UI's knob (0x80 a track's T_*, 0x40 the acid line's A_*, else G_*), track
 * for a T_*; v: the value - lo (255: none yet). A pool shared by all the banks, saved in the last bank sector. */
#define AUTO_LANES 28
#define AUTO_PTS 128
typedef struct {
    uint8_t k, track, bank, bars;        /* bars 0: free */
    uint8_t v[AUTO_PTS];
} auto_lane_t;
_Static_assert(AUTO_LANES * sizeof(auto_lane_t) <= PLAT_STORE_MAX, "the automation must fit its sector");
extern auto_lane_t proj_auto[AUTO_LANES];
extern uint8_t proj_auto_dirty;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
int project_preset_save(int n);          /* the state now into preset slot n: 0 = saved */
int project_preset_load(int n);          /* preset slot n becomes the project and plays: 0 = loaded */
void project_apply(void);                /* every value to the engine (boot, load) */

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);

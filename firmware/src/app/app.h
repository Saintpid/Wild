/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se app: the project (everything that is saved) and the app's entry points. Included by the unity
 * build after gfx.c (drawing helpers in scope) and by the host simulator. (FiMba-1's app.h, from FoMni's.) */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../dsp/n001se.h"

#ifndef OM_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define OM_VERSION "DEV"
#endif
#define PROJ_MAGIC 0x53313031u           /* "101S": never a FiMba-1 or FoMni project */
/* The format: a newer version reads an older project and gives what it lacks its defaults. Add a value
 * at the end of G_* / T_*, bump the format and say here how many the older one had. */
#define PROJ_FORMAT 2u                    /* 2: master filter, PRESETS knob, Tune, the preset number */
#define PROJ_G_OF(f) ((f) == 1u ? 12 : G_NPARAMS)   /* the values each format holds */
#define PROJ_T_OF(f) ((f) == 1u ? 14 : T_NPARAMS)

typedef struct {
    uint32_t magic, format;
    int16_t g[G_NPARAMS];                /* n001se.h G_* */
    int16_t t[NE_TRACKS][T_NPARAMS];     /* n001se.h T_*, per track */
    ne_state_t st;                       /* the patterns and the sounds, as saved */
    uint8_t preset;                      /* the preset slot it came from / went to last (0..63) */
    uint8_t rsv[31];                     /* room to grow */
} project_t;

extern project_t proj;

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

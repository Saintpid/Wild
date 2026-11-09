/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too. (The frame of it, knobs, bands and autosave, is FiMba-1's and FoMni's ui.c.)
 *
 *   SEQ mode (SEQ lit)  the 16 white keys are the 16 steps of one bar of the selected track: lit = a hit,
 *                  the dim one walking = where it plays. A key sets its step, strong; pressed on a hit,
 *                  when let go it takes it a strength down: a strong one (over 82) soft (50), a soft one
 *                  off (the acid line's: a note with an accent, plain, off); not if a knob turned meanwhile: held, KNOB 1 is the hit's velocity,
 *                  KNOB 2 its note. A track in triplets (the Rhythm page, HOME again: Grid): keys 1-12 its
 *                  12 steps, three to a beat; 13-16 none.
 *   LIVE mode      the white keys play the sounds: four keys a sound, each a step up the scale
 *   black 1 / 2    the bar shown (and edited): one down / up, and it stays; both: follow the playhead
 *   black 3        the Master page (hi cut, lo cut, resonance, glue); again: back
 *   black 4        the preset list to load: a slot (SELECT, OCT-/+, PRESETS, a white key), black 4 again
 *   black 5-9      mute track 1-5 (dim = on, flashing with its hits; dark = muted); unmuted, it is selected
 *   black 10 / 11  a new pattern / a new sound for the selected track; with GLO held: for every track
 *   PLAY start / stop    SEQ the mode
 *   REC            automation: the knobs turned while it plays are recorded, REC again ends it, and they
 *                  loop (a knob with automation: a red dot); REC held and a knob turned: its automation erased
 *   OCT- / OCT+    the selected track          SELECT the track   ALGORITHM the page
 *   PRESETS        the knob of the Setup page's choice (Glue by default)
 *   SEL            tap/release then white 1-8: select (empty: copy); held + white 1-8: overwrite/copy
 *   SEL + white 16 Chance: new scene each time; selecting the current bank leaves Chance
 *                  bank selection lights: playing bright, occupied dim; tempo shared
 *   HOME EDIT ENV FX LFO GLO   the pages (pressed again: the button's next page)
 *   GLO + OCT- / OCT+   the mixer / Audio Load; any button but PLAY, SEL and REC closes them (only that)
 *   GLO + HOME     clear all (asked, GLO + HOME again: yes): every bank, all automation, an empty start; saved songs stay
 *   GLO + REC      the selected track's automation erased
 *   SAVE           the preset list to save: a slot, SAVE again (it also saves the project by itself,
 *                  after a change, once stopped and quiet); held: reset
 *   Program Change n    loads preset n + 1
 *
 *   FX             the selected track's effects (reverb, delay, filter, drive); again: its modulation
 *                  effect (phaser, flanger, chorus, ring modulator, tremolo); again: the bus (returns)
 *
 * Track 5 is the acid line: its pages are its own (pattern, track, sound, tone, Deviance, FX: ENV's third page
 * is only its own, KNOB 1 its Deviance, as the drums' is on their Sound page); a step key sets a note (with an accent),
 * held, KNOB 1 is its note, KNOB 2 its accent, KNOB 3 its slide (into the next step). CC 74 / 71: its
 * cutoff / resonance. Its Chaos (the Tone page, KNOB 4): how far its new patterns leave the rules.
 *
 * The screen: the five tracks, one bar each (the bar shown), and the phrase: eight bars, the eighth red;
 * in it the beats count 1 2 3 4 to the next round. */

enum { V_PATTERN, V_RHYTHM, V_TRACK, V_SOUND, V_TONE, V_ADEV, V_TFX, V_TMOD, V_FX, V_DRIFT, V_ROUTE, V_MASTER, V_GLUE, V_GROOVE, V_SETUP, V_SCENE, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Pattern", "Rhythm", "Track", "Sound", "Tone", "Deviance", "Track FX", "Mod", "FX bus", "Drift",
                                              "Routing", "Master", "Glue", "Groove", "Setup", "Scene"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_HOME, B_EDIT, B_ENV, B_ENV, B_ENV, B_FX, B_FX, B_FX, B_LFO, B_LFO, B_GLO, B_GLO, B_GLO, B_GLO, B_GLO};
#define K_NONE 255                     /* a knob with nothing on it */
#define KT 0x80                        /* a track parameter (T_*), else global (G_*) */
#define KA 0x40                        /* an acid line parameter (A_*) */
#define UI_TRACKS (NE_TRACKS + 1)      /* the four drum tracks and the acid line */
static const uint8_t ACID_KNOB[V_TMOD + 1][4] = {    /* the track pages, for the acid line */
    {KA | A_BARS, KA | A_NOTES, KA | A_SLIDES, KA | A_EVOLVE},
    {KA | A_GRID, K_NONE, K_NONE, K_NONE},
    {KA | A_ACCENTS, KA | A_OCTAVES, KA | A_SCALE, KA | A_LEVEL},
    {KA | A_CUTOFF, KA | A_RESO, KA | A_ENVMOD, KA | A_DECAY},
    {KA | A_WAVE, KA | A_ACCENT, KA | A_TUNE, KA | A_CHAOS},
    {KA | A_DEV, K_NONE, K_NONE, K_NONE},
    {KA | A_REVERB, KA | A_DELAY, KA | A_FILTER, KA | A_DRIVE},
    {KA | A_MOD, KA | A_MODAMT, KA | A_MODRATE, KA | A_MODCHANCE},
};
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {KT | T_BARS, KT | T_HITLO, KT | T_HITHI, KT | T_EVOLVE},
    {KT | T_GRID, KT | T_DYN, K_NONE, K_NONE},       /* HOME again: the steps, sixteenths or triplets; how soft */
    {KT | T_RANDLEN, KT | T_PITCH, KT | T_LEVEL, KT | T_MUTE},
    {KT | T_DEV, KT | T_STRESS, KT | T_SPIKE, KT | T_BLEED},
    {KT | T_KIND, KT | T_FLUID, KT | T_TUNE, K_NONE},
    {K_NONE, K_NONE, K_NONE, K_NONE},                /* (the acid line's only) */
    {KT | T_REVERB, KT | T_DELAY, KT | T_FILTER, KT | T_DRIVE},
    {KT | T_MOD, KT | T_MODAMT, KT | T_MODRATE, KT | T_MODCHANCE},
    {G_REVERB, G_DELAY, G_FILTER, G_DRIVE},
    {G_CHAOS, G_MOTION, G_MUTATE, G_SPACE},
    {KT | T_DRIFT, K_NONE, K_NONE, K_NONE},          /* the selected track: in Drift's effects, or past them */
    {G_HICUT, G_LOCUT, G_RESO, G_GLUE},
    {G_GMIX, G_GEQ, G_GCOLOR, G_GWIDTH},
    {G_TEMPO, G_SWING, G_LOOSE, G_POLY},
    {G_LEVEL, G_MIDICH, G_PKNOB, G_USBAUDIO},
    {G_ACIDWAVES, K_NONE, K_NONE, K_NONE},           /* (G_SELMODE: kept in projects, unused since the banks) */
};
#define SAVE_HOLD_MS 1500u             /* SAVE held this long: reset to the defaults? (a tap: save) */
#define RESET_ASK_MS 5000u             /* ...asked this long: SAVE again resets, anything else keeps */

#define NWHITE 16
#define NBLACK 11
static const uint8_t WHITE_K[NWHITE] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[NBLACK] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
enum { BK_BARDN, BK_BARUP, BK_UNDO, BK_REDO, BK_MUTE1, BK_MUTE5 = BK_MUTE1 + 4, BK_PATTERN, BK_SOUND };
enum { L_NONE, L_SAVE, L_LOAD };       /* the preset list open */
enum { LA_NONE, LA_DELETE, LA_ERASE }; /* ...what it asks */
static const int8_t LIVE_NOTE[4] = {0, 3, 7, 10};   /* LIVE: the four keys of a sound, up its scale */

/* colours: near black, grey ink, one colour per track; red is only the phrase's last bar */
#define K_BG RGB(16, 16, 18)
#define K_PANEL RGB(32, 32, 36)
#define K_SEL RGB(56, 56, 62)                  /* the selected track's row */
#define K_LINE RGB(58, 58, 64)
#define K_DIM RGB(118, 118, 126)
#define K_TEXT RGB(232, 232, 236)
#define K_ACC RGB(255, 186, 72)
#define K_RED RGB(229, 72, 77)
static const uint16_t TRACK_COL[UI_TRACKS] = {RGB(255, 132, 64), RGB(96, 196, 255), RGB(246, 216, 84),
                                              RGB(178, 140, 255), RGB(110, 222, 120)};

static struct {
    uint8_t view, track, live;
    uint8_t view_bar, bar_lock;        /* the bar the keys show; locked: chosen, else the playhead's */
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t half_k[NE];                /* a slow knob's half step left over (half), and whose (half_k) */
    int8_t half[NE];
    uint8_t dirty;
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t save_t;                   /* when SAVE went down (held: reset to the defaults?) */
    uint32_t reset_ask;                /* until when "RESET?" waits for SAVE (0: not asking) */
    uint8_t reset_all;                 /* ...asked as "CLEAR ALL?" (GLO + HOME): every bank, all automation */
    int8_t held_step;                  /* a step key held (its step), -1 none */
    uint8_t held_was_on, held_edited;
    uint8_t held_vel;                  /* ...its velocity and note as edited (the engine's lag a block) */
    int8_t held_note;
    uint8_t held_flags;                /* ...an acid step's accent and slide */
    uint8_t voices;                    /* the CPU guard's voice limit now */
    uint8_t list, list_slot;           /* the preset list (L_*) and the slot on it */
    uint8_t list_ask;                  /* ...asked: LA_DELETE the song on it, LA_ERASE all songs */
    int8_t copy_from;                  /* ...a song being copied (SEL): its slot, -1 none */
    uint8_t bank_select;               /* tap SEL: next white key selects */
    uint8_t chance;                    /* a scene by chance plays (what was before: sc_*) */
    uint8_t rec;                       /* REC: the knobs turned are recorded (auto_*) */
    uint32_t pop_t;                    /* the last knob change: its box over the tracks (0: none) */
    char pop[2][16];                   /* ...the parameter and its value */
    int16_t beat;                      /* the beat counted big over the tracks (phrase step / 4), -1 none */
    uint32_t beat_t;                   /* ...when it came */
    uint32_t used[(PLAT_PRESETS + 31) / 32];   /* the slots holding a preset (read as the list opens) */
    int8_t part_of[PLAT_PRESETS];      /* ...and the song whose part a slot holds (-1: none) */
    uint8_t parts[PLAT_PRESETS];       /* ...and how many a song has */
    uint8_t vis[PLAT_PRESETS], nvis;   /* ...the slots on the list (a song's parts are not), how many */
    uint8_t nfree;                     /* ...and the free ones (room for songs and their parts) */
    float meter[UI_TRACKS];
    int8_t batt;                       /* the battery's % in the header (batt_shown), -1 none, 101 USB power */
} ui;

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
}
void ui_say(const char *a, const char *b) { say(a, b); }
int ui_dirty(void) { return ui.dirty != 0; }

static void itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void mark_dirty(void) { ui.dirty = 1; }

/* (no C library on the device: libc.c has only mem*) */
static int32_t s_len(const char *s)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static void s_cpy(char *d, const char *s)
{
    while ((*d++ = *s++))
        ;
}

/* ------------------------------------------------------------ values --- */
/* a drum track's parameter with the acid line selected: the first track's (the PRESETS encoder's) */
static int dtrack(void) { return ui.track < NE_TRACKS ? ui.track : 0; }
static const ne_param_t *kinfo(int k)
{
    return k & KT ? ne_tparam_info(k & ~KT) : k & KA ? ne_aparam_info(k & ~KA) : ne_param_info(k);
}
static int kval(int k) { return k & KT ? proj.t[dtrack()][k & ~KT] : k & KA ? proj.a[k & ~KA] : proj.g[k]; }
/* the name over a knob: a drum's Deviance from 90 lets the sound wander off (n001se.c), and says so */
static const char *kname(int k)
{
    return k == (KT | T_DEV) && kval(k) >= 90 ? "Wander" : kinfo(k)->name;
}
static void ktext(int k, int v, char *t)
{
    if (k & KT)
        ne_tparam_text(k & ~KT, v, t);
    else if (k & KA)
        ne_aparam_text(k & ~KA, v, t);
    else
        ne_param_text(k, v, t);
}
/* the pages a track has: the Deviance page under ENV is the acid line's (the drums' is on Sound) */
static int view_here(int v) { return v != V_ADEV || ui.track == NE_ACID; }
static int view_knob(int i)
{
    if (ui.track == NE_ACID && ui.view == V_ROUTE)
        return i ? K_NONE : KA | A_DRIFT;
    return ui.track == NE_ACID && ui.view <= V_TMOD ? ACID_KNOB[ui.view][i] : VIEW_KNOB[ui.view][i];
}

static void tpar_set(int t, int p, int v)
{
    const ne_param_t *i = ne_tparam_info(p);
    v = v < i->lo ? i->lo : v > i->hi ? i->hi : v;
    if (v == proj.t[t][p])
        return;
    proj.t[t][p] = (int16_t)v;
    if (p == T_GRID)                                 /* the pattern moves along into the grid */
        ne_regrid(t, v);
    else
        ne_tset(t, p, v);
    /* the hit range stays a range: the other end follows */
    if (p == T_HITLO && proj.t[t][T_HITHI] < v)
        tpar_set(t, T_HITHI, v);
    if (p == T_HITHI && proj.t[t][T_HITLO] > v)
        tpar_set(t, T_HITLO, v);
    mark_dirty();
}

static void sel_forget(void);
static void knob_set(int k, int v)
{
    const ne_param_t *p = kinfo(k);
    if (k & KT) {
        tpar_set(dtrack(), k & ~KT, v);
        return;
    }
    v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
    if (k & KA) {
        if (v == proj.a[k & ~KA])
            return;
        proj.a[k & ~KA] = (int16_t)v;
        if ((k & ~KA) == A_GRID)
            ne_regrid(NE_ACID, v);
        else
            ne_aset(k & ~KA, v);
    } else {
        if (v == proj.g[k])
            return;
        proj.g[k] = (int16_t)v;
        ne_set(k, v);
        if (k == G_SELMODE)
            sel_forget();
    }
    mark_dirty();
}

/* knob acceleration by speed: a slow turn is one step a detent, a quick one up to 8 */
static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (range <= 24 || !a || accel_off)
        return s;
    if (a > 1)
        dt /= a;
    m = dt < 15u ? 8u : dt < 30u ? 5u : dt < 60u ? 2u : 1u;
    return s * (int32_t)m;
}

/* ------------------------------------------------------------ undo --- */
static int track_bars(int t);
/* Bounded, aligned journal. Records swap with live state on undo/redo, so redo needs
 * no second snapshot. Copies also keep the overwritten bank and automation. The
 * four retired banks fund this budget; no allocation and no flash writes here. */
#define SC_N 9
static int16_t sc_was[UI_TRACKS][SC_N];
#define UNDO_JOIN 1500u
#define UNDO_BYTES (10u * sizeof(project_t))
static uint32_t undo_mem[UNDO_BYTES / 4u];
typedef struct {
    project_t state;
    int16_t scene[UI_TRACKS][SC_N];
    uint16_t size;
    uint8_t chance, target, automation, bank_op;
} undo_entry;
static uint16_t undo_at[16], undo_used;
static uint8_t undo_c, undo_n;
static uint32_t undo_key, undo_ms;
static void auto_finish_all(void);
static void auto_resume(void);
static void scene_bank_original(project_t *p);
static void undo_take(project_t *p)
{
    *p = proj;
    ne_snapshot(&p->st);
    ne_acid_snapshot(&p->acid);
}
static undo_entry *undo_entry_at(int i)
{
    return (undo_entry *)((uint8_t *)undo_mem + undo_at[i]);
}
static void undo_clear(void)
{
    undo_c = undo_n = 0;
    undo_used = 0;
    undo_key = 0;
}
static void undo_swap(void *a, void *b, unsigned n)
{
    uint8_t *x = a, *y = b;
    while (n--) { uint8_t t = *x; *x++ = *y; *y++ = t; }
}
static void undo_record(uint32_t key, int target, int automation, int bank_op)
{
    uint32_t now = plat_ms();
    unsigned size, i;
    undo_entry *r;
    uint8_t *payload;
    if (key && key == undo_key && undo_c == undo_n && undo_c && now - undo_ms < UNDO_JOIN) {
        undo_ms = now;
        return;
    }
    if (bank_op) auto_finish_all();
    for (i = 0; i < AUTO_LANES; i++)
        automation |= proj_auto[i].bars != 0;
    size = (sizeof(undo_entry) + (target >= 0 ? sizeof(project_t) : 0u) +
            (automation ? sizeof proj_auto : 0u) + 3u) & ~3u;
    if (undo_c < undo_n) undo_used = undo_at[undo_c];
    undo_n = undo_c;
    while (undo_n && (undo_used + size > sizeof undo_mem || undo_n == 16)) {
        unsigned drop = undo_entry_at(0)->size;
        uint8_t *bytes = (uint8_t *)undo_mem;
        for (i = drop; i < undo_used; i++) bytes[i - drop] = bytes[i];
        undo_used -= (uint16_t)drop;
        undo_n--; undo_c--;
        for (i = 0; i < undo_n; i++) undo_at[i] = (uint16_t)(undo_at[i + 1] - drop);
    }
    undo_at[undo_n++] = undo_used;
    r = undo_entry_at(undo_c++);
    undo_used += (uint16_t)size;
    r->size = (uint16_t)size;
    r->target = target < 0 ? 255 : (uint8_t)target;
    r->automation = (uint8_t)automation;
    r->bank_op = (uint8_t)bank_op;
    r->chance = ui.chance;
    memcpy(r->scene, sc_was, sizeof sc_was);
    undo_take(&r->state);
    payload = (uint8_t *)(r + 1);
    if (target >= 0) {
        memcpy(payload, &proj_bank[target], sizeof(project_t));
        payload += sizeof(project_t);
    }
    if (automation) memcpy(payload, proj_auto, sizeof proj_auto);
    undo_key = key;
    undo_ms = now;
}
static int recording_active(void);
static void undo_mark(uint32_t key)
{
    if (!recording_active()) undo_record(key, -1, 0, 0);
}
_Static_assert(sizeof(undo_entry) + sizeof(project_t) + sizeof proj_auto + 3u <= UNDO_BYTES,
               "one full bank operation must fit undo");
/* the values SEL's scenes and the banks never change: the tempo, the level, the Setup and Scene pages' */
static const uint8_t SEL_KEEP[] = {G_TEMPO, G_LEVEL, G_MIDICH, G_PKNOB, G_USBAUDIO, G_SELMODE, G_ACIDWAVES};
static int sel_kept(int g)
{
    uint32_t i;
    for (i = 0; i < sizeof SEL_KEEP; i++)
        if (SEL_KEEP[i] == g)
            return 1;
    return 0;
}
/* the project and the engine to another state: only what differs goes to the engine (the patterns and
 * sounds stop the sound a moment, the settings do not); keep: SEL_KEEP's stay. The state it leaves is in
 * the playing bank's place (stale meanwhile) after it: state_was() */
#define state_was() (&proj_bank[proj.bank])
static void state_go(const project_t *to, int keep)
{
    project_t *now = state_was();
    int i, t, st, ac;
    undo_take(now);
    st = memcmp(&now->st, &to->st, sizeof to->st) != 0;
    ac = memcmp(&now->acid, &to->acid, sizeof to->acid) != 0;
    if (st)
        ne_restore(&to->st);
    if (ac)
        ne_acid_restore(&to->acid);
    for (i = 0; i < G_NPARAMS; i++)
        if (proj.g[i] != to->g[i] && !(keep && sel_kept(i)))
            ne_set(i, proj.g[i] = to->g[i]);
    for (t = 0; t < NE_TRACKS; t++)
        for (i = 0; i < T_NPARAMS; i++)
            if (proj.t[t][i] != to->t[t][i]) {
                proj.t[t][i] = to->t[t][i];
                if (st && (i == T_BARS || i == T_RANDLEN))   /* (the restored state holds its length) */
                    ne_tset_keep(t, i, proj.t[t][i]);
                else
                    ne_tset(t, i, proj.t[t][i]);
            }
    for (i = 0; i < A_NPARAMS; i++)
        if (proj.a[i] != to->a[i]) {
            proj.a[i] = to->a[i];
            if (!ac || i != A_BARS)
                ne_aset(i, proj.a[i]);
        }
    ui.held_step = -1;
    if (ui.bar_lock && ui.view_bar >= track_bars(ui.track))
        ui.bar_lock = 0;
    mark_dirty();
}
/* Swap one complete operation, including both bank identities and overwritten data. */
static void undo_go(int d)
{
    undo_entry *r;
    uint8_t *payload;
    int old, next;
    if (d < 0 ? !undo_c : undo_c >= undo_n) {
        say("NOTHING TO", d < 0 ? "UNDO" : "REDO");
        return;
    }
    auto_finish_all();
    ui.rec = 0;
    ui.bank_select = 0;
    ui.pop_t = 0;
    r = undo_entry_at(d < 0 ? --undo_c : undo_c++);
    old = proj.bank;
    next = r->state.bank;
    state_go(&r->state, r->bank_op);
    r->state = *state_was();
    if (r->bank_op && old != next && ui.chance)
        scene_bank_original(&proj_bank[old]);
    proj.bank = (uint8_t)next;
    undo_swap(&ui.chance, &r->chance, 1);
    undo_swap(sc_was, r->scene, sizeof sc_was);
    payload = (uint8_t *)(r + 1);
    if (r->target != 255) {
        undo_swap(&proj_bank[r->target], payload, sizeof(project_t));
        proj_bank_dirty |= (uint8_t)(1u << (r->target / 2));
        payload += sizeof(project_t);
    }
    if (r->automation) {
        undo_swap(proj_auto, payload, sizeof proj_auto);
        proj_auto_dirty = 1;
    }
    proj_bank_dirty |= (uint8_t)((1u << (old / 2)) | (1u << (next / 2)));
    auto_resume();
    undo_key = 0;
    {
        char b[16];
        itoa_u(d < 0 ? undo_c : (uint32_t)(undo_n - undo_c), b);
        s_cpy(b + s_len(b), " LEFT");
        say(d < 0 ? "UNDO" : "REDO", b);
    }
}

/* Tune and Pitch: a semitone every two detents however quick the turn, so a sound walks
 * up or down the notes as it plays instead of jumping (the host's "spin": exact steps) */
static int slow_knob(int k)
{
    return k == (KA | A_TUNE) || k == (KT | T_TUNE) || k == (KT | T_PITCH);
}

static int auto_touch(int k, int t);
static void rec_press(void);
static void auto_finish_all(void);
static void auto_erase_bank(int b);
static void auto_new_pattern(int t);
static void auto_rec_track(void);
/* a knob turned: its change, and its name and value big over the tracks a moment (REC held: its
 * automation erased instead) */
static void turn(int role, int k, int32_t e)
{
    const ne_param_t *p;
    if (!e || k == K_NONE)
        return;
    if (auto_touch(k, dtrack()))
        return;
    p = kinfo(k);
    undo_mark(1u << 24 | (uint32_t)k << 8 | ui.track);
    if (slow_knob(k) && !accel_off) {                /* two detents a step, never quicker */
        if (ui.half_k[role] != k)
            ui.half_k[role] = (uint8_t)k, ui.half[role] = 0;
        e += ui.half[role];
        ui.half[role] = (int8_t)(e % 2);
        e /= 2;
    } else
        e = accel(role, e, p->hi - p->lo);
    if (e)
        knob_set(k, kval(k) + e);
    s_cpy(ui.pop[0], kname(k));
    ktext(k, kval(k), ui.pop[1]);
    ui.pop_t = plat_ms() | 1u;
}

/* ------------------------------------------------------------ pattern --- */
static const nseq_track *trk(int t) { return &ne_seq()->track[t < NE_TRACKS ? t : 0]; }
static int playing(void) { return ne_seq()->playing; }
/* any track's length, last step, hits, mute: a drum track's or the acid line's */
static int tlen(int t) { return t == NE_ACID ? ne_acid()->length : trk(t)->length; }
static int tlast(int t) { return t == NE_ACID ? ne_acid()->last : trk(t)->last; }
static int tbypass(int t) { return !(t == NE_ACID ? proj.a[A_DRIFT] : proj.t[t][T_DRIFT]); }
static int tmuted(int t) { return t == NE_ACID ? proj.a[A_MUTE] : trk(t)->mute; }
static int tsend(int t, int dly)              /* the track sends to the reverb (0) / the delay (1) */
{
    int v = t == NE_ACID ? proj.a[dly ? A_DELAY : A_REVERB] : proj.t[t][dly ? T_DELAY : T_REVERB];
    return v > 0 && proj.g[dly ? G_DELAY : G_REVERB] > 0;
}
static const char *tname(int t) { return t == NE_ACID ? "Acid" : ne_kind_name(ne_fm()->slot[t].kind); }
/* the steps in a bar: 16, or 12 in triplets (keys 13-16 none) */
static int tbar(int t) { return t == NE_ACID ? ne_acid()->bar : nseq_bar(trk(t)); }
static int track_bars(int t) { return (tlen(t) + tbar(t) - 1) / tbar(t); }
static int play_step(int t) { return playing() && tlast(t) >= 0 ? tlast(t) : -1; }
static int play_bar(int t) { int s = play_step(t); return s < 0 ? -1 : s / tbar(t); }
static int shown_bar(void)
{
    int b;
    if (ui.bar_lock)
        return ui.view_bar;
    b = play_bar(ui.track);
    return b < 0 ? ui.view_bar : b;
}
static int step_on(int t, int s)
{
    if (s >= tlen(t))
        return 0;
    return t == NE_ACID ? ne_acid()->step[s].flags & NACID_ON : trk(t)->step[s].on;
}

static void select_track(int t)
{
    ui.track = (uint8_t)((t + UI_TRACKS) % UI_TRACKS);
    if (!view_here(ui.view))
        ui.view = V_SOUND;
    if (ui.bar_lock && ui.view_bar >= track_bars(ui.track))
        ui.bar_lock = 0;
    {
        char a[8] = {'T', 'R', 'A', 'C', 'K', ' ', (char)('1' + ui.track), 0};
        say(a, tname(ui.track));
    }
}

static void bank_key(int w);
static int sel_held(void);
static void list_key(int w);
static void white_down(int w)
{
    int t = ui.track, s;
    if ((sel_held() || ui.bank_select) && !ui.list) {                    /* SEL held: the banks */
        bank_key(w);
        return;
    }
    if (ui.list) {                                   /* the list: a slot of its page of 16 */
        list_key(w);
        return;
    }
    if (ui.live) {
        ne_hit(w >> 2, 112, LIVE_NOTE[w & 3]);
        return;
    }
    if (w >= tbar(t)) {
        say("TRIPLETS", "12 STEPS A BAR");
        return;
    }
    s = shown_bar() * tbar(t) + w;
    if (s >= tlen(t)) {
        char b[16];
        itoa_u((uint32_t)tlen(t), b);
        say("PAST THE END", b);
        return;
    }
    undo_mark(2u << 24 | (uint32_t)t << 8 | (uint32_t)s);   /* the press and its edits: one */
    ui.held_step = (int8_t)s;
    ui.held_edited = 0;
    ui.held_was_on = (uint8_t)step_on(t, s);
    if (t == NE_ACID) {                              /* a note on the root, with an accent, no slide */
        const nacid_note *st = &ne_acid()->step[s];
        ui.held_flags = ui.held_was_on ? st->flags : NACID_ON | NACID_ACCENT;
        ui.held_note = ui.held_was_on ? st->note : 0;
        if (!ui.held_was_on) {
            ne_astep(s, NACID_ON | NACID_ACCENT, 0);
            mark_dirty();
        }
        return;
    }
    ui.held_vel = ui.held_was_on ? trk(t)->step[s].vel : NSEQ_VEL_STRONG;
    ui.held_note = ui.held_was_on ? trk(t)->step[s].note : 0;
    if (!ui.held_was_on) {
        ne_step(t, s, 1, NSEQ_VEL_STRONG, 0);
        mark_dirty();
    }
}

static void white_up(int w)
{
    int t = ui.track;
    if (ui.live || ui.list || ui.held_step < 0 || ui.held_step % tbar(t) != w)
        return;
    if (ui.held_was_on && !ui.held_edited) {       /* a strength down: strong, soft, off (acid: accent, plain, off) */
        if (t == NE_ACID && ui.held_flags & NACID_ACCENT)
            ne_astep(ui.held_step, ui.held_flags & ~NACID_ACCENT, ui.held_note);   /* (a slide stays) */
        else if (t == NE_ACID)
            ne_astep(ui.held_step, 0, ui.held_note);
        else if (ui.held_vel > NSEQ_VEL_SPLIT)
            ne_step(t, ui.held_step, 1, NSEQ_VEL_SOFT, ui.held_note);
        else
            ne_step(t, ui.held_step, 0, ui.held_vel, ui.held_note);
        mark_dirty();
    }
    ui.held_step = -1;
}

static void say_note(int note)
{
    char b[16];
    if (note < 0) {
        b[0] = '-';
        itoa_u((uint32_t)-note, b + 1);
    } else {
        b[0] = '+';
        itoa_u((uint32_t)note, b + 1);
    }
    say("NOTE", b);
}

/* a held acid step: KNOB 1 its note, KNOB 2 its accent, KNOB 3 its slide (right: on, left: off) */
static void acid_step_edit(int knob, int32_t e)
{
    int note = ui.held_note, f = ui.held_flags;
    undo_mark(2u << 24 | (uint32_t)NE_ACID << 8 | (uint32_t)ui.held_step);
    if (knob == 0) {
        note += (int)e;
        note = note < -12 ? -12 : note > 24 ? 24 : note;
        say_note(note);
    } else if (knob == 1 || knob == 2) {
        int bit = knob == 1 ? NACID_ACCENT : NACID_SLIDE;
        f = e > 0 ? f | bit : f & ~bit;
        say(knob == 1 ? "ACCENT" : "SLIDE", f & bit ? "ON" : "OFF");
    } else {
        return;
    }
    ui.held_note = (int8_t)note;
    ui.held_flags = (uint8_t)(f | NACID_ON);
    ne_astep(ui.held_step, ui.held_flags, note);
    ui.held_edited = 1;
    mark_dirty();
}

/* a held step: KNOB 1 its velocity, KNOB 2 its note */
static void step_edit(int knob, int32_t e)
{
    int t = ui.track, s = ui.held_step, vel = ui.held_vel, note = ui.held_note;
    char b[16];
    undo_mark(2u << 24 | (uint32_t)t << 8 | (uint32_t)s);
    if (knob == 0) {
        vel += (int)accel(EN_K1, e, 127) * 4;
        vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
        itoa_u((uint32_t)vel, b);
        say("VELOCITY", b);
    } else if (knob == 1) {
        note += (int)e;
        note = note < -24 ? -24 : note > 24 ? 24 : note;
        say_note(note);
    } else {
        return;
    }
    ui.held_vel = (uint8_t)vel;
    ui.held_note = (int8_t)note;
    ne_step(t, s, 1, vel, note);
    ui.held_edited = 1;
    mark_dirty();
}

static void set_view(int v);
static void list_open(int which);
static void list_go(void);

/* a new sound. The acid line's sound is its settings: new ones by chance, within what makes acid
 * (resonant, a strong envelope), a 303's saw or square (303 waves Original) or any wave (All). The acid line, perc, click and metal also land in an octave
 * by chance (Tune -24, -12, 0, +12 or +24); the others at their own pitch (Tune 0). */
static uint32_t ui_rng = 0x9e3779b9u;
static int rnd_in(int lo, int hi)
{
    ui_rng ^= ui_rng << 13;
    ui_rng ^= ui_rng >> 17;
    ui_rng ^= ui_rng << 5;
    return lo + (int)((ui_rng >> 8) % (uint32_t)(hi - lo + 1));
}
static void new_sound(int t)
{
    if (t == NE_ACID) {
        ui_rng ^= plat_ms();
        knob_set(KA | A_CUTOFF, rnd_in(10, 55));
        knob_set(KA | A_RESO, rnd_in(45, 92));
        knob_set(KA | A_ENVMOD, rnd_in(30, 90));
        knob_set(KA | A_DECAY, rnd_in(15, 70));
        knob_set(KA | A_ACCENT, rnd_in(30, 90));
        knob_set(KA | A_WAVE, rnd_in(NACID_SAW, proj.g[G_ACIDWAVES] ? NACID_WAVES - 1 : NACID_SQUARE));
        knob_set(KA | A_TUNE, 12 * rnd_in(-2, 2));
        return;
    }
    ne_new_sound(t, proj.t[t][T_KIND]);              /* (the engine's Tune: 0) */
    proj.t[t][T_TUNE] = 0;
    {
        int k = proj.t[t][T_KIND];
        if (k == NFM_KIND_PERC || k == NFM_KIND_CLICK || k == NFM_KIND_METAL) {
            ui_rng ^= plat_ms();
            tpar_set(t, T_TUNE, 12 * rnd_in(-2, 2));
        }
    }
}

/* a new sound on every track that plays (GLO + black 11, CC 117) */
static void new_sounds_all(void)
{
    int t;
    for (t = 0; t < NE_TRACKS; t++)
        if (!proj.t[t][T_MUTE])
            new_sound(t);
    if (!proj.a[A_MUTE])
        new_sound(NE_ACID);
}

/* SEL: a SCENE by chance. Which tracks play (each by a chance of 0.6, at least two) and each sound's
 * effects: its sends, filter share, drive and modulation effect. What they were is kept, and SEL again
 * puts it all back, whatever was turned in the scene. In the scene the mute keys set the kept mutes (the
 * original's), so what plays after it is chosen during it. A preset loaded or a reset ends the scene there. */
static const uint8_t SC_T[SC_N] = {T_MUTE, T_REVERB, T_DELAY, T_FILTER, T_DRIVE, T_MOD, T_MODAMT, T_MODRATE, T_MODCHANCE};
static const uint8_t SC_A[SC_N] = {A_MUTE, A_REVERB, A_DELAY, A_FILTER, A_DRIVE, A_MOD, A_MODAMT, A_MODRATE, A_MODCHANCE};
static void scene_bank_original(project_t *p)
{
    int t, i;
    for (t = 0; t < UI_TRACKS; t++)
        for (i = 0; i < SC_N; i++)
            if (t == NE_ACID) p->a[SC_A[i]] = sc_was[t][i];
            else p->t[t][SC_T[i]] = sc_was[t][i];
}
/* in a scene by chance, the mute keys set the original's mutes: what plays when SEL brings it back */
static int sc_chance(void) { return ui.chance; }
static void sc_set(int t, int i, int v)
{
    if (t == NE_ACID)
        knob_set(KA | SC_A[i], v);
    else
        tpar_set(t, SC_T[i], v);
}
static int rnd_or0(int zero_pct, int lo, int hi) { return rnd_in(0, 99) < zero_pct ? 0 : rnd_in(lo, hi); }
static void scene_back(void)
{
    int t, i;
    for (t = 0; t < UI_TRACKS; t++)
        for (i = 0; i < SC_N; i++)
            sc_set(t, i, sc_was[t][i]);
    ui.chance = 0;
}
/* a new scene by chance; one playing already goes first (what it was before stays the original) */
static void scene_new(void)
{
    int t, i, plays = 0;
    if (ui.chance)
        scene_back();
    ui_rng ^= plat_ms();
    for (t = 0; t < UI_TRACKS; t++)
        for (i = 0; i < SC_N; i++)
            sc_was[t][i] = t == NE_ACID ? proj.a[SC_A[i]] : proj.t[t][SC_T[i]];
    for (t = 0; t < UI_TRACKS; t++) {
        int on = rnd_in(0, 99) < 60;
        plays += on;
        sc_set(t, 0, !on);
        sc_set(t, 1, rnd_or0(35, 5, 60));            /* reverb */
        sc_set(t, 2, rnd_or0(35, 5, 60));            /* delay */
        sc_set(t, 3, rnd_or0(40, 10, 70));           /* filter */
        sc_set(t, 4, rnd_or0(40, 10, 60));           /* drive */
        sc_set(t, 5, rnd_or0(50, 1, NFM_MODS - 1));  /* mod */
        sc_set(t, 6, rnd_in(20, 90));
        sc_set(t, 7, rnd_in(5, 80));
        sc_set(t, 8, rnd_in(30, 100));
    }
    while (plays < 2) {                              /* at least two play */
        t = rnd_in(0, UI_TRACKS - 1);
        if (t == NE_ACID ? proj.a[A_MUTE] : proj.t[t][T_MUTE]) {
            sc_set(t, 0, 0);
            plays++;
        }
    }
    ui.chance = 1;
    say("SCENE", "BY CHANCE");
}
static void scene(void)
{
    undo_mark(0);
    if (ui.chance) {
        scene_back();
        say("SCENE", "BACK");
        return;
    }
    scene_new();
}

/* Tap/release SEL to select; SEL still down at the white key means copy.
 * No long-press timer. The highest white key always makes a new Chance scene. */
static void sel_forget(void) { ui.chance = 0; }
static void sel_press(void)
{
    ui.bank_select ^= 1u;
    say(ui.bank_select ? "SELECT BANK" : "CANCELLED", ui.bank_select ? "WHITE 1-8 / 16 CHANCE" : 0);
    if (ui.bank_select) ui.held_step = -1;
}
#define BANK_CHANCE 15
static void say_bank(const char *a, int n, const char *b)
{
    char s[16] = "BANK ";
    itoa_u((uint32_t)n + 1u, s + 5);
    say(a ? a : s, a ? s : b);
}
/* A copy needs one free lane per source lane after the destination's lanes go.
 * Refuse the whole operation if it cannot fit; never make a partial copy. */
static int bank_auto_room(int from, int to)
{
    int i, need = 0, room = 0;
    for (i = 0; i < AUTO_LANES; i++) {
        need += proj_auto[i].bars && proj_auto[i].bank == from;
        room += !proj_auto[i].bars || proj_auto[i].bank == to;
    }
    return need <= room;
}
static void bank_auto_copy(int from, int to)
{
    int i, j;
    for (i = 0; i < AUTO_LANES; i++)
        if (proj_auto[i].bank == to) proj_auto[i].bars = 0;
    for (i = 0; i < AUTO_LANES; i++)
        if (proj_auto[i].bars && proj_auto[i].bank == from) {
            for (j = 0; j < AUTO_LANES && proj_auto[j].bars; j++) {}
            proj_auto[j] = proj_auto[i];
            proj_auto[j].bank = (uint8_t)to;
        }
    proj_auto_dirty = 1;
}
static void bank_go(int n, int copy)
{
    int cur = proj.bank, has;
    if (n == cur) {
        if (!copy && ui.chance) scene();
        else say_bank(0, n, "PLAYS");
        return;
    }
    has = proj_bank_has(n);
    copy |= !has;
    if (copy && !bank_auto_room(cur, n)) {
        say("COPY NOT MADE", "AUTOMATION FULL");
        return;
    }
    undo_record(0, copy ? n : -1, copy, 1);
    ui.rec = 0;
    /* Leaving Chance keeps the original in the source bank. A copy captures
     * what is playing, including Chance, as an ordinary editable bank. */
    project_bank_keep(cur);
    if (copy) {
        proj_bank[n] = proj_bank[cur];
        proj_bank[n].bank = (uint8_t)n;
        bank_auto_copy(cur, n);
    }
    if (ui.chance) scene_back();
    project_bank_keep(cur);
    state_go(&proj_bank[n], 1);
    proj.bank = (uint8_t)n;
    sel_forget();
    auto_resume();
    ui.pop_t = 0;
    proj_bank_dirty |= (uint8_t)(1u << (n / 2));
    mark_dirty();
    say_bank(0, n, copy ? (has ? "REPLACED: A COPY" : "NEW: A COPY") : "");
}
static int sel_held(void) { return (ui.btn >> B_SEL & 1u) != 0; }
static void bank_key(int w)
{
    int copy = sel_held();
    ui.btn_used |= 1u << B_SEL;
    ui.held_step = -1;
    if (w == BANK_CHANCE) {
        ui.bank_select = 0;
        undo_mark(0);
        ne_space_shift();
        scene_new();
        mark_dirty();
    } else if (w < PROJ_BANKS) {
        ui.bank_select = 0;
        bank_go(w, copy);
    } else {
        say("NO BANK", "USE WHITE 1-8 OR 16");
    }
}

static void say_bar(int b)
{
    char s[16] = "BAR ";
    itoa_u((uint32_t)b + 1u, s + 4);
    say(s, "SHOWN");
}

static void black_down(int b)
{
    int t = ui.track;
    if (b == BK_BARDN || b == BK_BARUP) {
        int other = BLACK_K[b == BK_BARDN ? BK_BARUP : BK_BARDN], nb;
        if (ui.keys >> other & 1u) {                 /* both: follow the playhead */
            ui.bar_lock = 0;
            say("FOLLOW", "PLAYHEAD");
            return;
        }
        nb = shown_bar() + (b == BK_BARUP ? 1 : -1);
        if (nb < 0 || nb >= track_bars(t)) {
            char s[16];
            itoa_u((uint32_t)track_bars(t), s);
            s_cpy(s + s_len(s), track_bars(t) == 1 ? " BAR" : " BARS");
            say("TRACK HAS", s);
            return;
        }
        ui.view_bar = (uint8_t)nb;
        ui.bar_lock = 1;
        say_bar(nb);
        return;
    }
    if (b == BK_UNDO || b == BK_REDO) {
        undo_go(b == BK_UNDO ? -1 : 1);
        return;
    }
    if (b >= BK_MUTE1 && b <= BK_MUTE5) {
        int m = b - BK_MUTE1;
        char a[8] = {'T', 'R', 'A', 'C', 'K', ' ', (char)('1' + m), 0};
        if (sc_chance()) {                           /* in a scene by chance: the original's mutes */
            sc_was[m][0] = (int16_t)!sc_was[m][0];
            say(a, sc_was[m][0] ? "MUTED AFTER" : "ON AFTER");
            return;
        }
        undo_mark(0);
        if (m == NE_ACID)
            knob_set(KA | A_MUTE, !proj.a[A_MUTE]);
        else
            tpar_set(m, T_MUTE, !proj.t[m][T_MUTE]);
        if (!(m == NE_ACID ? proj.a[A_MUTE] : proj.t[m][T_MUTE])) {
            select_track(m);                         /* back on: it is the one to edit */
            say(a, "ON");
        } else {
            say(a, "MUTED");
        }
    } else if ((b == BK_PATTERN || b == BK_SOUND) && (ui.btn >> B_GLO & 1u)) {
        ui.btn_used |= 1u << B_GLO;                  /* GLO held: every track (GLO keeps its page) */
        undo_mark(0);
        if (b == BK_PATTERN) {
            ne_generate(-1);
            auto_new_pattern(-1);
        } else
            new_sounds_all();
        mark_dirty();
        say(b == BK_PATTERN ? "NEW PATTERNS" : "NEW SOUNDS", "ALL TRACKS");
    } else if (b == BK_PATTERN) {
        undo_mark(0);
        ne_generate(t);
        auto_new_pattern(t);
        mark_dirty();
        say("NEW PATTERN", t == NE_ACID ? "Acid" : ne_kind_name(proj.t[t][T_KIND]));
    } else if (b == BK_SOUND) {
        undo_mark(0);
        new_sound(t);
        mark_dirty();
        say("NEW SOUND", t == NE_ACID ? "Acid" : ne_kind_name(proj.t[t][T_KIND]));
    }
}

/* ------------------------------------------------------------ presets --- */
/* SAVE or ARP opens the list on the last slot used; the same key again saves / loads the slot on it.
 * SEL copies the song on it (SEL again on a free slot: there), HOME deletes it (HOME again: yes), GLO + HOME
 * erases every song (GLO + HOME again: yes); any other button cancels that, or closes the list. A song's
 * parts (its banks 3-8, its automation) are in slots of their own, from the top down: not on the list. A
 * save erases a flash sector for each slot of the song: the sound stops a moment. */
static int slot_used(int n) { return (int)(ui.used[n >> 5] >> (n & 31) & 1u); }
static void slot_name(int n, char *b)               /* "005" */
{
    b[0] = (char)('0' + (n + 1) / 100);
    b[1] = (char)('0' + (n + 1) / 10 % 10);
    b[2] = (char)('0' + (n + 1) % 10);
    b[3] = 0;
}
/* the slots from flash: songs, their parts (a part whose song is gone: free), what is on the list */
static void list_scan(void)
{
    int n;
    for (n = 0; n < PLAT_PRESETS; n++) {
        uint32_t m = 1u << (n & 31);
        ui.used[n >> 5] = plat_preset_used(n) ? ui.used[n >> 5] | m : ui.used[n >> 5] & ~m;
        ui.parts[n] = 0;
    }
    for (n = 0; n < PLAT_PRESETS; n++) {
        int c = plat_part_of(n);
        if (c >= 0 && !slot_used(c / 8))
            c = -1;
        ui.part_of[n] = (int8_t)(c < 0 ? -1 : c / 8);
        if (c >= 0)
            ui.parts[c / 8]++;
    }
    ui.nvis = ui.nfree = 0;
    for (n = 0; n < PLAT_PRESETS; n++)
        if (ui.part_of[n] < 0) {
            ui.vis[ui.nvis++] = (uint8_t)n;
            ui.nfree += !slot_used(n);
        }
}
static int list_pos(void)                           /* where the slot chosen is on the list */
{
    int i;
    for (i = 0; i < ui.nvis; i++)
        if (ui.vis[i] >= ui.list_slot)
            return i;
    return ui.nvis ? ui.nvis - 1 : 0;
}
static void list_at(int i)
{
    if (!ui.nvis)
        return;
    i = i < 0 ? 0 : i >= ui.nvis ? ui.nvis - 1 : i;
    ui.list_slot = ui.vis[i];
}
static void list_open(int which)
{
    list_scan();
    ui.list = (uint8_t)which;
    ui.list_ask = LA_NONE;
    ui.copy_from = -1;
    ui.list_slot = proj.preset < PLAT_PRESETS ? proj.preset : 0;
    list_at(list_pos());                             /* (on the list: a part's slot is not) */
    ui.held_step = -1;
}
static void list_move(int d)
{
    ui.list_ask = LA_NONE;
    list_at(list_pos() + d);
}
static void list_key(int w)                          /* a white key: that place of its page of 16 */
{
    int i = list_pos() / NWHITE * NWHITE + w;
    ui.list_ask = LA_NONE;
    if (i < ui.nvis)
        list_at(i);
}
static int preset_load(int n)
{
    char b[8];
    slot_name(n, b);
    if (!plat_preset_used(n)) {
        say("PRESET", "EMPTY");
        return -1;
    }
    auto_finish_all();                               /* (a recording ends with its song) */
    int result = project_preset_load(n);
    if (result != 0) {
        say(result == -4 ? "12-BANK SONG" : "PRESET", result == -4 ? "USE 0.3.29" : "EMPTY");
        return -1;
    }
    undo_clear();                             /* another song starts fresh */
    undo_key = 0;
    ui.held_step = -1;
    ui.bar_lock = 0;
    sel_forget();
    if (ui.view_bar >= track_bars(ui.track))
        ui.view_bar = 0;
    mark_dirty();                                    /* the project now: saved when quiet */
    say("LOADED", b);
    return 0;
}
static void list_go(void)
{
    char b[8];
    int n = ui.list_slot;
    slot_name(n, b);
    if (ui.list == L_SAVE) {
        int rc;
        auto_finish_all();
        rc = project_preset_save(n);
        if (rc == 0) {
            mark_dirty();
            say("SAVED", b);
        } else if (rc == -2) {
            say("SLOT TAKEN", "BY A SONG");
        } else if (rc == -4) {
            say("OLD BANKS", "USE 0.3.29");
        } else if (rc == -3) {
            say("SAVE: NO ROOM", "FOR ITS BANKS");
        } else {
            say("SAVE", "FAILED");
        }
    } else {
        preset_load(n);
    }
    ui.list = L_NONE;
}
/* HOME on the list: the song on it deleted, asked first */
static void list_delete(void)
{
    char b[16] = "DELETE ";
    int n = ui.list_slot;
    slot_name(n, b + 7);
    if (!slot_used(n)) {
        ui.list_ask = LA_NONE;
        say("NOTHING", "TO DELETE");
    } else if (ui.list_ask != LA_DELETE) {
        ui.list_ask = LA_DELETE;
        s_cpy(b + 10, "?");
        say(b, "HOME AGAIN: YES");
        ui.msg_until = plat_ms() + RESET_ASK_MS;
    } else {
        int rc = project_song_delete(n);
        ui.list_ask = LA_NONE;
        say(rc ? "DELETE" : "DELETED", rc ? "FAILED" : b + 7);
        list_scan();
    }
}
/* SEL on the list: the song on it is the one to copy; SEL again, on a free slot, copies it there */
static void list_copy(void)
{
    char b[16] = "COPY ";
    int n = ui.list_slot, rc;
    ui.list_ask = LA_NONE;
    if (ui.copy_from < 0) {
        if (!slot_used(n)) {
            say("NOTHING", "TO COPY");
            return;
        }
        ui.copy_from = (int8_t)n;
        slot_name(n, b + 5);
        say(b, "TO? SEL ON A FREE ONE");
        ui.msg_until = plat_ms() + RESET_ASK_MS;
        return;
    }
    if (n == ui.copy_from) {
        ui.copy_from = -1;
        say("COPY", "CANCELLED");
        return;
    }
    if (slot_used(n)) {
        say("SLOT HAS A SONG", "DELETE IT FIRST");
        return;
    }
    rc = project_song_copy(ui.copy_from, n);
    s_cpy(b, "COPIED TO ");
    slot_name(n, b + 10);
    if (rc == 0)
        say(b, 0);
    else
        say(rc == -3 ? "COPY: NO ROOM" : "COPY", rc == -3 ? "FOR ITS BANKS" : "FAILED");
    ui.copy_from = -1;
    list_scan();
}
/* GLO + HOME on the list: every song erased, asked first */
static void list_erase_all(void)
{
    int rc;
    if (ui.list_ask != LA_ERASE) {
        ui.list_ask = LA_ERASE;
        ui.copy_from = -1;
        say("ERASE ALL SONGS?", "GLO+HOME AGAIN: YES");
        ui.msg_until = plat_ms() + RESET_ASK_MS;
        return;
    }
    ui.list_ask = LA_NONE;
    rc = project_songs_erase();
    say(rc ? "ERASE" : "ALL SONGS", rc ? "FAILED" : "ERASED");
    list_scan();
    list_at(0);
}

static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

/* CLEAR ALL's starting point: every track 1 bar, Fixed; nothing by chance (Loose, Chaos, Motion,
 * Mutate, Space, Evolve, Deviance, Dynamics, Pitch, the 303's Chaos and Deviance at 0) */
static void empty_values(void)
{
    int t;
    static const uint8_t G0[] = {G_LOOSE, G_CHAOS, G_MOTION, G_MUTATE, G_SPACE};
    static const uint8_t T0[] = {T_RANDLEN, T_PITCH, T_EVOLVE, T_DEV, T_DYN};
    static const uint8_t A0[] = {A_EVOLVE, A_CHAOS, A_DEV};
    unsigned i;
    for (i = 0; i < sizeof G0; i++)
        proj.g[G0[i]] = 0;
    for (t = 0; t < NE_TRACKS; t++) {
        proj.t[t][T_BARS] = 1;
        for (i = 0; i < sizeof T0; i++)
            proj.t[t][T0[i]] = 0;
    }
    proj.a[A_BARS] = 1;
    for (i = 0; i < sizeof A0; i++)
        proj.a[A0[i]] = 0;
}
/* the defaults (empty: CLEAR ALL's, no patterns and nothing by chance) */
static void reset_defaults(int empty)
{
    int16_t keep = proj.g[G_MIDICH];
    uint8_t bank = proj.bank;
    int t;
    undo_mark(0);
    project_defaults();
    proj.g[G_MIDICH] = keep;
    proj.bank = bank;                                /* (the bank playing: the others stay) */
    if (empty)
        empty_values();
    auto_erase_bank(bank);
    sel_forget();
    ne_play(0);
    project_apply();                                 /* the starting set's settings, then its sounds */
    for (t = 0; t < NE_TRACKS; t++) {
        ne_new_sound(t, proj.t[t][T_KIND]);          /* (the starting set: at their own pitch) */
        if (!empty)
            ne_generate(t);
    }
    if (empty)
        ne_clear();                                  /* (after project_apply()'s new 303 line) */
    mark_dirty();
    say("RESET", "TO DEFAULTS");
}
/* CLEAR ALL (GLO + HOME, and again to say yes): a clean start. Every bank empty, all automation gone, bank 1
 * as reset but empty (no hits or notes, 1 bar, nothing by chance); the saved songs stay */
static void clear_all(void)
{
    project_legacy = 0;
    project_legacy_cleanup = 1;
    auto_finish_all();
    memset(proj_auto, 0, sizeof proj_auto);
    proj_auto_dirty = 1;
    memset(proj_bank, 0, sizeof proj_bank);
    proj_bank_dirty = (uint8_t)((1u << ((PROJ_BANKS + 1) / 2)) - 1u);
    proj.bank = 0;
    reset_defaults(1);
    undo_clear();                             /* (nothing to go back to: the banks are gone) */
    undo_key = 0;
    ui.held_step = -1;
    ui.bar_lock = 0;
    ui.view_bar = 0;
    say("CLEARED", "ALL BANKS");
}
static void reset_ask(int on)
{
    ui.reset_ask = on ? plat_ms() + RESET_ASK_MS : 0u;
    if (on) {
        say(ui.reset_all ? "CLEAR ALL?" : "RESET?", ui.reset_all ? "GLO+HOME: YES" : "SAVE: YES");
        ui.msg_until = ui.reset_ask;
    } else {
        say("KEPT", 0);
    }
}

static void button(int b)
{
    int v;
    if (b != B_PLAY && b != B_SEL) ui.bank_select = 0;
    if (ui.list && b != B_PLAY) {                    /* the list: its arrows, SAVE / ARP, SEL, HOME */
        if (b == B_OCTDN || b == B_OCTUP)
            list_move(b == B_OCTUP ? 1 : -1);
        else if (b == B_SEL)
            list_copy();
        else if (b == B_HOME)
            list_delete();
        else if (ui.list_ask || ui.copy_from >= 0) {  /* (anything else: that cancelled, the list stays) */
            ui.list_ask = LA_NONE;
            ui.copy_from = -1;
            say("CANCELLED", 0);
        } else if ((b == B_SAVE && ui.list == L_SAVE) || (b == B_ARP && ui.list == L_LOAD))
            list_go();
        else
            ui.list = L_NONE;
        return;
    }
    switch (b) {
    case B_PLAY:
        ne_play(!playing());
        if (!playing())
            ui.bar_lock = 0;
        break;
    case B_SEQ:
        ui.live = !ui.live;
        ui.held_step = -1;
        say("MODE", ui.live ? "LIVE" : "SEQ");
        break;
    case B_OCTDN: select_track(ui.track - 1); break;
    case B_OCTUP: select_track(ui.track + 1); break;
    case B_SAVE:
        if (ui.reset_ask) {
            ui.reset_ask = 0;
            if (ui.reset_all)
                say("KEPT", 0);                      /* (clear all: GLO + HOME says yes) */
            else
                reset_defaults(0);
            break;
        }
        list_open(L_SAVE);
        break;
    case B_ARP: list_open(L_LOAD); break;
    case B_SEL: sel_press(); break;
    case B_REC: rec_press(); break;
    default:
        for (v = 0; v < NVIEWS; v++)                 /* a page button: its page, or its next one */
            if (VIEW_BTN[v] == b) {
                if (VIEW_BTN[ui.view] == b && ui.view + 1 < NVIEWS && VIEW_BTN[ui.view + 1] == b && view_here(ui.view + 1))
                    v = ui.view + 1;
                set_view(v);
                break;
            }
        break;
    }
}

/* ------------------------------------------------------------- MIDI --- */
/* Notes follow nseq's map: C1 D1.. (36..40) a new pattern for track 1..5, C2 (48) for all, C3..C5
 * (60..84) the root of the pitched tracks and the acid line. Controllers (as n001se in FM1 Open):
 * 102..106 the sounds' macros (all tracks), 107 108 chaos and motion, 109..112 filter, drive, delay,
 * reverb, 113 swing, 114 loose, 115 max hits, 7 / 116 level, 117 new sounds (all), 118 new patterns
 * (all), 74 / 71 the acid line's cutoff / resonance, 120 / 123 stop. */
static void midi_cc(uint32_t cc, uint32_t v)
{
    static const uint8_t MAC[5] = {T_DEV, T_STRESS, T_SPIKE, T_BLEED, T_FLUID};
    static const uint8_t FXG[4] = {G_FILTER, G_DRIVE, G_DELAY, G_REVERB};
    int pc = (int)(v * 100u / 127u), t;
    undo_mark(3u << 24 | cc);
    if (cc >= 102u && cc <= 106u) {
        for (t = 0; t < NE_TRACKS; t++)
            tpar_set(t, MAC[cc - 102u], pc);
    } else if (cc == 107u || cc == 108u) {
        knob_set(cc == 107u ? G_CHAOS : G_MOTION, pc);
    } else if (cc >= 109u && cc <= 112u) {
        knob_set(FXG[cc - 109u], pc);
    } else if (cc == 113u || cc == 114u) {
        knob_set(cc == 113u ? G_SWING : G_LOOSE, pc);
    } else if (cc == 115u) {
        knob_set(G_POLY, 1 + (int)(v * 4u / 128u));
    } else if (cc == 7u || cc == 116u) {
        knob_set(G_LEVEL, pc);
    } else if (cc == 117u && v >= 64u) {
        new_sounds_all();
        mark_dirty();
    } else if (cc == 118u && v >= 64u) {
        ne_generate(-1);
        auto_new_pattern(-1);
        mark_dirty();
    } else if (cc == 74u || cc == 71u) {
        knob_set(KA | (cc == 74u ? A_CUTOFF : A_RESO), pc);
    } else if (cc == 120u || cc == 123u) {
        ne_play(0);
    }
}

static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, type = st & 0xF0u;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {   /* clock: tempo, start, stop */
            ne_clock(st == 0xF8u ? NE_CLK_TICK : st == 0xFAu ? NE_CLK_START : st == 0xFBu ? NE_CLK_CONTINUE : NE_CLK_STOP);
            continue;
        }
        if (st >= 0xF0u)
            continue;                                       /* sysex and the rest: not ours */
        if (proj.g[G_MIDICH] && (st & 0x0Fu) + 1u != (uint32_t)proj.g[G_MIDICH])
            continue;
        if (type == 0x90u && d2) {
            if (d1 >= 36u && d1 <= 48u)
                undo_mark(0);
            ne_midi_note((int)d1, (int)d2);
            if (d1 >= 36u && d1 <= 48u) {            /* (C1..E1 a track's new pattern, C2 the drums') */
                if (d1 <= 36u + NE_ACID || d1 == 48u)
                    auto_new_pattern(d1 == 48u ? -2 : (int)d1 - 36);
                mark_dirty();
            }
        } else if (type == 0xB0u) {
            midi_cc(d1, d2);
        } else if (type == 0xC0u && d1 < (uint32_t)PLAT_PRESETS) {
            ui.list = L_NONE;
            preset_load((int)d1);                   /* Program Change: that preset (empty: kept) */
        }
        ui.act_t = plat_ms();
    }
}

/* ------------------------------------------------------------- input --- */
static uint8_t diagnostics_open, diagnostics_chord, mixer_open;
static uint32_t mixer_at;
static float mixer_peak[UI_TRACKS], mixer_level[UI_TRACKS];
static uint32_t mixer_hold[UI_TRACKS];
static uint32_t diagnostics_at;

/* the mixer or Audio Load opened or closed: the screen drawn anew */
static void overlay_redraw(void)
{
    diagnostics_at = plat_ms() - 250u;
    mixer_at = plat_ms() - 66u;
    lcd_fill(0, 0, 240, 240, K_BG);
    memset(ui.sig, 0, sizeof ui.sig);
}
static void overlay_close(void)
{
    mixer_open = diagnostics_open = 0;
    overlay_redraw();
}

static void mixer_turn(int channel, int32_t delta)
{
    int k = channel == NE_ACID ? KA | A_LEVEL : KT | T_LEVEL;
    int old = channel == NE_ACID ? proj.a[A_LEVEL] : proj.t[channel][T_LEVEL];
    int value = old + (int)delta;
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    if (auto_touch(k, channel == NE_ACID ? 0 : channel) || value == old) return;
    undo_mark(1u << 24 | (uint32_t)k << 8 | (uint32_t)channel);
    if (channel == NE_ACID) knob_set(k, value);
    else tpar_set(channel, T_LEVEL, value);
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            if (i != B_SEL && i != B_PLAY) ui.bank_select = 0;
            if ((i == B_HOME || i == B_REC) && (btn >> B_GLO & 1u)) {
                ui.btn_used |= m | 1u << B_GLO;     /* GLO + HOME: clear all (asked); GLO + REC: the track's automation */
                if (i == B_HOME && ui.list) {
                    list_erase_all();                /* (on the list: all songs, asked) */
                } else if (i == B_HOME && ui.reset_ask && ui.reset_all) {
                    ui.reset_ask = 0;                /* asked, and GLO + HOME again: yes */
                    clear_all();
                } else if (i == B_HOME) {
                    ui.reset_all = 1;
                    reset_ask(1);
                } else {
                    auto_rec_track();
                }
                continue;
            }
            if (i == B_SAVE)
                ui.save_t = plat_ms();
            if (i == B_PLAY)                         /* on the press: in time */
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY) {
            if ((mixer_open || diagnostics_open) && i != B_SEL && i != B_REC)
                overlay_close();                     /* the mixer, Audio Load: a button leaves (only that) */
            else
                button((int)i);
        }
    }
    {
        const uint32_t glo = 1u << B_GLO;
        uint32_t which = (btn & glo) && (btn & (1u << B_OCTDN)) ? 1u :
                         (btn & glo) && (btn & (1u << B_OCTUP)) ? 2u : 0u;
        if (which) {
            ui.btn_used |= glo | (1u << (which == 1u ? B_OCTDN : B_OCTUP));
            if (!diagnostics_chord) {
                if (which == 1u) { mixer_open ^= 1u; diagnostics_open = 0; }
                else { diagnostics_open ^= 1u; mixer_open = 0; }
                overlay_redraw();
            }
            diagnostics_chord = 1;
        } else diagnostics_chord = 0;
    }
    if ((btn >> B_SAVE & 1u) && !(ui.btn_used >> B_SAVE & 1u) && !ui.reset_ask &&
        plat_ms() - ui.save_t >= SAVE_HOLD_MS) {
        ui.reset_all = 0;
        reset_ask(1);                                /* SAVE held: ask (no save on release) */
        ui.btn_used |= 1u << B_SAVE;
    }
    if (ui.reset_ask && (int32_t)(plat_ms() - ui.reset_ask) >= 0)
        reset_ask(0);                                /* not answered: kept */
    ch = keys ^ ui.keys;
    if (ui.reset_ask && (ch & keys))
        reset_ask(0);
    ui.keys = keys;
    for (i = 0; i < NBLACK; i++)
        if ((ch & keys) >> BLACK_K[i] & 1u)
            black_down((int)i);
    for (i = 0; i < NWHITE; i++)
        if (ch >> WHITE_K[i] & 1u) {
            if (keys >> WHITE_K[i] & 1u)
                white_down((int)i);
            else
                white_up((int)i);
        }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (mixer_open) mixer_turn(0, e);
        else if (ui.list)
            list_move(e > 0 ? 1 : -1);
        else
            select_track(ui.track + (e > 0 ? 1 : -1));
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {              /* the pages */
        int v = ui.view;
        do
            v = (v + (e > 0 ? 1 : NVIEWS - 1)) % NVIEWS;
        while (!view_here(v));
        set_view(v);
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        int k = ne_pknob_target(proj.g[G_PKNOB]);   /* (NE_PK_TRACK is KT: the selected track's) */
        if (ui.list)
            list_move((int)e);
        else
            turn(EN_PRESET, k, e);
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            if (mixer_open) {
                mixer_turn((int)i + 1, e);
            } else if (ui.held_step >= 0 && !ui.live && ui.track == NE_ACID)
                acid_step_edit((int)i, e);
            else if (ui.held_step >= 0 && !ui.live)
                step_edit((int)i, e);
            else
                turn(EN_K1 + (int)i, view_knob((int)i), e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = plat_ms();
        }
}

/* ------------------------------------------------------------- screen --- */
/* three bands: the header (28 px), the tracks and the phrase (144), the knobs (68) */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68
#define ROW_H 21
#define GRID_X 62
#define CELL_W 11
#define PH_X 8                         /* the phrase: eight bars of PH_W, then the count */
#define PH_W 23

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static uint16_t mix(uint16_t a, uint16_t b, int t)   /* t 0..16: a -> b */
{
    int r = ((a >> 11) * (16 - t) + (b >> 11) * t) / 16, g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16,
        bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* the global step playing now (-1: stopped): the phrase counts from the start */
static int phrase_step(void) { return playing() && ne_seq()->steps ? (int)((ne_seq()->steps - 1u) % (8u * NE_BAR)) : -1; }

/* ------------------------------------------------------------ automation --- */
/* REC: from then on each knob turned while it plays is recorded (the mixer's faders too), its value at
 * each sixteenth, until REC again (or the stop, or a change of bank). Its lane is as many bars as it was
 * recorded for, made 1, 2, 4 or 8, and loops in step with the phrase; where nothing was recorded the
 * value before stays. A knob with a lane plays it; turned by hand it is the hand's, and a second after
 * the hand lets go the lane's again. REC held and a knob turned: that knob's automation erased. A knob
 * that sets what the patterns are (bars, grid, kind) or the device (tempo, MIDI, Setup, Scene) has none. */
#define AUTO_HOLD 1000u                                /* ms the hand keeps a knob after its last turn */
#define REC_MAX 8                                    /* knobs recorded at once */
static uint32_t auto_hold[AUTO_LANES];               /* until when the hand has a lane's knob */
static struct { int8_t lane; uint32_t start, last; } rec_l[REC_MAX];
static uint8_t rec_n;
static void auto_resume(void)
{
    rec_n = 0;
    memset(auto_hold, 0, sizeof auto_hold);
}
static int recording_active(void) { return rec_n != 0; }
static int rec_held(void) { return (ui.btn >> B_REC & 1u) != 0; }
static int auto_ok(int k)
{
    static const uint8_t NOT_T[] = {T_BARS, T_RANDLEN, T_KIND, T_GRID, T_HITLO, T_HITHI}, NOT_A[] = {A_BARS, A_GRID},
                         NOT_G[] = {G_TEMPO, G_MIDICH, G_PKNOB, G_USBAUDIO, G_SELMODE, G_ACIDWAVES};
    const uint8_t *no = k & KT ? NOT_T : k & KA ? NOT_A : NOT_G;
    uint32_t n = k & KT ? sizeof NOT_T : k & KA ? sizeof NOT_A : sizeof NOT_G, i;
    for (i = 0; i < n; i++)
        if (no[i] == (k & 0x3F))
            return 0;
    return 1;
}
/* the lane of knob k (track t for a T_*) in the bank playing, -1 none */
static int auto_find(int k, int t)
{
    int i;
    t = k & KT ? t : 0;
    for (i = 0; i < AUTO_LANES; i++)
        if (proj_auto[i].bars && proj_auto[i].bank == proj.bank && proj_auto[i].k == k && proj_auto[i].track == t)
            return i;
    return -1;
}
static int auto_recording(int l)
{
    int r;
    for (r = 0; r < rec_n; r++)
        if (rec_l[r].lane == l)
            return 1;
    return 0;
}
static int auto_value(const auto_lane_t *l)          /* the knob's value now (proj) */
{
    int i = l->k & 0x3F;
    return l->k & KT ? proj.t[l->track][i] : l->k & KA ? proj.a[i] : proj.g[i];
}
static void auto_set(const auto_lane_t *l, int v)
{
    if (l->k & KT)
        tpar_set(l->track, l->k & 0x3F, v);
    else
        knob_set(l->k, v);
}
/* the phrase's sixteenths since the start: the step playing and how far into it */
static uint32_t auto_step(void) { return ne_seq()->steps - 1u; }
static float auto_frac(void)
{
    const nseq *q = ne_seq();
    float len = NE_SR * 60.0f / (q->bpm < 20.0f ? 20.0f : q->bpm) / 4.0f, f = 1.0f - q->to_next / len;
    return f < 0.0f ? 0.0f : f > 0.999f ? 0.999f : f;
}

/* a recording done: its sixteenths into a lane of 1, 2, 4 or 8 bars, AUTO_PTS values over it */
static void auto_finish(int r)
{
    auto_lane_t *l = &proj_auto[rec_l[r].lane];
    uint8_t out[AUTO_PTS];
    uint32_t span = rec_l[r].last - rec_l[r].start + 1u;
    int bars = 1, n, q, j, i, have = -1;
    while (bars < 8 && (uint32_t)bars * NE_BAR < span)
        bars *= 2;
    n = bars * NE_BAR;
    for (q = 0; q < n; q++) {                        /* each sixteenth of the lane: the phrase's that has one */
        out[q] = 255;
        for (j = q; j < AUTO_PTS; j += n)
            if (l->v[j] != 255)
                out[q] = l->v[j];
        if (out[q] != 255)
            have = q;
    }
    rec_l[r] = rec_l[--rec_n];
    if (have < 0) {                                  /* (nothing came: no lane) */
        l->bars = 0;
        return;
    }
    for (q = 1; q <= n; q++)                         /* where nothing was recorded, the value before stays */
        if (out[(have + q) % n] == 255)
            out[(have + q) % n] = out[(have + q - 1) % n];
    for (i = 0; i < AUTO_PTS; i++) {                 /* over AUTO_PTS, between the sixteenths in a line */
        uint32_t x = (uint32_t)i * (uint32_t)n, a = x / AUTO_PTS, f = x % AUTO_PTS;
        int v0 = out[a], v1 = out[(a + 1u) % (uint32_t)n];
        l->v[i] = (uint8_t)(v0 + ((v1 - v0) * (int)f + (v1 > v0 ? AUTO_PTS / 2 : -AUTO_PTS / 2)) / AUTO_PTS);
    }
    l->bars = (uint8_t)bars;
    proj_auto_dirty = 1;
    mark_dirty();
}
static void auto_finish_all(void)
{
    while (rec_n)
        auto_finish(rec_n - 1);
}
/* a new pattern: the automation of its track goes with the old one (a track's knobs, its mixer fader;
 * the acid line's). t -1: every track playing (as ne_generate(-1)); -2: the four drum tracks (MIDI C2) */
static void auto_new_pattern(int t)
{
    int i, x;
    for (i = 0; i < AUTO_LANES; i++) {
        auto_lane_t *l = &proj_auto[i];
        if (!l->bars || l->bank != proj.bank || !(l->k & (KT | KA)) || auto_recording(i))
            continue;
        x = l->k & KA ? NE_ACID : l->track;
        if (t == -1 ? !tmuted(x) : t == -2 ? x < NE_TRACKS : x == t) {
            l->bars = 0;
            proj_auto_dirty = 1;
        }
    }
}
/* GLO + REC: the selected track's automation goes (in the bank playing; the global knobs' stays) */
static void auto_rec_track(void)
{
    int i, had = 0;
    for (i = 0; i < AUTO_LANES; i++)
        had += proj_auto[i].bars && proj_auto[i].bank == proj.bank && (proj_auto[i].k & (KT | KA)) &&
               (proj_auto[i].k & KA ? NE_ACID : proj_auto[i].track) == ui.track;
    auto_finish_all();
    if (had) undo_mark(0);
    auto_new_pattern(ui.track);
    mark_dirty();
    say(had ? "AUTOMATION" : "NO AUTOMATION", had ? "TRACK ERASED" : "ON THIS TRACK");
}
static void auto_erase_bank(int b)
{
    int i;
    auto_finish_all();
    for (i = 0; i < AUTO_LANES; i++)
        if (proj_auto[i].bars && proj_auto[i].bank == b) {
            proj_auto[i].bars = 0;
            proj_auto_dirty = 1;
        }
}

/* a knob is turned by hand: REC held, its lane goes (1: the turn is done); recording, it is recorded from
 * now; else its lane, if it has one, waits for the hand */
static int auto_touch(int k, int t)
{
    int l, i;
    if (k == K_NONE || !auto_ok(k)) {
        if (!rec_held())
            return 0;
        ui.btn_used |= 1u << B_REC;
        say("NO AUTOMATION", "ON THIS KNOB");
        return 1;
    }
    l = auto_find(k, t);
    if (rec_held()) {
        ui.btn_used |= 1u << B_REC;
        if (l >= 0 && !auto_recording(l)) {
            undo_mark(0);
            proj_auto[l].bars = 0;
            proj_auto_dirty = 1;
            mark_dirty();
        }
        say("AUTOMATION", l >= 0 ? "ERASED" : "NONE HERE");
        return 1;
    }
    if (l >= 0)
        auto_hold[l] = plat_ms() + AUTO_HOLD;
    if (!ui.rec || !playing() || (l >= 0 && auto_recording(l)))
        return 0;
    if (rec_n >= REC_MAX) {
        say("RECORDING", "8 AT MOST");
        return 0;
    }
    if (l < 0)
        for (i = 0; i < AUTO_LANES && l < 0; i++)
            if (!proj_auto[i].bars)
                l = i;
    if (l < 0) {
        say("AUTOMATION", "FULL");
        return 0;
    }
    if (!rec_n) undo_record(0, -1, 1, 0);
    proj_auto[l].k = (uint8_t)k;
    proj_auto[l].track = (uint8_t)(k & KT ? t : 0);
    proj_auto[l].bank = proj.bank;
    proj_auto[l].bars = 8;
    memset(proj_auto[l].v, 255, AUTO_PTS);
    rec_l[rec_n].lane = (int8_t)l;
    rec_l[rec_n].start = rec_l[rec_n].last = auto_step();
    rec_n++;
    return 0;
}

static void rec_press(void)
{
    if (ui.rec) {
        int n = rec_n;
        char b[16];
        auto_finish_all();
        ui.rec = 0;
        itoa_u((uint32_t)n, b);
        s_cpy(b + s_len(b), n == 1 ? " KNOB" : " KNOBS");
        say("RECORDED", b);
    } else {
        ui.rec = 1;
        say("REC", playing() ? "TURN A KNOB" : "PLAY, TURN A KNOB");
    }
}

/* each frame: what is recorded written down, the lanes played (the hand's knobs left alone) */
static void auto_frame(void)
{
    uint32_t now = plat_ms(), st;
    int i, r;
    float pos;
    if (!playing()) {
        auto_finish_all();
        return;
    }
    if (!ne_seq()->steps)
        return;
    st = auto_step();
    for (r = 0; r < rec_n; r++) {
        auto_lane_t *l = &proj_auto[rec_l[r].lane];
        if (st - rec_l[r].start >= (uint32_t)AUTO_PTS)  /* (over 8 bars: the oldest written over) */
            rec_l[r].start = st - (AUTO_PTS - 1u);
        rec_l[r].last = st;
        l->v[st % AUTO_PTS] = (uint8_t)(auto_value(l) - kinfo(l->k)->lo);
    }
    pos = (float)(st % AUTO_PTS) + auto_frac();
    for (i = 0; i < AUTO_LANES; i++) {
        const auto_lane_t *l = &proj_auto[i];
        const ne_param_t *p;
        float x, f;
        int a, v;
        if (!l->bars || l->bank != proj.bank || (int32_t)(now - auto_hold[i]) < 0 || auto_recording(i))
            continue;
        x = pos;
        while (x >= (float)(l->bars * NE_BAR))
            x -= (float)(l->bars * NE_BAR);
        x *= (float)AUTO_PTS / (float)(l->bars * NE_BAR);
        a = (int)x;
        f = x - (float)a;
        p = kinfo(l->k);
        v = p->lo + (int)((float)l->v[a % AUTO_PTS] + ((float)l->v[(a + 1) % AUTO_PTS] - (float)l->v[a % AUTO_PTS]) * f + 0.5f);
        v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
        if (v != auto_value(l))
            auto_set(l, v);
    }
}
static int auto_has(int k)                           /* the knob on screen has a lane here */
{
    return k != K_NONE && auto_find(k, dtrack()) >= 0;
}

/* the battery's % from its voltage: a Li-ion cell's curve under a light load. An estimate: the divider's scale is
 * not measured, though the stock firmware's bars (531 / 561 / 591 on the ADC: 3.43 / 3.62 / 3.81 V) fall on
 * about 2, 13 and 51 % of it */
static int batt_pct(uint32_t mv)
{
    static const uint16_t MV[] = {3400, 3500, 3600, 3650, 3700, 3750, 3800, 3900, 4000, 4100, 4180};
    static const uint8_t PCT[] = {0, 4, 11, 18, 28, 40, 50, 64, 78, 90, 100};
    int i;
    if (mv <= MV[0])
        return 0;
    for (i = 1; i < (int)sizeof PCT; i++)
        if (mv < MV[i])
            return PCT[i - 1] + (int)((mv - MV[i - 1]) * (uint32_t)(PCT[i] - PCT[i - 1]) / (MV[i] - MV[i - 1]));
    return 100;
}

/* what the header shows (ui.batt): the % goes down as it falls, up only by 5 or more (charged, not a sag
 * that came back as a load let go), USB power its own */
static void batt_shown(void)
{
    uint32_t mv = plat_batt_mv();
    int p;
    if (plat_usb_power())
        ui.batt = 101;
    else if (!mv)
        ui.batt = -1;
    else if (p = batt_pct(mv), ui.batt < 0 || ui.batt > 100 || p < ui.batt || p >= ui.batt + 5)
        ui.batt = (int8_t)p;
}

/* the battery at x (its left), 26 x 17 with the tip: filled as far as it is, its % on it; red at 10 % and
 * below; a bolt on USB power */
static int32_t draw_batt(int32_t x)
{
    char t[4];
    int p = ui.batt;
    uint16_t c = p <= 10 ? K_RED : K_DIM;
    cv_rect(x, 5, 24, 1, c);
    cv_rect(x, 21, 24, 1, c);
    cv_rect(x, 5, 1, 17, c);
    cv_rect(x + 23, 5, 1, 17, c);
    cv_rect(x + 24, 10, 2, 7, c);
    if (p > 100) {                                  /* a bolt */
        static const int8_t B[13][2] = {{14, 2}, {13, 2}, {12, 2}, {11, 2}, {10, 2}, {9, 6}, {12, 2}, {11, 2},
                                        {10, 2}, {9, 2}, {8, 2}, {7, 2}, {7, 1}};
        int i;
        for (i = 0; i < 13; i++)
            cv_rect(x + B[i][0], 7 + i, B[i][1], 1, K_ACC);
        return x;
    }
    if (p > 0)
        cv_rect(x + 2, 7, (p * 20 + 99) / 100, 13, p <= 10 ? mix(K_BG, K_RED, 7) : K_LINE);
    itoa_u((uint32_t)p, t);
    text_c(x + 12, 7, &FONT_XS, t, K_TEXT);
    return x;
}

static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(2166136261u, ui.view | (uint32_t)ui.live << 8 | (uint32_t)playing() << 9 |
                                            (uint32_t)ui.dirty << 10 | (uint32_t)msg << 11 | (uint32_t)ne_ext << 12),
                      (uint32_t)ne_bpm_x10);
    batt_shown();
    h = hash(h, (uint32_t)(ui.batt + 1));
    h = hash(h, ui.bank_select | (uint32_t)sel_held() << 1);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (ui.view == V_SETUP)
        h = hash(h, plat_cpu_pct() | plat_xruns() << 8);
    if (ui.rec)
        h = hash(h, 1u + rec_n + ((plat_ms() / 160u) & 1u) * 16u);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if ((ui.bank_select || sel_held()) && !ui.list) {
        cv_text(6, 6, &FONT_S, sel_held() ? "COPY 1-8" : "BANK 1-8", K_ACC);
        cv_text(116, 6, &FONT_S, "16 CHANCE", K_TEXT);
    } else if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_ACC);
    } else {
        int32_t x = 232, w;
        cv_text(10, 6, &FONT_B, VIEW_NAME[ui.view], K_TEXT);
        {   /* the tempo, "112" or "121.5 EXT" (MIDI clock) */
            uint32_t b = ne_bpm_x10;
            itoa_u(b / 10u, t);
            if (ne_ext) {
                w = s_len(t);
                t[w++] = '.';
                t[w++] = (char)('0' + b % 10u);
                s_cpy(t + w, " EXT");
            }
            x -= text_w(&FONT_S, t);
            cv_text(x, 6, &FONT_S, t, K_TEXT);
            x -= text_w(&FONT_XS, "BPM") + 4;
            cv_text(x, 8, &FONT_XS, "BPM", K_DIM);
        }
        if (ui.batt >= 0 && ui.view != V_SETUP)       /* (Setup: the audio load in its place) */
            x = draw_batt(x - 34);
        if (ui.view == V_SETUP) {                     /* the audio load, for a check on the hardware */
            itoa_u(plat_cpu_pct(), t);
            w = s_len(t);
            t[w++] = '%';
            t[w] = 0;
            if (plat_xruns()) {
                t[w++] = ' ';
                t[w++] = 'X';
                itoa_u(plat_xruns(), t + w);
            }
            x -= text_w(&FONT_S, t) + 10;
            cv_text(x, 6, &FONT_S, t, K_DIM);
        }
        if (ui.rec) {                                 /* REC: red, the dot blinking while it records */
            x -= text_w(&FONT_S, "REC") + 22;
            if (!rec_n || (plat_ms() / 160u) & 1u)
                cv_round(x, 9, 9, 9, 4, K_RED);
            cv_text(x + 13, 6, &FONT_S, "REC", K_RED);
        }
        x = 18 + text_w(&FONT_B, VIEW_NAME[ui.view]);
        {   /* the button's pages, when it has more than one: a dot each, this one filled */
            int first = ui.view, n = 0, v;
            while (first > 0 && VIEW_BTN[first - 1] == VIEW_BTN[ui.view])
                first--;
            for (v = first; v < NVIEWS && VIEW_BTN[v] == VIEW_BTN[ui.view]; v++)
                n += view_here(v);
            if (n > 1) {
                for (v = 0; v < n; v++)
                    cv_round(x - 4 + v * 9, 12, 6, 6, 3, first + v == ui.view ? K_TEXT : K_LINE);
                x += n * 9 + 2;
            }
        }
        if (ui.live) {
            w = text_w(&FONT_XS, "LIVE") + 10;
            cv_round(x, 6, w, 16, 6, K_ACC);
            cv_text(x + 5, 7, &FONT_XS, "LIVE", K_BG);
            x += w + 4;
        }
        if (ui.dirty)
            cv_round(x, 12, 5, 5, 2, K_DIM);
    }
    cv_blit(0, 0);
}

/* a step's cell in its row: sixteenths 9 wide, a pixel between quarters; triplets 12 wide, where they
 * fall in time (three over the four sixteenths of a quarter) */
static int32_t cell_x(int bar, int s)
{
    return bar == NE_BAR ? GRID_X + s * CELL_W + s / 4 : GRID_X + s / 3 * (4 * CELL_W + 1) + s % 3 * 4 * CELL_W / 3;
}

/* one track: its name, its bar of 16 steps (12 in triplets; the bar shown, for the selected track; the
 * bar it plays, for the others), the playhead */
static void draw_track(int t, int32_t y)
{
    const nseq_track *tr = trk(t);
    const nacid *ac = ne_acid();
    int acid = t == NE_ACID, len = tlen(t), nb = tbar(t), cw = nb == NE_BAR ? 9 : 12;
    int sel = t == ui.track, muted = tmuted(t), bar = sel ? shown_bar() : play_bar(t), ps = play_step(t), s;
    uint16_t col = muted ? K_DIM : TRACK_COL[t];
    char name[16];
    if (bar < 0)
        bar = 0;
    if (sel)
        cv_round(2, y, 236, ROW_H - 1, 5, K_SEL);
    {   /* the meter: the sound's level, a bar left of the name */
        int m = (int)(ui.meter[t] * 15.0f);
        cv_rect(6, y + 3, 3, 14, K_LINE);
        if (m > 0)
            cv_rect(6, y + 17 - (m > 14 ? 14 : m), 3, m > 14 ? 14 : m, col);
    }
    name[0] = (char)('1' + t);
    name[1] = ' ';
    s_cpy(name + 2, tname(t));
    cv_text(13, y, &FONT_XS, name, muted ? K_DIM : sel ? K_TEXT : mix(K_TEXT, K_DIM, 8));
    {   /* "2b", or the steps when the length is not whole bars, "3" after in triplets; R / D: it owns
         * the reverb / delay */
        char b[12];
        int32_t x;
        if (len % nb) {
            itoa_u((uint32_t)len, b);
            s_cpy(b + s_len(b), "st");
        } else {
            itoa_u((uint32_t)track_bars(t), b);
            s_cpy(b + s_len(b), "b");
        }
        if (nb != NE_BAR)
            s_cpy(b + s_len(b), " 3");
        x = cv_text(13, y + 10, &FONT_XS, b, K_DIM);
        if (tsend(t, 0))
            x = cv_text(x + 4, y + 10, &FONT_XS, "R", K_ACC);
        if (tsend(t, 1))
            x = cv_text(x + 4, y + 10, &FONT_XS, "D", K_ACC);
        if (tbypass(t))                              /* B: past Drift's effects */
            cv_text(x + 4, y + 10, &FONT_XS, "B", K_TEXT);
    }
    for (s = 0; s < nb; s++) {
        int st = bar * nb + s;
        int32_t x = cell_x(nb, s);
        int past = st >= len, on = step_on(t, st), here = st == ps, note;
        uint16_t c;
        /* Quarter-note tick stays visible on rests and sounding steps alike. */
        if (s % (nb / 4) == 0)
            cv_rect(x + cw / 2 - 1, y, 3, 1, K_TEXT);
        if (past) {
            cv_rect(x + cw / 2 - 1, y + 9, 3, 2, K_LINE);
            continue;
        }
        if (!on) {
            cv_round(x, y + 2, cw, 15, 3, here ? K_DIM : sel ? mix(K_LINE, K_DIM, 4) : mix(K_BG, K_LINE, 10));
            continue;
        }
        if (acid) {     /* a note: brighter with an accent; a slide reaches into the next step */
            uint8_t f = ac->step[st].flags;
            note = ac->step[st].note;
            c = here ? K_TEXT : mix(K_PANEL, col, f & NACID_ACCENT ? 16 : 9);
            cv_round(x, y + 2, cw, 15, 3, c);
            if (f & NACID_SLIDE && s < nb - 1)
                cv_rect(x + cw, y + 8, cell_x(nb, s + 1) - x - cw, 3, c);
        } else {
            int v = tr->step[st].vel;
            note = tr->step[st].note;
            c = here ? K_TEXT : mix(K_PANEL, col, 6 + v * 10 / 127);
            cv_round(x, y + 2, cw, 15, 3, c);
        }
        if (note)                                    /* a pitched one: a notch at its height */
            cv_rect(x + 2, y + 9 - (note < -18 ? -6 : note > 18 ? 6 : note / 3), cw - 4, 1, K_BG);
    }
}

/* the phrase: eight bars, the eighth red; in the eighth, the beats count 1 2 3 4 */
static void draw_phrase(int32_t y)
{
    int ps = phrase_step(), bar = ps < 0 ? -1 : ps / NE_BAR, b;
    int32_t x0 = PH_X, w = PH_W - 3, gap = 3;
    for (b = 0; b < 8; b++) {
        int32_t x = x0 + b * (w + gap);
        uint16_t base = b == 7 ? mix(K_BG, K_RED, 6) : K_LINE, fill = b == 7 ? K_RED : K_TEXT;
        cv_round(x, y, w, 10, 3, base);
        if (bar > b)
            cv_round(x, y, w, 10, 3, b == 7 ? K_RED : mix(K_LINE, K_TEXT, 7));
        else if (bar == b) {
            if (b == 7)
                cv_round(x, y, w, 10, 3, K_RED);    /* the last bar: red and steady */
            else
                cv_round(x, y, 3 + (w - 3) * (ps % NE_BAR + 1) / NE_BAR, 10, 3, fill);
        }
    }
    {   /* 1 2 3 4: the beat now, big; the ones before it small */
        int32_t x = x0 + 8 * PH_W + 3, i;
        if (bar == 7) {
            int beat = (ps % NE_BAR) / 4;
            for (i = 0; i < 4; i++) {
                char d[2] = {(char)('1' + i), 0};
                if (i == beat)
                    cv_text(x + i * 10, y - 7, &FONT_M, d, K_RED);
                else
                    cv_text(x + i * 10 + 1, y - 2, &FONT_XS, d, i < beat ? mix(K_RED, K_BG, 8) : K_LINE);
                if (i == beat)
                    break;
            }
        } else {
            cv_text(x, y - 2, &FONT_XS, "1234", K_LINE);
        }
    }
}

/* the song list: five places around the one chosen, a song ("song", or how many slots it takes), "empty";
 * how many are free; at the foot, the keys */
static void draw_list(void)
{
    uint32_t h = hash(hash(2166136261u, (uint32_t)ui.list << 20 | (uint32_t)ui.list_slot << 4 | ui.list_ask),
                      ui.used[0] ^ ui.used[1] ^ (uint32_t)ui.nvis << 8 ^ (uint32_t)(uint8_t)ui.copy_from << 16);
    int i, p = list_pos();
    char f[16];
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_text(10, 2, &FONT_S, ui.list == L_SAVE ? "SAVE SONG" : "LOAD SONG", K_ACC);
    itoa_u(ui.nfree, f);
    s_cpy(f + s_len(f), " free");
    cv_text(230 - text_w(&FONT_XS, f), 6, &FONT_XS, f, K_DIM);
    for (i = -2; i <= 2; i++) {
        int n, used;
        int32_t y = 22 + (i + 2) * 20;
        char b[8];
        if (p + i < 0 || p + i >= ui.nvis)
            continue;
        n = ui.vis[p + i];
        used = slot_used(n);
        slot_name(n, b);
        if (!i)
            cv_round(4, y - 2, 232, 20, 5, K_PANEL);
        cv_text(14, y, i ? &FONT_S : &FONT_B, b, i ? K_DIM : K_TEXT);
        if (n == ui.copy_from) {
            cv_text(64, y + 2, &FONT_XS, "copy from here", K_ACC);
        } else if (used && ui.parts[n]) {            /* a song: the slots it takes */
            char m[16] = "song, ";
            itoa_u(1u + ui.parts[n], m + 6);
            s_cpy(m + s_len(m), " slots");
            cv_text(64, y + 2, &FONT_XS, m, i ? K_DIM : K_ACC);
        } else {
            cv_text(64, y + 2, &FONT_XS, used ? "song" : "empty", used ? (i ? K_DIM : K_ACC) : K_LINE);
        }
        if (n == proj.preset)
            cv_round(222, y + 6, 6, 6, 3, K_DIM);
    }
    if (ui.copy_from >= 0)
        cv_text(10, 124, &FONT_XS, "SEL on a free slot: copy  other: cancel", K_DIM);
    else
        cv_text(10, 124, &FONT_XS, ui.list == L_SAVE ? "SAVE: save  SEL: copy  HOME: delete" :
                                                       "ARP: load  SEL: copy  HOME: delete", K_DIM);
    cv_blit(0, MAIN_Y);
}

/* Over the tracks, see-through: in the phrase's fourth and eighth bars the beat counts big, each one
 * fading out until the next comes (the eighth's in red); a knob turned shows its name and value big, and
 * fades away once it rests. Their strength now, 0..256 (0: not shown). */
#define BEAT_A 150u                    /* a beat's count as it comes */
#define POP_HOLD 400u                  /* the knob's box: full this long after the last change, */
#define POP_FADE 500u                  /* ...then gone over this */
static uint32_t beat_alpha(void)
{
    int ps = phrase_step(), bar = ps < 0 ? -1 : ps / NE_BAR, beat = ps < 0 ? -1 : ps / 4;
    uint32_t len, age;
    if (bar != 3 && bar != 7)
        beat = -1;
    if (beat != ui.beat) {
        ui.beat = (int16_t)beat;
        ui.beat_t = plat_ms();
    }
    if (beat < 0)
        return 0;
    len = 600000u / (ne_bpm_x10 ? ne_bpm_x10 : 1200u);
    age = plat_ms() - ui.beat_t;
    return age >= len ? 0u : BEAT_A * (len - age) / len;
}
static uint32_t pop_alpha(void)
{
    uint32_t age = plat_ms() - ui.pop_t;
    if (!ui.pop_t || age >= POP_HOLD + POP_FADE) {
        ui.pop_t = 0;
        return 0;
    }
    return age <= POP_HOLD ? 256u : 256u * (POP_HOLD + POP_FADE - age) / POP_FADE;
}
static void draw_overlay(uint32_t ba, uint32_t pa)
{
    if (ba) {
        char d[2] = {(char)('1' + ui.beat % 4), 0};
        int32_t w = text_w(&FONT_L, d) * 4;
        cv_text_a((240 - w) / 2, (MAIN_H - 32 * 4) / 2 - 4, &FONT_L, d, 4, ui.beat / 4 % 8 == 7 ? K_RED : K_TEXT, ba);
    }
    if (pa) {
        const char *v = ui.pop[1], *q;
        int lower = 0, s;
        int32_t vw, vh;
        const felucca_font_t *vf;
        for (q = v; *q; q++)
            lower |= *q >= 'a' && *q <= 'z';
        vf = lower ? &FONT_L : &FONT_M;              /* the value: the big face doubled, if it has it */
        s = lower ? 1 : 2;
        vw = text_w(vf, v) * s;
        if (vw > 224) {
            vf = &FONT_L;
            s = 1;
            vw = text_w(vf, v);
        }
        vh = (int32_t)vf->h * s;
        cv_rect_a(8, 10, 224, MAIN_H - 20, K_PANEL, pa * 7u / 8u);
        cv_rect_a(8, 10, 224, 1, K_LINE, pa);
        cv_rect_a(8, MAIN_H - 11, 224, 1, K_LINE, pa);
        cv_text_a(120 - text_w(&FONT_L, ui.pop[0]) / 2, 18, &FONT_L, ui.pop[0], 1, K_TEXT, pa);
        cv_text_a(120 - vw / 2, 18 + 32 + (MAIN_H - 20 - 8 - 32 - vh) / 2, vf, v, s, K_ACC, pa);
    }
}

static void draw_grid(void)
{
    uint32_t ba = beat_alpha(), pa = pop_alpha();
    uint32_t h = hash(2166136261u, ui.track | (uint32_t)shown_bar() << 4 | (uint32_t)playing() << 8 |
                                       (uint32_t)phrase_step() << 9);
    int t, s;
    h = hash(h, ba >> 3 | (uint32_t)ui.beat << 8 | (pa >> 3) << 20);
    if (pa)
        for (s = 0; s < 16; s++)
            h = hash(h, (uint32_t)(uint8_t)ui.pop[0][s] | (uint32_t)(uint8_t)ui.pop[1][s] << 8);
    {
        const nacid *ac = ne_acid();
        h = hash(h, (uint32_t)(uint16_t)ac->last | (uint32_t)ac->length << 16 | (uint32_t)proj.a[A_MUTE] << 24 |
                        (uint32_t)(ui.meter[NE_ACID] * 15.0f) << 25 | (uint32_t)(ac->bar != NE_BAR) << 31);
        h = hash(h, (uint32_t)proj.a[A_DRIFT]);
        for (s = 0; s < ac->length; s++)
            if (ac->step[s].flags)
                h = hash(h, (uint32_t)s | (uint32_t)ac->step[s].flags << 8 | (uint32_t)(uint8_t)ac->step[s].note << 16);
    }
    for (t = 0; t < NE_TRACKS; t++) {
        const nseq_track *tr = trk(t);
        h = hash(h, (uint32_t)tr->last | (uint32_t)tr->length << 16 | (uint32_t)tr->mute << 24 |
                        (uint32_t)(ne_fm()->slot[t].kind & 7) << 25);
        h = hash(h, (uint32_t)(ui.meter[t] * 15.0f) | (uint32_t)tsend(t, 0) << 8 | (uint32_t)tsend(t, 1) << 9 |
                        (uint32_t)tr->triplet << 10 | (uint32_t)tbypass(t) << 11);
        for (s = 0; s < tr->length; s++)
            if (tr->step[s].on)
                h = hash(h, (uint32_t)s | (uint32_t)tr->step[s].vel << 8 | (uint32_t)(uint8_t)tr->step[s].note << 16);
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (t = 0; t < UI_TRACKS; t++)
        draw_track(t, 2 + t * ROW_H);
    {   /* the bar numbers over the phrase, for the black keys: the one shown outlined */
        int b, bars = track_bars(ui.track), sb = shown_bar();
        for (b = 0; b < 8; b++) {
            char d[2] = {(char)('1' + b), 0};
            int32_t x = PH_X + b * PH_W;
            text_c(x + (PH_W - 3) / 2, 110, &FONT_XS, d, b >= bars ? K_LINE : b == sb ? K_TEXT : K_DIM);
            if (b == sb && ui.bar_lock)
                cv_rect(x + (PH_W - 3) / 2 - 5, 124, 10, 1, K_TEXT);
        }
    }
    draw_phrase(130);
    draw_overlay(ba, pa);
    cv_blit(0, MAIN_Y);
}

static void draw_main(void)
{
    if (ui.list)
        draw_list();
    else
        draw_grid();
}

static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | (uint32_t)ui.track << 16), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, view_knob((int)i) == K_NONE ? 0xFFFFu : (uint32_t)kval(view_knob((int)i)) | (uint32_t)view_knob((int)i) << 16 |
                    (uint32_t)auto_has(view_knob((int)i)) << 15);
    h = hash(h, (uint32_t)now_touch);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    cv_round(4, 2, 232, KNB_H - 6, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int k = view_knob((int)i), lo, hi, v;
        int32_t x = (int32_t)i * 60, cx = x + 30, bw;
        int hot = now_touch && ui.touched == (int)i;
        uint16_t bar = k != K_NONE && (k & (KT | KA)) ? TRACK_COL[k & KA ? NE_ACID : dtrack()] : K_TEXT;
        char t[16];
        if (i)
            cv_rect(x, 14, 1, KNB_H - 30, K_LINE);
        if (k == K_NONE)
            continue;
        lo = kinfo(k)->lo;
        hi = kinfo(k)->hi;
        v = kval(k);
        text_c(cx, 6, &FONT_XS, kname(k), C_WHITE);   /* white: read on the small screen */
        if (auto_has(k))                             /* a red dot: it has automation */
            cv_round(x + 51, 4, 5, 5, 2, K_RED);
        ktext(k, v, t);
        {   /* the value: big, unless it has lower case (the big face has none) or is long */
            const char *q = t;
            int lower = 0;
            for (; *q; q++)
                lower |= *q >= 'a' && *q <= 'z';
            if (lower || text_w(&FONT_M, t) > 56)
                text_c(cx, 26, &FONT_B, t, hot ? K_ACC : K_TEXT);
            else
                text_c(cx, 22, &FONT_M, t, hot ? K_ACC : K_TEXT);
        }
        bw = hi > lo ? (int32_t)(44 * (v - lo) / (hi - lo)) : 0;
        cv_round(cx - 22, 50, 44, 6, 3, K_LINE);
        if (bw)
            cv_round(cx - 22, 50, bw < 6 ? 6 : bw, 6, 3, hot ? K_ACC : bar);
    }
    cv_blit(0, KNB_Y);
}

/* the lights. SEQ: the white keys the bar's hits (bright), the playhead (dim walking; dark on a hit);
 * black 1 / 2 dim where the bar can go (the bar locked: one blinks); black 3 lit on the Master page;
 * black 5-8 the tracks: dim on, bright on a hit, dark muted. LIVE: the white keys light with their
 * sound. */
static void leds(void)
{
    uint32_t b = VIEW_BTN[ui.view] < NB ? 1u << VIEW_BTN[ui.view] : 0u, k = 0, d = 0, i;
    int t = ui.track, sb = shown_bar(), ps = play_step(t), blink = (plat_ms() / 160u) & 1u;
    if (playing())
        b |= 1u << B_PLAY;
    if (!ui.live)
        b |= 1u << B_SEQ;
    if (ui.chance || ui.bank_select || sel_held() || (ui.list && ui.copy_from >= 0 && blink))
        b |= 1u << B_SEL;                            /* (a song to copy: blinking) */
    if (ui.rec && (!rec_n || blink))                 /* REC: lit, blinking while it records */
        b |= 1u << B_REC;
    if ((sel_held() || ui.bank_select) && !ui.list) {                    /* SEL held: the banks (lit: playing, dim: one there) */
        for (i = 0; i < PROJ_BANKS; i++)
            if ((int)i == proj.bank)
                k |= 1u << WHITE_K[i];
            else if (proj_bank_has((int)i))
                d |= 1u << WHITE_K[i];
        d |= 1u << WHITE_K[BANK_CHANCE];
    } else if (ui.live) {
        for (i = 0; i < NWHITE; i++)
            if (ui.meter[i >> 2] > 0.15f)
                d |= 1u << WHITE_K[i];
    } else {
        for (i = 0; i < NWHITE && (int)i < tbar(t); i++) {   /* (triplets: keys 13-16 dark) */
            int s = sb * tbar(t) + (int)i;
            if (step_on(t, s))
                k |= 1u << WHITE_K[i];
            if (s == ps) {
                if (k >> WHITE_K[i] & 1u)
                    k &= ~(1u << WHITE_K[i]);        /* the playhead on a hit: dark a moment */
                else
                    d |= 1u << WHITE_K[i];
            }
        }
    }
    if (ui.list && blink)                            /* the list: its key blinks */
        b |= 1u << (ui.list == L_SAVE ? B_SAVE : B_ARP);
    if (sb > 0)                                      /* the bar keys: lit where they can go */
        d |= 1u << BLACK_K[BK_BARDN];
    if (sb + 1 < track_bars(t))
        d |= 1u << BLACK_K[BK_BARUP];
    if (ui.bar_lock && blink)
        k |= 1u << BLACK_K[sb + 1 < track_bars(t) ? BK_BARUP : BK_BARDN];
    if (undo_c)                                      /* undo / redo: dim while there is one */
        d |= 1u << BLACK_K[BK_UNDO];
    if (undo_c < undo_n)
        d |= 1u << BLACK_K[BK_REDO];
    for (i = 0; i < UI_TRACKS; i++) {                /* the mutes: dim = on, bright with a hit, dark muted */
        if (sc_chance() ? sc_was[i][0] : tmuted((int)i))   /* (a scene by chance: the original's) */
            continue;
        if (ui.meter[i] > 0.15f)
            k |= 1u << BLACK_K[BK_MUTE1 + i];
        else
            d |= 1u << BLACK_K[BK_MUTE1 + i];
    }
    k |= ui.keys;
    plat_leds(b, k, d);
}

/* AUTOSAVE: a while after a change, once stopped and quiet (a flash erase silences the audio a moment,
 * so never while it plays: a live set does not stop for it) */
#define AUTOSAVE_QUIET 4000u
static void autosave(void)
{
    uint32_t now = plat_ms();
    if (project_legacy || !ui.dirty || playing() || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET || ne_out_level > 0.0005f)
        return;
    if (project_save() == 0 && project_banks_save() == 0)
        ui.dirty = 0;
}

/* the CPU guard: over 85 % of the audio budget, new hits share fewer voices; back under 60 %, all */
static void cpu_guard(void)
{
    uint32_t c = plat_cpu_pct();
    uint8_t want = c > 85u ? 4u : c < 60u ? NFM_VOICES : ui.voices;
    if (want != ui.voices) {
        ui.voices = want;
        ne_voices(want);
    }
}

static void diagnostic_row(int row, const char *label, uint32_t value, const char *unit)
{
    char text[32];
    int w;
    itoa_u(value, text);
    w = s_len(text);
    while (*unit && w < 30) text[w++] = *unit++;
    text[w] = 0;
    cv_begin(240, 20, K_BG);
    cv_text(5, 1, &FONT_S, label, K_TEXT);
    cv_text(235 - text_w(&FONT_S, text), 1, &FONT_S, text, K_TEXT);
    cv_blit(0, 26 + row * 20);
}
/* t[w..]: a number (or none: v < 0), then a text; the new length */
static int diag_put(char *t, int w, int32_t v, const char *text)
{
    char n[12];
    int i;
    if (v >= 0) {
        itoa_u((uint32_t)v, n);
        for (i = 0; n[i] && w < 38; i++)
            t[w++] = n[i];
    }
    while (*text && w < 38)
        t[w++] = *text++;
    t[w] = 0;
    return w;
}
static void diagnostic_text(int row, const char *label, const char *text)
{
    cv_begin(240, 20, K_BG);
    cv_text(5, 1, &FONT_S, label, K_TEXT);
    cv_text(235 - text_w(&FONT_S, text), 1, &FONT_S, text, K_TEXT);
    cv_blit(0, 26 + row * 20);
}
static void draw_diagnostics(void)
{
    plat_audio_diag d;
    if (plat_ms() - diagnostics_at < 250u) return;
    diagnostics_at = plat_ms();
    plat_audio_diag_read(&d);
    cv_begin(240, 26, K_BG);
    cv_text(5, 3, &FONT_S, d.hardware ? "AUDIO LOAD" : "AUDIO LOAD (HOST)", K_TEXT);
    cv_blit(0, 0);
    diagnostic_row(0, "Render now", d.render_now, "%");
    diagnostic_row(1, "Render avg", d.render_avg, "%");
    diagnostic_row(2, "Render peak", d.render_peak, "%");
    {   /* the output's and the screen's peaks on one row; the render's split (% of the budget, since
         * the last read: the drum voices, the 303, the sequencer with the hits and the reverb and delay,
         * the master's chain) on the next */
        static const char *const part[] = {"D ", "  A ", "  F ", "  M "};
        char t[40];
        int w = 0, i;
        w = diag_put(t, w, (int32_t)d.irq_peak_us, " us  ");
        diag_put(t, w, (int32_t)(d.ui_peak_us / 1000u), " ms");
        diagnostic_text(3, "IRQ / screen", t);
        w = 0;
        for (i = 0; i < 4; i++) {
            w = diag_put(t, w, -1, part[i]);
            w = diag_put(t, w, (int32_t)d.split[i], "");
        }
        diagnostic_text(4, "Split %", t);
    }
    {
        char t[24];
        diag_put(t, diag_put(t, 0, (int32_t)d.ready, "/"), (int32_t)d.slots, " blocks");
        diagnostic_text(5, "PCM ready", t);
    }
    diagnostic_row(6, "Empty buffer", d.underruns, "");
    diagnostic_row(7, "Late output", d.late, "");
    diagnostic_row(8, "RAM free", d.ram_total - d.ram_used, d.hardware ? " B" : " (n/a)");
    diagnostic_row(9, "Pool free", d.pool_total - d.pool_used, d.hardware ? " B" : " (n/a)");
}

/* Five independent faders; reuse the existing strip canvas, never a framebuffer. */
static void draw_mixer(void)
{
    static const char *const control[UI_TRACKS] = {"SELECT", "KNOB1", "KNOB2", "KNOB3", "KNOB4"};
    int t;
    if (plat_ms() - mixer_at < 66u) return; /* 15 Hz, audio stays above display */
    mixer_at = plat_ms();
    cv_begin(240, 28, K_BG);
    cv_text(8, 5, &FONT_B, "MIXER", K_TEXT);
    cv_text(132, 7, &FONT_XS, "GLO + OCT-", K_DIM);
    cv_blit(0, 0);
    /* Draw in short horizontal strips to stay within the existing canvas. */
    for (int strip = 0; strip < 5; ++strip) {
        int sy = strip * 36;
        cv_begin(240, 36, K_BG);
        for (t = 0; t < UI_TRACKS; ++t) {
            int x = t * 48, level = t == NE_ACID ? proj.a[A_LEVEL] : proj.t[t][T_LEVEL];
            int fy = 24 + (100 - level) * 104 / 100;
            int bar = (int)(mixer_level[t] * 104.0f), peak = (int)(mixer_peak[t] * 104.0f);
            char text[12];
            uint16_t col = TRACK_COL[t];
            text[0] = (char)('1' + t); text[1] = 0;
            cv_text(x + 11, 1 - sy, &FONT_S, t == NE_ACID ? "303" : text, col);
            cv_rect(x + 17, 24 - sy, 2, 109, K_LINE);
            for (int tick = 0; tick <= 4; ++tick)
                cv_rect(x + 10, 26 + tick * 26 - sy, 5, 1, K_DIM);
            cv_rect(x + 29, 26 - sy, 6, 104, K_LINE);
            cv_rect(x + 29, 130 - bar - sy, 6, bar, col);
            cv_rect(x + 28, 130 - peak - sy, 8, 1, K_TEXT);
            cv_round(x + 8, fy - sy, 19, 6, 2, col);
            itoa_u((uint32_t)level, text);
            cv_text(x + 24 - text_w(&FONT_S, text) / 2, 139 - sy, &FONT_S, text, K_TEXT);
            cv_text(x + 24 - text_w(&FONT_XS, control[t]) / 2, 162 - sy, &FONT_XS, control[t], K_DIM);
        }
        cv_blit(0, 29 + sy);
    }
}

void ui_init(void)
{
    diagnostics_open = diagnostics_chord = mixer_open = 0;
    memset(mixer_peak, 0, sizeof mixer_peak);
    memset(mixer_level, 0, sizeof mixer_level);
    memset(mixer_hold, 0, sizeof mixer_hold);
    memset(&ui, 0, sizeof ui);
    undo_clear();
    auto_resume();
    ui.touched = -1;
    ui.held_step = -1;
    ui.beat = -1;
    ui.batt = -1;
    ui.voices = NFM_VOICES;
    if (project_legacy) {
        say("OLD BANKS", "USE 0.3.29");
        ui.msg_until = plat_ms() + 10000u;
    }
    lcd_fill(0, 0, 240, 240, K_BG);
}

void ui_frame(void)
{
    int t;
    input();
    for (t = 0; t < UI_TRACKS; t++) {               /* the meters: up at once, down slowly */
        float l = (t == NE_ACID ? ne_acid_level : ne_track_level[t]) * 2.0f;
        ui.meter[t] = l > ui.meter[t] ? (l > 1.0f ? 1.0f : l) : ui.meter[t] * 0.82f;
        if (ui.meter[t] < 0.02f)
            ui.meter[t] = 0.0f;
    }
    {
        float peaks[UI_TRACKS];
        ne_take_peaks(peaks);
        for (t = 0; t < UI_TRACKS; ++t) {
            float value = peaks[t] * 2.0f;
            if (value > 1.0f) value = 1.0f;
            mixer_level[t] = value > mixer_level[t] ? value : mixer_level[t] * 0.82f;
            if (mixer_level[t] >= mixer_peak[t]) {
                mixer_peak[t] = mixer_level[t]; mixer_hold[t] = plat_ms() + 600u;
            } else if ((int32_t)(plat_ms() - mixer_hold[t]) >= 0)
                mixer_peak[t] *= 0.92f;
        }
    }
    auto_frame();
    cpu_guard();
    autosave();
    if (mixer_open) draw_mixer();
    else if (diagnostics_open) draw_diagnostics();
    else { draw_header(); draw_main(); draw_knobs(); }
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }

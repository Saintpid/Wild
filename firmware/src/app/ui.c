/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too. (The frame of it, knobs, bands and autosave, is FiMba-1's and FoMni's ui.c.)
 *
 *   SEQ mode (SEQ lit)  the 16 white keys are the 16 steps of one bar of the selected track: lit = a hit,
 *                  the dim one walking = where it plays. A key sets its step; pressed on a hit, it clears
 *                  it when let go, unless a knob turned meanwhile: held, KNOB 1 is the hit's velocity,
 *                  KNOB 2 its note.
 *   LIVE mode      the white keys play the sounds: four keys a sound, each a step up the scale
 *   black 1 / 2    the bar shown (and edited): one down / up, and it stays; both: follow the playhead
 *   black 3        the Master page (hi cut, lo cut, resonance, glue); again: back
 *   black 4        the preset list to load: a slot (SELECT, OCT-/+, PRESETS, a white key), black 4 again
 *   black 5-9      mute track 1-5 (dim = on, flashing with its hits; dark = muted); unmuted, it is selected
 *   black 10 / 11  a new pattern / a new sound for the selected track
 *   PLAY start / stop    REC new patterns for every unmuted track    SEQ the mode
 *   OCT- / OCT+    the selected track          SELECT the track   ALGORITHM the page
 *   PRESETS        the knob of the Setup page's choice (Glue by default)
 *   SEL            by the Scene page's SEL mode: Chance, a scene by chance (which tracks play, how much of
 *                  each effect each sound gets), SEL again back; A/B, two scenes to work on and swap; Copy,
 *                  a copy to play with, SEL again back to the original (SEL lit: in the scene, B, the copy)
 *   HOME EDIT ENV FX LFO GLO   the pages (pressed again: the button's next page)
 *   SAVE           the preset list to save: a slot, SAVE again (it also saves the project by itself,
 *                  after a change, once stopped and quiet); held: reset
 *   Program Change n    loads preset n + 1
 *
 *   FX             the selected track's effects (reverb, delay, filter, drive); again: its modulation
 *                  effect (phaser, flanger, chorus, ring modulator, tremolo); again: the bus (returns)
 *
 * Track 5 is the acid line: its pages are its own (pattern, track, sound, tone, FX); a step key sets a note,
 * held, KNOB 1 is its note, KNOB 2 its accent, KNOB 3 its slide (into the next step). CC 74 / 71: its
 * cutoff / resonance. Its Chaos (the Tone page, KNOB 4): how far its new patterns leave the rules.
 *
 * The screen: the five tracks, one bar each (the bar shown), and the phrase: eight bars, the eighth red;
 * in it the beats count 1 2 3 4 to the next round. */

enum { V_PATTERN, V_TRACK, V_SOUND, V_TONE, V_TFX, V_TMOD, V_FX, V_DRIFT, V_MASTER, V_GLUE, V_GROOVE, V_SETUP, V_SCENE, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Pattern", "Track", "Sound", "Tone", "Track FX", "Mod", "FX bus", "Drift",
                                              "Master", "Glue", "Groove", "Setup", "Scene"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_EDIT, B_ENV, B_ENV, B_FX, B_FX, B_FX, B_LFO, B_GLO, B_GLO, B_GLO, B_GLO, B_GLO};
#define K_NONE 255                     /* a knob with nothing on it */
#define KT 0x80                        /* a track parameter (T_*), else global (G_*) */
#define KA 0x40                        /* an acid line parameter (A_*) */
#define UI_TRACKS (NE_TRACKS + 1)      /* the four drum tracks and the acid line */
static const uint8_t ACID_KNOB[V_TMOD + 1][4] = {    /* the track pages, for the acid line */
    {KA | A_BARS, KA | A_NOTES, KA | A_SLIDES, KA | A_EVOLVE},
    {KA | A_ACCENTS, KA | A_OCTAVES, KA | A_SCALE, KA | A_LEVEL},
    {KA | A_CUTOFF, KA | A_RESO, KA | A_ENVMOD, KA | A_DECAY},
    {KA | A_WAVE, KA | A_ACCENT, KA | A_TUNE, KA | A_CHAOS},
    {KA | A_REVERB, KA | A_DELAY, KA | A_FILTER, KA | A_DRIVE},
    {KA | A_MOD, KA | A_MODAMT, KA | A_MODRATE, KA | A_MODCHANCE},
};
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {KT | T_BARS, KT | T_HITLO, KT | T_HITHI, KT | T_EVOLVE},
    {KT | T_RANDLEN, KT | T_PITCH, KT | T_LEVEL, KT | T_MUTE},
    {KT | T_DEV, KT | T_STRESS, KT | T_SPIKE, KT | T_BLEED},
    {KT | T_KIND, KT | T_FLUID, KT | T_TUNE, K_NONE},
    {KT | T_REVERB, KT | T_DELAY, KT | T_FILTER, KT | T_DRIVE},
    {KT | T_MOD, KT | T_MODAMT, KT | T_MODRATE, KT | T_MODCHANCE},
    {G_REVERB, G_DELAY, G_FILTER, G_DRIVE},
    {G_CHAOS, G_MOTION, G_MUTADE, G_SPACE},
    {G_HICUT, G_LOCUT, G_RESO, G_GLUE},
    {G_GMIX, G_GEQ, G_GCOLOR, G_GWIDTH},
    {G_TEMPO, G_SWING, G_LOOSE, G_POLY},
    {G_LEVEL, G_MIDICH, G_PKNOB, G_USBAUDIO},
    {G_SELMODE, K_NONE, K_NONE, K_NONE},
};
#define SAVE_HOLD_MS 1500u             /* SAVE held this long: reset to the defaults? (a tap: save) */
#define RESET_ASK_MS 5000u             /* ...asked this long: SAVE again resets, anything else keeps */

#define NWHITE 16
#define NBLACK 11
static const uint8_t WHITE_K[NWHITE] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[NBLACK] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
enum { BK_BARDN, BK_BARUP, BK_UNDO, BK_REDO, BK_MUTE1, BK_MUTE5 = BK_MUTE1 + 4, BK_PATTERN, BK_SOUND };
enum { L_NONE, L_SAVE, L_LOAD };       /* the preset list open */
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
    int8_t held_step;                  /* a step key held (its step), -1 none */
    uint8_t held_was_on, held_edited;
    uint8_t held_vel;                  /* ...its velocity and note as edited (the engine's lag a block) */
    int8_t held_note;
    uint8_t held_flags;                /* ...an acid step's accent and slide */
    uint8_t voices;                    /* the CPU guard's voice limit now */
    uint8_t list, list_slot;           /* the preset list (L_*) and the slot on it */
    uint8_t scene;                     /* SEL: a scene by chance plays (what was before: sc_*) */
    uint32_t pop_t;                    /* the last knob change: its box over the tracks (0: none) */
    char pop[2][16];                   /* ...the parameter and its value */
    int16_t beat;                      /* the beat counted big over the tracks (phrase step / 4), -1 none */
    uint32_t beat_t;                   /* ...when it came */
    uint32_t used[(PLAT_PRESETS + 31) / 32];   /* the slots holding a preset (read as the list opens) */
    float meter[UI_TRACKS];
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
static void ktext(int k, int v, char *t)
{
    if (k & KT)
        ne_tparam_text(k & ~KT, v, t);
    else if (k & KA)
        ne_aparam_text(k & ~KA, v, t);
    else
        ne_param_text(k, v, t);
}
static int view_knob(int i)
{
    return ui.track == NE_ACID && ui.view <= V_TMOD ? ACID_KNOB[ui.view][i] : VIEW_KNOB[ui.view][i];
}

static void tpar_set(int t, int p, int v)
{
    const ne_param_t *i = ne_tparam_info(p);
    v = v < i->lo ? i->lo : v > i->hi ? i->hi : v;
    if (v == proj.t[t][p])
        return;
    proj.t[t][p] = (int16_t)v;
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
/* The whole project before each edit, UNDO_N deep: undo_h[0 .. undo_c - 1] are the states to undo to,
 * undo_h[undo_c + 1 .. undo_n - 1] the ones to redo. One turn of a knob (the same knob, changes less than
 * UNDO_JOIN apart) or one held step is one edit. */
#define UNDO_N 12
#define UNDO_JOIN 1500u
static project_t undo_h[UNDO_N];
static uint8_t undo_c, undo_n;
static uint32_t undo_key, undo_ms;
static void undo_take(project_t *p)
{
    *p = proj;
    ne_snapshot(&p->st);
    ne_acid_snapshot(&p->acid);
}
/* before an edit; key: what is edited (0: an edit of its own, never joined) */
static void undo_mark(uint32_t key)
{
    uint32_t now = plat_ms();
    int join = key && key == undo_key && now - undo_ms < UNDO_JOIN;
    undo_key = key;
    undo_ms = now;
    if (join)
        return;
    if (undo_c >= UNDO_N - 1) {                      /* full: the oldest goes */
        int i;
        for (i = 0; i + 1 < UNDO_N; i++)
            undo_h[i] = undo_h[i + 1];
        undo_c--;
    }
    undo_take(&undo_h[undo_c++]);
    undo_n = undo_c;
}
/* the project and the engine to another state: only what differs goes to the engine (the patterns and
 * sounds stop the sound a moment, the settings do not) */
static void state_go(const project_t *to)
{
    static project_t now;
    int i, t, st, ac;
    undo_take(&now);
    st = memcmp(&now.st, &to->st, sizeof to->st) != 0;
    ac = memcmp(&now.acid, &to->acid, sizeof to->acid) != 0;
    if (st)
        ne_restore(&to->st);
    if (ac)
        ne_acid_restore(&to->acid);
    for (i = 0; i < G_NPARAMS; i++)
        if (proj.g[i] != to->g[i])
            ne_set(i, proj.g[i] = to->g[i]);
    for (t = 0; t < NE_TRACKS; t++)
        for (i = 0; i < T_NPARAMS; i++)
            if (proj.t[t][i] != to->t[t][i]) {
                proj.t[t][i] = to->t[t][i];
                if (!st || i != T_BARS)              /* (the restored state holds its length) */
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
/* back (d -1) or forward (+1) */
static void undo_go(int d)
{
    if (d < 0 ? !undo_c : undo_c + 1 >= undo_n) {
        say("NOTHING TO", d < 0 ? "UNDO" : "REDO");
        return;
    }
    undo_take(&undo_h[undo_c]);                      /* the state now: where a redo comes back to */
    if (undo_n < undo_c + 1)
        undo_n = (uint8_t)(undo_c + 1);
    undo_c = (uint8_t)(undo_c + d);
    state_go(&undo_h[undo_c]);
    undo_key = 0;
    {
        char b[16];
        itoa_u(d < 0 ? undo_c : (uint32_t)(undo_n - 1 - undo_c), b);
        s_cpy(b + s_len(b), " LEFT");
        say(d < 0 ? "UNDO" : "REDO", b);
    }
}

/* a knob turned: its change, and its name and value big over the tracks a moment */
static void turn(int role, int k, int32_t e)
{
    const ne_param_t *p;
    if (!e || k == K_NONE)
        return;
    p = kinfo(k);
    undo_mark(1u << 24 | (uint32_t)k << 8 | ui.track);
    knob_set(k, kval(k) + accel(role, e, p->hi - p->lo));
    s_cpy(ui.pop[0], p->name);
    ktext(k, kval(k), ui.pop[1]);
    ui.pop_t = plat_ms() | 1u;
}

/* ------------------------------------------------------------ pattern --- */
static const nseq_track *trk(int t) { return &ne_seq()->track[t < NE_TRACKS ? t : 0]; }
static int playing(void) { return ne_seq()->playing; }
/* any track's length, last step, hits, mute: a drum track's or the acid line's */
static int tlen(int t) { return t == NE_ACID ? ne_acid()->length : trk(t)->length; }
static int tlast(int t) { return t == NE_ACID ? ne_acid()->last : trk(t)->last; }
static int tmuted(int t) { return t == NE_ACID ? proj.a[A_MUTE] : trk(t)->mute; }
static int tsend(int t, int dly)              /* the track sends to the reverb (0) / the delay (1) */
{
    int v = t == NE_ACID ? proj.a[dly ? A_DELAY : A_REVERB] : proj.t[t][dly ? T_DELAY : T_REVERB];
    return v > 0 && proj.g[dly ? G_DELAY : G_REVERB] > 0;
}
static const char *tname(int t) { return t == NE_ACID ? "Acid" : ne_kind_name(ne_fm()->slot[t].kind); }
static int track_bars(int t) { return (tlen(t) + NE_BAR - 1) / NE_BAR; }
static int play_step(int t) { return playing() && tlast(t) >= 0 ? tlast(t) : -1; }
static int play_bar(int t) { int s = play_step(t); return s < 0 ? -1 : s / NE_BAR; }
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
    if (ui.bar_lock && ui.view_bar >= track_bars(ui.track))
        ui.bar_lock = 0;
    {
        char a[8] = {'T', 'R', 'A', 'C', 'K', ' ', (char)('1' + ui.track), 0};
        say(a, tname(ui.track));
    }
}

static void white_down(int w)
{
    int t = ui.track, s;
    if (ui.list) {                                   /* the list: a slot of its page of 16 */
        ui.list_slot = (uint8_t)(ui.list_slot / NWHITE * NWHITE + w);
        return;
    }
    if (ui.live) {
        ne_hit(w >> 2, 112, LIVE_NOTE[w & 3]);
        return;
    }
    s = shown_bar() * NE_BAR + w;
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
    if (t == NE_ACID) {                              /* a note on the root, no accent, no slide */
        const nacid_note *st = &ne_acid()->step[s];
        ui.held_flags = ui.held_was_on ? st->flags : NACID_ON;
        ui.held_note = ui.held_was_on ? st->note : 0;
        if (!ui.held_was_on) {
            ne_astep(s, NACID_ON, 0);
            mark_dirty();
        }
        return;
    }
    ui.held_vel = ui.held_was_on ? trk(t)->step[s].vel : 100u;
    ui.held_note = ui.held_was_on ? trk(t)->step[s].note : 0;
    if (!ui.held_was_on) {
        ne_step(t, s, 1, 100, 0);
        mark_dirty();
    }
}

static void white_up(int w)
{
    int t = ui.track;
    if (ui.live || ui.list || ui.held_step < 0 || ui.held_step % NE_BAR != w)
        return;
    if (ui.held_was_on && !ui.held_edited) {
        if (t == NE_ACID)
            ne_astep(ui.held_step, 0, ui.held_note);
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
 * (resonant, a strong envelope), any wave. The acid line, perc, click and metal also land in an octave
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
        knob_set(KA | A_WAVE, rnd_in(0, NACID_WAVES - 1));
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

/* SEL: a SCENE by chance. Which tracks play (each by a chance of 0.6, at least two) and each sound's
 * effects: its sends, filter share, drive and modulation effect. What they were is kept, and SEL again
 * puts it all back, whatever was turned in the scene. In the scene the mute keys set the kept mutes (the
 * original's), so what plays after it is chosen during it. A preset loaded or a reset ends the scene there. */
#define SC_N 9
static const uint8_t SC_T[SC_N] = {T_MUTE, T_REVERB, T_DELAY, T_FILTER, T_DRIVE, T_MOD, T_MODAMT, T_MODRATE, T_MODCHANCE};
static const uint8_t SC_A[SC_N] = {A_MUTE, A_REVERB, A_DELAY, A_FILTER, A_DRIVE, A_MOD, A_MODAMT, A_MODRATE, A_MODCHANCE};
static int16_t sc_was[UI_TRACKS][SC_N];
/* in a scene by chance, the mute keys set the original's mutes: what plays when SEL brings it back */
static int sc_chance(void) { return ui.scene && proj.g[G_SELMODE] == NE_SEL_CHANCE; }
static void sc_set(int t, int i, int v)
{
    if (t == NE_ACID)
        knob_set(KA | SC_A[i], v);
    else
        tpar_set(t, SC_T[i], v);
}
static int rnd_or0(int zero_pct, int lo, int hi) { return rnd_in(0, 99) < zero_pct ? 0 : rnd_in(lo, hi); }
static void scene(void)
{
    int t, i, plays = 0;
    undo_mark(0);
    if (ui.scene) {
        for (t = 0; t < UI_TRACKS; t++)
            for (i = 0; i < SC_N; i++)
                sc_set(t, i, sc_was[t][i]);
        ui.scene = 0;
        say("SCENE", "BACK");
        return;
    }
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
    ui.scene = 1;
    say("SCENE", "BY CHANCE");
}

/* SEL's other modes (Scene page: SEL mode). A/B: the first SEL keeps a copy of everything (patterns,
 * sounds, settings) and goes on in it; each SEL after swaps the two, so both can be worked on and played
 * in turn (SEL lit: in B). Copy: SEL keeps the state as it is and goes on in a copy of it; SEL again goes
 * back to what was kept, and what was done in the copy is gone (SEL lit: in the copy). The Setup and
 * Scene pages' values are never swapped. Changing the mode, loading a preset or a reset forget the copy. */
static project_t sel_keep;                           /* A/B: the other scene; Copy: the original */
static uint8_t sel_have;
static void sel_forget(void)
{
    ui.scene = 0;
    sel_have = 0;
}
static void sel_go(const project_t *to)
{
    static project_t t;
    static const uint8_t KEEP[] = {G_LEVEL, G_MIDICH, G_PKNOB, G_USBAUDIO, G_SELMODE};
    uint32_t i;
    t = *to;
    for (i = 0; i < sizeof KEEP; i++)
        t.g[KEEP[i]] = proj.g[KEEP[i]];
    state_go(&t);
}
static void sel_press(void)
{
    static project_t now;
    int m = proj.g[G_SELMODE];
    ne_space_shift();                                /* (by Space's chance: at 100 always) */
    if (m == NE_SEL_CHANCE) {
        scene();
        return;
    }
    undo_mark(0);
    if (m == NE_SEL_AB) {
        undo_take(&now);
        if (sel_have)
            sel_go(&sel_keep);
        sel_keep = now;
        sel_have = 1;
        ui.scene ^= 1u;
        say("SCENE", ui.scene ? "B" : "A");
    } else if (!ui.scene) {
        undo_take(&sel_keep);
        ui.scene = 1;
        say("COPY", "CHANGE ANYTHING");
    } else {
        sel_go(&sel_keep);
        ui.scene = 0;
        say("BACK TO", "THE ORIGINAL");
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
    } else if (b == BK_PATTERN) {
        undo_mark(0);
        ne_generate(t);
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
/* SAVE or ARP opens the list on the last slot used; the same key again saves / loads the slot on it,
 * any other button closes it. A save erases a flash sector: the sound stops a moment. */
static int slot_used(int n) { return (int)(ui.used[n >> 5] >> (n & 31) & 1u); }
static void slot_name(int n, char *b)               /* "005" */
{
    b[0] = (char)('0' + (n + 1) / 100);
    b[1] = (char)('0' + (n + 1) / 10 % 10);
    b[2] = (char)('0' + (n + 1) % 10);
    b[3] = 0;
}
static void list_open(int which)
{
    int n;
    for (n = 0; n < PLAT_PRESETS; n++) {
        uint32_t m = 1u << (n & 31);
        ui.used[n >> 5] = plat_preset_used(n) ? ui.used[n >> 5] | m : ui.used[n >> 5] & ~m;
    }
    ui.list = (uint8_t)which;
    ui.list_slot = proj.preset < PLAT_PRESETS ? proj.preset : 0;
    ui.held_step = -1;
}
static void list_move(int d)
{
    int n = ui.list_slot + d;
    ui.list_slot = (uint8_t)(n < 0 ? 0 : n >= PLAT_PRESETS ? PLAT_PRESETS - 1 : n);
}
static int preset_load(int n)
{
    char b[8];
    slot_name(n, b);
    if (!plat_preset_used(n)) {
        say("PRESET", "EMPTY");
        return -1;
    }
    undo_mark(0);
    if (project_preset_load(n) != 0) {
        say("PRESET", "EMPTY");
        return -1;
    }
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
        if (project_preset_save(n) == 0) {
            mark_dirty();
            say("SAVED", b);
        } else {
            say("SAVE", "FAILED");
        }
    } else {
        preset_load(n);
    }
    ui.list = L_NONE;
}

static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static void reset_defaults(void)
{
    int16_t keep = proj.g[G_MIDICH];
    int t;
    undo_mark(0);
    project_defaults();
    proj.g[G_MIDICH] = keep;
    sel_forget();
    ne_play(0);
    project_apply();                                 /* the starting set's settings, then its sounds */
    for (t = 0; t < NE_TRACKS; t++) {
        ne_new_sound(t, proj.t[t][T_KIND]);          /* (the starting set: at their own pitch) */
        ne_generate(t);
    }
    mark_dirty();
    say("RESET", "TO DEFAULTS");
}
static void reset_ask(int on)
{
    ui.reset_ask = on ? plat_ms() + RESET_ASK_MS : 0u;
    if (on) {
        say("RESET?", "SAVE: YES");
        ui.msg_until = ui.reset_ask;
    } else {
        say("KEPT", 0);
    }
}

static void button(int b)
{
    int v;
    if (ui.list && b != B_PLAY) {                    /* the list: its arrows, SAVE; anything else closes it */
        if (b == B_OCTDN || b == B_OCTUP)
            list_move(b == B_OCTUP ? 1 : -1);
        else if ((b == B_SAVE && ui.list == L_SAVE) || (b == B_ARP && ui.list == L_LOAD))
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
    case B_REC:
        undo_mark(0);
        ne_generate(-1);
        mark_dirty();
        say("NEW PATTERNS", "ALL TRACKS");
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
            reset_defaults();
            break;
        }
        list_open(L_SAVE);
        break;
    case B_ARP: list_open(L_LOAD); break;
    case B_SEL: sel_press(); break;
    default:
        for (v = 0; v < NVIEWS; v++)                 /* a page button: its page, or its next one */
            if (VIEW_BTN[v] == b) {
                if (VIEW_BTN[ui.view] == b && ui.view + 1 < NVIEWS && VIEW_BTN[ui.view + 1] == b)
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
        for (t = 0; t < NE_TRACKS; t++)
            if (!proj.t[t][T_MUTE])
                new_sound(t);
        if (!proj.a[A_MUTE])
            new_sound(NE_ACID);
        mark_dirty();
    } else if (cc == 118u && v >= 64u) {
        ne_generate(-1);
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
            if (d1 >= 36u && d1 <= 48u)
                mark_dirty();
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
            if (i == B_SAVE)
                ui.save_t = plat_ms();
            if (i == B_PLAY)                         /* on the press: in time */
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY) {
            button((int)i);
        }
    }
    if ((btn >> B_SAVE & 1u) && !(ui.btn_used >> B_SAVE & 1u) && !ui.reset_ask &&
        plat_ms() - ui.save_t >= SAVE_HOLD_MS) {
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
        if (ui.list)
            list_move(e > 0 ? 1 : -1);
        else
            select_track(ui.track + (e > 0 ? 1 : -1));
    }
    if ((e = plat_enc(EN_ALGO)) != 0)                /* the pages */
        set_view((ui.view + (e > 0 ? 1 : NVIEWS - 1)) % NVIEWS);
    if ((e = plat_enc(EN_PRESET)) != 0) {
        int k = ne_pknob_target(proj.g[G_PKNOB]);   /* (NE_PK_TRACK is KT: the selected track's) */
        if (ui.list)
            list_move((int)e);
        else
            turn(EN_PRESET, k, e);
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            if (ui.held_step >= 0 && !ui.live && ui.track == NE_ACID)
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

static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(2166136261u, ui.view | (uint32_t)ui.live << 8 | (uint32_t)playing() << 9 |
                                            (uint32_t)ui.dirty << 10 | (uint32_t)msg << 11 | (uint32_t)ne_ext << 12),
                      (uint32_t)ne_bpm_x10);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (ui.view == V_SETUP)
        h = hash(h, plat_cpu_pct() | plat_xruns() << 8);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
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
        x = 18 + text_w(&FONT_B, VIEW_NAME[ui.view]);
        {   /* the button's pages, when it has more than one: a dot each, this one filled */
            int first = ui.view, n = 0, v;
            while (first > 0 && VIEW_BTN[first - 1] == VIEW_BTN[ui.view])
                first--;
            for (v = first; v < NVIEWS && VIEW_BTN[v] == VIEW_BTN[ui.view]; v++)
                n++;
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

/* one track: its name, its bar of 16 steps (the bar shown, for the selected track; the bar it plays,
 * for the others), the playhead */
static void draw_track(int t, int32_t y)
{
    const nseq_track *tr = trk(t);
    const nacid *ac = ne_acid();
    int acid = t == NE_ACID, len = tlen(t);
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
    {   /* "2b", or the steps when the length is not whole bars; R / D: it owns the reverb / delay */
        char b[12];
        int32_t x;
        if (len % NE_BAR) {
            itoa_u((uint32_t)len, b);
            s_cpy(b + s_len(b), "st");
        } else {
            itoa_u((uint32_t)track_bars(t), b);
            s_cpy(b + s_len(b), "b");
        }
        x = cv_text(13, y + 10, &FONT_XS, b, K_DIM);
        if (tsend(t, 0))
            x = cv_text(x + 4, y + 10, &FONT_XS, "R", K_ACC);
        if (tsend(t, 1))
            cv_text(x + 4, y + 10, &FONT_XS, "D", K_ACC);
    }
    for (s = 0; s < NE_BAR; s++) {
        int st = bar * NE_BAR + s;
        int32_t x = GRID_X + s * CELL_W + (s / 4) * 1;
        int past = st >= len, on = step_on(t, st), here = st == ps, note;
        uint16_t c;
        if (past) {
            cv_rect(x + 3, y + 9, 3, 2, K_LINE);
            continue;
        }
        if (!on) {
            cv_round(x, y + 2, 9, 15, 3, here ? K_DIM : sel ? mix(K_LINE, K_DIM, 4) : mix(K_BG, K_LINE, 10));
            continue;
        }
        if (acid) {     /* a note: brighter with an accent; a slide reaches into the next step */
            uint8_t f = ac->step[st].flags;
            note = ac->step[st].note;
            c = here ? K_TEXT : mix(K_PANEL, col, f & NACID_ACCENT ? 16 : 9);
            cv_round(x, y + 2, 9, 15, 3, c);
            if (f & NACID_SLIDE && s < NE_BAR - 1)
                cv_rect(x + 9, y + 8, 2 + (s % 4 == 3), 3, c);
        } else {
            int v = tr->step[st].vel;
            note = tr->step[st].note;
            c = here ? K_TEXT : mix(K_PANEL, col, 6 + v * 10 / 127);
            cv_round(x, y + 2, 9, 15, 3, c);
        }
        if (note)                                    /* a pitched one: a notch at its height */
            cv_rect(x + 2, y + 9 - (note < -18 ? -6 : note > 18 ? 6 : note / 3), 5, 1, K_BG);
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

/* the preset list: five slots around the one chosen, "used" or "empty"; at the foot, how to go on */
static void draw_list(void)
{
    uint32_t h = hash(hash(2166136261u, (uint32_t)ui.list << 20 | (uint32_t)ui.list_slot << 4), ui.used[0] ^ ui.used[1]);
    int i;
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_text(10, 2, &FONT_S, ui.list == L_SAVE ? "SAVE TO PRESET" : "LOAD PRESET", K_ACC);
    for (i = -2; i <= 2; i++) {
        int n = ui.list_slot + i, used;
        int32_t y = 24 + (i + 2) * 21;
        char b[8];
        if (n < 0 || n >= PLAT_PRESETS)
            continue;
        used = slot_used(n);
        slot_name(n, b);
        if (!i)
            cv_round(4, y - 2, 232, 21, 5, K_PANEL);
        cv_text(14, y, i ? &FONT_S : &FONT_B, b, i ? K_DIM : K_TEXT);
        cv_text(64, y + 2, &FONT_XS, used ? "used" : "empty", used ? (i ? K_DIM : K_ACC) : K_LINE);
        if (n == proj.preset)
            cv_round(222, y + 6, 6, 6, 3, K_DIM);
    }
    cv_text(10, 128, &FONT_XS, ui.list == L_SAVE ? "SAVE again: save   other: cancel" : "ARP again: load   other: cancel",
            K_DIM);
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
                        (uint32_t)(ui.meter[NE_ACID] * 15.0f) << 25);
        for (s = 0; s < ac->length; s++)
            if (ac->step[s].flags)
                h = hash(h, (uint32_t)s | (uint32_t)ac->step[s].flags << 8 | (uint32_t)(uint8_t)ac->step[s].note << 16);
    }
    for (t = 0; t < NE_TRACKS; t++) {
        const nseq_track *tr = trk(t);
        h = hash(h, (uint32_t)tr->last | (uint32_t)tr->length << 16 | (uint32_t)tr->mute << 24 |
                        (uint32_t)(ne_fm()->slot[t].kind & 7) << 25);
        h = hash(h, (uint32_t)(ui.meter[t] * 15.0f) | (uint32_t)tsend(t, 0) << 8 | (uint32_t)tsend(t, 1) << 9);
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
        h = hash(h, view_knob((int)i) == K_NONE ? 0xFFFFu : (uint32_t)kval(view_knob((int)i)) | (uint32_t)view_knob((int)i) << 16);
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
        text_c(cx, 6, &FONT_XS, kinfo(k)->name, C_WHITE);   /* white: read on the small screen */
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
    if (ui.scene)
        b |= 1u << B_SEL;
    if (ui.live) {
        for (i = 0; i < NWHITE; i++)
            if (ui.meter[i >> 2] > 0.15f)
                d |= 1u << WHITE_K[i];
    } else {
        for (i = 0; i < NWHITE; i++) {
            int s = sb * NE_BAR + (int)i;
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
    if (undo_c + 1 < undo_n)
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
    if (!ui.dirty || playing() || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET || ne_out_level > 0.0005f)
        return;
    if (project_save() == 0)
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

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.held_step = -1;
    ui.beat = -1;
    ui.voices = NFM_VOICES;
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
    cpu_guard();
    autosave();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }

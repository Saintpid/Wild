/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se project: one flash object (OBJ_PROJ): the settings, the four patterns and the four sounds, the
 * acid line's pattern and settings.
 * A project of another kind, or none, gives the defaults (the engine's starting set). Part of the unity
 * build, after app.h. */
project_t proj;
static uint8_t proj_has_state;           /* the project carries patterns and sounds (not just defaults) */
static uint8_t proj_has_acid;            /* ...and an acid pattern (not from before format 3) */
project_t proj_bank[PROJ_BANKS];
uint8_t proj_bank_dirty;
static uint8_t project_legacy, project_legacy_cleanup;
auto_lane_t proj_auto[AUTO_LANES] OM_POOL;
uint8_t proj_auto_dirty;
static project_t proj_tmp;               /* an older project's layout, as it moves into this one */

void project_defaults(void)
{
    int i, t;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < G_NPARAMS; i++)
        proj.g[i] = ne_param_info(i)->def;
    for (t = 0; t < NE_TRACKS; t++)
        for (i = 0; i < T_NPARAMS; i++)
            proj.t[t][i] = (int16_t)ne_tparam_def(t, i);
    for (i = 0; i < A_NPARAMS; i++)
        proj.a[i] = ne_aparam_info(i)->def;
    proj_has_state = 0;
    proj_has_acid = 0;
}

/* An older format moves into this one: its values in place, the ones it lacks at their defaults */
static void project_upgrade(void)
{
    project_t *old = &proj_tmp;
    uint32_t f = proj.format;
    int ng = PROJ_G_OF(f), nt = PROJ_T_OF(f), na = PROJ_A_OF(f), i, t;
    const uint8_t *p;
    proj_has_acid = f >= 3u;
    if (f == PROJ_FORMAT)
        return;
    *old = proj;
    project_defaults();
    proj_has_acid = f >= 3u;
    p = (const uint8_t *)old + 8;                   /* the old layout, field by field */
    memcpy(proj.g, p, 2u * (uint32_t)ng);
    p += 2 * ng;
    for (t = 0; t < NE_TRACKS; t++, p += 2 * nt)
        memcpy(proj.t[t], p, 2u * (uint32_t)nt);
    memcpy(&proj.st, p, sizeof proj.st);
    p += sizeof proj.st;
    if (f >= 2u) {
        proj.preset = p[0];
        proj.bank = p[1];
        p += 32;
    }
    if (f >= 3u) {
        memcpy(&proj.acid, p, sizeof proj.acid);
        p += sizeof proj.acid;
        memcpy(proj.a, p, 2u * (uint32_t)na);
    }
    for (i = ng; i < G_NPARAMS; i++)
        proj.g[i] = ne_param_info(i)->def;
    for (t = 0; t < NE_TRACKS; t++)
        for (i = nt; i < T_NPARAMS; i++)
            proj.t[t][i] = (int16_t)ne_tparam_def(t, i);
    for (i = na; i < A_NPARAMS; i++)
        proj.a[i] = ne_aparam_info(i)->def;
    if (f < 4u) {                                    /* the effects' global values mean something else now */
        static const uint8_t FXG[4] = {G_REVERB, G_DELAY, G_FILTER, G_DRIVE};
        for (i = 0; i < 4; i++)
            proj.g[FXG[i]] = ne_param_info(FXG[i])->def;
    }
}

/* the bytes a project of format f takes */
static uint32_t project_size(uint32_t f)
{
    uint32_t n = 8u + 2u * (uint32_t)PROJ_G_OF(f) + 2u * NE_TRACKS * (uint32_t)PROJ_T_OF(f) + sizeof proj.st;
    if (f >= 2u)
        n += 32u;
    if (f >= 3u)
        n += sizeof proj.acid + 2u * (uint32_t)PROJ_A_OF(f);
    return n;
}

/* each value out of its range takes its default; the rest are kept */
static int project_valid(int bytes)
{
    int i, t;
    if (proj.magic != PROJ_MAGIC || proj.format < 1u || proj.format > PROJ_FORMAT ||
        (uint32_t)bytes < project_size(proj.format))
        return 0;
    project_upgrade();
    for (i = 0; i < G_NPARAMS; i++) {
        const ne_param_t *p = ne_param_info(i);
        if (proj.g[i] < p->lo || proj.g[i] > p->hi)
            proj.g[i] = p->def;
    }
    for (t = 0; t < NE_TRACKS; t++)
        for (i = 0; i < T_NPARAMS; i++) {
            const ne_param_t *p = ne_tparam_info(i);
            if (proj.t[t][i] < p->lo || proj.t[t][i] > p->hi)
                proj.t[t][i] = (int16_t)ne_tparam_def(t, i);
        }
    for (i = 0; i < A_NPARAMS; i++) {
        const ne_param_t *p = ne_aparam_info(i);
        if (proj.a[i] < p->lo || proj.a[i] > p->hi)
            proj.a[i] = p->def;
    }
    if (proj.preset >= PLAT_PRESETS)
        proj.preset = 0;
    if (proj.bank >= PROJ_BANKS)
        proj.bank = 0;
    return 1;
}

/* the bank or two loaded at proj_bank[first] (n bytes: more than a project, two of half that each; a
 * newer, larger project_t moves the second along), each read as a project (proj the workspace) */
static void banks_take(int first, int n)
{
    const int sz = (int)sizeof(project_t);
    uint8_t *b = (uint8_t *)&proj_bank[first];
    int two = n > sz, rec = two ? n / 2 : n, i;
    if (rec > sz || rec < 8) {                       /* (from a later firmware, or none) */
        memset(b, 0, (uint32_t)(first + 1 < PROJ_BANKS ? 2 : 1) * (uint32_t)sz);
        return;
    }
    if (two && rec < sz) {                           /* (back to front: the two overlap) */
        for (i = rec; i-- > 0;)
            b[sz + i] = b[rec + i];
        memset(b + sz + rec, 0, (uint32_t)(sz - rec));
    }
    if (rec < sz)
        memset(b + rec, 0, (uint32_t)(sz - rec));
    for (i = first; i < first + 1 + two && i < PROJ_BANKS; i++) {
        proj = proj_bank[i];
        if (project_valid(rec)) {
            proj.bank = (uint8_t)i;
            proj_bank[i] = proj;
        } else {
            memset(&proj_bank[i], 0, sizeof proj_bank[i]);
        }
    }
}

/* the banks from flash (proj the workspace: before the project itself loads) */
static void project_banks_load(void)
{
    int s;
    project_legacy = project_legacy_cleanup = 0;
    /* Probe old banks 9-12 before reusing the first pair as the normal load buffer. */
    for (s = PROJ_BANKS / 2; s < PLAT_BANK_SECTORS - 1; s++) {
        memset(proj_bank, 0, 2u * sizeof(project_t));
        if (plat_bank_load(s, proj_bank, 2u * sizeof(project_t)) > 0 &&
            (proj_bank[0].magic == PROJ_MAGIC || proj_bank[1].magic == PROJ_MAGIC))
            project_legacy = 1;
    }
    memset(proj_bank, 0, sizeof proj_bank);
    proj_bank_dirty = 0;
    for (s = 0; 2 * s < PROJ_BANKS; s++)
        banks_take(2 * s, plat_bank_load(s, &proj_bank[2 * s], 2u * sizeof(project_t)));
}

/* the automation: a lane that is not one this firmware knows is dropped */
static void auto_check(void)
{
    int i;
    for (i = 0; i < AUTO_LANES; i++) {
        auto_lane_t *l = &proj_auto[i];
        int k = l->k, n = k & 0x80 ? T_NPARAMS : k & 0x40 ? A_NPARAMS : G_NPARAMS;
        if ((l->bars != 1 && l->bars != 2 && l->bars != 4 && l->bars != 8) || l->bank >= PROJ_BANKS ||
            l->track >= NE_TRACKS || (k & 0x3F) >= n || (k & 0xC0) == 0xC0)
            memset(l, 0, sizeof *l);
    }
}
static void project_auto_load(void)
{
    proj_auto_dirty = 0;
    if (plat_bank_load(PLAT_BANK_SECTORS - 1, proj_auto, sizeof proj_auto) != (int)sizeof proj_auto)
        memset(proj_auto, 0, sizeof proj_auto);
    else
        auto_check();
}

int proj_bank_has(int n)
{
    return n >= 0 && n < PROJ_BANKS && (n == proj.bank || proj_bank[n].magic == PROJ_MAGIC);
}

void project_bank_keep(int n)
{
    project_t *b = &proj_bank[n];
    *b = proj;
    ne_snapshot(&b->st);
    ne_acid_snapshot(&b->acid);
    b->bank = (uint8_t)n;
    proj_bank_dirty |= (uint8_t)(1u << (n / 2));
}

/* the changed sectors, the one playing in its place too (it is the project as well) */
int project_banks_save(void)
{
    int s, rc = 0;
    if (project_legacy) return -4;
    if (project_legacy_cleanup) {
        memset(&proj_tmp, 0, sizeof proj_tmp);
        for (s = PROJ_BANKS / 2; s < PLAT_BANK_SECTORS - 1; s++)
            if (plat_bank_save(s, &proj_tmp, sizeof proj_tmp)) rc = -1;
        if (!rc) project_legacy_cleanup = 0;
    }
    if (proj_auto_dirty) {
        if (plat_bank_save(PLAT_BANK_SECTORS - 1, proj_auto, sizeof proj_auto) == 0)
            proj_auto_dirty = 0;
        else
            rc = -1;
    }
    if (!proj_bank_dirty)
        return rc;
    project_bank_keep(proj.bank);
    for (s = 0; 2 * s < PROJ_BANKS; s++)
        if (proj_bank_dirty >> s & 1u) {
            if (plat_bank_save(s, &proj_bank[2 * s], 2u * sizeof(project_t)) == 0)
                proj_bank_dirty &= (uint8_t)~(1u << s);
            else
                rc = -1;
        }
    return rc;
}

int project_load(void)
{
    int n;
    project_banks_load();
    project_auto_load();
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n >= 8 && proj.magic == PROJ_MAGIC && proj.bank >= PROJ_BANKS) project_legacy = 1;
    if (project_legacy) {
        /* Keep ALL flash untouched until an explicit compatible song load or clear. */
        memset(proj_bank, 0, sizeof proj_bank);
        memset(proj_auto, 0, sizeof proj_auto);
        project_defaults();
        return -4;
    }
    if (n >= 8 && project_valid(n)) {
        proj_has_state = 1;
        return 0;
    }
    project_defaults();
    return -1;
}

int project_save(void)
{
    if (project_legacy) return -4;
    ne_snapshot(&proj.st);
    ne_acid_snapshot(&proj.acid);
    proj_has_state = proj_has_acid = 1;
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

/* SONGS: a preset is a song, all its banks and their automation. Its own slot holds banks 1 and 2; the
 * rest go to parts, in slots that hold no preset (taken from the top down): banks 3-4, 5-6.. 11-12, then
 * the automation. A part is only written when it holds something. */
#define SONG_PARTS 6
#define SONG_AUTO (SONG_PARTS - 1)
static void song_parts(int n, int8_t at[SONG_PARTS])    /* the slots of song n's parts now (-1: none) */
{
    int i, c;
    for (i = 0; i < SONG_PARTS; i++)
        at[i] = -1;
    for (i = 0; i < PLAT_PRESETS; i++)
        if ((c = plat_part_of(i)) >= 0 && c / 8 == n && c % 8 < SONG_PARTS)
            at[c % 8] = (int8_t)i;
}
static int auto_any(void)
{
    int i;
    for (i = 0; i < AUTO_LANES; i++)
        if (proj_auto[i].bars)
            return 1;
    return 0;
}
int project_slot_free(int n)
{
    return n >= 0 && n < PLAT_PRESETS && !plat_preset_used(n) && plat_part_of(n) < 0;
}

int project_preset_save(int n)
{
    const uint32_t sz = sizeof(project_t);
    int8_t at[SONG_PARTS];
    int need[SONG_PARTS], p, i, more = 0, rc = 0, next = PLAT_PRESETS - 1;
    if (n < 0 || n >= PLAT_PRESETS)
        return -1;
    if (plat_part_of(n) >= 0)
        return -2;                                   /* (a part of another song) */
    if (project_legacy) return -4;
    song_parts(n, at);
    /* Old song part IDs stay fixed: 3/4 are banks 9-12, 5 is automation. */
    if (at[3] >= 0 || at[4] >= 0) return -4;
    proj.preset = (uint8_t)n;
    project_bank_keep(proj.bank);
    if (!proj_bank_has(0))                           /* (bank 1 is always there: the one playing, if none) */
        project_bank_keep(0);
    song_parts(n, at);
    for (p = 0; p < SONG_PARTS; p++) {
        need[p] = p == SONG_AUTO ? auto_any() : proj_bank_has(2 * p + 2) || proj_bank_has(2 * p + 3);
        more += need[p] && at[p] < 0;
    }
    for (i = 0; i < PLAT_PRESETS && more > 0; i++)
        more -= i != n && project_slot_free(i);
    if (more > 0)
        return -3;                                   /* (no room) */
    for (p = 0; p < SONG_PARTS; p++) {
        int at_p = at[p];
        if (!need[p]) {
            if (at_p >= 0 && plat_part_free(at_p) != 0)
                rc = -1;
            continue;
        }
        if (at_p < 0) {
            while (next == n || !project_slot_free(next))
                next--;
            at_p = next--;
        }
        if (p == SONG_AUTO ? plat_part_save(at_p, n * 8 + p, proj_auto, sizeof proj_auto)
                           : plat_part_save(at_p, n * 8 + p, &proj_bank[2 * p + 2], 2u * sz))
            rc = -1;
    }
    proj_has_state = proj_has_acid = 1;
    for (i = 0; i < PROJ_BANKS; i++)
        if (proj_bank_has(i))
            proj_bank[i].preset = (uint8_t)n;
    if (plat_preset_save(n, &proj_bank[0], proj_bank_has(1) ? 2u * sz : sz))   /* (last: the song is there) */
        rc = -1;
    return rc;
}

/* a song loads whole, in place of all the banks there are, and starts on its bank 1; it becomes the one
 * the device starts with. (An older preset: a song of one bank.) */
int project_preset_load(int n)
{
    const uint32_t sz = sizeof(project_t);
    project_t *keep = &proj_bank[proj.bank];         /* (the bank playing: its place is free) */
    int8_t at[SONG_PARTS];
    int got, p;
    song_parts(n, at);
    if (at[3] >= 0 || at[4] >= 0) return -4;
    *keep = proj;
    memset(&proj, 0, sizeof proj);
    got = plat_preset_load(n, &proj, sizeof proj);
    if (got < 8 || !project_valid(got)) {
        proj = *keep;
        return -1;
    }
    memset(proj_bank, 0, sizeof proj_bank);
    memset(proj_auto, 0, sizeof proj_auto);
    song_parts(n, at);
    banks_take(0, plat_preset_load(n, &proj_bank[0], 2u * sz));
    for (p = 0; 2 * p + 2 < PROJ_BANKS; p++)
        if (at[p] >= 0)
            banks_take(2 * p + 2, plat_part_load(at[p], n * 8 + p, &proj_bank[2 * p + 2], 2u * sz));
    if (at[SONG_AUTO] >= 0 &&
        plat_part_load(at[SONG_AUTO], n * 8 + SONG_AUTO, proj_auto, sizeof proj_auto) == (int)sizeof proj_auto)
        auto_check();
    else
        memset(proj_auto, 0, sizeof proj_auto);
    project_legacy = 0;
    project_legacy_cleanup = 1;
    proj_bank_dirty = (uint8_t)((1u << ((PROJ_BANKS + 1) / 2)) - 1u);
    proj_auto_dirty = 1;
    memset(&proj, 0, sizeof proj);
    got = plat_preset_load(n, &proj, sizeof proj);   /* (bank 1 again, with its own flags) */
    if (got < 8 || !project_valid(got))
        proj = proj_bank[0];
    proj.preset = (uint8_t)n;
    proj.bank = 0;
    proj_has_state = 1;
    project_apply();
    return 0;
}

/* the patterns and sounds first (their kinds), then the settings over them */
void project_apply(void)
{
    int i, t;
    if (proj_has_state)
        ne_restore(&proj.st);
    for (i = 0; i < G_NPARAMS; i++)
        ne_set(i, proj.g[i]);
    for (t = 0; t < NE_TRACKS; t++)
        for (i = 0; i < T_NPARAMS; i++)
            if (proj_has_state && (i == T_BARS || i == T_RANDLEN))   /* (the saved length stands) */
                ne_tset_keep(t, i, proj.t[t][i]);
            else if (!proj_has_state || i != T_KIND) /* (and the saved kind) */
                ne_tset(t, i, proj.t[t][i]);
    if (proj_has_acid)
        ne_acid_restore(&proj.acid);
    for (i = 0; i < A_NPARAMS; i++)
        if (!proj_has_acid || i != A_BARS)
            ne_aset(i, proj.a[i]);
    if (!proj_has_acid)
        ne_generate(NE_ACID);
}

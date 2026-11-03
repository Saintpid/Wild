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

/* the banks from flash, each read as a project (proj the workspace: before the project itself loads).
 * A sector holds two of the size they had when saved: a newer, larger project_t moves the second along. */
static void project_banks_load(void)
{
    const int sz = (int)sizeof(project_t);
    int s, i, n, rec;
    memset(proj_bank, 0, sizeof proj_bank);
    proj_bank_dirty = 0;
    for (s = 0; 2 * s < PROJ_BANKS; s++) {
        uint8_t *b = (uint8_t *)&proj_bank[2 * s];
        n = plat_bank_load(s, b, 2u * (uint32_t)sz);
        rec = n > 0 ? n / 2 : 0;
        if (rec > sz || rec < 8) {                   /* (from a later firmware, or none) */
            memset(b, 0, 2u * (uint32_t)sz);
            continue;
        }
        if (rec < sz) {                              /* (back to front: the two overlap) */
            for (i = rec; i-- > 0;)
                b[sz + i] = b[rec + i];
            memset(b + rec, 0, (uint32_t)(sz - rec));
            memset(b + sz + rec, 0, (uint32_t)(sz - rec));
        }
        for (i = 2 * s; i < 2 * s + 2 && i < PROJ_BANKS; i++) {
            proj = proj_bank[i];
            if (project_valid(rec)) {
                proj.bank = (uint8_t)i;
                proj_bank[i] = proj;
            } else {
                memset(&proj_bank[i], 0, sizeof proj_bank[i]);
            }
        }
    }
}

/* the automation: a lane that is not one this firmware knows is dropped */
static void project_auto_load(void)
{
    int i;
    memset(proj_auto, 0, sizeof proj_auto);
    proj_auto_dirty = 0;
    if (plat_bank_load(PLAT_BANK_SECTORS - 1, proj_auto, sizeof proj_auto) != (int)sizeof proj_auto) {
        memset(proj_auto, 0, sizeof proj_auto);
        return;
    }
    for (i = 0; i < AUTO_LANES; i++) {
        auto_lane_t *l = &proj_auto[i];
        int k = l->k, n = k & 0x80 ? T_NPARAMS : k & 0x40 ? A_NPARAMS : G_NPARAMS;
        if ((l->bars != 1 && l->bars != 2 && l->bars != 4 && l->bars != 8) || l->bank >= PROJ_BANKS ||
            l->track >= NE_TRACKS || (k & 0x3F) >= n || (k & 0xC0) == 0xC0)
            memset(l, 0, sizeof *l);
    }
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
    if (n >= 8 && project_valid(n)) {
        proj_has_state = 1;
        return 0;
    }
    project_defaults();
    return -1;
}

int project_save(void)
{
    ne_snapshot(&proj.st);
    ne_acid_snapshot(&proj.acid);
    proj_has_state = proj_has_acid = 1;
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

int project_preset_save(int n)
{
    proj.preset = (uint8_t)n;
    ne_snapshot(&proj.st);
    ne_acid_snapshot(&proj.acid);
    proj_has_state = proj_has_acid = 1;
    return plat_preset_save(n, &proj, sizeof proj);
}

/* a preset is a project: it loads the same way, and becomes the one the device starts with */
int project_preset_load(int n)
{
    project_t *keep = &proj_bank[proj.bank];         /* (the bank playing: its place is free) */
    int got;
    *keep = proj;
    memset(&proj, 0, sizeof proj);
    got = plat_preset_load(n, &proj, sizeof proj);
    if (got < 8 || !project_valid(got)) {
        proj = *keep;
        return -1;
    }
    proj.preset = (uint8_t)n;
    proj.bank = keep->bank;                          /* it becomes the bank playing */
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

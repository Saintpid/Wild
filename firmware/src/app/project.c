/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se project: one flash object (OBJ_PROJ): the settings, the four patterns and the four sounds.
 * A project of another kind, or none, gives the defaults (the engine's starting set). Part of the unity
 * build, after app.h. */
project_t proj;
static uint8_t proj_has_state;           /* the project carries patterns and sounds (not just defaults) */

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
    proj_has_state = 0;
}

/* each value out of its range takes its default; the rest are kept */
static int project_valid(int bytes)
{
    int i, t;
    if (bytes != (int)sizeof proj || proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT)
        return 0;
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
    return 1;
}

int project_load(void)
{
    int n;
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
    proj_has_state = 1;
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
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
            if (!proj_has_state || (i != T_KIND && i != T_BARS))   /* (the saved length and kind stand) */
                ne_tset(t, i, proj.t[t][i]);
}

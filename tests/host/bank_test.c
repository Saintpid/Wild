/* SPDX-License-Identifier: GPL-3.0-only */
/* Whole-app bank/storage regression: the same UI and project code as firmware. */
#define main simulator_main
#include "../../host/n001se_host.c"
#undef main
#include <assert.h>

static int lane_count(int bank)
{
    int i, n = 0;
    for (i = 0; i < AUTO_LANES; i++) n += proj_auto[i].bars && proj_auto[i].bank == bank;
    return n;
}
static void lane(int i, int bank, int value)
{
    proj_auto[i].bank = (uint8_t)bank;
    proj_auto[i].k = KT | T_REVERB;
    proj_auto[i].track = 0;
    proj_auto[i].bars = 1;
    memset(proj_auto[i].v, value, AUTO_PTS);
}
int main(void)
{
    int i;
    project_t saved, before, legacy[2];
    auto_lane_t automation[AUTO_LANES];
    boot();
    assert(PROJ_BANKS == 8);
    printf("project=%zu journal=%zu entry=%zu automation=%zu\n",
           sizeof(project_t), sizeof undo_mem, sizeof(undo_entry), sizeof proj_auto);
    tpar_set(0, T_REVERB, 12);
    lane(0, 0, 12);
    undo_clear();
    bank_go(7, 0); /* empty -> copy incl automation */
    assert(proj.bank == 7 && proj.t[0][T_REVERB] == 12 && lane_count(7) == 1);
    undo_go(-1);
    assert(proj.bank == 0 && !proj_bank_has(7) && lane_count(7) == 0 && lane_count(0) == 1);
    undo_go(1);
    assert(proj.bank == 7 && lane_count(7) == 1);
    tpar_set(0, T_REVERB, 70);
    lane(1, 7, 70);
    bank_go(0, 0);
    assert(proj.t[0][T_REVERB] == 12);
    saved = proj_bank[7];
    memcpy(automation, proj_auto, sizeof automation);
    undo_clear();
    bank_go(7, 1); /* occupied -> replace, atomically undo both banks */
    assert(proj.t[0][T_REVERB] == 12 && lane_count(7) == 1);
    undo_go(-1);
    assert(proj.bank == 0 && !memcmp(&saved, &proj_bank[7], sizeof saved));
    assert(!memcmp(automation, proj_auto, sizeof automation));
    undo_go(1);
    assert(proj.bank == 7 && proj.t[0][T_REVERB] == 12);
    undo_mark(0); tpar_set(0, T_REVERB, 35);
    undo_go(-1); assert(proj.t[0][T_REVERB] == 12);
    undo_go(-1); assert(proj.bank == 0 && proj_bank[7].t[0][T_REVERB] == 70);
    undo_go(1); assert(proj.bank == 7 && proj.t[0][T_REVERB] == 12);
    undo_go(1); assert(proj.t[0][T_REVERB] == 35);
    /* Same bank is a no-op, including its history. */
    i = undo_c; bank_go(7, 1); assert(undo_c == i);
    /* Undo after a stopped autosave must still restore the overwritten bank. */
    bank_go(0, 0); undo_clear(); bank_go(7, 1);
    assert(project_save() == 0 && project_banks_save() == 0);
    undo_go(-1);
    assert(proj.bank == 0 && proj_bank[7].t[0][T_REVERB] == 35);
    assert(project_save() == 0 && project_banks_save() == 0);
    boot(); assert(proj.bank == 0 && proj_bank[7].t[0][T_REVERB] == 35);
    /* Full automation pool: insufficient room must change NOTHING. */
    for (i = 0; i < AUTO_LANES; i++) lane(i, i < 20 ? 0 : 1, i);
    undo_take(&before); saved = proj_bank[7];
    memcpy(automation, proj_auto, sizeof automation);
    i = undo_c; bank_go(7, 1);
    assert(proj.bank == 0 && undo_c == i);
    assert(!memcmp(&saved, &proj_bank[7], sizeof saved));
    assert(!memcmp(automation, proj_auto, sizeof automation));
    /* Bounded eviction and redo truncation, with large and small operations. */
    memset(proj_auto, 0, sizeof proj_auto); undo_clear();
    for (i = 0; i < 80; i++) { undo_mark(0); tpar_set(0, T_REVERB, i); }
    assert(undo_used <= sizeof undo_mem && undo_c >= 6);
    for (i = 0; i < 4; i++) undo_go(-1);
    undo_mark(0); tpar_set(0, T_REVERB, 99);
    assert(undo_c == undo_n);
    for (i = 0; i < 30; i++) bank_go(proj.bank == 0 ? 7 : 0, 1);
    assert(undo_used <= sizeof undo_mem && undo_c >= 2);
    while (undo_c) undo_go(-1);
    while (undo_c < undo_n) undo_go(1);
    /* Chance is captured by a copy; its source keeps the original. Undo restores Chance. */
    clear_all(); tpar_set(0, T_REVERB, 30); scene_new();
    undo_take(&before);
    bank_go(1, 1);
    assert(!ui.chance && proj_bank[0].t[0][T_REVERB] == 30);
    assert(proj.t[0][T_REVERB] == before.t[0][T_REVERB]);
    undo_go(-1); assert(ui.chance && proj.bank == 0 && !proj_bank_has(1));
    undo_go(1); assert(!ui.chance && proj.bank == 1);
    assert(proj_bank[0].t[0][T_REVERB] == 30);
    /* Recording into an empty automation pool is undoable and redoable. */
    run_ms(50); clear_all(); run_ms(50); ne_play(1); run_ms(50); rec_press();
    assert(playing() && ui.rec);
    turn(EN_K1, KT | T_REVERB, 4);
    run_ms(100); rec_press();
    assert(lane_count(0) == 1);
    undo_go(-1); assert(lane_count(0) == 0);
    undo_go(1); assert(lane_count(0) == 1);
    ne_play(0);
    /* Compatible saved songs and fixed part IDs round-trip. */
    clear_all(); bank_go(7, 1); lane(0, 7, 60);
    assert(project_preset_save(2) == 0);
    clear_all(); assert(project_preset_load(2) == 0);
    assert(proj_bank_has(7) && lane_count(7) == 1);
    /* Format 16 stores absolute counts in the same slots. Load through the song path. */
    saved = proj;
    saved.format = 16;
    saved.t[0][T_BARS] = 2;
    saved.t[0][T_RANDLEN] = 0;
    saved.t[0][T_HITLESS] = 12;
    saved.t[0][T_HITFILL] = 16;
    assert(plat_preset_save(4, &saved, sizeof saved) == 0);
    assert(project_preset_load(4) == 0);
    assert(proj.format == PROJ_FORMAT && proj.t[0][T_HITLESS] == 25 && proj.t[0][T_HITFILL] == 50);
    assert(!memcmp(&proj.st, &saved.st, sizeof proj.st));
    assert(project_preset_load(2) == 0);
    /* A legacy song containing banks 9-12 is rejected before ANY state changes or writes. */
    legacy[0] = proj; legacy[1] = proj;
    legacy[0].bank = 8; legacy[1].bank = 9;
    assert(plat_part_save(50, 2 * 8 + 3, legacy, sizeof legacy) == 0);
    undo_take(&before); i = store_writes;
    assert(project_preset_load(2) == -4 && project_preset_save(2) == -4);
    assert(store_writes == (unsigned)i && proj.bank == before.bank);
    /* Active legacy banks survive boot, attempted save and autosave byte-for-byte. */
    assert(plat_bank_save(4, legacy, sizeof legacy) == 0);
    boot(); assert(project_legacy);
    i = store_writes;
    ui.dirty = 1; now_ms += 6000; autosave();
    assert(project_save() == -4 && project_banks_save() == -4 && project_preset_save(3) == -4);
    assert(store_writes == (unsigned)i && !memcmp(banks[4], legacy, sizeof legacy));
    clear_all(); assert(!project_legacy);
    assert(project_save() == 0 && project_banks_save() == 0);
    boot(); assert(!project_legacy);
    /* Songs kept: a part whose song is gone (a delete torn by power off) is free again; a copy goes only
     * to a free slot and takes its parts along; erase all leaves nothing, the legacy part too. */
    assert(project_songs_erase() == 0);
    for (i = 0; i < PLAT_PRESETS; i++) assert(project_slot_free(i));
    bank_go(7, 1);
    assert(project_preset_save(5) == 0 && plat_part_of(63) == 5 * 8 + 2 && !project_slot_free(63));
    assert(project_song_copy(5, 5) == -1 && project_song_copy(4, 6) == -1);
    assert(project_song_copy(5, 6) == 0 && plat_part_of(62) == 6 * 8 + 2 && plat_preset_used(6));
    assert(project_song_copy(5, 63) == -1);              /* (a part's slot: not free) */
    assert(plat_part_free(5) == 0 && project_slot_free(63) && !project_slot_free(62));
    assert(project_preset_save(7) == 0 && plat_part_of(63) == 7 * 8 + 2);   /* (the orphan reused) */
    assert(project_song_delete(6) == 0 && !plat_preset_used(6) && plat_part_of(62) < 0);
    assert(project_song_delete(6) == -1);
    assert(plat_part_save(50, 7 * 8 + 4, legacy, sizeof legacy) == 0 && project_songs_erase() == 0);
    for (i = 0; i < PLAT_PRESETS; i++) assert(!plat_preset_used(i) && plat_part_of(i) < 0);
    puts("bank tests passed");
    return 0;
}

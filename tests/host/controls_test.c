/* SPDX-License-Identifier: GPL-3.0-only */
#define main simulator_main
#include "../../host/n001se_host.c"
#undef main
#include <assert.h>

static void spin_test(int enc, int delta)
{
    enc_acc[enc]+=delta; accel_off=1; run_ms(20); accel_off=0;
}
static int assign(int target)
{
    for(int i=0;i<=ne_param_info(G_PKNOB)->hi;i++) if(ne_pknob_target(i)==target) return i+1;
    assert(0); return 0;
}
int main(void)
{
    char text[32];
    boot(); run_ms(200);
    assert(proj.g[G_SKNOB]==0 && proj.g[G_AKNOB]==0 && proj.g[G_RUMBLE]==0);
    spin_test(EN_SELECT,1); assert(ui.track==1 && !rumble_editing());
    set_view(V_PATTERN); spin_test(EN_ALGO,1); assert(ui.view==V_RHYTHM);
    select_track(0); set_view(V_PATTERN); button(B_ENV); button(B_ENV); button(B_ENV); assert(rumble_editing() && ui.view==V_RUMBLE);
    spin_test(EN_K1,45); assert(proj.t[0][T_RLEVEL]==45);
    spin_test(EN_K2,10); assert(proj.t[0][T_RTAIL]==65);
    for(int i=0;i<7;i++) button(B_GLO);
    assert(ui.view==V_CONTROLS);
    spin_test(EN_K2,assign(KT|T_RLEVEL));
    int assignment=proj.g[G_AKNOB]; assert(assignment==assign(KT|T_RLEVEL));
    ne_param_text(G_AKNOB,assignment,text); assert(!strcmp(text,"Rumble"));
    spin_test(EN_ALGO,20); assert(proj.t[0][T_RLEVEL]==65 && ui.view==V_CONTROLS);
    spin_test(EN_K1,assign(KA|A_CUTOFF));
    int cutoff=proj.a[A_CUTOFF]; spin_test(EN_SELECT,3);
    assert(proj.a[A_CUTOFF]==cutoff+3 && ui.track==0);
    /* Mixer and preset-list SELECT navigation keep priority. */
    mixer_open=1; int level=proj.t[0][T_LEVEL]; spin_test(EN_SELECT,-3);
    assert(proj.t[0][T_LEVEL]==level-3 && proj.a[A_CUTOFF]==cutoff+3); mixer_open=0;
    list_open(L_SAVE); int slot=ui.list_slot; spin_test(EN_SELECT,1);
    assert(ui.list_slot!=slot && proj.a[A_CUTOFF]==cutoff+3); ui.list=0;
    /* The assigned ALGORITHM reaches the real REC path, just like a page knob. */
    ne_play(1); run_ms(100); rec_press(); spin_test(EN_ALGO,5);
    run_ms(200); rec_press();
    assert(auto_find(KT|T_RLEVEL,0)>=0);
    ne_play(0); run_ms(100); tpar_set(0,T_RLEVEL,65);
    /* Assignments survive bank selection and persistence; musical amount is bank-local. */
    bank_go(1,1); tpar_set(0,T_RLEVEL,10); bank_go(0,0);
    assert(proj.t[0][T_RLEVEL]==65 && proj.g[G_AKNOB]==assignment);
    assert(project_save()==0 && project_banks_save()==0);
    boot(); run_ms(200); assert(proj.g[G_AKNOB]==assignment && proj.t[0][T_RLEVEL]==65);
    /* Controls page always permits recovery of original navigation. */
    set_view(V_CONTROLS); spin_test(EN_K1,-100); spin_test(EN_K2,-100);
    assert(proj.g[G_SKNOB]==0 && proj.g[G_AKNOB]==0);
    spin_test(EN_ALGO,1); assert(ui.view==V_RDETAIL);
    /* ENV forms one complete cycle; no extra selectable track is introduced. */
    select_track(0); set_view(V_SOUND);
    int pages[]={V_TONE,V_RUMBLE,V_RDETAIL,V_RMIX,V_SOUND};
    for(unsigned i=0;i<sizeof pages/sizeof pages[0];++i) {
        button(B_ENV); assert(ui.view==pages[i]);
    }
    set_view(V_RUMBLE);
    ne_state_t before,after; ne_snapshot(&before);
    white_down(1); white_up(1); run_ms(20);
    ne_snapshot(&after); assert(memcmp(before.step,after.step,sizeof before.step));
    ne_snapshot(&before);
    tpar_set(0,T_RDUCK,100);
    int old_seed=proj.t[0][T_RSEED]; black_down(BK_SOUND); run_ms(100);
    assert(proj.t[0][T_RDUCK]==100);
    assert(proj.t[0][T_RSEED]!=old_seed);
    ne_snapshot(&after); assert(!memcmp(before.patch,after.patch,sizeof before.patch));
    int new_seed=proj.t[0][T_RSEED]; undo_go(-1); run_ms(100);
    assert(proj.t[0][T_RSEED]==old_seed); undo_go(1); run_ms(100);
    assert(proj.t[0][T_RSEED]==new_seed);
    int16_t layer[T_NPARAMS-T_RLEVEL];
    memcpy(layer,&proj.t[0][T_RLEVEL],sizeof layer);
    assert(project_save()==0 && project_banks_save()==0);
    boot(); run_ms(100);
    assert(!memcmp(layer,&proj.t[0][T_RLEVEL],sizeof layer));
    /* Removing the parent kick removes its child and exits the panel safely. */
    tpar_set(0,T_KIND,NFM_KIND_SNARE); new_sound(0); run_ms(100);
    assert(!has_rumble(0) && !rumble_editing());
    for(int t=0;t<NE_TRACKS;++t) {
        tpar_set(t,T_KIND,NFM_KIND_KICK); new_sound(t);
    }
    run_ms(100); select_track(0);
    for(int t=0;t<UI_TRACKS;++t) {
        assert(ui.track==t);
        spin_test(EN_SELECT,1);
    }
    assert(ui.track==0);
    spin_test(EN_SELECT,-1); assert(ui.track==NE_ACID);
    run_ms(1500); ui_frame(); png_write("build/host/kick-fx-tracks.png");
    /* Assignment selectors must not become automation lanes. */
    assert(!auto_ok(G_SKNOB) && !auto_ok(G_AKNOB) && auto_ok(KT|T_RLEVEL));
    assert(ne_dropped==0);
    puts("controls tests passed"); return 0;
}

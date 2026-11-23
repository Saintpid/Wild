/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include "../../firmware/src/dsp/n001se.c"
static float l[256],r[256],in[256];
static void run(nrumble *s,int n)
{
    memset(l,0,sizeof l); memset(r,0,sizeof r);
    nr_render(s,in,l,r,n,1.0f);
    for(int i=0;i<n;++i) assert(isfinite(l[i]) && isfinite(r[i]) && fabsf(l[i])<4.0f && fabsf(r[i])<4.0f);
}
static void setup(nrumble *s,int seed)
{
    nr_init(s); s->requested_seed=(uint8_t)seed; nr_tank_reset(s);
    s->amount=0.8f; s->tail=0.7f; s->colour=0.65f; s->drive=0.7f;
    s->duck_amount=0.75f; s->width=0.85f; s->motion=0.6f;
    nr_kick(s,20045.0f);
}
int main(void)
{
    nrumble a,b;
    int32_t pcm[512];
    nr_init(&a);
    for(int i=0;i<256;++i) l[i]=r[i]=0.123f;
    nr_render(&a,in,l,r,256,1.0f);
    for(int i=0;i<256;++i) assert(l[i]==0.123f && r[i]==0.123f);
    setup(&a,4);
    for(int j=0;j<20;++j) { run(&a,256); for(int i=0;i<256;++i) assert(l[i]==0.0f && r[i]==0.0f); }
    /* No source audio means no sound: the old free-running pitched layer is gone. */
    setup(&a,3); b=a;
    for(int i=0;i<256;++i) in[i]=i<80 ? 0.4f : -0.2f;
    run(&a,256);
    float left[256],right[256]; memcpy(left,l,sizeof l);memcpy(right,r,sizeof r);
    for(int i=0;i<256;++i) {
        float sample=in[i]; float sl=0,sr=0;
        nr_render(&b,&sample,&sl,&sr,1,1.0f);
        assert(sl==left[i] && sr==right[i]);
    }
    assert(!memcmp(&a,&b,sizeof a));
    /* No fresh attack: a flat hidden section then a smooth, zero-slope rise
     * ending exactly one eighth after the actual hit, at multiple tempos. */
    const int tempos[]={60,112,132,200,300};
    memset(in,0,sizeof in);
    for(unsigned t=0;t<sizeof tempos/sizeof tempos[0];++t) {
        float quarter=44100.0f*60.0f/(float)tempos[t];
        setup(&a,2); a.duck_amount=1.0f; nr_kick(&a,quarter);
        int eighth=(int)(quarter*0.5f+0.5f), wait=(int)a.duck_wait;
        float previous=0,largest=0;
        for(int i=0;i<eighth;++i) {
            run(&a,1);
            if(i<wait) assert(a.duck==0.0f);
            assert(a.duck>=previous && a.duck<=1.0f);
            float slope=a.duck-previous; if(slope>largest) largest=slope;
            previous=a.duck;
            if(i<eighth-1 && i>=wait) assert(a.duck_rise>0);
        }
        assert(a.duck==1.0f && !a.duck_wait && !a.duck_rise && largest<0.003f);
        nr_kick(&a,quarter); int initial_wait=(int)a.duck_wait;
        for(int i=0;i<123;++i) run(&a,1);
        nr_kick(&a,quarter); assert((int)a.duck_wait==initial_wait);
        nr_stop(&a); assert(!a.duck_wait && !a.duck_rise);
        for(int i=0;i<eighth+11025;++i) run(&a,1);
        assert(a.gain==0.0f && a.duck==0.0f);
    }
    /* Same kick input, different tank shapes: every variation has a non-silent
     * stereo tail, and doesn't become a fixed timbre independent of Shape. */
    double signatures[16];
    for(int seed=0;seed<16;++seed) {
        double energy=0,side=0; float previous=0,jump=0;
        setup(&a,seed);
        for(int i=0;i<44100;++i) {
            float t=(float)i/44100.0f;
            in[0]=i<7000 ? nfm_sine(t*(58.0f+90.0f*exp2_(-t*60.0f)))*exp2_(-t*30.0f) : 0.0f;
            run(&a,1); energy+=(double)l[0]*(double)l[0];
            side+=((double)l[0]-(double)r[0])*((double)l[0]-(double)r[0]);
            float d=fabsf(l[0]-previous); if(d>jump)jump=d; previous=l[0];
            if(i==12000) a.requested_seed=(uint8_t)(seed+16); /* live randomisation fades before replacing tank */
            if(i==23000) nr_kick(&a,20045.0f);
            if(i==30000) nr_stop(&a);
        }
        assert(energy>0.001 && side>0.00001 && jump<0.08f && a.gain<0.00001f);
        signatures[seed]=energy;
        for(int j=0;j<seed;++j) assert(fabs(signatures[j]-energy)>0.000001);
    }
    /* Measure the actual four-pole Side filter independently of source spectrum. */
    for(int band=0;band<2;++band) {
        nr_init(&a); double energy=0; float hz=band?300.0f:50.0f;
        for(int i=0;i<46148;++i) {
            float x=nfm_sine((float)i*hz/44100.0f),y=nr_hp_side(&a,x);
            if(i>=2048) energy+=(double)y*(double)y;
        }
        double ratio=sqrt(energy/44100.0*2.0);
        printf("Side at %.0f Hz: gain %.5f\n",(double)hz,ratio);
        assert(band ? ratio>0.55 && ratio<0.7 : ratio<0.015);
    }
    ne_init(); ne_tset(0,T_RLEVEL,80); ne_render(pcm,256,4096);
    ne_hit(1,127,0); ne_render(pcm,256,4096); assert(!rumble[0].remaining);
    ne_hit(0,127,0); ne_render(pcm,256,4096);
    assert(rumble[0].remaining && !rumble[1].remaining);
    ne_play(1); ne_render(pcm,256,4096);
    ne_clock(NE_CLK_STOP); ne_render(pcm,256,4096); assert(rumble[0].stopping);
    for(int i=0;i<80;++i) ne_render(pcm,256,4096);
    assert(rumble[0].gain==0.0f);
    /* Real sequencer swing: schedule the kick on sixteenth 2 at maximum swing.
     * The eighth timer must start at that delayed hit, not at the grid tick. */
    ne_init(); ne_clear(); ne_set(G_TEMPO,120); ne_set(G_SWING,100); ne_set(G_LOOSE,0);
    ne_tset(0,T_EVOLVE,0); ne_tset(0,T_RLEVEL,80); ne_tset(0,T_RDUCK,100);
    ne_step(0,1,1,127,0); ne_play(1);
    int hit=-1;
    for(int i=0;i<10000;++i) {
        ne_render(pcm,1,4096);
        if(rumble[0].remaining) { hit=i; break; }
    }
    assert(hit>=8268 && hit<=8270);
    int eighth=11025;
    assert(rumble[0].duck_wait+rumble[0].duck_rise==(uint32_t)(eighth-1));
    for(int i=1;i<eighth;++i) ne_render(pcm,1,4096);
    assert(rumble[0].duck==1.0f);
    printf("swung kick at %d, swell maximum at %d samples\n",hit,hit+eighth-1);
    /* All four kick layers fit and run independently, including dense limiting. */
    ne_init(); ne_voices(2);
    for(int t=0;t<4;++t) {
        ne_new_sound(t,NFM_KIND_KICK); ne_tset(t,T_RLEVEL,100);
        ne_tset(t,T_RTAIL,100); ne_tset(t,T_RDRIVE,100); ne_tset(t,T_RSEED,30+t);
    }
    for(int i=0;i<1000;++i) {
        if(i%13==0) ne_hit((i/13)%4,127,0);
        ne_render(pcm,256,4096);
        for(int j=0;j<512;++j) assert(pcm[j]>=-8388608 && pcm[j]<=8388607);
    }
    ne_tset(0,T_MUTE,1); ne_hit(0,127,0); ne_render(pcm,256,4096); assert(rumble[0].stopping);
    ne_panic(); ne_render(pcm,256,4096);
    for(int t=0;t<4;++t) assert(rumble[t].stopping);
    puts("rumble tests passed"); return 0;
}

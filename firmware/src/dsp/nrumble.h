/* SPDX-License-Identifier: GPL-3.0-only */
/* Kick audio -> HP/drive -> two-line scattering tank at 11025 Hz.
 * Interpolation, duck and gain run at 44100 Hz; Side HP follows modulation.
 * Independent BSS state per drum slot. No additional FM voices or pool storage. */
#ifndef NRUMBLE_H
#define NRUMBLE_H
#define NR_LINE 256
typedef struct {
    float amount, tail, colour, drive, duck_amount, width, motion;
    float gain, feedback, damp, pre, duck, duck_k;
    float hp, x1, lp[2], dc, side_lp[4], accum;
    float mid, side, mid_to, side_to, phase, phase_inc;
    float smooth_width, smooth_motion;
    int16_t line[2][NR_LINE];
    uint16_t pos[2], len[2];
    uint8_t sub, seed, requested_seed, stopping, dirty;
    uint32_t rng, remaining, duck_wait, duck_rise;
} nrumble;
static void nr_tank_reset(nrumble *r)
{
    uint32_t s=0x726d626cu+7919u*(uint32_t)r->requested_seed;
    nfm_zero(r->line,sizeof r->line); nfm_zero(r->lp,sizeof r->lp);
    r->pos[0]=r->pos[1]=0;
    r->len[0]=(uint16_t)(103u+s%79u);
    r->len[1]=(uint16_t)(191u+(s>>8)%61u);
    r->hp=r->x1=r->accum=r->mid=r->side=r->mid_to=r->side_to=r->dc=0.0f;
    nfm_zero(r->side_lp,sizeof r->side_lp);
    r->seed=r->requested_seed; r->rng=s; r->sub=0; r->remaining=0;
    r->duck_wait=r->duck_rise=0; r->duck=0.0f;
    r->phase=(float)(s&255u)*(1.0f/256.0f);
}
static void nr_init(nrumble *r)
{
    nfm_zero(r,sizeof *r);
    r->feedback=0.75f; r->damp=0.3f; r->pre=2.0f;
    nr_tank_reset(r);
}
static void nr_stop(nrumble *r)
{
    r->stopping=1; r->duck_wait=r->duck_rise=0;
}
static void nr_kick(nrumble *r, float quarter)
{
    if (r->amount<=0.0f) return;
    /* Latest actual kick owns the swell. Reach full weight on its next eighth,
     * using the last 15% of a quarter for a zero-slope, attack-free rise. */
    uint32_t eighth=(uint32_t)(quarter*0.5f+0.5f);
    r->duck_rise=(uint32_t)(quarter*0.15f+0.5f);
    if (!r->duck_rise) r->duck_rise=1;
    r->duck_wait=eighth>r->duck_rise ? eighth-r->duck_rise : 0;
    r->duck_k=1.0f/(float)r->duck_rise;
    r->phase_inc=4.0f/(quarter*(0.5f+0.25f*(float)(r->seed&7u)));
    r->stopping=0;
    r->remaining=(uint32_t)(44100.0f*(0.4f+2.4f*r->tail));
}
NFM_HOT static float nr_hp_side(nrumble *r, float x)
{
    for (int j=0;j<4;++j) {
        r->side_lp[j]+=0.021144f*(x-r->side_lp[j]);
        x-=r->side_lp[j];
    }
    return x;
}
NFM_HOT static void nr_render(nrumble *r, const float *input, float *l, float *rr, int n, float master)
{
    int changing=r->seed!=r->requested_seed;
    float target=changing||r->stopping ? 0.0f : r->amount;
    float feedback_tail=0.60f+0.36f*r->tail;
    float damp_to=0.09f+0.64f*r->colour;
    float pre_to=1.0f+7.0f*r->drive;
    if (target<=0.0f && r->gain<0.00001f) {
        if (changing||r->dirty) nr_tank_reset(r);
        r->dirty=0; r->gain=0.0f;
        return;
    }
    if (!r->remaining && !r->dirty) { r->gain=target; return; }
    r->dirty=1;
    for (int i=0;i<n;++i) {
        float mid,side,g;
        r->gain+=0.0015f*(target-r->gain);
        r->smooth_width+=0.001f*(r->width-r->smooth_width);
        r->smooth_motion+=0.001f*(r->motion-r->smooth_motion);
        if (r->stopping || r->duck_wait) {
            r->duck+=0.008f*(0.0f-r->duck);
            if (r->duck<1e-8f) r->duck=0.0f;
            if (r->duck_wait) --r->duck_wait;
        } else if (r->duck_rise) {
            float p=1.0f-(float)(--r->duck_rise)*r->duck_k;
            r->duck=p*p*(3.0f-2.0f*p);
        } else r->duck=1.0f;
        r->accum+=input[i];
        if (++r->sub==4) {
            float x=r->accum*0.25f, y[2], scatter[2], pulse;
            r->sub=0; r->accum=0.0f;
            /* Preserve the excitation during the hidden part, then let Tail
             * govern its decay as it swells into view. No long audio delay. */
            float feedback_to=r->duck_wait && !r->stopping ?
                feedback_tail+(0.99f-feedback_tail)*r->duck_amount : feedback_tail;
            r->feedback+=0.002f*(feedback_to-r->feedback);
            r->damp+=0.002f*(damp_to-r->damp);
            r->pre+=0.002f*(pre_to-r->pre);
            r->hp=0.9775f*(r->hp+x-r->x1); r->x1=x;
            x=sat_curve(r->hp*r->pre);
            r->rng^=r->rng<<13; r->rng^=r->rng>>17; r->rng^=r->rng<<5;
            x+=((float)(r->rng>>9)*(1.0f/4194304.0f)-1.0f)*r->drive*0.07f*(x<0.0f?-x:x);
            for (int j=0;j<2;++j) {
                y[j]=(float)r->line[j][r->pos[j]]*(1.0f/16384.0f);
                r->lp[j]+=r->damp*(y[j]-r->lp[j]);
            }
            scatter[0]=0.7071068f*(r->lp[0]+r->lp[1]);
            scatter[1]=0.7071068f*(r->lp[0]-r->lp[1]);
            for (int j=0;j<2;++j) {
                float z=x*0.45f+scatter[j]*r->feedback;
                z=z>1.9f?1.9f:z< -1.9f?-1.9f:z;
                r->line[j][r->pos[j]]=(int16_t)(z*16384.0f);
                if (++r->pos[j]>=r->len[j]) r->pos[j]=0;
            }
            r->phase+=r->phase_inc;
            if (r->phase>=1.0f) r->phase-=1.0f;
            pulse=1.0f-r->smooth_motion*0.35f*(1.0f+nfm_sine(r->phase));
            r->mid_to=(r->lp[0]+r->lp[1])*0.9f*pulse;
            r->side_to=(y[0]-y[1])*0.65f*pulse;
        }
        r->mid+=0.22f*(r->mid_to-r->mid);
        r->side+=0.22f*(r->side_to-r->side);
        r->dc+=0.00355f*(r->mid-r->dc);
        g=r->gain*(1.0f-r->duck_amount+r->duck_amount*r->duck*r->duck)*master;
        mid=(r->mid-r->dc)*g;
        side=nr_hp_side(r,r->side*g*r->smooth_width);
        l[i]+=mid+side; rr[i]+=mid-side;
        if (r->remaining) --r->remaining;
    }
    if (!r->remaining && r->mid*r->mid+r->side*r->side<1e-12f) {
        nr_tank_reset(r); r->dirty=0;
    }
}
#endif

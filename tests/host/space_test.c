/* SPACE: still at 0; at 1 a station is left now and then (never before 4 bars at it), each glide 1-4
 * bars; at 0.5 less often; SEL's shift by its chance; back home when it goes to 0; and the delay's
 * crossfade from one tap to the other without a jump. */
#include <math.h>
#include <stdio.h>
#include "nfx.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

#define SR 44100.0f
static nfm_engine e;
static nfx fx;
static float ph, prev, L[64], R[64];   /* (prev: the last sample, across runs) */

/* a sine into the delay's send and the reverb's */
static void src(void *ctx, float *l, float *r, float *rev, float *dly, int n)
{
    (void)ctx;
    (void)l;
    (void)r;
    for (int i = 0; i < n; ++i) {
        float x = 0.3f * sinf(6.2831853f * ph);
        ph += 220.0f / SR;
        if (ph >= 1.0f) ph -= 1.0f;
        dly[i] += x;
        rev[i] += x;
    }
}

typedef struct { int shifts, short_stay, glide_bad; float jump; } run_t;

/* bars bars at 120 BPM; shifts counted at their start, the bars at a station before it */
static run_t run(int bars, float space)
{
    const float bar = SR * 2.0f;
    run_t r = { 0, 0, 0, 0.0f };
    nfx_set_space(&fx, space);
    for (int b = 0; b < bars; ++b) {
        uint16_t rs = fx.rv_stay, ds = fx.dl_stay;
        float rx = fx.rv_x, dx = fx.dl_x;
        nfx_bar(&fx, bar);
        if (rx >= 1.0f && fx.rv_x < 1.0f) {
            r.shifts++;
            r.short_stay += rs + 1u < 4u;
            float bars_long = 1.0f / (fx.rv_inc * bar);
            r.glide_bad += bars_long < 0.99f || bars_long > 4.01f;
        }
        if (dx >= 1.0f && fx.dl_x < 1.0f) {
            r.shifts++;
            r.short_stay += ds + 1u < 4u;
            float bars_long = 1.0f / (fx.dl_inc * bar);
            r.glide_bad += bars_long < 0.99f || bars_long > 4.01f;
        }
        for (int i = 0; i < (int)bar; i += 64) {
            for (int k = 0; k < 64; ++k) L[k] = R[k] = 0.0f;
            nfx_render(&fx, &e, L, R, 64);
            for (int k = 0; k < 64; ++k) {
                float j = fabsf(L[k] - prev);
                if (j > r.jump) r.jump = j;
                prev = L[k];
            }
        }
    }
    return r;
}

int main(void)
{
    nfm_init(&e, SR, 1u);
    nfx_init(&fx, SR, 7u);
    nfx_set_tempo(&fx, 120.0f);
    nfx_set_amount(&fx, NFX_DELAY, 1.0f);
    nfx_set_amount(&fx, NFX_REVERB, 0.0f);       /* (the delay's jumps alone) */
    nfx_set_source(&fx, src, 0);

    run_t r0 = run(64, 0.0f);
    CHECK(r0.shifts == 0 && fx.rv_to == 0 && fx.dl_to == 0, "space 0: %d shifts", r0.shifts);
    run_t r1 = run(256, 1.0f);
    run_t rh = run(256, 0.5f);
    printf("space 100: %d shifts in 256 bars (reverb and delay), space 50: %d, space 0: %d\n", r1.shifts, rh.shifts, r0.shifts);
    CHECK(r1.shifts >= 30 && r1.shifts <= 80, "space 1: %d shifts", r1.shifts);
    CHECK(rh.shifts < r1.shifts && rh.shifts >= 6, "space 0.5: %d shifts", rh.shifts);
    CHECK(!r1.short_stay && !rh.short_stay, "a station left before 4 bars (%d %d)", r1.short_stay, rh.short_stay);
    CHECK(!r1.glide_bad && !rh.glide_bad, "a glide not 1-4 bars (%d %d)", r1.glide_bad, rh.glide_bad);
    /* the delay's crossfade: no step in the sound bigger than the sine's own (0.3 x 2 pi 220 / 44100 = 0.0094 a
     * sample, through the delay's feedback a few times that) */
    printf("the largest step from one sample to the next, with the delay gliding: %.4f\n", (double)r1.jump);
    CHECK(r1.jump < 0.06f, "the delay jumped %f", (double)r1.jump);
    /* SEL: at 1 always (from a station), at 0 never */
    run(8, 1.0f);
    while (fx.rv_x < 1.0f || fx.dl_x < 1.0f) run(1, 1.0f);
    nfx_set_space(&fx, 1.0f);
    uint8_t rv = fx.rv_to, dl = fx.dl_to;
    nfx_space_shift(&fx);
    CHECK(fx.rv_to != rv && fx.dl_to != dl && fx.rv_x < 1.0f && fx.dl_x < 1.0f, "SEL at space 1: no shift");
    while (fx.rv_x < 1.0f || fx.dl_x < 1.0f) run(1, 1.0f);
    nfx_set_space(&fx, 0.0f);
    rv = fx.rv_to;
    nfx_space_shift(&fx);
    CHECK(fx.rv_to == rv && fx.rv_x >= 1.0f, "SEL at space 0: a shift");
    /* back home at 0 */
    run(4, 0.0f);
    CHECK(fx.rv_to == 0 && fx.dl_to == 0 && fx.rv_x >= 1.0f && fx.dl_x >= 1.0f, "space 0: not home (%d %d)", fx.rv_to, fx.dl_to);
    printf(fails ? "space: %d FAILED\n" : "space: all ok\n", fails);
    return fails != 0;
}

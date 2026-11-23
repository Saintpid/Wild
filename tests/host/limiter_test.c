/* SPDX-License-Identifier: GPL-3.0-only */
/* Include the owner to attach a deterministic bus source without a firmware
 * test API. Exercise the real render path, including its final limiter. */
#include <stdio.h>
#include "../../firmware/src/dsp/n001se.c"

static unsigned phase;
static void signal_bus(void *ctx, float *left, float *right, float *rev, float *dly, int n)
{
    (void)ctx; (void)rev; (void)dly;
    for (int i = 0; i < n; ++i) {
        float x = phase++ & 128u ? 4.0f : -4.0f;
        left[i] += x;
        right[i] -= x;
    }
}
int main(void)
{
    int fails = 0;
    for (int enabled = 0; enabled < 2; ++enabled) {
        int32_t out[512];
        float peak = 0.0f, ceiling = enabled ? 0.891f : 0.966f;
        ne_init();
        ne_set(G_GLUE, enabled ? 88 : 0);
        ne_set(G_GMIX, 0);
        ne_set(G_GEQ, 0);
        ne_set(G_GCOLOR, 0);
        ne_set(G_GWIDTH, 60);
        nfx_set_source(&fx, signal_bus, 0);
        for (int b = 0; b < 64; ++b) {
            ne_render(out, 256, 4096);
            for (int i = 0; i < 512; ++i) {
                float x = absf((float)out[i] / 8388608.0f);
                if (x > peak) peak = x;
            }
        }
        printf("fixed overload, Glue %d: peak %.6f, ceiling %.6f\n", enabled, (double)peak, (double)ceiling);
        /* The release follower permits a sub-0.004 dB one-frame overshoot. */
        if (!(peak > ceiling - 0.002f && peak <= ceiling + 0.002f)) ++fails;
    }
    return fails != 0;
}

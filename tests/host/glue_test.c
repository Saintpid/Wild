/* SPDX-License-Identifier: GPL-3.0-only */
/* The GLUE as a mastering chain, on the starting set (8 bars, Chaos 0, each setting from the same start): as loud as
 * without it, never over its ceiling (-1 dBFS), Width 0 mono and the sides' lows out at any width, the EQ's
 * pinky leaning the balance over / under 700 Hz towards its slope. Writes glue_off.wav and glue_nbb.wav (the
 * set it was tuned to) to the directory given, to listen to. Built without libm, as on the device. */
#include <stdio.h>
#include <string.h>
#include "n001se.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

#define FRAMES (int)(8.0f * 4.0f * 60.0f / 112.0f * NE_SR)
static int32_t blk[2 * 256];
typedef struct { float e, pk, eside, eside_lo, elo, ehi, ediff; } meas;

static void wav_header(FILE *f, uint32_t n)
{
    uint32_t v;
    uint16_t h[2] = {1, 2}, al[2] = {4, 16};
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    v = n + 36u;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16u;
    fwrite(&v, 4, 1, f);
    fwrite(h, 2, 2, f);
    v = 44100u;
    fwrite(&v, 4, 1, f);
    v = 44100u * 4u;
    fwrite(&v, 4, 1, f);
    fwrite(al, 2, 2, f);
    fwrite("data", 1, 4, f);
    fwrite(&n, 4, 1, f);
}

/* the starting set with the Glue's knobs as given; the first bar left out of the levels (the followers settle) */
static int hot;                                  /* every level at 100: into the limiter */
static meas run(int glue, int mix, int eq, int color, int width, const char *dir, const char *name)
{
    meas m;
    float slo = 0.0f, slo2 = 0.0f, lp = 0.0f;
    int f = 0, i;
    FILE *wav = 0;
    char path[512];
    memset(&m, 0, sizeof m);
    if (dir) {
        snprintf(path, sizeof path, "%s/%s", dir, name);
        wav = fopen(path, "wb");
        if (wav)
            fseek(wav, 44, SEEK_SET);
    }
    ne_init();
    ne_set(G_CHAOS, 0);                          /* no glitches: the same music each time */
    ne_set(G_GLUE, glue);
    ne_set(G_GMIX, mix);
    ne_set(G_GEQ, eq);
    ne_set(G_GCOLOR, color);
    ne_set(G_GWIDTH, width);
    if (hot) {
        ne_set(G_LEVEL, 100);
        for (i = 0; i < NE_TRACKS; i++)
            ne_tset(i, T_LEVEL, 100);
        ne_aset(A_LEVEL, 100);
    }
    ne_play(1);
    while (f < FRAMES) {
        ne_render(blk, 256, 4096);
        for (i = 0; i < 256; i++, f++) {
            float a = (float)blk[2 * i] * (1.0f / 8388608.0f), b = (float)blk[2 * i + 1] * (1.0f / 8388608.0f);
            float s = 0.5f * (a - b), x = 0.5f * (a + b);
            CHECK(a == a && b == b, "NaN in the output");
            slo += 0.0085f * (s - slo);          /* the sides under 60 Hz (12 dB an octave) */
            slo2 += 0.0085f * (slo - slo2);
            lp += 0.09492f * (x - lp);           /* the middle under 700 Hz */
            if (wav) {
                int16_t o[2] = {(int16_t)(blk[2 * i] >> 8), (int16_t)(blk[2 * i + 1] >> 8)};
                fwrite(o, 2, 2, wav);
            }
            if (f < FRAMES / 8)
                continue;
            m.e += a * a + b * b;
            m.eside += s * s;
            m.eside_lo += slo2 * slo2;
            m.elo += lp * lp;
            m.ehi += (x - lp) * (x - lp);
            m.ediff += (a - b) * (a - b);
            if (a < 0.0f)
                a = -a;
            if (b < 0.0f)
                b = -b;
            if (a > m.pk)
                m.pk = a;
            if (b > m.pk)
                m.pk = b;
        }
    }
    if (wav) {
        wav_header(wav, (uint32_t)FRAMES * 4u);
        fclose(wav);
    }
    return m;
}

static float db(float r)                         /* 10 log10 r, by log2 (n001se.c's; no libm) */
{
    union { float f; uint32_t u; } v;
    float e, m;
    v.f = r;
    e = (float)((int)(v.u >> 23 & 255u) - 127);
    v.u = (v.u & 0x7FFFFFu) | 0x3F800000u;
    m = v.f;
    return 3.0103f * (e - 1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : 0;
    meas off = run(0, 50, 50, 50, 60, 0, 0);     /* (the first run after start differs: a warm-up) */
    off = run(0, 50, 50, 50, 60, dir, "glue_off.wav");
    meas nbb = run(88, 50, 50, 100, 65, dir, "glue_nbb.wav");
    meas flat = run(88, 50, 0, 100, 65, 0, 0);
    meas mono = run(60, 50, 50, 50, 0, 0, 0);
    meas wide = run(60, 50, 50, 50, 100, 0, 0);
    meas clean = run(88, 50, 50, 0, 65, 0, 0), loud, loud_off;
    hot = 1;
    loud = run(100, 100, 100, 100, 100, 0, 0);
    loud_off = run(0, 50, 50, 50, 60, 0, 0);
    hot = 0;
    float rel = db(nbb.e / off.e);
    float r_off = off.ehi / off.elo, r_nbb = nbb.ehi / nbb.elo, r_flat = flat.ehi / flat.elo;
    printf("loudness with the Glue (88, mix 50, EQ 50, color 100, width 65): %+.2f dB; peak %.3f (off: %.3f)\n",
           (double)rel, (double)nbb.pk, (double)off.pk);
    printf("over / under 700 Hz: off %.3f, EQ 0 %.3f, EQ 50 %.3f (the slope: 0.29)\n", (double)r_off, (double)r_flat,
           (double)r_nbb);
    printf("sides under 60 Hz: off %.2e, Glue %.2e; sides at Width 100 / 60 off: %.2f\n", (double)off.eside_lo,
           (double)nbb.eside_lo, (double)(wide.eside / (off.eside > 0.0f ? off.eside : 1.0f)));
    CHECK(rel > -2.0f && rel < 2.0f, "the Glue changed the loudness by %.2f dB", (double)rel);
    CHECK(nbb.pk <= 0.8913f + 0.002f, "a peak of %.4f over the Glue's ceiling (-1 dBFS)", (double)nbb.pk);
    printf("every level at 100: peak %.3f with the Glue at 100 (all of it), %.3f without\n", (double)loud.pk,
           (double)loud_off.pk);
    CHECK(loud.pk > 0.85f && loud.pk <= 0.8913f + 0.002f, "hot, a peak of %.4f: not up to the ceiling or over it",
          (double)loud.pk);
    CHECK(loud_off.pk <= 0.966f + 0.002f, "hot without the Glue, a peak of %.4f", (double)loud_off.pk);
    CHECK(off.pk <= 0.966f + 0.002f, "a peak of %.4f over the ceiling without the Glue (-0.3 dBFS)", (double)off.pk);
    CHECK(mono.ediff == 0.0f, "Width 0 not mono");
    CHECK(off.eside > 0.0f && wide.eside > off.eside * 1.5f, "Width 100 not wider (%.3g, %.3g)", (double)wide.eside,
          (double)off.eside);
    CHECK(nbb.eside_lo < off.eside_lo * 0.25f || off.eside_lo < 1e-6f, "the sides' lows not out (%.3g, %.3g)",
          (double)nbb.eside_lo, (double)off.eside_lo);
    printf("sides under 60 Hz, Color 0: %.2e\n", (double)clean.eside_lo);
    {   /* the pinky: nearer the slope than without the EQ (in log, the way it leans) */
        float d_flat = db(r_flat / 0.29f), d_nbb = db(r_nbb / 0.29f);
        if (d_flat < 0.0f)
            d_flat = -d_flat;
        if (d_nbb < 0.0f)
            d_nbb = -d_nbb;
        CHECK(d_nbb < d_flat - 0.3f, "the EQ did not lean towards the slope (%.2f dB off, flat %.2f)", (double)d_nbb,
              (double)d_flat);
    }
    printf(fails ? "glue: %d FAILED\n" : "glue: all ok\n", fails);
    return fails != 0;
}

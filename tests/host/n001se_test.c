/* SPDX-License-Identifier: GPL-3.0-only */
/* n001se engine tests, built without libm as on the device: every kind of sound stays finite and
 * within range, the sequencer plays the hits it holds and never more than Max hits at once, a state
 * saved and restored plays the same pattern. Writes one WAV of 16 bars to the directory given. */
#include <stdio.h>
#include <string.h>
#include "n001se.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static int32_t blk[2 * 256];

static float render(int blocks, FILE *wav)
{
    float pk = 0.0f;
    int b, i;
    for (b = 0; b < blocks; b++) {
        ne_render(blk, 256, 4096);
        for (i = 0; i < 512; i++) {
            float x = (float)blk[i] * (1.0f / 8388608.0f);
            CHECK(x == x, "NaN in the output");
            if (x < 0.0f)
                x = -x;
            if (x > pk)
                pk = x;
            if (wav) {
                int16_t s = (int16_t)(blk[i] >> 8);
                fwrite(&s, 2, 1, wav);
            }
        }
    }
    return pk;
}

int main(int argc, char **argv)
{
    char path[512];
    FILE *wav = 0;
    float pk;
    int t, k, i;
    static ne_state_t a, b;
    ne_init();
    /* every kind, many times: a sound that blows up or stays silent fails */
    for (k = 0; k < NFM_KINDS; k++)
        for (i = 0; i < 20; i++) {
            ne_new_sound(0, k);
            ne_hit(0, 127, 0);
            pk = render(40, 0);
            CHECK(pk > 0.01f && pk <= 1.0f, "kind %d draw %d: peak %f", k, i, (double)pk);
        }
    /* a clap: three or four bursts in its first 50 ms (the noise jumps up again), every draw */
    {
        static nfm_engine ce;
        static float cl[64], cr[64];
        int d, bursts_ok = 0;
        nfm_init(&ce, NE_SR, 5u);
        for (d = 0; d < 20; d++) {
            int j, ups = 0, v = 0, w;
            float prev = 0.0f;
            nfm_randomize(&ce, 0, NFM_KIND_CLAP);
            nfm_trigger(&ce, 0, 1.0f, 0.0f);
            for (w = 0; w < NFM_VOICES; w++)        /* the voice it took: the newest */
                if (ce.voice[w].slot == 0 && ce.voice[w].age >= ce.voice[v].age)
                    v = w;
            for (j = 0; j < (int)(0.05f * NE_SR) / 64; j++) {
                nfm_render(&ce, cl, cr, 64);
                if (ce.voice[v].noise > prev * 1.5f + 0.01f)
                    ups++;
                prev = ce.voice[v].noise;
            }
            bursts_ok += ups >= 3 && ups <= 4;
        }
        CHECK(bursts_ok == 20, "clap: %d of 20 draws with 3-4 bursts", bursts_ok);
    }
    ne_init();
    if (argc > 1) {
        snprintf(path, sizeof path, "%s/n001se_16bars.wav", argv[1]);
        wav = fopen(path, "wb");
        if (wav)
            fseek(wav, 44, SEEK_SET);
    }
    ne_play(1);
    pk = render((int)(16.0f * 4.0f * 60.0f / 112.0f * NE_SR / 256.0f), wav);
    CHECK(pk > 0.1f && pk <= 1.0f, "16 bars: peak %f", (double)pk);
    printf("16 bars: peak %.2f\n", (double)pk);
    if (wav) {   /* the header, now the length is known */
        uint32_t n = (uint32_t)ftell(wav) - 44u, v;
        uint16_t h[2] = {1, 2}, al[2] = {4, 16};
        fseek(wav, 0, SEEK_SET);
        fwrite("RIFF", 1, 4, wav);
        v = n + 36u;
        fwrite(&v, 4, 1, wav);
        fwrite("WAVEfmt ", 1, 8, wav);
        v = 16u;
        fwrite(&v, 4, 1, wav);
        fwrite(h, 2, 2, wav);
        v = 44100u;
        fwrite(&v, 4, 1, wav);
        v = 44100u * 4u;
        fwrite(&v, 4, 1, wav);
        fwrite(al, 2, 2, wav);
        fwrite("data", 1, 4, wav);
        fwrite(&n, 4, 1, wav);
        fclose(wav);
    }
    /* the hit range: a new pattern with min = max has exactly that many hits */
    for (t = 0; t < NE_TRACKS; t++) {
        int n = 0;
        ne_tset(t, T_RANDLEN, 0);
        ne_tset(t, T_BARS, 4);
        ne_tset(t, T_HITHI, 9);
        ne_tset(t, T_HITLO, 9);
        ne_generate(t);
        render(1, 0);
        for (i = 0; i < ne_seq()->track[t].length; i++)
            n += ne_seq()->track[t].step[i].on;
        CHECK(n == 9 && ne_seq()->track[t].length == 64, "track %d: %d hits in %d steps", t, n, ne_seq()->track[t].length);
    }
    /* Chaos and swing at full: glitches come, a beat repeat never longer than an eighth */
    ne_init();
    ne_set(G_CHAOS, 100);
    ne_set(G_SWING, 100);
    ne_play(1);
    pk = render((int)(32.0f * 4.0f * 60.0f / 112.0f * NE_SR / 256.0f), 0);
    CHECK(ne_glitches > 3u && pk <= 1.0f, "glitches %u, peak %f", (unsigned)ne_glitches, (double)pk);
    CHECK(ne_repeat_max > 0u && ne_repeat_max <= (uint32_t)(2.0f * NE_SR * 15.0f / 112.0f) + 2u, "the longest repeat %u frames",
          (unsigned)ne_repeat_max);
    printf("32 bars at Chaos and Swing 100: %u glitches, the longest repeat %u frames\n", (unsigned)ne_glitches,
           (unsigned)ne_repeat_max);
    /* saved and restored: the same */
    ne_snapshot(&a);
    ne_init();
    ne_restore(&a);
    ne_snapshot(&b);
    CHECK(!memcmp(&a, &b, sizeof a), "restore differs from the snapshot");
    printf(fails ? "n001se: %d FAILED\n" : "n001se: all ok\n", fails);
    return fails != 0;
}

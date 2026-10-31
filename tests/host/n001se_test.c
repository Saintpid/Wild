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
    /* Drift at Bypass on every track, no sends: Chaos and Mutate at full change nothing (the glitches
     * play on an empty main bus, the filter stands at rest, the mutes stay); Drift in, they do */
    {
        uint32_t h[2][3], gl[2][3], mu[2][3];
        int pass, in, j, p;
        for (in = 0; in < 2; in++)
            for (pass = -1; pass < 2; pass++) {   /* (-1: as 0, so all start from the same rest) */
                p = pass + 1;
                ne_init();
                for (t = 0; t < NE_TRACKS; t++) {
                    ne_tset(t, T_LEVEL, 80);
                    ne_tset(t, T_FILTER, 100);
                    ne_tset(t, T_REVERB, 0);
                    ne_tset(t, T_DELAY, 0);
                    ne_tset(t, T_DRIFT, in);
                    ne_generate(t);
                }
                ne_aset(A_REVERB, 0);
                ne_aset(A_DELAY, 0);
                ne_aset(A_FILTER, 100);
                ne_aset(A_DRIFT, in);
                ne_set(G_MOTION, 100);
                ne_set(G_CHAOS, pass > 0 ? 100 : 0);
                ne_set(G_MUTATE, pass > 0 ? 100 : 0);
                ne_play(1);
                gl[in][p] = ne_glitches;
                mu[in][p] = ne_mutates;
                h[in][p] = 2166136261u;
                for (i = 0; i < (int)(16.0f * 4.0f * 60.0f / 112.0f * NE_SR / 256.0f); i++) {   /* 16 bars */
                    ne_render(blk, 256, 4096);
                    for (j = 0; j < 512; j++)
                        h[in][p] = (h[in][p] ^ (uint32_t)blk[j]) * 16777619u;
                }
                gl[in][p] = ne_glitches - gl[in][p];
                mu[in][p] = ne_mutates - mu[in][p];
            }
        CHECK(h[0][1] == h[0][2] && gl[0][2] > 0u && mu[0][2] == 0u, "Drift at Bypass: the same %d, glitches %u, mutates %u",
              h[0][1] == h[0][2], (unsigned)gl[0][2], (unsigned)mu[0][2]);
        CHECK(h[1][1] != h[1][2] && gl[1][2] > 0u && mu[1][2] > 0u, "Drift in: differs %d, glitches %u, mutates %u",
              h[1][1] != h[1][2], (unsigned)gl[1][2], (unsigned)mu[1][2]);
        printf("16 bars at Chaos and Mutate 100, Drift at Bypass: %u glitches, unheard\n", (unsigned)gl[0][2]);
    }
    /* the acid line's Deviance follows the drums: harder sounds and more hits, a higher intensity;
     * the drums muted, next to none */
    {
        float calm, hard, quiet;
        int bars = (int)(8.0f * 4.0f * 60.0f / 112.0f * NE_SR / 256.0f);
        ne_init();
        ne_set(G_CHAOS, 0);
        ne_aset(A_DEV, 100);
        ne_play(1);
        render(bars, 0);
        calm = ne_acid()->d_mood;
        for (t = 0; t < NE_TRACKS; t++) {
            ne_tset(t, T_SPIKE, 100);
            ne_tset(t, T_DRIVE, 100);
            ne_tset(t, T_BARS, 1);
            ne_tset(t, T_HITHI, 8);
            ne_tset(t, T_HITLO, 8);
            ne_generate(t);
        }
        pk = render(bars, 0);
        hard = ne_acid()->d_mood;
        for (t = 0; t < NE_TRACKS; t++)
            ne_tset(t, T_MUTE, 1);
        render(bars, 0);
        quiet = ne_acid()->d_mood;
        printf("303 Deviance: the drums' intensity %.2f as they start, %.2f hard, %.2f muted\n", (double)calm, (double)hard,
               (double)quiet);
        CHECK(calm > 0.15f && calm < 0.5f && hard > 0.85f && quiet < 0.05f && pk <= 1.0f,
              "303 Deviance: intensity %f %f %f, peak %f", (double)calm, (double)hard, (double)quiet, (double)pk);
    }
    /* Deviance 90..100: the sound wanders off. After 8 bars: at 89 and muted, its base as drawn; at 90 a
     * little way; at 100 far (gone from its kind in about that) */
    {
        static nfm_slot was[NE_TRACKS];
        float d[NE_TRACKS];
        static const int DEV[NE_TRACKS] = {89, 90, 100, 100};
        ne_init();
        for (t = 0; t < NE_TRACKS; t++) {
            ne_tset(t, T_DEV, DEV[t]);
            was[t] = ne_fm()->slot[t];
        }
        ne_tset(3, T_MUTE, 1);
        ne_play(1);
        render((int)(8.0f * 4.0f * 60.0f / 112.0f * NE_SR / 256.0f), 0);
        for (t = 0; t < NE_TRACKS; t++) {
            float e = 0.0f;
            for (i = 0; i < NFM_PARAMS; i++) {
                float x = ne_fm()->slot[t].p[i] - was[t].p[i];
                if (i != NFM_LEVEL)
                    e += x * x;
            }
            d[t] = e;
        }
        for (t = 0; t < NE_TRACKS; t++) {           /* (the spread per parameter) */
            float x = d[t] / (float)(NFM_PARAMS - 1), r = x > 0.0f ? x : 0.0f;
            for (i = 0; i < 20 && x > 0.0f; i++)
                r = 0.5f * (r + x / r);
            d[t] = r;
        }
        printf("Deviance after 8 bars, the base moved: %.3f at 89, %.3f at 90, %.3f at 100, %.3f muted\n", (double)d[0],
               (double)d[1], (double)d[2], (double)d[3]);
        CHECK(d[0] == 0.0f && d[3] == 0.0f && d[1] > 0.03f && d[1] < 0.12f && d[2] > 0.18f && d[2] > 2.5f * d[1],
              "Deviance wander: %f %f %f %f", (double)d[0], (double)d[1], (double)d[2], (double)d[3]);
    }
    /* triplets: a track moved into them keeps its hits where they were in time (the offbeat eighth to
     * the third triplet), the same bars; back again, as it was; they play three to a quarter, evenly,
     * unswung, the acid line too */
    {
        static const int16_t HIT[] = {0, 2, 3, 4, 6, 17};    /* sixteenths: 3 joins 2 (the louder stays) */
        const nseq_track *tr = &ne_seq()->track[0];
        int on[NE_STEPS], n = 0, ok = 1;
        uint32_t f, first = 0, prev = 0, clk;
        int hits = 0, bad = 0, alast, asteps = 0, abad = 0;
        uint32_t aprev = 0;
        ne_init();
        ne_tset(0, T_RANDLEN, 0);
        ne_tset(0, T_BARS, 2);
        render(1, 0);
        for (i = 0; i < NE_STEPS; i++)
            ne_step(0, i, 0, 100, 0);
        for (i = 0; i < (int)(sizeof HIT / sizeof HIT[0]); i++)
            ne_step(0, HIT[i], 1, HIT[i] == 3 ? 120 : 90, 0);
        ne_regrid(0, 1);
        render(1, 0);
        for (i = 0; i < NE_STEPS; i++)
            if (tr->step[i].on)
                on[n++] = i;
        CHECK(tr->triplet && tr->length == 24 && n == 5 && on[0] == 0 && on[1] == 2 && on[2] == 3 && on[3] == 5 &&
                  on[4] == 13 && tr->step[2].vel == 120,
              "triplets: length %d, %d hits (%d %d %d %d %d)", tr->length, n, on[0], on[1], on[2], on[3], on[4]);
        ne_regrid(0, 0);
        render(1, 0);
        for (i = 0; i < NE_STEPS; i++)
            if (tr->step[i].on != (i == 0 || i == 2 || i == 4 || i == 6 || i == 17))
                ok = 0;
        CHECK(!tr->triplet && tr->length == 32 && ok, "triplets and back: length %d, hits %s", tr->length, ok ? "ok" : "moved");
        /* every step of a bar of triplets, alone; Loose 0, Swing at full (not for them) */
        ne_set(G_TEMPO, 120);
        ne_set(G_LOOSE, 0);
        ne_set(G_SWING, 100);
        ne_set(G_CHAOS, 0);
        for (t = 1; t < NE_TRACKS; t++)
            ne_tset(t, T_MUTE, 1);
        ne_regrid(0, 1);
        ne_tset(0, T_BARS, 1);
        ne_regrid(NE_ACID, 1);
        ne_aset(A_BARS, 1);
        render(1, 0);
        CHECK(tr->length == 12 && ne_acid()->length == 12 && ne_acid()->bar == 12, "triplets: a bar is %d steps, the acid's %d",
              tr->length, ne_acid()->length);
        for (i = 0; i < 12; i++)
            ne_step(0, i, 1, 100, 0);
        ne_play(1);
        clk = ne_fm()->clock;
        alast = ne_acid()->last;
        for (f = 0; f < 176400u; f++) {            /* two bars at 120: a triplet every 7350 frames */
            ne_render(blk, 1, 4096);
            if (ne_fm()->clock != clk) {
                clk = ne_fm()->clock;
                if (!hits)
                    first = f;
                else if (f - prev < 7348u || f - prev > 7352u)
                    bad++;
                prev = f;
                hits++;
            }
            if (ne_acid()->last != alast) {         /* the acid line's step: when it plays (a note or a rest) */
                alast = ne_acid()->last;
                if (asteps && (ne_acid()->at - aprev < 7348u || ne_acid()->at - aprev > 7352u))
                    abad++;
                aprev = ne_acid()->at;
                asteps++;
            }
        }
        printf("triplets: %d hits in two bars, %d off the grid; the acid line %d steps, %d off\n", hits, bad, asteps, abad);
        CHECK(hits == 24 && bad == 0 && first < 2u, "triplets: %d hits, %d off the grid, the first at %u", hits, bad,
              (unsigned)first);
        CHECK(asteps == 24 && abad == 0, "acid triplets: %d steps, %d off the grid", asteps, abad);
        /* a new pattern in triplets: its hits within its length, on the grid's steps */
        ne_tset(0, T_HITLO, 6);
        ne_tset(0, T_HITHI, 6);
        ne_tset(0, T_BARS, 3);
        ne_generate(0);
        ne_generate(NE_ACID);
        render(1, 0);
        for (i = n = 0; i < NE_STEPS; i++)
            n += tr->step[i].on && i < tr->length;
        for (i = asteps = 0; i < NE_ASTEPS; i++)
            asteps += (ne_acid()->step[i].flags & NACID_ON) && i >= 4 * 12;   /* (four bars drawn, as in 16ths) */
        CHECK(tr->length == 36 && n == 6 && asteps == 0, "triplets: a new pattern %d steps, %d hits; acid notes past four bars %d",
              tr->length, n, asteps);
    }
    /* two strengths, then Dynamics: at 0 each new hit is strong (127) or soft (NSEQ_VEL_SOFT), strong
     * on 85 % of the quarters and 45 % of the rest; at 100 a strong one stays over the split, a soft one
     * goes down to 20 */
    {
        int nb = 0, no = 0, sbn = 0, son = 0, two = 1, lo = 127, strong_lo = 127, d;
        const nseq_track *tr = &ne_seq()->track[1];
        ne_init();
        ne_tset(1, T_RANDLEN, 0);
        ne_tset(1, T_BARS, 8);
        ne_tset(1, T_HITHI, 64);
        ne_tset(1, T_HITLO, 64);
        for (d = 0; d < 2; d++) {
            ne_tset(1, T_DYN, d ? 100 : 0);
            for (i = 0; i < 60; i++) {
                int s2;
                ne_generate(1);
                render(1, 0);
                for (s2 = 0; s2 < tr->length; s2++) {
                    int v = tr->step[s2].vel;
                    if (!tr->step[s2].on)
                        continue;
                    if (d) {
                        lo = v < lo ? v : lo;
                        if (v > NSEQ_VEL_SPLIT && v < strong_lo)
                            strong_lo = v;
                        continue;
                    }
                    two &= v == 127 || v == NSEQ_VEL_SOFT;
                    if (s2 % 4 == 0)
                        nb++, sbn += v == 127;
                    else
                        no++, son += v == 127;
                }
            }
        }
        printf("two strengths: strong on %d%% of the quarters, %d%% of the rest%s; at Dynamics 100 the softest %d, "
               "the softest strong %d\n", 100 * sbn / (nb ? nb : 1), 100 * son / (no ? no : 1),
               two ? "" : " (other values at 0!)", lo, strong_lo);
        CHECK(two && sbn * 100 > nb * 78 && sbn * 100 < nb * 92 && son * 100 > no * 37 && son * 100 < no * 53 &&
                  lo >= 20 && lo < 32 && strong_lo >= 95,
              "two strengths: %d of %d quarters strong, %d of %d others, flat %d, softest %d, softest strong %d", sbn, nb,
              son, no, two, lo, strong_lo);
    }
    /* saved and restored: the same */
    ne_snapshot(&a);
    ne_init();
    ne_restore(&a);
    ne_snapshot(&b);
    CHECK(!memcmp(&a, &b, sizeof a), "restore differs from the snapshot");
    printf(fails ? "n001se: %d FAILED\n" : "n001se: all ok\n", fails);
    return fails != 0;
}

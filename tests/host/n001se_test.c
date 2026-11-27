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
    /* every kind, many times: a sound that blows up or stays silent fails (Gen: below) */
    for (k = 0; k < NFM_KIND_GEN; k++)
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
    /* a rumble: low (next to no highs: its low-pass, below 900 Hz), at the kick's
     * pitch, and ducked by a kick, whichever of the two a step plays first */
    {
        static nfm_engine re;
        static float rl[64], rr[64];
        int d, low_ok = 0, duck_ok = 0, same_ok = 0, pitch_ok = 0;
        nfm_init(&re, NE_SR, 7u);                 /* (slot 0: a kick) */
        for (d = 0; d < 20; d++) {
            int j, v, kv;
            float lo = 0.0f, hi = 0.0f, x1 = 0.0f, x2 = 0.0f, h1, before = 0.0f, after = 0.0f;
            nfm_randomize(&re, 3, NFM_KIND_RUMBLE);
            pitch_ok += re.slot[3].p[NFM_PITCH] > re.slot[0].p[NFM_PITCH] - 0.005f &&
                        re.slot[3].p[NFM_PITCH] < re.slot[0].p[NFM_PITCH] + 0.005f;
            v = nfm_trigger(&re, 3, 1.0f, 0.0f);
            for (j = 0; j < (int)(0.2f * NE_SR) / 64; j++) {
                int i;
                nfm_render(&re, rl, rr, 64);
                for (i = 0; i < 64; i++) {       /* its second difference: the highs, +12 dB an octave */
                    h1 = rl[i] - 2.0f * x1 + x2;
                    x2 = x1;
                    x1 = rl[i];
                    lo += rl[i] * rl[i];
                    hi += h1 * h1;
                    if (j >= (int)(0.15f * NE_SR) / 64) before += rl[i] * rl[i];
                }
            }
            low_ok += hi < lo * 1e-6f;              /* (low-passed at 8 kHz instead: 7e-6 and up) */
            kv = nfm_trigger(&re, 0, 1.0f, 0.0f);
            for (j = 0; j < 10; j++) {            /* 10..15 ms on: the rumble alone, ducked */
                int i;
                re.voice[kv].gain_l = 0.0f;
                nfm_render(&re, rl, rr, 64);
                if (j >= 7)
                    for (i = 0; i < 64; i++) after += rl[i] * rl[i];
            }
            duck_ok += re.voice[v].slot == 3 && after < before * (192.0f / (0.05f * NE_SR)) * 0.05f;
            nfm_stop_all(&re);
            for (j = 0; j < 40; j++) nfm_render(&re, rl, rr, 64);
            kv = nfm_trigger(&re, 0, 1.0f, 0.0f);     /* the kick first, the rumble after it */
            v = nfm_trigger(&re, 3, 1.0f, 0.0f);
            same_ok += re.voice[v].r_duck == 0.0f;
            (void)kv;
            nfm_stop_all(&re);
            for (j = 0; j < 40; j++) nfm_render(&re, rl, rr, 64);
        }
        CHECK(pitch_ok == 20, "rumble: %d of 20 draws at the kick's pitch", pitch_ok);
        CHECK(low_ok == 20, "rumble: %d of 20 draws low", low_ok);
        CHECK(duck_ok == 20, "rumble: %d of 20 ducked by a kick", duck_ok);
        CHECK(same_ok == 20, "rumble: %d of 20 ducked by a kick on its step", same_ok);
        /* and no clicks: a rumble on every sixteenth but the beats, cut by the next (choke), a kick on
         * every beat ducking it (heard alone): its largest second difference stays near its own (no
         * jump). Cut in 12 ms and ducked at once it was 0.6..1.3 x its RMS; now about 0.02 */
        for (d = 1; d <= 6; d++) {
            int st, j, i, step = (int)(NE_SR * 60.0f / 132.0f / 4.0f), t = 0;
            float x1 = 0.0f, x2 = 0.0f, mx = 0.0f, ss = 0.0f;
            nfm_init(&re, NE_SR, (uint32_t)d);
            nfm_randomize(&re, 3, NFM_KIND_RUMBLE);
            for (st = 0; st < 32; st++) {
                if (st % 4 == 0) {
                    int kv = nfm_trigger(&re, 0, 1.0f, 0.0f);
                    re.voice[kv].gain_l = 0.0f;
                } else
                    nfm_trigger(&re, 3, 1.0f, 0.0f);
                for (j = 0; j < step; j += 64) {
                    nfm_render(&re, rl, rr, 64);
                    for (i = 0; i < 64; i++, t++) {
                        float dd = rl[i] - 2.0f * x1 + x2;
                        x2 = x1;
                        x1 = rl[i];
                        if (t < 2000) continue;
                        dd = dd < 0.0f ? -dd : dd;
                        if (dd > mx) mx = dd;
                        ss += rl[i] * rl[i];
                    }
                }
            }
            ss /= (float)(t - 2000);
            CHECK(mx * mx < ss * 0.1f * 0.1f, "rumble %d: a click, (its jump / RMS)^2 %f", d, (double)(mx * mx / (ss > 0.0f ? ss : 1.0f)));
        }
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
    /* Fill uses the length chosen for this pattern, including shortened random bars. */
    for (int random = 0; random <= 1; random++) {
        const nseq_track *tr = &ne_seq()->track[0];
        ne_tset(0, T_RANDLEN, random ? 100 : 0);
        for (int bars = 1; bars <= 2; bars++) {
            ne_tset(0, T_BARS, bars);
            for (int fill = 0; fill <= 100; fill += 50) {
                ne_tset(0, T_HITFILL, fill);
                for (int draw = 0; draw < 16; draw++) {
                    int n = 0;
                    ne_generate(0);
                    render(1, 0);
                    for (i = 0; i < tr->length; i++) n += tr->step[i].on;
                    int hi = (tr->length * fill + 50) / 100;
                    CHECK(random || tr->length == bars * NE_BAR, "fixed length %d steps", tr->length);
                    CHECK(n == hi, "fill %d: %d hits in %d steps, expected %d", fill, n, tr->length, hi);
                }
            }
        }
    }
    /* Fill turned: the hits follow, live. A pattern by hand that does not match the knob counts as if it
     * did: four quarters at Fill 80, turned up, fill the other twelve in proportion (81..100), turned
     * down nothing goes before 25 %; one busier than the knob the mirror. Set as a restore sets it
     * (ne_tset), the hits stay. */
    {
        const nseq_track *tr = &ne_seq()->track[0];
        static const struct { int fill, hits, to, want; } L[] = {
            {80, 4, 81, 5}, {80, 4, 90, 10}, {80, 4, 100, 16}, {80, 4, 79, 4}, {80, 4, 30, 4}, {80, 4, 25, 4},
            {80, 4, 20, 3}, {80, 4, 0, 0}, {20, 12, 50, 12}, {20, 12, 78, 12}, {20, 12, 79, 13}, {20, 12, 19, 11},
            {20, 12, 10, 6}, {25, 4, 75, 12}, {25, 4, 0, 0},
        };
        for (unsigned l = 0; l < sizeof L / sizeof L[0]; l++) {
            int n = 0, quarters = 0;
            ne_init();
            ne_tset(0, T_RANDLEN, 0);
            ne_tset(0, T_BARS, 1);
            ne_tset(0, T_HITFILL, L[l].fill);
            for (i = 0; i < NE_BAR; i++)
                ne_step(0, i, L[l].hits == 4 ? i % 4 == 0 : i % 4 != 3, 100, 0);
            render(1, 0);
            ne_density(0, L[l].to);
            render(1, 0);
            for (i = 0; i < tr->length; i++) n += tr->step[i].on, quarters += tr->step[i].on && i % 4 == 0;
            CHECK(n == L[l].want && tr->hits_fill == L[l].to, "Fill %d with %d hits by hand, turned to %d: %d hits, expected %d",
                  L[l].fill, L[l].hits, L[l].to, n, L[l].want);
            CHECK(L[l].hits != 4 || L[l].want < 4 || quarters == 4, "Fill turned: the hand's quarters went (%d left)", quarters);
            /* ...and back: exactly the hand's pattern again, whichever way it went */
            ne_density(0, L[l].fill);
            render(1, 0);
            for (i = n = quarters = 0; i < tr->length; i++)
                n += tr->step[i].on, quarters += tr->step[i].on != (L[l].hits == 4 ? i % 4 == 0 : i % 4 != 3);
            CHECK(n == L[l].hits && quarters == 0, "Fill %d -> %d -> back: %d hits, %d not the hand's", L[l].fill, L[l].to, n,
                  quarters);
            ne_tset(0, T_HITFILL, 100);
            render(1, 0);
            for (i = 0, quarters = n; i < tr->length; i++) quarters -= tr->step[i].on;
            CHECK(quarters == 0, "Fill set as a restore moved the hits (%d)", quarters);
        }
        /* a generated pattern matches its Fill: turned, it follows the % exactly, both ways */
        ne_init();
        ne_tset(0, T_RANDLEN, 0);
        ne_tset(0, T_BARS, 2);
        ne_tset(0, T_HITFILL, 50);
        ne_generate(0);
        for (int k = 0; k < 6; k++) {
            int n = 0;
            int f = (int[]){75, 60, 25, 0, 100, 40}[k];
            ne_density(0, f);
            render(1, 0);
            for (i = 0; i < tr->length; i++) n += tr->step[i].on;
            CHECK(n == (32 * f + 50) / 100, "a generated pattern turned to Fill %d: %d hits", f, n);
        }
        /* up and down, down and up, any way round: back at the start, the start's pattern */
        {
            static const int R[3][5] = {{75, 60, 90, 55, 50}, {20, 35, 10, 45, 50}, {80, 5, 100, 0, 50}};
            uint8_t was[NE_STEPS];
            ne_tset(0, T_HITFILL, 50);
            ne_generate(0);
            render(1, 0);
            for (i = 0; i < NE_STEPS; i++) was[i] = tr->step[i].on;
            for (int r = 0; r < 3; r++) {
                int diff = 0, mid = 0;
                for (int k = 0; k < 5; k++) {
                    ne_density(0, R[r][k]);
                    render(1, 0);
                    if (k == 2) {                   /* (and the same on the way, at the same knob) */
                        ne_density(0, R[r][1]);
                        render(1, 0);
                        for (i = 0; i < tr->length; i++) mid += tr->step[i].on;
                        ne_density(0, R[r][2]);
                        render(1, 0);
                    }
                }
                for (i = 0; i < tr->length; i++) diff += tr->step[i].on != was[i];
                CHECK(diff == 0 && mid == (32 * R[r][1] + 50) / 100, "Fill turned %d %d %d %d back to 50: %d steps not the start's",
                      R[r][0], R[r][1], R[r][2], R[r][3], diff);
            }
        }
        /* longer at once, the new bars copies of the first: 1 bar to 3 */
        ne_tset(0, T_BARS, 1);
        ne_generate(0);
        render(1, 0);
        ne_tset(0, T_BARS, 3);
        render(1, 0);
        {
            int same = 1;
            for (i = NE_BAR; i < 3 * NE_BAR; i++)
                same &= tr->step[i].on == tr->step[i % NE_BAR].on && (!tr->step[i].on || tr->step[i].vel == tr->step[i % NE_BAR].vel);
            CHECK(tr->length == 48 && same, "Bars 1 to 3: %d steps, the new bars %s", tr->length, same ? "copies" : "not copies");
        }
        /* the 303: Notes turned, the notes follow (the rests keep their pitches); longer, bar 1 again */
        {
            const nacid *a = ne_acid();
            int n, want;
            ne_aset(A_BARS, 1);
            ne_aset(A_NOTES, 60);
            ne_generate(NE_ACID);
            render(1, 0);
            for (int k = 0; k < 4; k++) {
                int f = (int[]){80, 100, 30, 0}[k];
                ne_density(NE_ACID, f);
                render(1, 0);
                for (i = n = 0; i < a->length; i++) n += !!(a->step[i].flags & NACID_ON);
                want = (16 * f + 50) / 100;
                CHECK(n == want, "303 Notes turned to %d: %d notes, expected %d", f, n, want);
            }
            ne_density(NE_ACID, 50);
            ne_aset(A_BARS, 2);
            render(1, 0);
            for (i = 16, n = 1; i < 32; i++)
                n &= a->step[i].flags == a->step[i - 16].flags && a->step[i].note == a->step[i - 16].note;
            CHECK(a->length == 32 && n, "303 Bars 1 to 2: %d steps, %s", a->length, n ? "bar 1 twice" : "not a copy");
        }
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
            ne_tset(t, T_HITFILL, 50);
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
        CHECK(d[0] == 0.0f && d[3] == 0.0f && d[1] > 0.03f && d[1] < 0.12f && d[2] > 0.15f && d[2] > 2.0f * d[1],
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
        ne_tset(0, T_HITFILL, 50);
        ne_tset(0, T_BARS, 3);
        ne_generate(0);
        ne_generate(NE_ACID);
        render(1, 0);
        for (i = n = 0; i < NE_STEPS; i++)
            n += tr->step[i].on && i < tr->length;
        for (i = asteps = 0; i < NE_ASTEPS; i++)
            asteps += (ne_acid()->step[i].flags & NACID_ON) && i >= 4 * 12;   /* (four bars drawn, as in 16ths) */
        CHECK(tr->length == 36 && n == 18 && asteps == 0, "triplets: a new pattern %d steps, %d hits; acid notes past four bars %d",
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
        ne_tset(1, T_HITFILL, 50);
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
    /* the modulation effects on a low sine, loud and 50 dB down: smooth (no 2.8 kHz gain staircase, no
     * coarse steps in the delay line: what sounded like a bitcrusher) and as loud as what went in */
    {
        static int16_t ml[NFM_MOD_LEN];
        static const float amp[2] = { 0.3f, 0.001f };
        static const char *const nm[NFM_MODS] = { "", "phaser", "flanger", "chorus", "ring", "" };
        int ty, q;
        for (ty = NFM_MOD_PHASER; ty <= NFM_MOD_RING; ty++)
            for (q = 0; q < 2; q++) {
                nfm_mod m;
                nfm_mod_state st;
                float c = 0.0f, sn = amp[q], y1 = 0.0f, y2 = 0.0f;
                const float w = 6.2831853f * 40.0f / NE_SR;
                double ein = 0.0, eout = 0.0, ed = 0.0;
                int bl, j;
                memset(&m, 0, sizeof m);
                memset(&st, 0, sizeof st);
                m.type = (uint8_t)ty;
                m.amount = 0.7f;
                m.rate = 0.3f;
                m.chance = 1.0f;
                st.line = ml;
                nfm_mod_reset(&st);
                c = amp[q], sn = 0.0f;
                for (bl = 0; bl < 690; bl++) {     /* 2 s */
                    nfm_mod_block(&m, NE_SR, 128);
                    for (j = 0; j < 128; j++) {
                        float x = sn, y;
                        float nc = c - w * sn;     /* a rotation: the sine without libm */
                        sn += w * nc;
                        c = nc;
                        y = nfm_mod_tick(&m, &st, x, j);
                        if (bl >= 345) {
                            float d = y - 2.0f * y1 + y2;
                            ein += (double)(x * x), eout += (double)(y * y), ed += (double)(d * d);
                        }
                        y2 = y1, y1 = y;
                    }
                }
                /* the second difference against the signal: a clean 40 Hz is 3e-9; the stepped gain made 4e-7..3e-5,
                 * the int16 line 50 dB down 6e-4 (flanger) and 2e-3 (chorus). The level within
                 * the match's 0.25..4 (the chorus's 12 ms is near half a 40 Hz wave: it nearly cancels) */
                CHECK(ed < eout * 1.6e-7 && eout > ein * 0.25 && eout < ein * 4.0,
                      "%s at %g: rough %.1e of the level, out/in %.2f", nm[ty], (double)amp[q], ed / eout, eout / ein);
            }
    }
    /* back to Fixed: a track that drew an odd length plays its Bars again, at once and with each new
     * pattern; a restored odd length (a project, an undo) stands until the next new pattern, which plays
     * the Bars noted with it */
    {
        const nseq_track *tr = &ne_seq()->track[2];
        int odd = 0, back, keep, gen, tries;
        ne_init();
        ne_tset(2, T_BARS, 1);
        ne_tset(2, T_RANDLEN, 100);
        for (tries = 0; tries < 50 && !odd; tries++) {
            ne_generate(2);
            render(1, 0);
            odd = tr->length % 16 != 0;
        }
        ne_tset(2, T_RANDLEN, 0);
        render(1, 0);
        back = tr->length;
        ne_snapshot(&a);
        a.len[2] = 29;
        ne_restore(&a);
        ne_tset_keep(2, T_BARS, 2);
        render(1, 0);
        keep = tr->length;
        ne_generate(2);
        render(1, 0);
        gen = tr->length;
        CHECK(odd && back == 16 && keep == 29 && gen == 32, "Fixed: drew odd %d, back %d, kept %d, new pattern %d",
              odd, back, keep, gen);
    }
    /* LOW-PASS on a drum track: a hat through it at 100 keeps little of its peak; open again, all */
    {
        float open, shut, back;
        ne_init();
        ne_new_sound(2, NFM_KIND_HAT);
        ne_tset(2, T_DEV, 0);                    /* (every hit the same, dry) */
        ne_tset(2, T_REVERB, 0);
        ne_tset(2, T_DELAY, 0);
        ne_tset(2, T_FILTER, 0);
        ne_tset(2, T_MODCHANCE, 0);
        render(20, 0);
        ne_hit(2, 127, 0);
        open = render(20, 0);
        ne_tset(2, T_LPF, 100);
        render(40, 0);
        ne_hit(2, 127, 0);
        shut = render(20, 0);
        ne_tset(2, T_LPF, 0);
        render(40, 0);
        ne_hit(2, 127, 0);
        back = render(20, 0);
        printf("low-pass: a hat's peak %.3f open, %.3f at 100, %.3f open again\n", (double)open, (double)shut, (double)back);
        CHECK(shut < 0.3f * open && back > 0.8f * open, "low-pass: %f %f %f", (double)open, (double)shut, (double)back);
    }
    /* GEN: a track of kind Gen plays ngen, its steps as energy; one at a time; muted, it rings out; a bar
     * and more without steps, it goes quiet; its material made by ne_work */
    {
        int s, b, made = 0;
        float lv_on = 0.0f, lv_pre = 0.0f, lv_mute = 0.0f, lv_gone = 1.0f, lv_quiet = 1.0f, lv_back = 0.0f;
        ne_init();
        ne_set(G_TEMPO, 120);
        ne_new_sound(3, NFM_KIND_GEN);
        ne_tset(3, T_BARS, 1);
        for (s = 0; s < 16; s++)
            ne_step(3, s, s % 4 == 0 || s == 14, s % 8 ? 70 : 120, 0);
        ne_play(1);
        for (b = 0; b < 500; b++) {              /* (~2.9 s, a bar is 2 s: 344 blocks) */
            render(1, 0);
            ne_work();
            if (b > 300 && ne_track_level[3] > lv_on)
                lv_on = ne_track_level[3];
        }
        made = ne_gen_track();
        {   /* its low-pass (T_LPF): at 100 most of it is gone */
            float lv_f = 0.0f;
            ne_tset(3, T_LPF, 100);
            render(40, 0);
            for (b = 0; b < 300; b++) {
                render(1, 0);
                ne_work();
                if (ne_track_level[3] > lv_f)
                    lv_f = ne_track_level[3];
            }
            printf("Gen: its low-pass at 100, level %.3f (open %.3f)\n", (double)lv_f, (double)lv_on);
            CHECK(lv_f < 0.4f * lv_on, "Gen: low-pass %f, open %f", (double)lv_f, (double)lv_on);
            ne_tset(3, T_LPF, 0);
        }
        ne_new_sound(1, NFM_KIND_GEN);           /* a second one: the first plays on */
        render(4, 0);
        CHECK(made == 3 && ne_gen_track() == 3, "Gen: on track %d, then %d", made, ne_gen_track());
        ne_new_sound(1, NFM_KIND_SNARE);
        for (b = 0; b < 20; b++) {
            render(1, 0);
            if (ne_track_level[3] > lv_pre) lv_pre = ne_track_level[3];
        }
        ne_tset(3, T_MUTE, 1);                   /* muted it rings out: there a moment after, then gone */
        render(10, 0);
        for (b = 0; b < 20; b++) {               /* 60 .. 170 ms */
            render(1, 0);
            if (ne_track_level[3] > lv_mute) lv_mute = ne_track_level[3];
        }
        render(700, 0);                          /* 4 s */
        lv_gone = ne_track_level[3];
        ne_tset(3, T_MUTE, 0);
        for (b = 0; b < 200; b++) {
            render(1, 0);
            if (ne_track_level[3] > lv_back) lv_back = ne_track_level[3];
        }
        {   /* its track becomes a snare (as at another bank without it): what it plays rings out */
            float pkb, pk0, pk1;
            for (b = 0; b < 3; b++)              /* (the others silent, Gen dry: what is heard is Gen's own) */
                ne_tset(b, T_MUTE, 1);
            ne_aset(A_MUTE, 1);
            ne_tset(3, T_REVERB, 0);
            ne_tset(3, T_DELAY, 0);
            render(400, 0);
            pkb = render(16, 0);
            ne_new_sound(3, NFM_KIND_SNARE);
            ne_tset(3, T_MUTE, 1);
            render(40, 0);
            pk0 = render(16, 0);                 /* (0.23 .. 0.33 s) */
            render(1000, 0);
            pk1 = render(16, 0);
            printf("Gen: its track gone, %.4f before, %.4f 0.3 s after, %.5f 6 s on\n", (double)pkb, (double)pk0, (double)pk1);
            CHECK(ne_gen_track() == -1 && pk0 > 0.25f * pkb && pk1 < 0.0005f, "Gen: tail %f, then %f (track %d)",
                  (double)pk0, (double)pk1, ne_gen_track());
            for (b = 0; b < NE_TRACKS; b++)
                ne_tset(b, T_MUTE, 0);
            ne_aset(A_MUTE, 0);
            ne_new_sound(3, NFM_KIND_GEN);
            ne_tset(3, T_BARS, 1);
            for (s = 0; s < 16; s++)
                ne_step(3, s, s % 4 == 0 || s == 14, s % 8 ? 70 : 120, 0);
            for (b = 0; b < 400; b++) {
                render(1, 0);
                ne_work();
            }
        }
        ne_clear_track(3);
        for (b = 0; b < 1400; b++) {             /* 4 bars without a step */
            render(1, 0);
            ne_work();
        }
        lv_quiet = ne_track_level[3];
        printf("Gen: level %.3f playing, %.3f before the mute, %.3f muted 0.1 s, %.5f 4 s, %.3f back, %.5f after 4 empty bars\n",
               (double)lv_on, (double)lv_pre, (double)lv_mute, (double)lv_gone, (double)lv_back, (double)lv_quiet);
        CHECK(lv_on > 0.03f && lv_on < 1.0f, "Gen: level %f", (double)lv_on);
        CHECK(lv_mute > 0.4f * lv_pre && lv_gone < 0.002f && lv_back > 0.03f, "Gen: muted %f, then %f, back %f",
              (double)lv_mute, (double)lv_gone, (double)lv_back);
        CHECK(lv_quiet < 0.002f, "Gen: no steps %f", (double)lv_quiet);
        ne_play(0);
        ne_init();
    }
    /* Rnd len %: 0, every new pattern its Bars; 100, by chance (1..8 bars, sometimes a bar cut short);
     * 50, about half of them (some draws come out as the Bars anyway) */
    {
        const nseq_track *tr = &ne_seq()->track[1];
        int pc, k, other[3];
        static const int PC[3] = {0, 50, 100};
        ne_init();
        ne_tset(1, T_BARS, 2);
        for (pc = 0; pc < 3; pc++) {
            ne_tset(1, T_RANDLEN, PC[pc]);
            for (other[pc] = 0, k = 0; k < 200; k++) {
                ne_generate(1);
                render(1, 0);
                other[pc] += tr->length != 2 * NE_BAR;
            }
        }
        printf("Rnd len: lengths not the Bars in 200 new patterns: %d at 0 %%, %d at 50 %%, %d at 100 %%\n", other[0],
               other[1], other[2]);
        CHECK(other[0] == 0 && other[1] > 60 && other[1] < 110 && other[2] > 130,
              "Rnd len: %d %d %d of 200 not the Bars", other[0], other[1], other[2]);
    }
    /* One clock: a 1-bar track and a 2-bar one shortened to 1 bar in its second bar, a pattern restored
     * mid-bar, a grid switched there and back: still on track 1's sixteenth. SYNC: a 13-step track back on
     * track 1's loop, track 1 going on as it was (its place, its hits) */
    {
        const nseq *q = ne_seq();
        int off = 0, k, was, step_was, syncd, steady = 1;
        ne_init();
        for (t = 0; t < NE_TRACKS; t++) {
            ne_tset(t, T_RANDLEN, 0);
            ne_tset(t, T_BARS, t == 1 ? 2 : 1);
        }
        ne_play(1);
        while (q->steps < 16u + 6u)
            render(1, 0);
        ne_tset(1, T_BARS, 1);                    /* (it used to start its bar again here) */
        render(1, 0);
        ne_snapshot(&a);
        ne_restore(&a);
        ne_regrid(2, 1);
        ne_regrid(2, 0);
        for (k = 0; k < 200; k++) {
            render(1, 0);
            for (t = 1; t < NE_TRACKS; t++)
                off += q->track[t].last % 16 != q->track[0].last % 16;
        }
        ne_tset(3, T_BARS, 1);
        render(1, 0);
        ne_snapshot(&a);
        a.len[3] = 13;
        ne_restore(&a);
        render(150, 0);
        was = q->track[0].last;
        step_was = (int)q->steps;
        ne_sync(q->steps);
        while ((int)q->steps == step_was)
            render(1, 0);
        syncd = q->track[3].last == (int)((uint32_t)q->track[0].last % 13u);
        for (k = 0; k < 100; k++) {                 /* track 1 as before: one step a step, from where it was */
            render(1, 0);
            steady &= q->track[0].last == (int)((uint32_t)(was + (int)q->steps - step_was) % 16u);
        }
        printf("one clock: %d of %d steps off track 1; Sync: the 13-step track on track 1's loop %d, track 1 "
               "steady %d\n", off, 600, syncd, steady);
        CHECK(off == 0 && syncd && steady, "one clock: %d off, sync %d, steady %d", off, syncd, steady);
    }
    /* a change of bank while playing: what sounds rings on (track 1's kick not cut) */
    {
        int before, after;
        ne_init();
        ne_play(1);
        do
            render(1, 0);
        while (nfm_active_voices(ne_fm()) == 0);
        ne_snapshot(&a);
        before = nfm_active_voices(ne_fm());
        ne_restore(&a);
        after = 0;
        for (k = 0; k < NFM_VOICES; k++)
            after += ne_fm()->voice[k].slot >= 0 && !ne_fm()->voice[k].fading;
        printf("a change of bank while playing: %d voices sounding, %d still after\n", before, after);
        CHECK(before > 0 && after == before, "restore while playing: %d voices, %d after", before, after);
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

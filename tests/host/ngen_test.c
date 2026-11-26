/* SPDX-License-Identifier: GPL-3.0-only */
/* ngen: the energy rules, the plan, click-free hand-overs, starvation, stereo, determinism.
 * Writes demos to build/host/ngen-*.wav. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ngen.h"

#define SR 44100
static int16_t mem_a[NGEN_MEM_WORDS], mem_b[NGEN_MEM_WORDS];
static ngen ga, gb;

typedef struct {
    float *l, *r;
    int n, cap;
    int work;          /* background pieces per 64-frame block (0: starved) */
    uint32_t step_len;
    int step;
    uint32_t t;        /* samples into the current step */
    int swap;          /* frame where a new set first took over (-1: none) */
} run_t;

static float buf_l[SR * 40], buf_r[SR * 40];

/* n frames: steps from the pattern's clock, background work between blocks */
static void run(ngen *g, run_t *s, int frames)
{
    while (frames > 0) {
        int m = frames > 64 ? 64 : frames, i, k;
        if (s->t + (uint32_t)m > s->step_len)
            m = (int)(s->step_len - s->t);
        if (s->t == 0)
            ngen_step(g, s->step, s->step_len);
        assert(s->n + m <= s->cap);
        for (i = 0; i < m; i++)
            s->l[s->n + i] = s->r[s->n + i] = 0.0f;
        {
            uint8_t was = g->cur;
            ngen_render(g, s->l + s->n, s->r + s->n, m);
            if (g->cur != was && s->swap < 0)
                s->swap = s->n;                      /* where a new set took over */
        }
        s->n += m;
        s->t += (uint32_t)m;
        if (s->t >= s->step_len) {
            s->t = 0;
            s->step++;
        }
        for (k = 0; k < s->work; k++)
            ngen_work(g);
        frames -= m;
    }
}

static run_t start(float bpm, int work)
{
    run_t s;
    memset(&s, 0, sizeof s);
    s.l = buf_l;
    s.r = buf_r;
    s.cap = SR * 40;
    s.work = work;
    s.step_len = (uint32_t)(SR * 60.0 / bpm / 4.0 + 0.5);
    s.swap = -1;
    return s;
}

/* the set made before playing, as the firmware does while stopped */
static void prime(ngen *g)
{
    int k = 0;
    while (ngen_work(g))
        assert(++k < 100000);
}

static double rms(const float *x, int a, int b)
{
    double s = 0;
    int i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return b > a ? sqrt(s / (b - a)) : 0.0;
}

/* how bright: the slope's power against the signal's (rises with the share of high partials) */
static double bright(const float *x, int a, int b)
{
    double d = 0, e = 0;
    int i;
    for (i = a + 1; i < b; i++) {
        d += ((double)x[i] - x[i - 1]) * ((double)x[i] - x[i - 1]);
        e += (double)x[i] * x[i];
    }
    return e > 0 ? sqrt(d / e) : 0.0;
}

static void wav(const char *name, const run_t *s)
{
    char path[256];
    FILE *f;
    uint32_t n = (uint32_t)s->n, bytes = n * 4u, v;
    uint16_t w;
    int i;
    snprintf(path, sizeof path, "build/host/%s", name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    w = 1; fwrite(&w, 2, 1, f); w = 2; fwrite(&w, 2, 1, f);
    v = SR; fwrite(&v, 4, 1, f); v = SR * 4u; fwrite(&v, 4, 1, f);
    w = 4; fwrite(&w, 2, 1, f); w = 16; fwrite(&w, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (i = 0; i < s->n; i++) {
        float a = s->l[i] * 1.4f, b = s->r[i] * 1.4f;
        int16_t x = (int16_t)(a > 1 ? 32767 : a < -1 ? -32767 : a * 32767);
        int16_t y = (int16_t)(b > 1 ? 32767 : b < -1 ? -32767 : b * 32767);
        fwrite(&x, 2, 1, f); fwrite(&y, 2, 1, f);
    }
    fclose(f);
    printf("  wrote %s (%.1f s)\n", path, (double)s->n / SR);
}

/* the spec's example: 50 0 0 0 | 0 0 0 0 | 0 0 0 0 | 100 100 100 100 */
static const uint8_t EXAMPLE[16] = {64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 127, 127, 127};

static void test_example(void)
{
    run_t s = start(120.0f, 4);
    double early, late;
    int bar;
    ngen_init(&ga, mem_a, SR, 1234u);
    ngen_pattern(&ga, EXAMPLE, 16);
    prime(&ga);
    run(&ga, &s, (int)s.step_len * 16 * 8);
    bar = (int)s.step_len * 16;
    /* the last bar played: its middle (after the last climax has died down) calm, its last quarter full */
    early = rms(s.l, 7 * bar + 4 * (int)s.step_len, 7 * bar + 12 * (int)s.step_len);
    late = rms(s.l, 7 * bar + 12 * (int)s.step_len, 8 * bar);
    printf("example: rms middle %.4f, last quarter %.4f\n", early, late);
    assert(late > 1.3 * early);
    {   /* intensity is more than level: the climax is brighter (more of its power in the highs) */
        double be = bright(s.l, 7 * bar + 8 * (int)s.step_len, 7 * bar + 12 * (int)s.step_len);
        double bl = bright(s.l, 7 * bar + 12 * (int)s.step_len, 8 * bar);
        printf("example: brightness %.3f -> %.3f\n", be, bl);
        assert(bl > 1.3 * be);
    }
    assert(early > 0.002);                          /* calm, not silent: the hold carries it */
    /* the plan leans the timbre up before the climax: step 10 plans higher than its own energy */
    assert(ga.plan[10] > ga.pred[10] + 0.05f);
    assert(ga.pred[15] > ga.pred[0]);
    {   /* nothing much below ~95 Hz: under 60 Hz (four poles) at least 30 dB below the whole */
        double a1 = 1.0 - exp(-2.0 * 3.14159265358979 * 60.0 / SR), y[4] = {0}, lo = 0, all = 0;
        int i, p;
        for (i = bar; i < s.n; i++) {
            double x = s.l[i];
            all += x * x;
            for (p = 0; p < 4; p++) {
                y[p] += a1 * (x - y[p]);
                x = y[p];
            }
            lo += x * x;
        }
        printf("example: below 60 Hz %.1f dB\n", 10.0 * log10(lo / all));
        assert(lo < all * 1e-3);
    }
    wav("ngen-example.wav", &s);
}

/* not silent before a bar without steps has passed, silent some time after */
static void test_hold(float bpm)
{
    uint8_t one[64];
    run_t s = start(bpm, 4);
    int bar, w, i;
    double peak = 0;
    memset(one, 0, sizeof one);
    one[0] = 127;
    ngen_init(&ga, mem_a, SR, 99u);
    ngen_pattern(&ga, one, 64);
    prime(&ga);
    run(&ga, &s, (int)s.step_len * 64);
    bar = (int)s.step_len * 16;
    w = (int)s.step_len;                            /* a sixteenth's window */
    for (i = 0; i + w <= bar; i += w) {
        double x = rms(s.l, i, i + w);
        if (x > peak)
            peak = x;
    }
    for (i = w; i + w <= bar; i += w)               /* every sixteenth of the first bar sounds */
        assert(rms(s.l, i, i + w) > peak * 0.05);
    assert(rms(s.l, 3 * bar, 4 * bar) < peak * 0.01);   /* two bars after the hold: gone */
    assert(!ngen_active(&ga));
    printf("hold at %.0f BPM: ok\n", bpm);
}

static void test_saturation(void)
{
    uint8_t all[128];
    run_t s = start(174.0f, 2);
    memset(all, 127, sizeof all);
    ngen_init(&ga, mem_a, SR, 5u);
    ngen_pattern(&ga, all, 128);
    prime(&ga);
    run(&ga, &s, (int)s.step_len * 128);
    assert(ga.e <= 1.0f && ga.e > 0.6f);
    assert(rms(s.l, 0, s.n) < 0.5);
    printf("saturation: e %.3f\n", ga.e);
}

/* A change while it plays, against the same run without it: the difference must grow in smoothly. The
 * two runs are the same up to the change (noise and all), so any jump in the difference is the change's. */
static float ref_l[SR * 8], ref_r[SR * 8];
static void handover(const char *what, void (*change)(ngen *), int at_swap)
{
    uint8_t pat[16] = {127, 0, 64, 0, 127, 0, 0, 90, 0, 127, 0, 0, 127, 64, 0, 0};
    run_t s = start(128.0f, 8), t;
    int i, n0 = -1, bar;
    double peak = 0, jump = 0;
    ngen_init(&ga, mem_a, SR, 42u);
    ngen_set_param(&ga, NGEN_NOISE, 0);             /* the tones alone: the noise's resonances follow the seed */
    ngen_pattern(&ga, pat, 16);
    prime(&ga);
    run(&ga, &s, (int)s.step_len * 32);
    bar = (int)s.step_len * 16;
    gb = ga;                                         /* the same state, on its own memory */
    gb.mem = mem_b;
    memcpy(mem_b, mem_a, sizeof mem_b);
    t = s;
    t.l = ref_l; t.r = ref_r; t.n = 0; t.cap = SR * 8;
    s.n = 0;
    s.swap = -1;
    change(&ga);
    run(&ga, &s, 2 * bar);
    run(&gb, &t, 2 * bar);
    for (i = 0; i < s.n; i++) {
        double d = fabs((double)s.l[i] - ref_l[i]) + fabs((double)s.r[i] - ref_r[i]);
        if (n0 < 0 && d > 1e-7)
            n0 = i;
        if (d > peak)
            peak = d;
    }
    assert(n0 >= 0);
    if (at_swap && s.swap >= 0)
        n0 = s.swap;                 /* (a new sound: from where its set took over; the drifts move before) */
    for (i = n0; i < n0 + 32 && i < s.n; i++) {     /* its first 32 samples: a hard switch is all there at once */
        double d = fabs((double)s.l[i] - ref_l[i]) + fabs((double)s.r[i] - ref_r[i]);
        if (d > jump)
            jump = d;
    }
    printf("handover, %s: in its first 32 samples the difference reaches %.2f %% of its peak\n", what, 100.0 * jump / peak);
    assert(jump < 0.05 * peak);
}
static void ch_sound(ngen *g) { ngen_new_sound(g, 777u); }
static void ch_key(ngen *g) { ngen_set_key(g, 50, NGEN_MAJOR); }
static void ch_complex(ngen *g) { ngen_set_param(g, NGEN_COMPLEXITY, 100); }
static void ch_spread(ngen *g) { ngen_set_param(g, NGEN_SPREAD, 10); }

static void test_handover(void)
{
    handover("new sound", ch_sound, 1);
    handover("new key", ch_key, 0);
    handover("Complexity", ch_complex, 0);
    handover("Spread", ch_spread, 0);
}

/* a pattern that keeps changing while it plays: one long demo */
static void demo_changes(void)
{
    uint8_t pat[16] = {127, 0, 64, 0, 127, 0, 0, 90, 0, 127, 0, 0, 127, 64, 0, 0};
    run_t s = start(128.0f, 8);
    int bar;
    ngen_init(&ga, mem_a, SR, 42u);
    ngen_pattern(&ga, pat, 16);
    prime(&ga);
    run(&ga, &s, (int)s.step_len * 32);
    bar = (int)s.step_len * 16;
    ngen_new_sound(&ga, 777u);
    run(&ga, &s, bar);
    ngen_set_key(&ga, 50, NGEN_MAJOR);
    run(&ga, &s, bar);
    ngen_set_param(&ga, NGEN_COMPLEXITY, 100);
    pat[3] = pat[5] = pat[11] = 127;
    ngen_pattern(&ga, pat, 16);
    run(&ga, &s, 2 * bar);
    wav("ngen-handover.wav", &s);
}

/* ten sounds on one groove, two bars each: the families, voicings and characters */
static const uint8_t GROOVE[32] = {127, 0, 0, 64, 0, 0, 90, 0, 0, 0, 0, 0, 127, 0, 64, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 100, 0, 127, 127, 127, 127};
static void demo_sounds(void)
{
    /* (chosen to show the range: a pad of saws, a struck vowel an octave up, half-struck squares, an organ
     * drone, struck buzz, soft, a vowel pad, struck saws, glass, squares without the root) */
    static const uint32_t seeds[10] = {6u, 43u, 193u, 67u, 38u, 202u, 69u, 136u, 470u, 61u};
    run_t s = start(126.0f, 8);
    int k;
    ngen_init(&ga, mem_a, SR, seeds[0]);
    ngen_pattern(&ga, GROOVE, 32);
    prime(&ga);
    for (k = 0; k < 10; k++) {
        if (k)
            ngen_new_sound(&ga, seeds[k]);
        ngen_set_key(&ga, k < 5 ? 45 : 41, k % 3 != 2 ? NGEN_MINOR : NGEN_MAJOR);
        run(&ga, &s, (int)s.step_len * 32);
    }
    wav("ngen-sounds.wav", &s);
}

/* many seeds, many sounds: each plays (not silent, no blow-up), and their brightness spreads wide */
static void test_variety(void)
{
    double lo = 1e9, hi = 0, qlo = 1e9, qhi = 0;
    int k;
    for (k = 0; k < 32; k++) {
        run_t s = start(126.0f, 8);
        double b, q;
        int i, bar;
        float pk = 0.0f;
        ngen_init(&ga, mem_a, SR, 7u + 7919u * (uint32_t)k);
        ngen_pattern(&ga, GROOVE, 32);
        prime(&ga);
        run(&ga, &s, (int)s.step_len * 32);
        bar = (int)s.step_len * 16;
        for (i = 0; i < s.n; i++) {
            assert(s.l[i] == s.l[i] && s.r[i] == s.r[i]);
            if (fabsf(s.l[i]) > pk) pk = fabsf(s.l[i]);
        }
        b = bright(s.l, bar, 2 * bar);
        q = rms(s.l, bar, 2 * bar);
        assert(q > 0.003 && pk < 1.5f);
        lo = b < lo ? b : lo; hi = b > hi ? b : hi;
        qlo = q < qlo ? q : qlo; qhi = q > qhi ? q : qhi;
    }
    printf("variety: 32 seeds, brightness %.3f .. %.3f, level %.4f .. %.4f\n", lo, hi, qlo, qhi);
    assert(hi > 3.0 * lo);
}

/* the chords: every tone in the scale (just intonation, the harmonic seventh a third of a semitone
 * flat); one note up to a ninth; the intense chord now and then another one */
static int pc_of(float f, int root, double *err)
{
    double st = 12.0 * log2(f / (440.0 * pow(2.0, (root - 69) / 12.0)));
    double r = floor(st + 0.5);
    *err = fabs(st - r);
    return (((int)r % 12) + 12) % 12;
}
static void test_chords(void)
{
    static const unsigned masks[7] = {NGEN_MINOR, NGEN_MAJOR, 0x6ADu, 0x4A9u, 0x5ABu, 0x4E9u, 0x9ADu};   /* (dorian, minor pentatonic, phrygian, blues, harmonic minor) */
    int sizes[6] = {0}, other = 0, k, total = 0;
    double worst = 0;
    for (k = 0; k < 280; k++) {
        unsigned m = masks[k % 7], pcs[2] = {0, 0};
        int st, i;
        ngen_init(&ga, mem_a, SR, 31u + 7919u * (uint32_t)k);
        ngen_set_key(&ga, 45 + k % 12, m);
        prime(&ga);
        for (st = 0; st < 2; st++)
            for (i = 0; i < ga.jn[st]; i++) {
                double e;
                int pc = pc_of(ga.jf0[st] * (float)ga.jq[st][i], ga.root, &e);
                assert(m >> pc & 1u);
                worst = e > worst ? e : worst;
                pcs[st] |= 1u << pc;
            }
        for (st = 0; st < 2; st++) {
            int n = 0;
            for (i = 0; i < 12; i++)
                n += pcs[st] >> i & 1u;
            sizes[n > 5 ? 5 : n]++;
        }
        other += (pcs[1] & pcs[0]) != pcs[0];       /* the intense chord leaves notes of the calm one */
        total++;
    }
    printf("chords: notes 1:%d 2:%d 3:%d 4:%d 5:%d, intense on another chord %d of %d, worst %.2f semitones off\n",
           sizes[1], sizes[2], sizes[3], sizes[4], sizes[5], other, total, worst);
    assert(worst < 0.35);
    assert(sizes[1] > 10 && sizes[3] > 40 && sizes[4] + sizes[5] > 20);
    assert(other > total / 5 && other < total * 4 / 5);
}

/* DEVIANCE: below 90 each step strays around the sound and stays near it; from 90 it walks on, and its
 * material with it; a new sound starts from its DNA again */
static void test_deviance(void)
{
    static const uint8_t pat[16] = {127, 0, 90, 0, 127, 0, 90, 0, 127, 0, 90, 0, 127, 0, 90, 0};
    int d, k;
    double spread[2];
    for (d = 0; d < 2; d++) {                       /* how much the steps differ: 0 against 70 */
        run_t s = start(120.0f, 8);
        double b[16], mean = 0, var = 0;
        int st;
        ngen_init(&ga, mem_a, SR, 555u);
        ngen_set_param(&ga, NGEN_DEVIANCE, d ? 70 : 0);
        ngen_pattern(&ga, pat, 16);
        prime(&ga);
        run(&ga, &s, (int)s.step_len * 96);
        for (st = 0; st < 16; st++) {               /* the loud steps only: the soft ones differ anyway */
            int a = (int)s.step_len * (32 + 4 * st);
            b[st] = bright(s.l, a + (int)s.step_len / 2, a + 2 * (int)s.step_len);
            mean += b[st] / 16;
        }
        for (st = 0; st < 16; st++)
            var += (b[st] - mean) * (b[st] - mean) / 16;
        spread[d] = sqrt(var) / mean;
        if (d)
            for (k = 0; k < NGEN_DEVS; k++)
                assert(fabsf(ga.dt[k]) < 2.01f * 70.0f / 89.0f * 2.0f);   /* (near its own) */
    }
    printf("deviance: the steps' brightness spreads %.3f at 0, %.3f at 70\n", spread[0], spread[1]);
    assert(spread[1] > 1.5 * spread[0]);
    {   /* from 90: it walks on, the material too; a new sound starts over */
        run_t s = start(120.0f, 8);
        float far = 0.0f;
        ngen_init(&ga, mem_a, SR, 556u);
        ngen_set_param(&ga, NGEN_DEVIANCE, 100);
        ngen_pattern(&ga, pat, 16);
        prime(&ga);
        run(&ga, &s, (int)s.step_len * 16 * 8);
        far = fabsf(ga.dt[0]) / 2.0f;
        printf("deviance at 100: after 8 bars the cutoff %.2f of 0..89's reach away, material %.2f %.2f %.2f, %u sets\n",
               (double)far, (double)ga.wm[0], (double)ga.wm[1], (double)ga.wm[2], (unsigned)ga.made);
        assert(ga.made > 3 && fabsf(ga.wm[0]) + fabsf(ga.wm[1]) + fabsf(ga.wm[2]) > 0.2f);
        ngen_new_sound(&ga, 557u);
        assert(ga.wm[0] == 0.0f && ga.wm[1] == 0.0f && ga.wm[2] == 0.0f);
    }
}

/* the background never runs: what plays plays on, a new sound waits */
static void test_starved(void)
{
    uint8_t pat[16] = {127, 0, 0, 0, 127, 0, 0, 0, 127, 0, 0, 0, 127, 0, 0, 0};
    run_t s = start(120.0f, 0);
    int bar;
    ngen_init(&ga, mem_a, SR, 3u);
    ngen_pattern(&ga, pat, 16);
    prime(&ga);
    ngen_new_sound(&ga, 4u);
    run(&ga, &s, (int)s.step_len * 64);
    bar = (int)s.step_len * 16;
    assert(ga.made == 1);
    assert(rms(s.l, 3 * bar, 4 * bar) > 0.005);
    printf("starved: plays on, %u set\n", (unsigned)ga.made);
}

static void test_stereo_and_seed(void)
{
    uint8_t pat[16] = {127, 0, 100, 0, 127, 0, 100, 0, 127, 0, 100, 0, 127, 0, 100, 127};
    run_t s = start(120.0f, 4), t = start(120.0f, 4);
    double sl = 0, sr_ = 0, slr = 0, mono, corr;
    int i, a;
    ngen_init(&ga, mem_a, SR, 2024u);
    ngen_pattern(&ga, pat, 16);
    prime(&ga);
    run(&ga, &s, (int)s.step_len * 64);
    a = (int)s.step_len * 32;
    for (i = a; i < s.n; i++) {
        sl += (double)s.l[i] * s.l[i];
        sr_ += (double)s.r[i] * s.r[i];
        slr += (double)s.l[i] * s.r[i];
    }
    corr = slr / sqrt(sl * sr_);
    mono = (sl + sr_ + 2 * slr) / 4 / ((sl + sr_) / 2);
    {   /* the low core (below ~250 Hz) is shared: there the sides move together */
        double a1 = 1.0 - exp(-2.0 * 3.14159265358979 * 250.0 / SR), yl[4] = {0}, yr[4] = {0}, ll = 0, rr = 0, lr = 0;
        int p;
        for (i = a; i < s.n; i++) {
            double xl = s.l[i], xr = s.r[i];
            for (p = 0; p < 4; p++) {               /* four poles: the highs out of the way */
                yl[p] += a1 * (xl - yl[p]);
                yr[p] += a1 * (xr - yr[p]);
                xl = yl[p];
                xr = yr[p];
            }
            ll += xl * xl; rr += xr * xr; lr += xl * xr;
        }
        printf("stereo: correlation %.3f (below 250 Hz %.3f), mono keeps %.0f %% of the power\n",
               corr, lr / sqrt(ll * rr), mono * 100);
        assert(lr / sqrt(ll * rr) > 0.6);
    }
    assert(corr > 0.0 && corr < 0.9);           /* wide, never mostly out of phase */
    assert(mono > 0.5);
    wav("ngen-pulse.wav", &s);
    /* the same seed and pattern: the same sound, sample for sample */
    {
        static float keep_l[SR * 40];
        int n = s.n;
        memcpy(keep_l, s.l, sizeof(float) * (size_t)n);
        ngen_init(&gb, mem_b, SR, 2024u);
        ngen_pattern(&gb, pat, 16);
        prime(&gb);
        run(&gb, &t, n);
        assert(!memcmp(keep_l, t.l, sizeof(float) * (size_t)n));
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_example();
    test_hold(60.0f);
    test_hold(120.0f);
    test_hold(300.0f);
    test_saturation();
    test_handover();
    demo_changes();
    demo_sounds();
    test_variety();
    test_chords();
    test_deviance();
    test_starved();
    test_stereo_and_seed();
    printf("ngen tests passed\n");
    return 0;
}

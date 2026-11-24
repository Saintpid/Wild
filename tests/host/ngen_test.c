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
        ngen_render(g, s->l + s->n, s->r + s->n, m);
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

/* the largest bend (second difference) in [a, b): a click is a sudden bend, the music's own are smooth */
static double max_step(const float *x, int a, int b)
{
    double m = 0;
    int i;
    for (i = a + 2; i < b; i++) {
        double d = fabs((double)x[i] - 2.0 * x[i - 1] + x[i - 2]);
        if (d > m)
            m = d;
    }
    return m;
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
        double be = bright(s.l, 7 * bar + 4 * (int)s.step_len, 7 * bar + 12 * (int)s.step_len);
        double bl = bright(s.l, 7 * bar + 12 * (int)s.step_len, 8 * bar);
        printf("example: brightness %.3f -> %.3f\n", be, bl);
        assert(bl > 1.3 * be);
    }
    assert(early > 0.002);                          /* calm, not silent: the hold carries it */
    /* the plan leans the timbre up before the climax: step 10 plans higher than its own energy */
    assert(ga.plan[10] > ga.pred[10] + 0.05f);
    assert(ga.pred[15] > ga.pred[0]);
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

/* a new sound, a new key, new settings and a new pattern while it plays: no jumps beyond the music's own */
static void test_handover(void)
{
    uint8_t pat[16] = {127, 0, 64, 0, 127, 0, 0, 90, 0, 127, 0, 0, 127, 64, 0, 0};
    run_t s = start(128.0f, 8);
    int bar, a;
    double base, worst = 0;
    ngen_init(&ga, mem_a, SR, 42u);
    ngen_pattern(&ga, pat, 16);
    prime(&ga);
    run(&ga, &s, (int)s.step_len * 32);
    bar = (int)s.step_len * 16;
    base = max_step(s.l, bar, 2 * bar);
    if (max_step(s.r, bar, 2 * bar) > base)
        base = max_step(s.r, bar, 2 * bar);
    a = s.n;
    ngen_new_sound(&ga, 777u);
    run(&ga, &s, bar);
    ngen_set_key(&ga, 50, 0);
    run(&ga, &s, bar / 2);
    ngen_set_param(&ga, NGEN_COMPLEXITY, 100);
    ngen_set_param(&ga, NGEN_SPREAD, 100);
    run(&ga, &s, bar / 2);
    pat[3] = pat[5] = 127;
    ngen_pattern(&ga, pat, 16);
    ngen_set_param(&ga, NGEN_INERTIA, 0);
    run(&ga, &s, 2 * bar);
    {   /* where it has settled with the last settings: their own largest jump */
        int z = s.n, e;
        run(&ga, &s, 2 * bar);
        e = s.n;
        if (max_step(s.l, z, e) > base)
            base = max_step(s.l, z, e);
        if (max_step(s.r, z, e) > base)
            base = max_step(s.r, z, e);
        worst = max_step(s.l, a, z);
        if (max_step(s.r, a, z) > worst)
            worst = max_step(s.r, a, z);
    }
    printf("handover: largest jump %.4f, steady %.4f, sets made %u\n", worst, base, (unsigned)ga.made);
    assert(ga.made >= 3);
    assert(worst < 1.25 * base);
    wav("ngen-handover.wav", &s);
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
    printf("stereo: correlation %.3f, mono keeps %.0f %% of the power\n", corr, mono * 100);
    assert(corr > 0.15 && corr < 0.97);
    assert(mono > 0.55);
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
    test_starved();
    test_stereo_and_seed();
    printf("ngen tests passed\n");
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* The acid line against a TB-303's rules: a note not slid into is a new attack and a slid one is not,
 * the oscillator's phase never jumps, slides in a row, a slide's RC (the same speed whatever the
 * interval, so an octave may not get there), the pitch going on from where it is when a new slide
 * starts, rests closing the gate, the last step into the first, accent on the VCA and the cutoff and
 * its sweep building up (weak on a slid note), a soft attack against an accent's sharp one, Env Mod
 * lowering the sweep's start, the square made from the saw, the filter's slope and its stability, and
 * the generator by Chaos: by the rules at 0, by chance at 1, and in between step by step. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nacid.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

#define SR 44100.0f
static nfm_engine eng;
static nacid ac;
static float L[1], R[1];

/* what one sample did */
typedef struct {
    float out, amp, env, cv, phase, dt, acc_vca, acc_cap, fc;
    int gate_on, retrig;
} smp;

static smp tick(void)
{
    smp m;
    float env0 = ac.env, ph0 = ac.phase;
    L[0] = R[0] = 0.0f;
    nacid_render(&ac, &eng, L, R, 0, 0, 1);
    m.out = L[0];
    m.amp = ac.amp;
    m.env = ac.env;
    m.cv = ac.cv;
    m.phase = ac.phase;
    m.dt = ac.freq / SR;
    m.acc_vca = ac.acc_vca;
    m.acc_cap = ac.acc_cap;
    m.fc = ac.ga;
    m.gate_on = ac.gate_on;
    m.retrig = ac.env > env0 + 0.01f;              /* the cutoff envelope started again */
    (void)ph0;
    return m;
}

static void fresh(float reso)
{
    nacid_init(&ac, SR, 1234u);
    memset(ac.step, 0, sizeof ac.step);
    ac.p[NACID_P_RESO] = reso;
    ac.p[NACID_P_EVOLVE] = 0.0f;
    ac.root = 0;
    nacid_set_length(&ac, 16);
}

/* play steps from..to-1 of the pattern, step_len samples each; per step: retriggered, the gate's
 * lowest and the amplitude's lowest in the step's first 40 %, the cv at its end, phase jumps */
typedef struct {
    int retrig[64], gate_lo[64];
    float amp_lo[64], cv_end[64], cv_jump[64], acc_peak[64], vca_step;
    int phase_bad;
} run_t;

static void run(int steps, float step_len, run_t *r)
{
    memset(r, 0, sizeof *r);
    float prev_ph = ac.phase, prev_dt = ac.freq / SR, prev_cv = ac.cv, prev_vca = ac.acc_vca;
    for (int s = 0; s < steps; ++s) {
        nacid_step(&ac, 0.0f, step_len);
        r->gate_lo[s] = 1;
        r->amp_lo[s] = 9.0f;
        for (int i = 0; i < (int)step_len; ++i) {
            smp m = tick();
            /* the phase: each sample one dt on (the dt of the sample before or this one), never reset */
            float want = prev_ph + m.dt, want2 = prev_ph + prev_dt;
            if (want >= 1.0f) want -= 1.0f;
            if (want2 >= 1.0f) want2 -= 1.0f;
            if (fabsf(m.phase - want) > 1e-4f && fabsf(m.phase - want2) > 1e-4f) r->phase_bad++;
            prev_ph = m.phase;
            prev_dt = m.dt;
            if (m.retrig) r->retrig[s] = 1;
            if (i < (int)(step_len * 0.4f)) {
                if (!m.gate_on) r->gate_lo[s] = 0;
                if (m.amp < r->amp_lo[s]) r->amp_lo[s] = m.amp;
            }
            float j = fabsf(m.cv - prev_cv);
            if (j > r->cv_jump[s]) r->cv_jump[s] = j;
            prev_cv = m.cv;
            if (m.acc_cap > r->acc_peak[s]) r->acc_peak[s] = m.acc_cap;
            if (m.amp > 0.01f && fabsf(m.acc_vca - prev_vca) > r->vca_step) r->vca_step = fabsf(m.acc_vca - prev_vca);
            prev_vca = m.acc_vca;
        }
        r->cv_end[s] = ac.cv;
    }
}

/* ---------------------------------------------------------------------------------- the voice --- */
static void test_gate(void)
{
    run_t r;
    const float sl = SR * 0.125f;                  /* a sixteenth at 120 */
    /* 1: notes on 0, 1, 2 without slides: each its own attack, the gate shut between */
    fresh(0.5f);
    for (int s = 0; s < 3; ++s) nacid_set_step(&ac, s, NACID_ON, s * 2);
    run(4, sl, &r);
    CHECK(r.retrig[0] && r.retrig[1] && r.retrig[2], "plain notes: attacks %d %d %d", r.retrig[0], r.retrig[1], r.retrig[2]);
    CHECK(r.amp_lo[1] < 0.05f && r.amp_lo[2] < 0.05f, "plain notes: the VCA did not close between (%f %f)",
          (double)r.amp_lo[1], (double)r.amp_lo[2]);
    CHECK(r.cv_jump[1] > 1.9f, "plain notes: the pitch should jump to the note (%f)", (double)r.cv_jump[1]);
    CHECK(!r.phase_bad, "plain notes: the phase jumped %d times", r.phase_bad);
    printf("plain notes: attacks on every note, the VCA down to %.4f between\n", (double)r.amp_lo[1]);

    /* 2 + 4: 0 slides into 1, 1 into 2, 2 into 3: one attack, the gate held, the VCA open throughout */
    fresh(0.5f);
    nacid_set_step(&ac, 0, NACID_ON | NACID_SLIDE, 0);
    nacid_set_step(&ac, 1, NACID_ON | NACID_SLIDE, 7);
    nacid_set_step(&ac, 2, NACID_ON | NACID_SLIDE, 3);
    nacid_set_step(&ac, 3, NACID_ON, 12);
    run(5, sl, &r);
    CHECK(r.retrig[0] && !r.retrig[1] && !r.retrig[2] && !r.retrig[3], "slides: attacks %d %d %d %d",
          r.retrig[0], r.retrig[1], r.retrig[2], r.retrig[3]);
    CHECK(r.gate_lo[1] && r.gate_lo[2] && r.gate_lo[3], "slides: the gate closed");
    CHECK(r.amp_lo[1] > 0.8f && r.amp_lo[3] > 0.8f, "slides: the VCA dipped (%f %f)", (double)r.amp_lo[1], (double)r.amp_lo[3]);
    CHECK(r.cv_jump[1] < 0.2f && r.cv_jump[2] < 0.2f && r.cv_jump[3] < 0.3f, "slides: the pitch jumped (%f %f %f)",
          (double)r.cv_jump[1], (double)r.cv_jump[2], (double)r.cv_jump[3]);
    CHECK(fabsf(r.cv_end[1] - 7.0f) < 1.2f && fabsf(r.cv_end[3] - 12.0f) < 1.2f, "slides: pitches %f %f",
          (double)r.cv_end[1], (double)r.cv_end[3]);
    CHECK(!r.phase_bad, "slides: the phase jumped %d times", r.phase_bad);
    /* 3 (the last step 3 had no slide): the gate shuts in step 3, step 4 a rest */
    CHECK(r.amp_lo[4] < 0.01f, "slides: the gate did not close after the last note");
    printf("three slides in a row: one attack, the VCA at least %.2f, pitch steps <= %.3f semitones a sample\n",
           (double)r.amp_lo[2], (double)r.cv_jump[2]);

    /* 7: a slide into a rest: the gate held its step out, then closed */
    fresh(0.5f);
    nacid_set_step(&ac, 0, NACID_ON | NACID_SLIDE, 0);
    run(2, sl, &r);
    CHECK(ac.gate_on == 0 && ac.amp < 0.01f, "slide into a rest: the gate is open (%d %f)", ac.gate_on, (double)ac.amp);
    CHECK(r.amp_lo[0] > 0.0f, "slide into a rest");
    printf("a slide into a rest: the note held to the step's end, then the gate shut\n");

    /* 8: the last step slides into the first, round the loop: no attack at the top; without the slide one */
    fresh(0.5f);
    nacid_set_length(&ac, 4);
    nacid_set_step(&ac, 0, NACID_ON, 0);
    nacid_set_step(&ac, 3, NACID_ON | NACID_SLIDE, 12);
    run(8, sl, &r);
    CHECK(r.retrig[3] && !r.retrig[4] && r.gate_lo[4] && r.amp_lo[4] > 0.9f, "last into first: attack %d, gate %d, VCA %f",
          r.retrig[4], r.gate_lo[4], (double)r.amp_lo[4]);
    CHECK(r.cv_jump[4] < 0.3f, "last into first: the pitch jumped %f", (double)r.cv_jump[4]);
    nacid_set_step(&ac, 3, NACID_ON, 12);
    run(8, sl, &r);
    CHECK(!r.retrig[0] && r.retrig[4], "last into first without a slide: attacks %d %d (the first still slid into)",
          r.retrig[0], r.retrig[4]);
    printf("the last step into the first: slid without an attack, and with one when the slide is off\n");
}

static void test_slide(void)
{
    run_t r;
    /* 5: the RC: after 60 ms 63 % of the way, whether one or two octaves; a short step leaves an octave short */
    float frac[2];
    for (int o = 0; o < 2; ++o) {
        fresh(0.5f);
        nacid_set_step(&ac, 0, NACID_ON | NACID_SLIDE, -12);
        nacid_set_step(&ac, 1, NACID_ON, -12 + 12 * (o + 1));
        nacid_step(&ac, 0.0f, SR);
        for (int i = 0; i < (int)SR / 10; ++i) tick();
        nacid_step(&ac, 0.0f, SR);
        for (int i = 0; i < (int)(0.06f * SR) + 1; ++i) tick();
        frac[o] = (ac.cv + 12.0f) / (12.0f * (float)(o + 1));
    }
    CHECK(fabsf(frac[0] - 0.632f) < 0.03f && fabsf(frac[0] - frac[1]) < 0.01f, "the slide's RC: %f %f of the way",
          (double)frac[0], (double)frac[1]);
    printf("slide: after 60 ms %.1f %% of an octave and %.1f %% of two: the same speed\n", (double)(frac[0] * 100.0f),
           (double)(frac[1] * 100.0f));
    const float fast = SR * 60.0f / 150.0f / 4.0f;   /* a sixteenth at 150 */
    fresh(0.5f);
    nacid_set_step(&ac, 0, NACID_ON | NACID_SLIDE, -12);
    nacid_set_step(&ac, 1, NACID_ON | NACID_SLIDE, 12);
    nacid_set_step(&ac, 2, NACID_ON, -12);
    run(3, fast, &r);
    CHECK(r.cv_end[1] < 10.0f && r.cv_end[1] > 4.0f, "two octaves up at 150: %f at the next note", (double)r.cv_end[1]);
    /* 6: the next slide starts from where the pitch got to: no jump, it turns */
    CHECK(r.cv_jump[2] < 0.2f, "a slide from the middle of a slide: the pitch jumped %f", (double)r.cv_jump[2]);
    CHECK(r.cv_end[2] < r.cv_end[1] - 10.0f, "a slide from the middle of a slide: it did not turn");
    printf("two octaves up at 150 BPM: %.2f semitones short at the next note, which slides on from there\n",
           (double)(12.0f - r.cv_end[1]));
}

static void test_accent(void)
{
    run_t r;
    const float sl = SR * 0.125f;
    /* 9: an accent: louder and brighter */
    float pk[2], fc[2];
    for (int a = 0; a < 2; ++a) {
        fresh(0.5f);
        ac.p[NACID_P_ENVMOD] = 0.0f;               /* (only the accent on the cutoff) */
        nacid_set_step(&ac, 0, NACID_ON | (a ? NACID_ACCENT : 0), 0);
        nacid_step(&ac, 0.0f, sl);
        pk[a] = fc[a] = 0.0f;
        for (int i = 0; i < (int)sl; ++i) {
            smp m = tick();
            if (fabsf(m.out) > pk[a]) pk[a] = fabsf(m.out);
            if (m.fc > fc[a]) fc[a] = m.fc;
        }
    }
    CHECK(pk[1] > pk[0] * 1.3f, "accent: not louder (%f %f)", (double)pk[0], (double)pk[1]);
    CHECK(fc[1] > fc[0] * 1.5f, "accent: the cutoff did not rise (%f %f)", (double)fc[0], (double)fc[1]);
    printf("accent: %.1f dB louder, the cutoff up %.1f times\n", (double)(20.0f * log10f(pk[1] / pk[0])), (double)(fc[1] / fc[0]));
    /* accents in a row: the sweep builds up, more with resonance */
    float grow[2];
    for (int q = 0; q < 2; ++q) {
        fresh(q ? 1.0f : 0.0f);
        for (int s = 0; s < 6; ++s) nacid_set_step(&ac, s, NACID_ON | NACID_ACCENT, 0);
        run(6, sl, &r);
        grow[q] = r.acc_peak[5] / r.acc_peak[0];
        CHECK(r.acc_peak[5] >= r.acc_peak[0], "accents in a row: the sweep fell");
    }
    CHECK(grow[1] > 1.4f && grow[1] > grow[0] + 0.3f, "accents in a row at full resonance: the sweep %f, at none %f",
          (double)grow[1], (double)grow[0]);
    printf("six accents in a row: the sweep x%.2f at full resonance, x%.2f at none\n", (double)grow[1], (double)grow[0]);
    /* an accent on a slid note: no new attack, the accent comes on without a click, and (a 303's) weaker
     * than on a note of its own: it takes the cutoff envelope as it has decayed */
    float own;
    fresh(0.7f);
    nacid_set_step(&ac, 1, NACID_ON | NACID_ACCENT, 5);
    run(2, sl, &r);
    own = r.acc_peak[1];
    fresh(0.7f);
    nacid_set_step(&ac, 0, NACID_ON | NACID_SLIDE, 0);
    nacid_set_step(&ac, 1, NACID_ON | NACID_ACCENT, 5);
    run(2, sl, &r);
    CHECK(!r.retrig[1] && r.acc_peak[1] > 0.05f && r.vca_step < 0.02f, "accent on a slid note: attack %d sweep %f VCA step %f",
          r.retrig[1], (double)r.acc_peak[1], (double)r.vca_step);
    CHECK(r.acc_peak[1] < own * 0.8f, "accent on a slid note: as strong as its own (%f %f)", (double)r.acc_peak[1], (double)own);
    printf("an accent on a slid note: no attack, its sweep %.2f (%.2f on a note of its own), the VCA's accent at most %.4f a sample\n",
           (double)r.acc_peak[1], (double)own, (double)r.vca_step);
    /* the VCA's attack: soft on a plain note, sharp on an accent (samples to 90 %) */
    int rise[2];
    for (int a = 0; a < 2; ++a) {
        fresh(0.5f);
        nacid_set_step(&ac, 0, NACID_ON | (a ? NACID_ACCENT : 0), 0);
        nacid_step(&ac, 0.0f, sl);
        rise[a] = 0;
        while (tick().amp < 0.9f && rise[a] < 2000) rise[a]++;
    }
    CHECK(rise[0] > 0.005f * SR && rise[0] < 0.012f * SR && rise[1] < 0.0015f * SR, "the attack: %d and %d samples",
          rise[0], rise[1]);
    printf("the VCA's attack: %.1f ms on a plain note, %.1f ms on an accent\n", (double)(rise[0] * 1000.0f / SR),
           (double)(rise[1] * 1000.0f / SR));
}

/* Env Mod as a 303's: the more, the lower the sweep starts; its top where it was */
static void test_envmod(void)
{
    float lo[2], hi[2];
    for (int e = 0; e < 2; ++e) {
        fresh(0.5f);
        ac.p[NACID_P_ENVMOD] = e ? 1.0f : 0.0f;
        ac.p[NACID_P_DECAY] = 0.0f;
        nacid_set_step(&ac, 0, NACID_ON | NACID_SLIDE, 0);
        nacid_set_step(&ac, 1, NACID_ON, 0);
        nacid_step(&ac, 0.0f, SR);
        lo[e] = 1.0f;
        hi[e] = 0.0f;
        for (int i = 0; i < (int)SR; ++i) {
            smp m = tick();
            if (i > 16 && m.fc > hi[e]) hi[e] = m.fc;
            if (m.fc < lo[e]) lo[e] = m.fc;
        }
    }
    /* ga = g / (1 + g), g ~ the cutoff for a low one */
    float drop = log2f((lo[0] / (1.0f - lo[0])) / (lo[1] / (1.0f - lo[1])));
    CHECK(drop > 1.2f && drop < 1.8f, "Env Mod: the floor down %f octaves", (double)drop);
    CHECK(hi[1] > hi[0] * 4.0f, "Env Mod: the sweep's top %f against %f", (double)hi[1], (double)hi[0]);
    printf("Env Mod at full: the sweep starts %.2f octaves under the knob\n", (double)drop);
}

/* the square as a 303's: the saw clipped, so its rise is slower than its fall (fall at the wrap) */
static void test_square(void)
{
    float f = 110.0f, w[1024];
    fresh(0.0f);
    ac.wave = NACID_SQUARE;
    ac.tune = 12.0f * log2f(f / 65.406f);
    ac.p[NACID_P_CUTOFF] = 1.0f;
    ac.p[NACID_P_ENVMOD] = 0.0f;
    ac.step_len = SR;
    nacid_hit(&ac, 0, 0);
    for (int i = 0; i < 4000; ++i) tick();
    int n = (int)(SR / f);
    float pk = 0.0f, up = 0.0f, dn = 0.0f;
    for (int i = 0; i < n; ++i) {
        w[i] = tick().out;
        if (fabsf(w[i]) > pk) pk = fabsf(w[i]);
    }
    for (int i = 1; i < n; ++i) {
        float d = (w[i] - w[i - 1]) / pk;
        if (d > up) up = d;
        if (-d > dn) dn = -d;
    }
    CHECK(dn > up * 1.5f, "square: the rise %f, the fall %f a sample", (double)up, (double)dn);
    printf("square: the fall %.2fx as steep as the rise\n", (double)(dn / up));
}

/* 10: the filter: a sine through it at a fixed cutoff (no envelope), its level against the VCA */
static float sine_gain(float f, float cut, float reso)
{
    fresh(reso);
    ac.wave = NACID_SINE;
    ac.p[NACID_P_CUTOFF] = cut;
    ac.p[NACID_P_ENVMOD] = 0.0f;
    ac.tune = 12.0f * log2f(f / 65.406f);
    ac.step_len = SR * 4.0f;
    nacid_hit(&ac, 0, 0);
    float pk = 0.0f;
    for (int i = 0; i < (int)(SR * 0.4f); ++i) {
        smp m = tick();
        if (i > (int)(SR * 0.3f) && fabsf(m.out) / m.amp > pk) pk = fabsf(m.out) / m.amp;
    }
    return pk;
}

static void test_filter(void)
{
    const float cut = 0.534f;                      /* 500 Hz */
    float g0 = sine_gain(125.0f, cut, 0.0f), g1 = sine_gain(500.0f, cut, 0.0f), g2 = sine_gain(1000.0f, cut, 0.0f),
          g4 = sine_gain(2000.0f, cut, 0.0f), g8 = sine_gain(4000.0f, cut, 0.0f);
    float s1 = 20.0f * log10f(g1 / g2), s2 = 20.0f * log10f(g2 / g4), s3 = 20.0f * log10f(g4 / g8);
    printf("filter at 500 Hz, no resonance: %.1f dB at the cutoff, then %.1f, %.1f, %.1f dB per octave\n",
           (double)(20.0f * log10f(g1 / g0)), (double)s1, (double)s2, (double)s3);
    CHECK(s1 > 13.0f && s1 < 21.0f && s2 > 16.0f && s2 < 24.5f, "filter slope %f %f", (double)s1, (double)s2);
    /* resonance: a peak at the cutoff, the bass kept (the high-pass in the feedback) */
    float r0 = sine_gain(125.0f, cut, 1.0f), rp = 0.0f, fp = 0.0f;
    for (float f = 300.0f; f < 800.0f; f *= 1.03f) {
        float g = sine_gain(f, cut, 1.0f);
        if (g > rp) { rp = g; fp = f; }
    }
    printf("full resonance: the peak %.1f dB over the bass at %.0f Hz, the bass %.1f dB against none\n",
           (double)(20.0f * log10f(rp / r0)), (double)fp, (double)(20.0f * log10f(r0 / g0)));
    CHECK(rp > r0 * 2.0f, "resonance: no peak");
    const float cut2 = 0.735f;                     /* 1.5 kHz */
    float h0 = sine_gain(200.0f, cut2, 1.0f), hp = 0.0f, hfp = 0.0f;
    for (float f = 700.0f; f < 2000.0f; f *= 1.02f) {
        float g = sine_gain(f, cut2, 1.0f);
        if (g > hp) { hp = g; hfp = f; }
    }
    printf("full resonance at 1.5 kHz: the peak %.1f dB over the bass at %.0f Hz\n", (double)(20.0f * log10f(hp / h0)), (double)hfp);
    CHECK(hp > h0 * 3.0f, "resonance at 1.5 kHz: no peak");   /* (a full sine: the clip on the way in holds it) */
    CHECK(r0 > g0 * 0.5f, "resonance: the bass went (%f %f)", (double)r0, (double)g0);
    /* stable: every wave, full resonance, the cutoff from the bottom to the top with the envelope and accents */
    for (int w = 0; w < NACID_WAVES; ++w)
        for (float c = 0.0f; c <= 1.0f; c += 0.25f) {
            fresh(1.0f);
            ac.wave = (uint8_t)w;
            ac.p[NACID_P_CUTOFF] = c;
            ac.p[NACID_P_ENVMOD] = 1.0f;
            ac.p[NACID_P_ACCENT] = 1.0f;
            for (int s = 0; s < 16; ++s) nacid_set_step(&ac, s, NACID_ON | NACID_ACCENT | (s % 3 ? NACID_SLIDE : 0), (s * 7) % 36 - 12);
            float pk = 0.0f;
            int bad = 0;
            for (int s = 0; s < 32; ++s) {
                nacid_step(&ac, 0.0f, SR * 0.1f);
                for (int i = 0; i < (int)(SR * 0.1f); ++i) {
                    smp m = tick();
                    if (m.out != m.out) bad = 1;
                    if (fabsf(m.out) > pk) pk = fabsf(m.out);
                }
            }
            CHECK(!bad && pk < 2.0f, "wave %d cutoff %f: %s peak %f", w, (double)c, bad ? "NaN," : "", (double)pk);
        }
    printf("filter stable: 6 waves x 5 cutoffs at full resonance, envelope and accents\n");
    /* the level with the resonance: within a few dB (a saw at 110 Hz), at three cutoffs */
    for (float c = 0.2f; c < 0.9f; c += 0.3f) {
        float lv[2];
        for (int q = 0; q < 2; ++q) {
            fresh(q ? 1.0f : 0.0f);
            ac.p[NACID_P_CUTOFF] = c;
            ac.p[NACID_P_ENVMOD] = 0.0f;
            ac.tune = 12.0f * log2f(110.0f / 65.406f);
            ac.step_len = SR * 4.0f;
            nacid_hit(&ac, 0, 0);
            double e = 0.0;
            for (int i = 0; i < (int)(SR * 0.5f); ++i) {
                smp m = tick();
                if (i > (int)(SR * 0.1f)) e += (double)(m.out * m.out / (m.amp * m.amp));
            }
            lv[q] = (float)e;
        }
        float d = 10.0f * log10f(lv[1] / lv[0]);
        printf("a saw through the filter, cutoff %.0f %%: %+.1f dB at full resonance against none\n", (double)(c * 100.0f), (double)d);
        CHECK(d > -6.0f && d < 6.0f, "the level with resonance: %f dB", (double)d);
    }
}

/* -------------------------------------------------------------------------------- the generator --- */
typedef struct { float out_of_scale, ones, repeat, leap, mean_iv, slide_moves, acc_off, apart; } stats;

static stats measure(float chaos, float octaves)
{
    stats st = { 0 };
    int pairs = 0, apart = 0;
    int notes = 0, out = 0, ones = 0, same = 0, cmp = 0, leaps = 0, ivs = 0, sl = 0, slmv = 0, acc = 0, accoff = 0;
    float iv_sum = 0.0f;
    static const int minor[12] = { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 };
    fresh(0.5f);
    ac.p[NACID_P_CHAOS] = chaos;
    ac.p[NACID_P_SLIDES] = 0.3f;
    ac.p[NACID_P_ACCENTS] = 0.3f;
    ac.p[NACID_P_OCTAVES] = octaves;
    for (int g = 0; g < 400; ++g) {
        nacid_generate(&ac);
        int prev = -100;
        for (int s = 0; s < NACID_STEPS; ++s) {
            const nacid_note *n = &ac.step[s];
            if (s % NACID_BAR == 0) ones += (n->flags & NACID_ON) != 0;
            if (s >= 2 * NACID_BAR && s < 3 * NACID_BAR) {   /* bar 3 against bar 1 */
                cmp++;
                same += n->flags == ac.step[s - 2 * NACID_BAR].flags && n->note == ac.step[s - 2 * NACID_BAR].note;
            }
            if (!(n->flags & NACID_ON)) continue;
            notes++;
            out += !minor[((n->note % 12) + 12) % 12];
            if (n->flags & NACID_ACCENT) {
                acc++;
                accoff += s % 4 != 0;
            }
            if (s > 0 && (ac.step[s - 1].flags & NACID_ON)) {   /* side by side: an octave or more apart */
                int d = n->note - ac.step[s - 1].note;
                pairs++;
                apart += d >= 12 || d <= -12 || ((n->note >= 12) != (ac.step[s - 1].note >= 12) && (d > 7 || d < -7));
            }
            if (prev > -100) {
                int iv = n->note - prev;
                iv = iv < 0 ? -iv : iv;
                iv_sum += (float)iv;
                ivs++;
                leaps += iv > 7;
            }
            prev = n->note;
            if (n->flags & NACID_SLIDE) {
                const nacid_note *nx = &ac.step[(s + 1) % NACID_STEPS];
                sl++;
                slmv += (nx->flags & NACID_ON) && nx->note != n->note;
            }
        }
    }
    st.out_of_scale = (float)out / (float)notes;
    st.ones = (float)ones / (400.0f * NACID_MAX_BARS);
    st.repeat = (float)same / (float)cmp;
    st.leap = (float)leaps / (float)ivs;
    st.mean_iv = iv_sum / (float)ivs;
    st.slide_moves = sl ? (float)slmv / (float)sl : 0.0f;
    st.acc_off = acc ? (float)accoff / (float)acc : 0.0f;
    st.apart = (float)apart / (float)pairs;
    return st;
}

static void test_generator(void)
{
    stats s[5];
    for (int i = 0; i < 5; ++i) {
        s[i] = measure(0.25f * (float)i, 0.2f);
        printf("chaos %3d %%: outside the scale %4.1f %%, the one %4.1f %%, bar 3 = bar 1 %4.1f %%, leaps %4.1f %%, "
               "interval %.2f, slides that move %4.1f %%, accents off the beat %4.1f %%, neighbours pushed apart %4.1f %%\n",
               25 * i, (double)(s[i].out_of_scale * 100.0f), (double)(s[i].ones * 100.0f), (double)(s[i].repeat * 100.0f),
               (double)(s[i].leap * 100.0f), (double)s[i].mean_iv, (double)(s[i].slide_moves * 100.0f),
               (double)(s[i].acc_off * 100.0f), (double)(s[i].apart * 100.0f));
    }
    /* 11: at 0 by the rules, at 1 by chance */
    CHECK(s[0].out_of_scale < 0.07f && s[0].out_of_scale > 0.0f, "chaos 0: outside the scale %f", (double)s[0].out_of_scale);
    CHECK(s[0].ones > 0.9f, "chaos 0: the one %f", (double)s[0].ones);
    CHECK(s[0].repeat > 0.85f, "chaos 0: the motif %f", (double)s[0].repeat);
    CHECK(s[0].apart > 0.35f, "chaos 0: neighbours an octave apart %f", (double)s[0].apart);
    CHECK(s[4].out_of_scale > 0.3f, "chaos 1: outside the scale %f", (double)s[4].out_of_scale);
    CHECK(s[4].repeat < 0.6f && s[4].ones < 0.8f, "chaos 1: motif %f, the one %f", (double)s[4].repeat, (double)s[4].ones);
    /* 12: step by step: each measure moves one way with Chaos */
    for (int i = 1; i < 5; ++i) {
        CHECK(s[i].out_of_scale > s[i - 1].out_of_scale, "chaos %d: outside the scale not up", 25 * i);
        CHECK(s[i].repeat < s[i - 1].repeat, "chaos %d: the motif not down", 25 * i);
        CHECK(s[i].ones < s[i - 1].ones + 0.02f, "chaos %d: the one not down", 25 * i);
    }
    /* Octaves: neighbours pushed an octave apart, more and more with the knob (Chaos 0) */
    float last = -1.0f;
    for (int o = 0; o <= 100; o += 25) {
        stats so = measure(0.0f, 0.01f * (float)o);
        printf("octaves %3d %%: neighbours an octave apart %4.1f %%\n", o, (double)(so.apart * 100.0f));
        CHECK(so.apart > last - 0.02f, "octaves %d: not more apart", o);
        last = so.apart;
    }
    /* the gate and the slides by the rules whatever the pattern: at Chaos 1, a long run checked step by
     * step: an attack exactly where the step before did not slide into this one */
    fresh(0.8f);
    ac.p[NACID_P_CHAOS] = 1.0f;
    ac.p[NACID_P_SLIDES] = 0.6f;
    ac.p[NACID_P_EVOLVE] = 1.0f;
    nacid_set_length(&ac, 64);
    int wrong = 0, slid = 0;
    nacid_note prev = { 0, 0 };
    for (int g = 0; g < 8; ++g) {
        nacid_generate(&ac);
        for (int s = 0; s < 64; ++s) {
            nacid_note now = ac.step[ac.pos];
            run_t r;
            run(1, SR * 0.11f, &r);
            if (now.flags & NACID_ON) {
                int want = !((prev.flags & NACID_ON) && (prev.flags & NACID_SLIDE));
                if (r.retrig[0] != want || r.phase_bad)
                    printf("  step %d: flags %d note %d, before %d %d: attack %d, phase %d\n", s, now.flags, now.note,
                           prev.flags, prev.note, r.retrig[0], r.phase_bad);
                wrong += r.retrig[0] != want || r.phase_bad;
                slid += !want;
            } else {
                wrong += ac.gate_on;
            }
            prev = now;
        }
    }
    CHECK(!wrong && slid > 20, "chaos 1: %d steps against the rules (%d slid)", wrong, slid);
    printf("chaos 100 %%, 512 steps: attacks and slides by the rules (%d slid), %d wrong\n", slid, wrong);
}

int main(void)
{
    nfm_init(&eng, SR, 1u);
    eng.master = 1.0f;
    test_gate();
    test_slide();
    test_accent();
    test_envmod();
    test_square();
    test_filter();
    test_generator();
    printf(fails ? "nacid: %d FAILED\n" : "nacid: all ok\n", fails);
    return fails != 0;
}

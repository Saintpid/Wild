/* SPDX-License-Identifier: GPL-3.0-only */
/* Single core: render-IRQ producer, higher-priority DMA consumer. Publish only complete
 * blocks. The consumer never touches the producer's uncommitted slot. */
#ifndef WILD_STREAM_H
#define WILD_STREAM_H
#include <stdint.h>
#define WILD_FRAMES 256u
#define WILD_SLOTS 2u
#define WILD_QUANTUM 64u
#define WILD_FENCE() __asm__ volatile("" ::: "memory")
typedef void (*wild_renderer)(int32_t *, uint32_t, uint32_t);
typedef struct {
    int32_t pcm[WILD_SLOTS][WILD_FRAMES * 2u];
    volatile uint32_t written, read;
    uint32_t partial;
    volatile uint32_t produced, consumed, underruns, recoveries;
    volatile uint32_t last_ticks, peak_ticks;
    int32_t last[2];
    uint32_t gain, recovering;
} wild_stream;

/* One bounded quantum, with all synth state owned by the renderer. */
static inline int wild_produce(wild_stream *s, wild_renderer render)
{
    uint32_t w = s->written;
    if (w - s->read >= WILD_SLOTS) return 0;
    WILD_FENCE();
    render(s->pcm[w % WILD_SLOTS] + 2u * s->partial, WILD_QUANTUM, 4096u);
    s->partial += WILD_QUANTUM;
    s->produced += WILD_QUANTUM;
    if (s->partial == WILD_FRAMES) {
        s->partial = 0;
        WILD_FENCE();
        s->written = w + 1u;
    }
    return 1;
}

/* Exactly one DMA half. No synthesis, allocation, locks or waiting. */
static inline int wild_consume(wild_stream *s, int32_t *out, uint32_t gain)
{
    uint32_t r = s->read, i, c;
    int available = r != s->written;
    WILD_FENCE();
    if (gain > 4096u) gain = 4096u;
    if (!available) {
        ++s->underruns;
        for (i = 0; i < WILD_FRAMES; ++i)
            for (c = 0; c < 2u; ++c)
                out[2u*i+c] = i < 64u ? (int32_t)((int64_t)s->last[c] * (63u-i) / 64) : 0;
        s->last[0] = s->last[1] = 0;
        s->recovering = 1;
        return 0;
    }
    if (s->recovering) ++s->recoveries;
    for (i = 0; i < WILD_FRAMES; ++i) {
        /* A master change ramps across this half, independent of lookahead. */
        int32_t g = (int32_t)s->gain + ((int32_t)gain - (int32_t)s->gain) * (int32_t)(i+1u) / (int32_t)WILD_FRAMES;
        for (c = 0; c < 2u; ++c) {
            int32_t v = (int32_t)((int64_t)s->pcm[r % WILD_SLOTS][2u*i+c] * g / 4096);
            if (s->recovering && i < 64u) v = (int32_t)((int64_t)v * (i+1u) / 64);
            out[2u*i+c] = v;
            s->last[c] = v;
        }
    }
    s->gain = gain;
    s->recovering = 0;
    s->consumed += WILD_FRAMES;
    WILD_FENCE();
    s->read = r + 1u;
    return 1;
}
#endif

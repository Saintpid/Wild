/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "n001se.h"
#include "wild_stream.h"

static wild_stream stream;
static int32_t reference[200 * WILD_FRAMES * 2u], out[WILD_FRAMES * 2u];
static void setup(void)
{
    ne_init();
    ne_set(G_TEMPO, 300);
    ne_aset(A_RESO, 100);
    ne_aset(A_SLIDES, 100);
    ne_aset(A_OCTAVES, 100);
    ne_play(1);
}
static uint32_t ramp;
static void count_render(int32_t *p, uint32_t n, uint32_t gain)
{
    uint32_t i;
    (void)gain;
    for (i=0; i<n; ++i) { p[2*i]=(int32_t)ramp++; p[2*i+1]=-p[2*i]; }
}
int main(int argc, char **argv)
{
    unsigned b, q, i;
    clock_t start;
    FILE *f;
    assert(argc == 2 || argc == 3);
    if (argc == 3) {
        setup();
        for (b=0; b<200; ++b) ne_render(reference+b*WILD_FRAMES*2u, WILD_FRAMES, 4096);
        f=fopen(argv[1], "wb"); assert(f);
        assert(fwrite(reference, sizeof reference, 1, f)==1); fclose(f); return 0;
    }
    f=fopen(argv[1], "rb"); assert(f);
    assert(fread(reference, sizeof reference, 1, f)==1); fclose(f);
    setup();
    stream.gain=4096;
    start=clock();
    for (b=0; b<200; ++b) {
        for (q=0; q<4; ++q) {
            assert(wild_produce(&stream, ne_render));
            if (q<3) assert(stream.written==b); /* incomplete PCM is invisible */
        }
        assert(wild_consume(&stream, out, 4096));
        assert(memcmp(out, reference+b*WILD_FRAMES*2u, sizeof out)==0);
    }
    printf("200 blocks exact against unbuffered 303+drums+FX; %.3f s CPU for %.3f s audio\n",
        (double)(clock()-start)/CLOCKS_PER_SEC, (double)(200u*WILD_FRAMES)/44100.0);
    assert(stream.produced==stream.consumed && !stream.underruns);
    /* Full queue/backpressure, uint32 wrap, exact ordering for many swaps. */
    memset(&stream,0,sizeof stream); stream.gain=4096;
    stream.written=stream.read=0xfffffffeu;
    for (q=0; q<8; ++q) assert(wild_produce(&stream,count_render));
    assert(!wild_produce(&stream,count_render));
    for (b=0; b<4000; ++b) {
        assert(wild_consume(&stream,out,4096));
        for (i=0;i<WILD_FRAMES;++i) assert(out[2*i]==(int32_t)(b*WILD_FRAMES+i));
        for(q=0;q<4;++q) assert(wild_produce(&stream,count_render));
    }
    /* Starvation fades once, then stays silent, and resumes without stale PCM. */
    memset(&stream,0,sizeof stream); stream.last[0]=6400; stream.last[1]=-6400;
    assert(!wild_consume(&stream,out,4096));
    assert(out[0]==6300 && out[126]==0 && out[127]==0);
    assert(!wild_consume(&stream,out,4096));
    for(i=0;i<WILD_FRAMES*2u;++i) assert(!out[i]);
    for(q=0;q<4;++q) assert(wild_produce(&stream,count_render));
    assert(wild_consume(&stream,out,4096));
    assert(stream.underruns==2 && stream.recoveries==1);
    puts("wild stream: ordering, partial publication, backpressure, wrap, starvation and recovery OK");
    return 0;
}

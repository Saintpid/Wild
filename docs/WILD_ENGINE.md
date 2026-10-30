# Wild 0.1.4 - Mixer and encoder update

GLO + OCT- toggles the five-channel mixer; Audio Load is now GLO + OCT+.
SELECT controls drum 1, KNOB1/2/3 drums 2/3/4, KNOB4 the acid voice.
Uses existing saved 0..100 track levels, with undo/redo. Mixer refresh is 15 Hz
using short strips of the existing canvas; no extra pool buffers are allocated.
Signal meters use latched voice-level maxima between UI frames and a 600 ms
peak hold. These are relative indicators, not calibrated dBFS/effect-return meters.

Fixed encoder learning: a pause at a complementary electrical contact previously
caused 11 full cycles to count as 21. Full-cycle resolution now stays fixed
across slow turns, intermediate pauses, fast flicks and direction reversals.
This matches the full-cycle FM-1 encoder tests; physical detents need user QA.

The scheduling, feature and diagnostics details below are historical by version.

# Wild 0.1.2 update (supersedes scheduling details below)

All musical and effect features remain. SOFT0 (IRQ 120, priority 2) performs
up to four 64-frame quanta per DMA wake. ALNK priority 3 only consumes PCM,
then requests SOFT0; TIMER5 priority 4 retains input scan and USB audio service.
The foreground only handles control/display/storage and cannot delay the
renderer except in the existing explicit IRQ-off flash windows. The PCM queue
is unchanged. Snapshots now hold DSP state briefly, as restores already did;
render defers if held. No foreground/LCD producer remains after boot priming.

Normal display capped at 30 Hz. GLO + OCT- opens diagnostics, drawn at 4 Hz.
The page shows render budget use (current, EMA over 32 quanta, peak since
priming), output IRQ peak, elapsed display peak (including preemption), queued
blocks, empty-buffer events, DMA lateness and linked RAM/pool headroom.
These are not a whole-chip CPU meter or a dynamic heap monitor. Host preview
has no hardware memory/IRQ measurements. See MANUAL.txt for control details.

Linked 0.1.2 resource results and tests are recorded in build/wild-0.1.2-build.log
and build/host. Software scheduling and interrupt nesting need physical FM-1
verification; a host test cannot prove interrupt delivery on AC79. Earlier
0.1.0/0.1.1 timings do not validate this version. 0.1.1 booted on the user's
FM-1 but stuttered; 0.1.0's startup crash was fixed in 0.1.1.

The historical implementation and resource investigation follows.

# Wild 0.1.0: architecture and verification

Base: n001se 1.7.1, commit b3a4ac2, from Saintpid/n001se's
claude/project-thread-uvgf52 branch. GitHub main was only 1.6.1;
C:/Projects/fm1_open is an older FM-only implementation.

## Resources and scheduling

The actual platform is JieLi AC79/r3, single-precision DSP, 44100 Hz,
stereo Q23 int32 PCM, 256-frame DMA halves (5.805 ms). The linker reserves
98304 bytes data/BSS and 344064 bytes for the large buffer pool.
One 16-step phrase at 120 BPM would need 705600 bytes of stereo int32 PCM,
so full-phrase storage is unsuitable.

wild_stream.h uses two 256-frame software slots: 4096 PCM bytes plus 52 bytes
of state. Main-loop passes and LCD transfer boundaries render 64 frames per
service call. Four quanta publish a complete slot. The ISR consumes one slot,
ramps master gain and taps output to USB. No synthesis, locks, allocation,
storage access or waiting occurs in the stream consumer. Existing DMA, panel
scan, flash protection and update paths remain. Firmware never writes WAVs;
the inherited host tests can export recordings for analysis.

Software lookahead is at most 512 frames (11.61 ms), in addition to DMA latency.
The foreground renderer owns all synth state. USB MIDI polling defers during
each render quantum; nested USB audio service remains enabled. Commands drain
at the next quantum and affect future PCM. Stop/Panic wait only for the small
lookahead and DMA, not a phrase. Master gain acts at consumption. Existing
pattern replacement semantics are retained; there is no next-phrase commit
mode or speculative state rewind.

The queue is single-core foreground/interrupt SPSC, using the platform's
compiler publication fences. Incomplete blocks are invisible to playback.
Each side owns its monotonic index; uint32 rollover is tested. This queue is
not designed for multicore use.

If empty, output fades to zero over 64 samples and stays silent. Recovery
fades in over 64 samples. No stale PCM is replayed. Render time pauses during
starvation instead of fast-forwarding; sustained starvation may lose external
clock alignment and is a hardware acceptance failure. Flash erase and heavy
display activity require specific hardware checks.

## Musical instructions and sound models

Existing nseq/nacid patterns remain the structured musical representation,
including Chaos, swing, rests, octaves, accents and outgoing slides.
nacid_event supplies pitch, note/rest, accent and gate duration/hold to
nacid_apply_event, the initial 303 model. Oscillator phase, RC pitch slide,
filter history, resonance, VCA/envelopes, accent capacitor and effects persist
between calls. Buffer boundaries never reset the synth.

wild_produce accepts a renderer function; ne_render is the first renderer,
retaining the existing 303, drums and effects. A future renderer can use the
same PCM consumer. The generator still shares its nacid owner with voice
state: this is a small separation of responsibilities, not a fully independent
phrase compiler or a complete multi-model framework. Independent sound
evolution between phrases and additional sound models are future work.

## Verified results (2026-10-30)

- Firmware compiled and linked with the JieLi Linux toolchain under WSL.
- App image: 168952 bytes. Data/BSS: 67584/98304 bytes (30720 spare).
- Pool: 335816/344064 bytes (8248 spare), only 56 bytes above the existing
  8192-byte safety margin. Keep the linker guard enabled; do not enlarge it
  casually. Soft-double and register-access checks pass.
- Package: build/Wild-0.1.0.fwsc, identity FM-1_827. The stable test alias
  build/Wild.fwsc is identical. Package CRC, loader and installer simulation
  checks pass. The optional Node comparison is skipped because Node is absent
  in WSL; this does not skip package CRC verification.
- All host suites pass: DSP, undefined-behavior sanitizer, acid, Glue, Space,
  storage, updater and eight whole-app scenarios. The host simulator also
  uses the same two-slot stream.
- New test: 200 buffered blocks exactly match direct 256-frame rendering from
  identical fresh processes at 300 BPM, maximum resonance, slide probability
  and octave probability. This verifies 51200 frames (1.161 seconds) of full
  303/drum/FX PCM without lost/duplicated samples or added normal-boundary
  discontinuities. Existing acid tests separately cover loop, gate and slide
  rules. Test also covers 4000 swaps, partial publication, backpressure,
  uint32 rollover, starvation fade, silent starvation and recovery.
- Host render time: about 0.015 CPU seconds per 1.161 seconds audio. This is
  desktop CPU throughput, not a measurement of AC79 performance.

Logs: build/wild-build.log and build/host/*.log. Test sources use separate
fresh processes for exact reference comparison because inherited ne_init does
not reset every DSP static for an in-process restart.

## On-device diagnostics and remaining acceptance

wild_audio has produced/consumed sample counts, underruns, recoveries and
last/peak ticks per 64-frame render quantum. Divide ticks by FM1_TICKS_PER_US;
compare against 1451 microseconds of produced audio. The CPU screen reports
the latest foreground quantum's load. om_dbg.last_us/max_us track ISR playback
separately; audio_xruns includes starvation and DMA lateness. Pause the CPU
for a consistent debugger snapshot of several fields.

No physical FM-1 has been flashed or tested. Measure actual render peaks,
headroom and underruns during long runs with high resonance, large sliding
octave jumps, fast tempo, all effects, display redraws, preset saves, USB audio
and dense MIDI. Verify external-clock stability, Stop/Panic and note latency,
master ramp, buffer clicks, boot/safe mode/updater and project compatibility
on the installed firmware. Compare CPU use with n001se 1.7.1 under the same
workload. Additional lookahead does not make synthesis cheaper.

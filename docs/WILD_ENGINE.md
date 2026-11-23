# Wild 0.3.42 - Rumble pages without a child track

The child-row UI from 874 is removed. Five fixed main rows and their bar strip
are drawn again. SELECT and OCT-/+ select the five real tracks. No independent
Rumble selection or child-mode flag remains. Rumble editing context derives
from the selected Kick and current ENV effect page. Kick ENV cycles Sound/Tone/
Rumble/Texture/Layer/Sound; non-Kicks skip effect pages. NEW SOUND in that context
randomises the effect only. Steps, NEW PATTERN, HOME, EDIT, FX and LFO retain
ordinary main-track behaviour. Assignments and stored per-track effect values
remain. Format 19 and all 875 audio processing are unchanged.

Regression coverage checks five-track navigation with four Kicks, ENV wraparound,
step editing on effect pages, isolated randomisation and persistence. Firmware
resource checks remain enabled. Logs: build/wild-0.3.42-build.log and -tests.log.

# Wild 0.3.41 - tempo-scaled offbeat envelope

nr_kick schedules a sample-counted hold and smoothstep rise relative to the
actual successful hit. Hold ends at 35% of a quarter; the final 15% rises to full
envelope level at round(quarter * 0.5). The previous envelope fades down under
retriggering, without restarting the tank or creating an additional kick transient.
Duck still blends between dry envelope and this contour. Default Duck is 100;
NEW SOUND preserves its value. Stored Duck values are not rewritten.

During the hold, feedback is blended towards 0.99 by Duck amount to retain the
excitation. As the rise begins, feedback glides back towards the Tail setting.
This changes the envelope/decay rather than delaying the audio through a long
buffer. Existing tank topology, seed families and Side filtering are retained.
Stop, bypass/reset and Shape replacement cancel pending timing. The newest kick
owns the contour: closely spaced hits can suppress a rise, keeping kick priority.

Tests exercise exact sample counts at five tempos, the rise slope, cancellation
and retriggers, and a real sequencer hit delayed by swing. At 120 BPM/max swing,
a Kick scheduled on sixteenth 2 fires at sample 8269; its envelope reaches full
level 11025 frames later (the last frame processed is 19293). These checks do not
establish subjective groove or physical FM-1 load. Project format stays 19.
Logs: build/wild-0.3.41-build.log and build/wild-0.3.41-tests.log.

# Wild 0.3.40 - independent kick effect children

The 873 oscillator prototype is superseded. Each drum slot owns a nrumble tank
and a 64-frame input accumulator. The FM voice render taps actual mono kick audio
before master gain. Only the owning Kick's unmuted, enabled layer accepts input;
other drum kinds cannot excite it. Successful scheduled triggers set the owning
layer's duck envelope and tempo-aware modulation, including loose/swing timing.

A two-line orthogonal scattering network runs at 11025 Hz with int16 state,
input high-pass/drive, damping and weak signal-dependent noise. Shape selects
line lengths and motion phase/rate; NEW SOUND also randomises the exposed controls.
No pitched oscillators produce the wet sound. One sine lookup per tank tick is
only an amplitude LFO. Full-rate reconstruction, a 25 Hz Mid HP, gain/duck ramps
and four 150 Hz Side HP poles follow the tank. The Side cutoff is a gradual slope.
Master processing can change the final result. Shape replacement fades before
resetting the tank; new excitation then comes from the next kick. Stop, panic,
parent/child mute and instrument changes release it. Bypass avoids tank processing.
Each parent can route its layer to the direct bus when Drift is bypassed.

Nine appended T_R* parameters store each layer in the normal project, bank,
automation and undo system. Shape is excluded from automation; replacing a tank
is an explicit action. Retired global Rumble fields remain as unused slots.
The old four encoder assignment positions now point to per-track level, colour,
tail and motion, followed by the additional layer targets. Format 19 stores
1848-byte projects (3696-byte bank pairs); older T counts/reserved lengths are
read explicitly. Undo retains its fixed 17760-byte allocation.

UI keeps five real track IDs and an ephemeral child-selection flag. SELECT walks
parent/child rows; OCT-/+ choose the real tracks. ENV on Kick can enter the child
without SELECT. Child step keys and NEW PATTERN cannot modify a sequence.
NEW SOUND randomises only layer parameters in one undo operation, enabling an
off layer at 55 without changing an existing nonzero level. Three ENV pages offer
all controls; parent identity changes invalidate the child selection safely.
The grid scrolls full rows if four Kicks cannot fit, without shrinking steps.

GLO order: FX bus, Master, Glue, Groove, Setup, Scene, Controls. Global FX defaults
30/50/100/100; FX on ordinary tracks offers Track FX and Mod. Resource limits
remain enabled. Logs are build/wild-0.3.40-build.log and -tests.log. Host checks do
not establish sound quality, CPU headroom or dropout-free playback on AC79.

# Wild 0.3.39 - Rumble return and encoder assignments

One nrumble state adds a kick-excited dry return alongside the acid source.
nfx_trigger notifies successful kick hits at their actual scheduled time.
No FM voices or sample/delay buffers are allocated. Three shared-table oscillators
provide a mono fundamental and quieter texture partials. Tonality moves partials
towards harmonic ratios; Movement glides a repeating motif relative to scene root
and acid Tune, with a rhythmic amplitude contour. Tail is tempo-scaled. Kicks
duck the return smoothly. Muted/zero-level kicks provide no excitation.
Master Level scales the return. Stop, panic, MIDI clock stop and state restore
release it. Audio Load counts the new synthesis under FX.

Side passes four 150 Hz high-pass poles AFTER amplitude modulation. The transition
is gradual, not a brick-wall crossover. Subsequent master processing can alter
the result. Off is bit-exact bypass after its fade. Enabled silence skips synthesis.

FX page 4 is Rumble. GLO page 6 is Controls. SELECT and ALGORITHM use 0 for their
original functions and 1..N for PRESETS target index + 1. PRESETS choices are
appended, preserving prior indices. Assignment selectors cannot be automated;
they are retained across banks like Setup values. SELECT retains its mixer and
preset-list roles in those contexts. Physical page buttons remain available.

Format 18 adds six globals and reduces reserved bytes by twelve. Projects remain
1776 bytes; banks and undo journal retain their sizes. Older formats read their
own parameter counts and reserved length. The old Rumble drum kind is unchanged.

Host checks do not establish sound quality or AC79 performance. Compare Audio
Load and underruns off/on in the same dense patch; evaluate against the user's
music references. Full firmware resource and test results are recorded in memory.

# Wild 0.3.38 - 303 generation and latent steps

Notes sets round(playing length * percentage / 100) active steps. The existing
generator supplies a motif; weighted gate corrections meet the count, changing
matching motif copies together when possible. This runs only on new pattern
generation, not on rendering or restore. Evolve and manual edits can change the count.

Inactive generated steps hold pitch, accent and slide. Live step activation keeps
those properties; the accent/plain/off cycle retains pitch and slide on switching
off. Grid mapping preserves inactive data and gives sounding steps priority in
collisions. Inserted sixteenths inherit nearby inactive properties. Saved format
17, parameter indices and internal identifiers remain unchanged.

Resource-checked firmware and complete host suite pass for 0.3.38 / FM-1_872:
image 190760 B, data/BSS 81044/90112 B, RAM code 35988/40960 B,
pool 335364/344064 B. Host checks include exact counts on both grids, actual
keys, persistence, Evolve and a fixed overload through the final limiter.
These tests do not establish real-device audio continuity or performance.

# Wild 0.3.30 - eight banks and reversible live copying

The active bank array is eight project_t records (1776 B each). White key 16 remains
Chance. Tap/release SEL selects a bank; holding SEL while choosing copies onto it.
An empty selection also copies. Automation is copied only if the whole operation fits.

Undo uses a fixed 17760-byte, four-byte-aligned journal with variable-size records.
Each record stores a project and Chance metadata; bank copies additionally store the
destination and the 3696-byte automation pool. Records swap with live state for redo,
without allocating a second snapshot or writing flash. Ordinary records with automation
also save the pool, so subsequent edits can be unwound before undoing a bank copy.
Oldest complete records expire to make room; nine small edits, three automation edits
or two full copies fit. Bank changes retain tempo and shared device settings.

Version 0.3.30 / FM-1_864: image 186608 B, data/BSS 80548/90112 B, RAM code
35692/40960 B, pool 335364/344064 B. The resource guards remain enabled.
Host tests cannot establish real-device audio continuity during snapshots/restores.

Project format 16 and stored song-part IDs remain compatible for banks 1-8. Old songs
with parts for banks 9-12 are refused before replacement. Legacy active storage is
protected from all saves until explicit Clear All or a compatible song load. Return to
0.3.29 to reorganise that material; no silent truncation of saved songs is performed.

The following sections describe earlier releases.

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

Software lookahead is at most 512 frames (11.61 ms), in addition to DMA latency (0.3.15: 768 frames, 17.4 ms).
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

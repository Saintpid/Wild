# Wild v001 - 2026-10-30

- User requested Wild as a separate experimental firmware based on n001se.
- Source: Saintpid/n001se commit b3a4ac2, branch claude/project-thread-uvgf52,
  n001se 1.7.1. Main was only 1.6.1. C:/Projects/fm1_open was an older FM base.
- Local branch wild. No push, publication or physical device flashing.
- Version 0.1.0, BUILD_ID 827. Product name Wild; USB name Wild FM-1.
- See docs/WILD_ENGINE.md for architecture, results and hardware acceptance.
- WSL dependencies installed: GCC, Pillow and Clang sanitizer runtime.
  Downloaded Linux toolchain and three hash-checked SDK files under ignored
  build/jieli and build/sdk. Normal build/test commands are in README.md.
- Pool has 8248 bytes spare, just 56 above the required 8192-byte guard.
- Render in foreground in 64-frame quanta; playback ISR only consumes PCM.
  Keep foreground wild_rendering separate from ISR om_dbg.in_audio.
- Retain upstream attribution and internal n001se identifiers. Do not push to
  the inherited upstream remote without a user request.

- Final checks: firmware, package CRC/OTA/installer and full host suite pass.
  Optional Node comparison skipped (Node absent in WSL). Deliberate reference
  corruption correctly fails the new PCM test; reference restored afterward.

## 0.1.1 / 828 - hardware startup correction
- User reports 0.1.0 fails boot; photo shows safe mode, PC 02007B18.
- Matching 0.1.0 disassembly: PC follows store to ALNK vector 0x1c7fe2c.
- audio_init had been moved after fm1_guard_lock_top: illegal vector write.
- Move normal-start guard lock after audio_init; register with IRQs disabled.
  Safe-mode guard ordering remains intact. Physical retest required.
- 0.1.1 build and full host/package suite pass; boot-order regression passes.
- User restored working firmware; previous crash RAM may no longer be available.

## 0.1.2 / 829 - priority and diagnostics
- User confirms 0.1.1 boots and sounds good but stutters heavily; CPU display
  photo shows ~57% and many xruns. Keep ALL functions for now (latest decision).
  Earlier drum/EQ/Width removal ideas are deferred, not authorized for this fix.
- Render moved to SOFT0 priority 2; DMA priority 3, input/USB TIMER5 priority 4.
  DMA requests a bounded four-quantum refill. Main no longer produces PCM.
  All vectors attached before top-memory protection. Snapshot/restore hold
  makes render defer instead of injecting silent PCM during foreground edits.
- GLO + OCT- opens/closes diagnostics; no ordinary button release actions.
  4 Hz diagnostic drawing, normal display 30 Hz, input polled between frames.
- Render average = 32-quantum EMA, peak since post-priming. Values measure
  render budget, not whole-chip CPU. Memory is linked static headroom.
- Physical SOFT0 scheduling/interrupt nesting and sustained audio must be tested.
- Final 0.1.2 build passes: image 170420 B; data/BSS 67632/98304 B;
  pool 335816/344064 B. Full host/UBSan/package/loader/installer suite passes,
  including diagnostic chord in both press orders while playing, preserving
  the original view. Diagnostic screenshot visually inspected.
- SHA256 Wild-0.1.2.fwsc: 04b0a536c9a388fee22466084cba445ae0e967026f7b7a54351ac98ae6fe00d8
- Versioned ELF/disassembly/bin preserved alongside package for crash lookup.

## 0.1.3 / 830
- Tempo retained across SEL A/B and Copy via G_TEMPO in KEEP.
- Quarter-note ticks mark steps 1/5/9/13, including rests.
- Explicit tempo edits and preset loading keep their existing semantics.
- Build, package, full host suite and new SEL tempo scenario pass.
  Beat-marker screenshot visually checked; hardware retest pending.

## 0.1.4 / 831 - five-channel mixer
- GLO+OCT- now mixer, Audio Load moved to GLO+OCT+.
- SELECT -> channel1, KNOB1..4 -> channels2..5. Existing track/acid levels,
  clamped 0..100, saved through existing project machinery.
- Five faders and relative voice signal meters; 600ms peak hold, 15Hz drawing.
- Per-render peak latches capture maxima between UI frames; short state hold
  protects read/reset. No pool buffers added, no musical/effect functions removed.
- User also reports 1/3/5 jumps, cannot select 2 bars. Bars already bypass
  acceleration. Reproduced decoder bug: after a pause at complementary contact,
  11 full cycles count 21; slow continuous turn of 10 cycles counts 0.
- Fix full-cycle detent anchor; no rest-state relearning while turning.
  Preserves fast skipped-state handling; tests include slow forward/reverse,
  intermediate pause, flick, bounce and 1/2/3/2-bar UI edits.
- Final 0.1.4 build/package and full host/UBSan suite pass, including mixer
  mapping, clamp, undo/redo, chord isolation, diagnostics relocation and exact
  1/2/3/2-bar edits. Encoder negative tests failed before the fix, now pass.
- Mixer screenshot visually verified. Physical mixer/encoder QA still needed.
- Image 171684 B; data/BSS 67712 B; pool unchanged 335816 B (8248 B free).
- Versioned ELF/dis/bin saved for crash lookup; firmware SHA256:
  06283cc145933d6963bff9850e0ad2da36e254557c2809a7c0dd4f0a62012c3e

## 0.2.0 / 832 - diode ladder, 303 waves
- Diode-ladder filter from n001se test fw 826 (commit 2164304) applied as is;
  about 20 % more host time for the acid voice. FM-1 Audio Load check needed.
- G_ACIDWAVES (Scene page KNOB 2): Original (default, saw/square) or All for
  a new acid sound. Project format 12; kept through SEL.
- Repo: github.com/Saintpid/Wild (public). Builds go to
  C:/Projects/M-Vave FM1 n001se/Firmware Wild.

## 0.2.1 / 833 - Drive without aliasing
- Cause: nfm_sat's loudness match set its gain every 8 samples (control rate) in steps: sidebands
  between the harmonics, -49 dB against -85 dB dry on a held saw. Not the curve (its static aliasing
  was under -70 dB on the filtered 303).
- Fix: gain glided per sample (0.003); curve now cubic x - 4x^3/27 (flat at +-1.5) with first-order
  ADAA (closed form on the cubic part, no divide), pre 0.75..5.25. Square's clip uses the same ADAA
  (gain 2.65 fits the old Pade shape). nacid_route keeps DC and gain over a Drive change.
- nacid_test test_clean: note period 403 1/3 samples so aliasing and control-rate products land
  between the harmonics; fails on 0.2.0 (drive -50 dB, square -36 dB).

## 0.3.5 / 839 - the 303 at 2x
- Oscillator + diode ladder (with its feedback high-pass) run at 2x; halfband 27 taps (7 multiplies,
  flat to 17 kHz, -57 dB from 27 kHz) back down; VCA, drive, inserts at 1x. Cutoff's top kept where it
  was at 1x (tan_pi argument capped at 0.3 of the 1x rate), so only aliasing and warping change.
- Between-harmonics (Hann, test_clean): saw open -58.3 -> -66.9 dB, square -47.2 -> -51.5. A
  Blackman-Harris 4x reference: full resonance open saw -54 (1x) / -63 (2x) / -70 (4x).
- Cost on host: acid voice +70 % (0.099 -> 0.172 s per 60 s), whole ne_render about +5 %.
- Not oversampled: FM ops (inherent, all voices = too costly), fold (x11 bandwidth on mostly low
  sounds; hats are noise anyway), crush (aliasing is the point), Glue inflator (cubic, HF low in level).


# Wild 0.3.42 / 876 - 2026-11-12

- User chose removing Rumble from the channel grid; keep it as a Kick effect.
  Five fixed main rows and normal SELECT/OCT navigation restored. No child flag
  or virtual selection remains. Kick ENV cycles Sound/Tone/Rumble/Texture/Layer/
  Sound. Other instruments skip Rumble pages. NEW SOUND on those three pages
  randomises only Rumble; elsewhere it changes the instrument. Steps and NEW
  PATTERN edit the selected main track normally, including on effect pages.
- Removed child-specific HOME/EDIT/FX/LFO behaviour and scrolling rows. Assigned
  Rumble encoder targets, per-track saved settings and format 19 are retained.
  DSP is unchanged from 875, including the offbeat swell and Duck preservation.
- Resource-checked firmware and complete host/UBSan/package/installer/scenario
  suite pass. Controls tests cover full ENV cycle, regular step editing, isolated
  randomisation/undo/save and exactly five navigation targets with four Kicks.
  Rumble-page screenshot visually checked: only the five real sequencers appear.
- Image 195128 B; data/BSS 87596/90112 B; RAM code 37988/40960 B;
  pool 335368/344064 B. Logs build/wild-0.3.42-build.log and -tests.log.
- Delivered ../Firmware Wild/Wild-0.3.42.fwsc; SHA256
  494dcda4ca00a665dc18f49f060de8654061bff07449189d9aaf778108ee29ed.
  Versioned ELF/dis/bin retained. No push/publication or device flashing.

# Wild 0.3.41 / 875 - 2026-11-11

- User says 874 sounds much better. Requests more drive by moving Rumble's
  weight to the eighth after each kick, without an attack; uses Duck at 100.
- Sample-counted hold until 35% of a quarter, smoothstep rise over the next 15%,
  full envelope at round(quarter/2) after the actual successful kick. Prior
  envelope fades down on retrigger; newest kick owns timing. Stop/reset cancel
  pending rise. Dense kicks can keep it down, retaining kick priority.
- Feedback blends towards 0.99 by Duck amount while hidden to retain excitation;
  returns smoothly to Tail feedback as the rise begins. No long audio buffer.
  Tank topology, Shape families and Side filtering stay. Format remains 19.
- Duck defaults to 100. NEW SOUND preserves the user's Duck setting while
  randomising other timbre controls. Existing saved Duck values are retained.
- Resource-checked firmware and full host/UBSan/package/installer/scenario suite
  pass. New tests cover five tempos, smooth rise, retriggers, Stop cancellation,
  Duck preservation and real swing-delayed scheduling (120 BPM: hit sample 8269,
  envelope full at last processed frame 19293, 11025 frames after the trigger).
- Image 196004 B; data/BSS 87596/90112 B; RAM code 37988/40960 B;
  pool 335368/344064 B. Logs build/wild-0.3.41-build.log and -tests.log.
- Delivered ../Firmware Wild/Wild-0.3.41.fwsc, SHA256
  8de5ede8a7925d8b4d9cecfa205153935f683cdde661d23f9f45c7d7acba6be8.
  Versioned ELF/dis/bin retained. No push/publication/flashing. Groove/CPU
  acceptance needs physical FM-1 listening and diagnostics.

# Wild 0.3.40 / 874 - 2026-11-11

- User reports 873 sounds like blowing across a bottle; the old Rumble sounded
  better. Authorized replacing that oscillator prototype with a deeper textured
  effect layer per Kick, wider controls and NEW SOUND randomisation. Global FX
  requested under GLO with default Reverb/Delay/Filter/Drive 30/50/100/100.
- Actual kick audio now feeds an independent compact two-line scattering tank
  per parent. Quarter-rate tank, full-rate gain/duck/interpolation and Side HP
  (four poles at 150 Hz), no extra FM voices or pool buffers. No audio input
  means no generated tone. Shape changes fade out/reset, then await the next kick.
- Each actual Kick shows a child row without steps. SELECT visits children;
  OCT-/+ intentionally retain real-track navigation. Kick ENV reaches Rumble
  even with SELECT remapped. Child ENV cycles Rumble (level/tail/colour/drive),
  Texture (duck/width/movement/shape), Layer (mute). HOME returns to parent.
  NEW SOUND randomises layer only, keeps nonzero level or enables it at 55.
  NEW PATTERN and step keys do not alter the parent in child context.
- Four Kick children are supported; list scrolls full rows when necessary.
  Child selection invalidates safely if parent stops being Kick. Mute/level
  follow parent. FX on child opens Layer; LFO opens Texture. Global FX is now
  the first GLO page: FX bus/Master/Glue/Groove/Setup/Scene/Controls. FX on normal
  tracks cycles Track FX and Mod. Assignments also offer all child parameters.
- Format 19 appends nine T_R* values per track. Projects 1848 B, paired banks
  3696 B (within sector). Old layouts read their correct T count/reserved bytes.
  Undo allocation fixed at 17760 B instead of growing with sizeof(project_t).
  REC supports layer controls/mute, not Shape. Old global 873 fields are retired.
- Full host/UBSan/package/installer/scenario suite passes; final controls UBSan
  and final rebuilt package CRC/installer checks pass. Tests cover source silence,
  seeded differences, Side rejection, block invariance, live Shape fades, four
  active layers, isolation/undo/save, navigation and changing instrument kind.
  One/multiple-kick screenshots inspected. Sound quality and FM-1 load unverified.
- Image 195748 B; data/BSS 87580/90112 B; RAM code 37864/40960 B;
  pool 335368/344064 B. Logs build/wild-0.3.40-build.log and -tests.log.
- Delivered ../Firmware Wild/Wild-0.3.40.fwsc, SHA256
  7c5ca949ad76f0c950f77e49caf558cadd52b5eaa3d8fe87ebd5dd4886de6167.
  Matching versioned ELF/dis/bin retained. No push/publication/device flashing.

# Wild 0.3.39 / 873 - 2026-11-11

- User authorized implementing a separate Rumble FX return plus assignable SELECT
  and ALGORITHM, defaulting to their original Track/Page roles. Rumble must also
  be available in FX. User explicitly waived old-project sound compatibility.
- FX page 4: Rumble level (0 off), Tonality, Tail, Movement. GLO page 6: Controls,
  with Select/Algorithm/Presets assignments. Existing PRESETS choices appended;
  choice 41 for Select/Algorithm is Rumble. Page buttons permit recovery.
- A fixed three-oscillator kick-excited dry return uses no FM voices or delay
  buffers. Gliding sparse pitch motif, tempo tail and ducking, smoothed controls
  and stop; four 150 Hz Side HP poles (gradual transition, not brick-wall mono).
  FX split includes this render cost. Off/silent skip per-sample synthesis.
- Actual successful kick events, including delayed sequencer/MIDI/live events,
  excite the return. Muted and zero-level kicks do not. Scene root and acid Tune
  inform pitch. Existing Rumble drum kind remains independent.
- Format 18: six globals consume twelve reserved bytes; project remains 1776 B,
  bank/journal size unchanged. Rumble is bank-local and automatable; assignments
  persist across banks and cannot be automated. Mixer/list SELECT keeps priority.
- Resource-checked firmware and entire host/UBSan/package/installer/scenario suite
  pass. Additional controls UBSan test covers actual REC through assigned ALGO.
  New pages' screenshots visually checked. FM-1 audio quality/load still untested.
- Image 192856 B; data/BSS 81156/90112 B; RAM code 37036/40960 B;
  pool 335368/344064 B. Logs: build/wild-0.3.39-build.log and -tests.log.
- Delivered ../Firmware Wild/Wild-0.3.39.fwsc, SHA256
  82a1fb9a2ac5dcf95236833e7b83b24a2c455b23332205f9e66958e1786b927f.
  Versioned ELF/dis/bin retained. No push, publication or physical flashing.
- Reference videos, extracted WAVs, frame sheet and analysis notes remain under
  ignored build/rumble-references. No direct agent listening was available;
  subjective sound quality must be judged by user, not inferred from measurements.

# Wild 0.3.38 / 872 - 2026-11-11

- User authorized improving PR #45 (ee603ef), imported locally onto wild by
  fast-forward. No GitHub merge/push or device flashing.
- Notes 0..100% now gives the exact rounded count in the playing length.
  Correct the existing generated motif with position-weighted gate changes,
  correcting identical motif copies together where the remaining count allows.
  Slides/Accents remain weighted probabilities with exact 0/100 endpoints.
  All three controls now display %.
- Generated inactive steps carry pitch/accent/slide; actual white-key activation
  retains them. Accent/plain/off cycle stays, off retains pitch and slide.
- Regrid preserves inactive properties, sounding notes win collisions, and
  inserted sixteenths inherit nearby properties with their gate off.
  Format 17 and parameter indices unchanged; stored patterns are not regenerated.
- Tests cover all Notes percentages, 1..4 bars, both grids and five Chaos levels,
  latent 0/100 endpoints, Evolve gate preservation, collisions, actual key presses
  and persistence. Deviance uses fixed notes rather than generated test music.
- Glue music test keeps its upper-ceiling check. Separate fixed overload test
  exercises the real final render limiter with Glue on/off. Disabling the limiter
  in a temporary source copy correctly fails that test.
- Final resource-checked firmware and entire host/package/installer/scenario suite
  pass. Additional nacid UBSan run passes. Percentage screenshot visually checked.
  Image 190760 B; data/BSS 81044/90112 B; RAM code 35988/40960 B;
  pool 335364/344064 B. Real FM-1 performance and gestures need hardware QA.
- Logs: build/wild-0.3.38-build.log, build/wild-0.3.38-tests.log,
  build/wild-0.3.38-nacid-ubsan.log, build/wild-0.3.38-limiter-mutation.log.
- Package SHA256 dbc09c2621fdba7d472f4b9b917d5cff87e88e8fdf7ddd99c7b55906ca78cb86.

# Wild 0.3.37 / 871 - 2026-11-10

- Drum HOME page: K2 Fill (live, as the 303's Notes; 0.3.54 retired Less), K3 empty.
  Round the requested fill to the nearest hit, then draw uniformly from that
  count down to the maximum whole-hit reduction. Use the generated length
  after random length selection, including triplets and shortened bars.
- Project format 17 retains the same layout and parameter indices. Older
  projects convert their drum generator settings using fixed bars or the saved
  random length as the reference. Stored patterns are copied as before.
- Resource-checked build and full host/UBSan/package suite pass. Image 190128 B,
  data/BSS 81044/90112 B, RAM code 35988/40960 B, pool 335364/344064 B.
  Logs: build/wild-0.3.37-build.log and build/wild-0.3.37-tests.log.
- Version 0.3.36 / 870 is in use by the separate Rumble PR #43.
- The local 0.3.30 worktree was backed up in stash
  "Wild local 0.3.30 before density sync 2026-11-10". Its bank changes are already
  in Saintpid/Wild main via c4766cc. Publish only to wild-source, not origin.

# Wild 0.3.30 / 864 - 2026-11-04

- Canonical local workspace: C:/Projects/M-Vave FM1 n001se/Wild, branch wild.
  Updated from Saintpid/Wild main 89356a7 (0.3.29), remote wild-source.
  origin still points to the inherited n001se repository: do not push there.
- Previous local 0.1.4 work preserved in stash "Wild local 0.1.4 before bank update
  2026-11-04" and backup/wild-before-bank-update-20261104. Do not apply that stash
  over current work without reconciling versions. No publishing or device flashing.
- User chose eight banks plus Chance on white 16. Tap/release SEL then white 1-8
  selects (empty: copy); held SEL + white overwrites and switches. Same-bank copy
  does nothing. Second SEL cancels selection. Select current bank to exit Chance.
  Copies capture the audible Chance scene; source keeps its pre-Chance values.
- Copies include automation; reject atomically if the shared 28-lane pool is full.
  17,760-byte variable-size swapping undo journal: at most nine ordinary edits
  without automation, three with automation, two full copies. Undo/redo crosses
  banks and restores destination contents/empty status, automation and Chance.
  Recording/deleting automation also participates. Load/Clear All/reboot reset undo.
- Project format remains 16; old song part IDs unchanged (automation still part 5).
  Reject loading/overwriting saved songs with bank 9-12 parts (-4). If active flash
  uses bank 9-12, show OLD BANKS / USE 0.3.29 and protect ALL saves; temporary defaults
  do not replace stored data. Explicit Clear All or compatible song load starts a new
  active workspace; saved legacy songs stay intact. Reorganise in 0.3.29 first.
- BUILD_ID in upstream was stale (857); CHANGELOG documented 0.3.29 as 863, so new
  identity is 864. README build commands and current version updated accordingly.
- Resource-checked build: image 186608 B; data/BSS 80548/90112 B (9564 spare),
  RAM code 35692/40960 B, pool 335364/344064 B (8700 spare; guard kept).
- Bank tests cover overwrite/empty copy, undo/redo, edits across banks, autosave and
  reboot, history eviction and redo truncation, Chance, automation exhaustion and
  recording, compatible songs and protection of legacy songs/startup flash.
  Physical FM-1 audio continuity and gestures still require hardware QA.
- Final firmware build, complete host/UBSan/package/loader/installer suite and all
  scenarios pass (build/wild-0.3.30-build.log, build/wild-0.3.30-tests.log).
  Bank unit tests also passed with AddressSanitizer; selection/replacement screenshots
  visually inspected. Delivered to ../Firmware Wild/Wild-0.3.30.fwsc with matching
  SHA256 5345f0d111ed37f66c86d0421b8b4c61dfc7c2ee4eecf5d5745412d62977a9d7.
  Versioned .elf/.dis/.bin retained in build for crash lookup. Changes remain local.

# Historical memory: Wild v001 - 2026-10-30

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


## 0.3.6 / 840 - triplets per track
- User asked for triplets as a per-track "time signature": 3 steps a quarter, shown on the step keys.
- T_GRID / A_GRID (project format 14), on a new HOME page 2 "Rhythm" (KNOB 1 Grid: 16ths/Triplets),
  for drums and the 303 alike; nothing moved off other pages (acid Level stays on its Track page).
- Triplet track: 12 steps a bar (max 96 drums, 48 acid), white keys 1-12; plays on global sixteenths
  0/1/2 of each quarter, delayed 0, 1/3, 2/3 of a sixteenth, unswung. Screen draws them time-aligned.
- Turning Grid = ne_regrid: hits to nearest step in time (16th 1234 -> T 1233, louder wins; back T 123
  -> 16th 123), bars kept. ne_tset/ne_aset(GRID) = flag only (restores already hold the layout).
- All scenario "expect view N" for N>=1 shifted by one (new view V_RHYTHM).

## 0.3.8 / 842 - drum velocities lean strong, Dynamics
- User: hits likelier strong, especially quarters, but a quarter can be weaker; 1..127; per-step
  velocity editing later (user still designing it).
- nseq velocity(): beat 85% x=0.35u^3, 15% x=0.35+0.65u; off-beat x=u^2(0.4+0.6u); vel=127-x*dyn*1.1.
  Own RNG (vrng) so hit placement keeps its stream. T_DYN (Rhythm page KNOB 2, default 50, format 16).
- drum_mood scales hits by 0.85 so the 303 Deviance reads the starting set as before.
- glue_test Width check now compares sides above 60 Hz (the lows go mono by design).

## 0.3.9 / 843 - two strengths on the step keys
- User: press empty -> strong (100, as before), again -> soft (80), again -> off; existing hit goes one
  strength down whatever it is (>90 -> 80, <=90 -> off). Must coexist with future exact per-step
  velocity (1..127): stored velocity untouched otherwise. NSEQ_VEL_STRONG/SOFT/SPLIT in nseq.h.
- Generator: first layer strong/soft (beat 85 %, else 45 %), then Dynamics spreads down
  (strong 127..95, soft 80..20). Acid steps unchanged.

## 0.3.10 / 844 - 303 step key: accent, plain, off
- User chose: empty -> note with accent -> plain (slide kept) -> off, like the drums' strong/soft/off.

## 0.3.11 / 845 - soft strength 64
- User: soft 80 was too close to strong 100; soft is now 64, SPLIT 82 (midway). Generator soft range 64..20.

## 0.3.12 / 846 - a more analogue 303
- Ladder: P, q, k and comp stepped per 2x tick towards the next solve (every 8 samples) instead of set
  there at once; numbers (slope, peak, aliasing) as before within 0.4 dB.
- Saw bowed (SAW_BOW 0.12 on the naive ramp: continuous at the wrap); the square takes it.
- VCA: VCA_ROOM 3 * soft(x / 3) before the level; peaks about -0.7 dB on squelchy lines.
- Drift (wander, wd, wrng): pitch +-2.5 cents, cutoff +-0.04 oct, new place ~every 0.7 s, 0.5 s glide.
  Own rng, so patterns and Deviance unchanged. ac.wander = 0 turns it off (tests).
- Host: acid voice about +15 %. Square coupling high-pass tried and dropped: the output's 40 Hz
  coupling already droops it, and no reference to tune more against.
- A/B: /mnt/project-files/wild-firmware/analog-303-demo/ (project files, not in the repo).

## 0.3.13 / 847 - lighter to run, the same sound
- User: Wild at the edge of the FM-1 (had to go to 2 voices in a heavy patch); optimise without
  losing any sound-quality work.
- All DSP ran from XIP flash through the cache (NFM_CODE/NFM_DATA are empty: NFM_FM1_OPEN unset),
  sine table too. Now NFM_HOT / NFM_HOT_DATA (build.py -DNFM_RAM_HOT) put the per-sample code and
  tables in .ram_dsp, linked after .ram_text (copied at boot, inside the PC guard's RAM window).
- Calls reach +-4 MiB (R_PI32V2_LONG_JUMP_23M2), so RAM code at 0x01C0xxxx could not call XIP code
  past 0x02008000: app.ld now POOL 0x01C00000 (0x54000), RAM 0x01C54000 (88K), RAMTEXT 0x01C6A000
  (40K: flash driver ~3K + .ram_dsp ~32K), stacks as before. RAM-run debug's 8K went to RAMTEXT.
  build.py's no-calls check covers .ram_text up to _rt_dsp only.
- Arithmetic: SVF 1/(1+g(g+k)) and k+g kept per change (resonator, noise, insert, master filter);
  sin_cyc without wrap1 (masked index); op phase wrap by compare; 303 dh cached (pitch_step);
  nfm_sat_tick inline in nfm.h. Host heavy patch -17 %; output within -80 dBFS (rounding through
  high-Q resonators and the crusher), rest bit-exact.
- Device gain unknown until measured: compare Audio Load (GLO + OCT+) with 0.3.12 on the same patch.

## 0.3.14 / 848 - Audio Load's split
- On the device 0.3.13 still dropped out on a full-on patch: Render avg 96 %, peak 149 %, 263 empty
  buffers. To see where the time goes on the chip itself: ne_set_clock(fm1_ticks) and ne_take_split
  (drums = nfm_render_bus' voice loop, 303 = acid_render, FX = nseq_render minus those, master = the
  rest of ne_render), shown as "Split %" on the diagnostics page (0.25 s windows). NULL clock on host.
- ram_total 88K (0.3.13's app.ld). No sound change.

## 0.3.15 / 849 - three blocks ahead
- User chose +6 ms latency to swallow render peaks (0.3.13: avg 96 %, peak 149 %, 263 empty buffers).
- wild_stream: WILD_SLOTS 4 (storage, a power of two so w % SLOTS stays in step at the uint32 wrap;
  3 slots broke the wrap test), WILD_AHEAD 3 (fill limit). wild_audio pool -> .bss (pool keeps 8K).
- fm1_render_irq renders up to two halves (8 quanta) a wake: one a wake only held a deficit.

## 0.3.16 / 850 - lighter drum voices
- Device 0.3.15 (full-on patch): now 76 %, avg 86 %, peak 147 %, 25 empty buffers (0.3.14: 85),
  Split D 50 A 22 F 10 M 9.
- nfm: OP() and op 4 skip the sine while env <= NFM_GONE (1e-6; level-0 operators are 0 from the
  trigger, bit-exact). The voice loop renders a voice's run into vb[64], then mod_run (the mod type's
  switch once a run, mod_phaser/line/ring/trem shared with nfm_mod_tick), then the mix: bit-exact.
  Resonator loops to v->modes (an off mode in between adds 0), chorus divide -> reciprocal.
- nacid halfband: hb_a[32], each tap written twice, taps read in a row (no masks): bit-exact.
- Host heavy patch Ir -4.7 %. Output: the exact parts bit-exact; with NFM_GONE within -82 dBFS
  (residual -102 dB) until a voice ends a control step earlier or later (peak < 3e-5 test), which
  changes voice allocation and so the shared noise rng's order from there on: a different take of
  the noise, not a quality change.

## 0.3.17 / 851 - slow Tune and Pitch
- User: Pitch/Tune too jumpy when turned. ui.c slow_knob(): A_TUNE, T_TUNE, T_PITCH take two
  detents a step (ui.half/half_k keep the odd detent per encoder), never accel(). Host "spin" stays
  exact (accel_off). Scenario slow-tune.kal (turn, 5 ms apart).

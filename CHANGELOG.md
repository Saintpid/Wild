# Changelog

A version is a `VERSION` change with its section below (CI checks the section is there). Firmware
files are not published here: the author hands them out. To build one yourself, see BUILDING.md; a
version's own build is `python3 tools/build.py --release X.Y.Z --id <BUILD_ID>` (M-UPGRADE takes only
a short identity, higher than the one installed).

## 0.3.33

Firmware identity FM-1_867. The song list keeps itself.

- **Only songs on the list:** a song with more than two banks, or with automation, keeps the rest in
  slots of its own, from the top down. Those slots are no longer on the save and load lists (the list
  ends before them), so they can't be picked, saved over or mistaken for free places. A song reads
  "song, 4 slots", and the top of the list says how many slots are free. Saving a song never touches
  another song or its slots.
- **Copy a song:** on the list, SEL on a song, then SEL again on a free slot. The copy takes all its
  banks and automation along.
- **Delete a song:** on the list, HOME on a song, HOME again to say yes. Its extra slots go with it.
- **Erase all songs:** on the list, GLO + HOME, and GLO + HOME again to say yes. What plays now stays
  (clear all, GLO + HOME away from the list, empties that).
- Any other button cancels a copy or a question; the list stays open. A delete cut short by power off
  leaves no lost slots: they count as free again.

## 0.3.32

Firmware identity FM-1_866. Eight live banks, explicit copy, bank-aware undo.

- Tap/release SEL then white 1-8 selects a bank; an empty bank copies the current one.
  Hold SEL with white 1-8 to overwrite and switch, without a confirmation menu or hold timer.
  Tap SEL again to cancel selection. Same-bank copy does nothing. White 16 still makes Chance;
  selecting the current bank exits Chance. Copying Chance captures the audible scene while its
  source keeps the original. Tempo and device-wide settings remain shared.
- Copies include automation. If the shared 28-lane pool cannot fit a complete copy, the operation
  is refused before changing anything (COPY NOT MADE / AUTOMATION FULL).
- A bounded 17,760-byte undo journal replaces the old six-step buffer. It restores navigation,
  overwritten bank contents, empty-bank status, Chance state and automation, with redo. It keeps
  up to 9 ordinary edits without automation, 3 with automation, or 2 full copy operations. Oldest
  operations expire as space is needed; an edit after undo discards redo. Song load and Clear All
  start a new history. No flash writes are added during playback.
- Existing project format and song part IDs stay unchanged. Songs containing banks 9-12 are
  refused on load and save, without changing them. If active startup storage uses those banks,
  it is protected from autosave and manual save; use 0.3.29 to reorganise/save it first. Explicit
  Clear All or loading a compatible song starts an eight-bank workspace. Saved legacy songs remain.
- Together with 0.3.30 (the newer FM-1s) and 0.3.31 (Drive without clicks). A test build of the
  banks alone went out as FM-1_864, the same identity as 0.3.30: update to this one.

## 0.3.31

Firmware identity FM-1_865. Drive without clicks.

- **Drive on the 303:** with any Drive on the acid line (from 1), a clean bass (a sine, say) clicked
  at the start of every note. The drive's DC filter took a little of the note itself, was left where
  it was when the line went quiet, and came back as a step at the next note. Now it follows only
  what the drive adds, and the drive (and the moving filter and the mod effect after it) runs out
  after each note. With a little drive a sine sounds as clean as without it; turned up it still warms.

## 0.3.30

Firmware identity FM-1_864. The newer FM-1s.

- **Newer FM-1s:** units sold since autumn 2026 (mostly through AliExpress) have a UCUN UC25HQ80
  flash in place of the Puya P25Q80H. Wild knew only the Puya: on a newer unit nothing was saved
  ("flash error", everything in RAM) and no update could start. Now both are accepted: saving,
  presets and songs, and updates with M-UPGRADE or FM-1 Switch work on either. Any other chip is
  still left alone, RAM only.
- **Rescue:** the chip's own update mode rescue (FM-1 Switch's tools) accepts the newer flash too.
- Not yet tried on a newer unit: if you have one, tell us whether a save survives a power cycle.

## 0.3.29

Firmware identity FM-1_863. Clear all starts from nothing.

- **Clear all, GLO + HOME:** bank 1 now starts empty: no hits on the drum tracks and no notes on the
  303, every track 1 bar long with Rnd len on Fixed, and nothing left to chance: Loose, Chaos, Motion,
  Mutate and Space at 0, each drum track's Evolve, Deviance, Dynamics and Pitch at 0, the 303's Evolve,
  Chaos and Deviance at 0. Build it up from there with steps or new patterns. Sounds, effects and
  levels start from the defaults as before.
- RESET (HOME, SAVE) is unchanged: the defaults with new patterns.

## 0.3.28

Firmware identity FM-1_862. Songs keep their banks; clear all for a clean start.

- **A preset is a song:** saving one saves all its banks and their automation, and loading it brings
  exactly those banks and starts on bank 1. Another song's banks never come along any more.
- **Room:** a song's own slot holds banks 1 and 2. Each further two banks, and the automation, take
  one more slot, from the top of the list down (064, 063 ...). The list shows those as "in 001", and
  the song's own slot as "used +3". A song with all 12 banks and automation takes 7 slots. If there is
  no room left, the save says so and nothing is saved.
- Presets saved before load as songs of one bank.
- **Clear all, GLO + HOME:** asks "CLEAR ALL?", and GLO + HOME again says yes (SAVE or a key keeps everything).
  Every bank is emptied and all automation erased, and bank 1 starts from the defaults: a clean
  starting point. Your saved songs are not touched.
- **GLO + REC:** erases the selected track's automation in the bank playing, at once.

## 0.3.27

Firmware identity FM-1_861. SEL alone takes you home to bank 1.

- **SEL alone:** back to bank 1 from wherever you are, as you left it (a scene by chance in the bank
  you leave ends there, and that bank keeps how it was before the scene). In bank 1 with a scene by
  chance playing, SEL alone ends the scene.
- **SEL mode is gone** from the Scene page (GLOBAL, page 5): A/B and Copy are what the banks do now,
  and Chance is SEL + white 16. 303 waves is on KNOB 1 of the Scene page now.

## 0.3.26

Firmware identity FM-1_860. Automation: REC records what you do with the knobs.

- **REC, turn, REC:** press REC (it lights, and REC shows red in the header), then turn any knob
  while the sequence plays; from your first touch its value is recorded. Press REC again and your
  moves play back in a loop. Several knobs can be recorded at once, also the mixer's faders.
- **The loop** is as long as you recorded, made 1, 2, 4 or 8 bars, and runs in step with the phrase
  (the bars shown at the bottom). Where you did not touch the knob, it keeps the value it had.
- **Your hand wins:** turn a knob that has automation and it follows you; a second after you let go,
  the automation takes it back. Record it again to replace it.
- **Erase:** hold REC and turn the knob. A red dot over a knob shows it has automation.
- **A new pattern, clean knobs:** a new pattern on a track (black 10, MIDI C1-E1) erases that track's
  automation (its knobs and its mixer fader); on all tracks (GLO + black 10, CC 118) that of every
  track that gets one. The global knobs' automation (master, FX bus, Glue, Groove) stays.
- **Per bank:** each bank has its own automation. 28 recorded knobs in all, shared by the banks; it is
  saved by itself like the banks, once stopped and quiet.
- Knobs that set what the patterns are (Bars, Rnd len, Hits, Kind, Grid) and the device's (tempo,
  MIDI, the Setup and Scene pages) are not recorded. Stopping, or changing bank, ends a recording.

## 0.3.25

Firmware identity FM-1_859. Twelve banks, for sharp turns in a live set.

- **Banks:** hold SEL and press one of the white keys 1-12. Each key is a bank: a whole setup of its
  own, with the patterns of all five tracks, the sounds, the effects and the settings. It comes in at
  once and plays from where the sequence is; the tempo, the level and the Setup and Scene pages stay
  the same for all banks. The bank you leave keeps everything you did in it.
- **An empty bank** starts as a copy of the one playing, so you can build the next part from where
  you are.
- **While SEL is held** the white keys show the banks: lit is the one playing, dim the ones with
  something in them.
- **SEL + key 16 (Chance):** a new scene by chance at every press (which tracks play, how much of each
  effect). Change bank and the bank keeps how it was before the scene.
- SEL on its own works as before, as long as no white key was pressed while it was held.
- The banks are saved by themselves like the project: once stopped and quiet. A preset holds the bank
  that plays when you save it, and loading one replaces the bank playing.
- **Undo is 6 steps now** (was 12) to make room, and it starts afresh in each bank.

## 0.3.24

Firmware identity FM-1_858. A Fixed length is the length.

- **Rnd len at Fixed plays the Bars you set:** a drum track that had drawn a random length (an odd
  one, such as a bar and 13 steps) kept it after Rnd len went back to Fixed, so it drifted against the
  others, and setting Bars to the value it already showed did nothing. Now Fixed takes the track back
  to its Bars at once, and every new pattern on a Fixed track is exactly its Bars long.
- **After loading a project:** a new pattern follows the Bars it was saved with. Before, it could
  keep the pattern's old length, and Bars had to be turned away and back for it to take.


Firmware identity FM-1_857. How much battery is left, on the screen.

- **The battery in the header:** a battery beside the tempo, filled as far as it is, with the % in
  it. Red at 10 % and below. It is read every two seconds (no load on the audio) and smoothed, so it
  settles a little after power-on, and it goes up again only when the battery has really been
  charged, not when it recovers a little as the playing stops.
- The % is an estimate from the battery's voltage (the FM-1 has no fuel gauge): it follows a usual
  Li-ion curve and agrees with where the FM-1's own firmware changes its battery bars.
- **On a computer's USB:** a lightning bolt instead, as the battery is charging. A charger alone is
  not seen, so it shows the % then.
- On the Setup page the audio load keeps its place, so the battery is not shown there.

## 0.3.22

Firmware identity FM-1_856. Everything new at once on GLO, and quicker out of the mixer.

- **GLO + black 10:** new patterns on all tracks (this was REC, which now does nothing).
- **GLO + black 11:** new sounds on all tracks that are not muted.
- **Leaving the mixer and Audio Load:** any button except PLAY, SEL and REC closes them. That press
  only closes them, so the page, the track and everything else stay as they were. PLAY, SEL and REC
  work as usual with the mixer open.

## 0.3.21

Firmware identity FM-1_855. Softer soft drum hits.

- **Soft hits at 50:** a drum step's soft strength (the second press on its key) is velocity 50
  instead of 64, and new patterns' soft hits start from 50 too. They show a shade dimmer than
  before on the screen, so strong and soft are easier to tell apart.
- Projects saved before keep their hits as they were (a soft one there stays at 64).

## 0.3.20

Firmware identity FM-1_854. The 303 on Open303's own voice, with Wild's touches on top.

- **Open303's filter:** the 303's diode ladder as mystran and kunn fitted it to a real one (the
  filter X0X uses), in place of Wild's own model. As on a 303, the sound thins out as the resonance
  goes up.
- **Its diodes bend:** each stage of the filter rounds when driven hard, so squelchy lines sing and
  growl a little more (Wild's, on top of Open303).
- **Open303's saw and VCA**, with Wild's bowed ramp, accents that build up with the resonance, a VCA
  that rounds loud accents and the slow drift kept.
- **Lighter to run:** the 303's render costs about 12 % less than in 0.3.19.

## 0.3.19

Firmware identity FM-1_853. The phaser, flanger, chorus and ring modulator without the grit.

- **No more bitcrusher in the modulation effects:** they keep their loudness matched to the dry
  sound, and that gain used to jump every 16 samples, a 2.8 kHz staircase that crunched like a
  bitcrusher, most on bass and kicks. It now slides smoothly, and follows the level more calmly so
  a low note's own wave no longer shakes it (on a 30 Hz tone the added distortion is 7 dB lower).
- **Flanger and chorus tails stay clean:** their delay lines now hold a small floating-point number
  instead of a fixed one, so a quiet tail keeps as much detail as a loud hit (it used to fall to
  a few bits 40 dB down). Same memory, same headroom.
- The effects sound otherwise the same and cost about the same to run.

## 0.3.18

Firmware identity FM-1_852. The 303 calibrated as Open303 measured a real one.

- **The 303's knobs over the 303's own ranges:** Cutoff from 314 Hz to 2.4 kHz (it was 30 Hz to
  5.8 kHz), Decay from 200 ms to 2 s (it was 80 ms to 2.5 s), the resonance coming in mostly in the
  knob's first half. Saved projects keep their knob positions, so they sound closer to a 303 now.
- **Env Mod follows Open303's measured curve:** the more of it, the further the envelope sweeps and
  the lower the filter rests, more with the cutoff up, and a little even at 0, as on a 303.
- **The 303's slide:** 60 ms, so a slide gets there within the step, octave jumps too.
- **The 303's VCA:** it opens at once (a de-clicker rounds it), decays slowly while the note is held
  and closes in 1 ms, 50 ms after an accent. The filter envelope pushes it up too: a little on every
  note, a lot on an accent, so accents stand out much more than before.
- **The filters around the ladder** as on the 303: a high-pass before it, a 150 Hz high-pass in the
  resonance's feedback (the bass stays), and the all-pass, high-pass and notch after it that give the
  wave its shape.
- **The 303 square:** Open303's, the ramp through a transistor, at half the saw's level.
- Kept from Wild: the diode ladder, the accent sweep that builds up with the resonance, the bowed
  saw, the drift. The 303 costs no more to run.

## 0.3.17

Firmware identity FM-1_851. Calmer Tune and Pitch knobs.

- **Tune and Pitch move a semitone every two detents, however quick the turn:** Tune on the drums
  and the 303, and Pitch on the drums, no longer speed up when turned fast (a quick twist could
  throw Tune from 0 to +24 at once). Playing with them while the pattern runs, the sound now walks
  up or down the notes instead of jumping. The other knobs still speed up on a quick turn.

## 0.3.16

Firmware identity FM-1_850. The drums and the 303 a little lighter to run, the same sound.

- **An operator that is off or has died away is no longer worked out:** a sound with operators at
  level 0 skips them entirely, and in a hit's tail the ones that have decayed below -120 dB stop
  costing time. What you hear is the same (any difference stays below -80 dBFS).
- The drum effects (phaser, flanger, chorus, ring, tremolo) run over a voice's whole block at once,
  and the resonator and the 303's halfband filter skip steps that changed nothing. Bit for bit the
  same sound.

## 0.3.15

Firmware identity FM-1_849. Fewer dropouts when everything plays at once.

- **The sound is rendered a little further ahead:** three blocks (17 ms) instead of two (12 ms). A
  moment when many hits land together and the render takes longer than the time it has is now
  swallowed by the queue instead of heard as a dropout, as long as it plays within its budget on
  average. Knobs, keys and MIDI answer about 6 ms later.
- After such a moment the render catches up (up to two blocks a turn) and wins its lead back.
- Audio Load's PCM ready reads out of 3.

## 0.3.14

Firmware identity FM-1_848. Audio Load shows where the time goes.

- **Audio Load (GLO + OCT+) splits the render:** a new row, Split %, shows the share of the audio
  budget the drum voices (D), the 303 (A), the sequencer with the reverb and delay (F) and the master
  chain with the Glue (M) take, over the last quarter second. The output IRQ's and the screen's peaks
  now share a row.
- RAM free reads right again (it still counted the old 96 KB since 0.3.13).

## 0.3.13

Firmware identity FM-1_847. Lighter to run, the same sound.

- **Lower Audio Load.** The code that makes the sound every sample (the drum voices, the 303, the
  reverb and delay, the Glue and the limiter) and the tables it reads now run from the FM-1's RAM
  instead of from its flash through a cache. Nothing about the sound changes: it is the same code,
  only where it lies.
- **Fewer sums per sample.** The drums' resonator, noise and insert filters, the 303's insert filter
  and the master filter work out their coefficients when their settings change instead of
  dividing every sample; the sine the FM voices are made of is cheaper to look up. The difference in
  the output is rounding only (at most -80 dBFS, about 98 dB under the music). On a computer, a heavy
  patch renders about 17 % faster; on the FM-1 the RAM move should count for more.
- Everything added for sound quality stays: the 303's 2x oversampling, the anti-aliased drive and
  clip, the Glue, all of it.

## 0.3.12

Firmware identity FM-1_846. A more analogue 303.

- **The 303's filter sweeps smoothly.** The cutoff, the resonance and their make-up gain now move
  every sample instead of in steps of eight, so fast Env Mod and accent sweeps are cleaner.
- **The saw bows a little**, as the 303's does (its ramp is a capacitor charging): steeper at the
  bottom, rounder at the top. The square, made from the saw, follows.
- **The VCA rounds loud notes**, as the 303's transistor VCA does: accents with the resonance up get
  a little bite instead of only getting louder.
- **A slow analogue drift:** the pitch wanders a few cents and the cutoff a few per cent, each on a
  slow way of its own.
- About 15 % more time for the 303 voice (around 2 % of the whole render).

## 0.3.11

Firmware identity FM-1_845. A softer soft hit.

- **The soft strength on the drum steps is now 64 instead of 80**, so it stands clearly apart from
  the strong one (100). A second press on a strong step makes it 64; any hit over 82 counts as
  strong. New patterns' soft hits start from 64 too (down to 20 at Dynamics 100).

## 0.3.10

Firmware identity FM-1_844. The 303's steps work like the drums'.

- **A 303 step key now goes accent, plain, off.** Press an empty step: a note on the root with an
  accent. Press it again: the accent comes off (a slide on it stays). Again: the note is gone. So
  it works like the drum steps' strong, soft, off. Holding a step and turning KNOB 1 (note), KNOB 2
  (accent) and KNOB 3 (slide) works as before.

## 0.3.9

Firmware identity FM-1_843. Two strengths on the step keys.

- **A step key now has two strengths.** Press an empty step: a strong hit (velocity 100, as
  before). Press it again: soft (80). Again: off. A hit already there goes one strength down
  whatever its velocity: anything over 90 becomes soft, 90 or under goes off. Holding a step and
  turning KNOB 1 still sets any velocity 1-127, and letting go after a turn changes nothing more.
  The 303's steps work as before (a press on a note takes it off).
- **New drum patterns use the same two strengths** as their first layer: strong on most quarters
  (85 %) and on about half of the other steps (45 %), soft otherwise. **Dynamics** (Rhythm page,
  KNOB 2) now spreads them from there: at 0 every hit is exactly 127 or 80, at 100 a strong one
  goes down to 95 and a soft one to 20.

## 0.3.8

Firmware identity FM-1_842. Drums that play like hands.

- **New drum hits lean strong.** A new pattern's velocities now sit mostly in the upper part of
  MIDI's 1-127: the quarters nearly always hard, but about one in seven a softer one; the steps
  between them spread wider, still mostly strong. Before, they were spread evenly between about
  half and full by where they fell.
- **Dynamics**, a new knob per drum track on the Rhythm page (HOME twice, KNOB 2): how soft the hits
  may get, 0-100, 50 to start. At 0 every hit is full, at 100 down to about 17. It shapes new
  patterns (black 10, REC); the hits already there stay, and a held step's KNOB 1 still sets one
  by hand. Saved with the project; older projects get 50.
- The 303's Deviance reads the drums as calm or hard as before, with the louder hits taken into
  account.

## 0.3.7

Firmware identity FM-1_841. Tracks that stay out of the Drift.

- **Each track can go past Chaos, Motion and Mutate.** A new page, **Routing**, comes after Drift
  (press LFO twice). Its knob **Drift** is the selected track's: **In** (as before, the default) or
  **Bypass**. A track at Bypass goes straight on to the master filter, the Glue and out: the
  glitches (beat repeat, reverse, tape stop) never cut it up, its moving filter stands still where
  it rests, and Mutate leaves its mute alone. Its reverb and delay sends work as before, and Space
  still moves the rooms for everyone. Every track has its own, the 303 too, and a **B** on a
  track's row shows it is out. Keep the kick and the 303 steady while the rest breaks up.
- **Mutade is now called Mutate.** It does the same.
- The master filter (hi cut, lo cut) now comes after the glitches, so a beat repeat is filtered as
  it plays rather than as it was recorded. You will hardly hear it.
- Projects and presets from before load as they were, every track In.

## 0.3.6

Firmware identity FM-1_840. Triplets.

- **Any track can run in triplets.** Press HOME twice for the new **Rhythm** page; its **Grid**
  knob turns the selected track (the 303 too) from 16ths to **Triplets**: eighth-note triplets,
  three steps to a quarter, 12 to a bar. White keys 1-12 are then the bar's steps, in groups of
  three, one group per beat; keys 13-16 do nothing. On the screen the row draws its 12 steps where
  they fall in time against the other tracks, and the length reads e.g. "2b 3". The other tracks
  play on in 16ths, so you can lay a triplet hat or a triplet acid line over a straight beat.
- **Turning Grid keeps the groove.** The pattern moves along with it: every hit goes to the nearest
  step in time (an offbeat eighth becomes the late third triplet, a shuffle), and the track keeps
  its number of bars. Turned back, the triplets land on the first three sixteenths of each beat.
  New patterns in triplets are drawn for triplets: kicks on the beats, hats on the shuffle's late
  third, the 303's rules counting beats of three. Swing leaves triplets alone. Saved with the
  project and the presets; older projects load in 16ths.
- **HOME now has two pages**, Pattern and Rhythm, like the other page buttons. HOME on the Pattern
  page goes on to Rhythm.

## 0.3.5

Firmware identity FM-1_839. A cleaner 303 at the top.

- **The 303 is oversampled.** Its oscillator and diode-ladder filter now run at twice the sample
  rate and come back down through a halfband filter. With the resonance up and the cutoff open
  (or swept open by Env Mod and accents), the filter's clip used to fold overtones back down as a
  faint inharmonic hiss and grit between the notes; that is now about 9 dB lower, and the square
  about 4 dB cleaner. The sound is otherwise the same: the cutoff, the resonance and the level are
  where they were. It costs the acid voice about 70 % more time, about 5 % of the whole render:
  check Audio Load (GLO + OCT+). The drums' FM, the fold and the master bus stay at the normal
  rate: their overtones mostly stay under the top, so oversampling them would cost much and give
  little.

## 0.3.4

Firmware identity FM-1_838. Drums that wander off.

- **Deviance 90 to 100 on a drum track lets the sound itself wander.** Up to 89 nothing changes:
  every hit varies around the sound as before. From 90 the sound moves a little at every sixteenth
  and each hit starts from where the last one left off, never back, so it slowly becomes something
  else: at 90 it has left its kind (a kick no longer a kick) after about 128 bars, at 100 after about
  8. It is as fast however many hits the track plays. The knob's name reads **Wander** from 90.
  Turn it below 90 and the sound stays where it went; save it and it is saved as it is now; a new
  sound (black 11) starts over. A muted track waits. Its loudness keeps a floor (it never wanders
  into silence) and Level, Pan and the crush stay as they are. Not the 303: its Deviance is its own.

## 0.3.3

Firmware identity FM-1_837. The click in the Glue, gone.

- **No more clicks from the Glue.** Its compressor read the level a little wrong (a natural log in
  place of a log2), so as a sound rose or died away its gain jumped about 1.5 dB at a time, in a
  fifth of a millisecond: a click, on almost every hit, louder the higher the Glue and the Glue mix.
  It now follows the level smoothly; it squeezes about the same, a tenth of a dB more at most.
- The Glue's punch (the lift it gives a hit) now comes in over 1.5 ms instead of all at once, which
  ticked too.
- Turning any Glue knob with the Glue over 60 no longer restarts its warmth (a click at each turn).

## 0.3.2

Firmware identity FM-1_836. The 303's Deviance under ENV, and it gets tired.

- **The acid line's Deviance has moved to ENV**, as the drums' Deviance: press ENV three times on
  the acid line for its own page, Deviance on KNOB 1. Its Track page (EDIT) has **Level** back on
  KNOB 4. The drum tracks' ENV still has its two pages.
- **Deviance needs a rest now and then.** Playing hard spends its energy: with hard drums it keeps
  going for about three phrases, give or take, then takes a break of mostly eight bars (now and then
  four or twelve), whatever the drums do: from a bar on a half phrase the knobs go back to their calm
  place in a bar or two, no twists and no build. Then it comes back hard, all at once, from where the
  drums are. With calm drums it doesn't tire and gets its energy back.

## 0.3.1

Firmware identity FM-1_835.

- **Your hand comes first:** turning the acid line's Cutoff, Reso, Env mod or Decay (the Sound page,
  PRESETS or MIDI CC 74 / 71) while Deviance plays takes all four straight to their set values (a
  20 ms glide, so no click). Deviance takes them up again, from there, 2 s after the last turn.

## 0.3.0

Firmware identity FM-1_834. The 303's own Deviance, and a major scale.

- **Deviance for the acid line** (its Track page, KNOB 4, where Level was: Level is on the mixer,
  GLO + OCT-, and PRESETS). It plays Cutoff, Reso, Env mod and Decay as a hand on a 303 would, so
  the acid line keeps moving while you work on the drums, without changing its pattern as Evolve
  does. It follows the drums: the harder they sound (Edge, Drive) and the busier they play, the more
  the resonance goes up, Env Mod to the middle or high and the cutoff to the middle; calm or muted
  drums bring it back. Each knob takes its own time, slow sweeps mostly, now and then a fast one,
  and one move in six goes against the rules. Now and then, on a beat, a quick twist there and back,
  likeliest in bars 4 and 8; in some phrases the cutoff builds over the last four bars and lets go
  on the one. 0 is off (the default); the knobs keep their values and Deviance plays around them.
  Saved with the project (format 13).
- **Major** joins the acid line's scales (Track page, KNOB 3).

## 0.2.1

Firmware identity FM-1_833. A warm Drive.

- **Drive no longer sounds like aliasing.** The grit came from how Drive kept its volume level: it
  reset its gain in steps every 8 samples, which rang as a whine and fizz between the notes'
  harmonics. The gain now glides, and the curve is a cubic (only the 2nd and 3rd harmonics until it
  flattens) worked out between the samples (ADAA), so what it makes over the top no longer folds
  back. At the same settings Drive is a touch gentler than before. Every Drive gets it: the acid
  line's, the drum sounds', and the Glue's warmth over 60.
- **The 303's square is cleaner:** the saw's clip that makes it is anti-aliased too, about 11 dB less
  fizz with the filter open.
- Turning the acid line's Drive no longer clicks.

## 0.2.0

Firmware identity FM-1_832. The 303's diode ladder.

- **The acid line's filter is now the 303's diode ladder:** four stages that load each other, the
  bottom one twice as fast, as with the 303's half-size capacitor. Gentle under the cutoff, about
  18 dB/oct above a resonant peak, and the level stays even as the resonance goes up. It takes
  about 20 % more time for the acid voice: watch Audio Load (GLO + OCT+) on the FM-1.
- **303 waves** (GLOBAL, the Scene page, KNOB 2): **Original** (the default) draws a new acid sound
  with the 303's own waves only, saw or square. **All** draws from all six, as before. Wave on the
  Tone page can still be set to any of them. Kept through SEL, saved with the project (format 12);
  older projects load with Original.

## 0.1.4

Firmware identity FM-1_831. Five-channel mixer.

- GLO + OCT- toggles five faders with signal meters and peak hold.
- SELECT controls channel 1; KNOB1..4 control channels 2..5 (303).
- Encoder decoding keeps full-cycle resolution at slow and fast speeds;
  contact pauses no longer teach false detents that doubled subsequent clicks.
- Levels use existing saved track volumes, clamped to 0..100.
- Audio Load moves to GLO + OCT+; mixer redraw capped at 15 Hz.

## 0.1.3

Firmware identity FM-1_830.

- The tempo stays the same when you switch patterns with SEL A/B or Copy.
- Quarter-note ticks mark steps 1, 5, 9 and 13, rests included.

## 0.1.2

Firmware identity FM-1_829. Audio priority and diagnostics.

- Keep all instruments, effects, mastering and controls.
- Render PCM in a dedicated software interrupt below DMA playback and input
  scanning, above foreground display work. Render at most one half per wake.
- Screen refresh is capped at 30 Hz; input stays responsive between frames.
- GLO + OCT- toggles Audio Load: render current/average/peak, output and screen
  timing peaks, queued PCM, underruns, late output and static free RAM/pool.
- Diagnostics refresh at 4 Hz. CPU values describe render budget, not total CPU.
- Protect snapshots against concurrent rendering; retain startup vector guard fix.
- Physical timing and software interrupt operation require an FM-1 retest.

## 0.1.1

Firmware identity FM-1_828. Startup fix.

- Register the audio interrupt before locking the vector memory region.
- Fix the startup crash observed on hardware at PC 02007B18 in 0.1.0.
- Hardware confirmation of this correction is still required.

## 0.1.0

Firmware identity FM-1_827. Wild (experimental).

- Fork n001se 1.7.1 (b3a4ac2). Continuous PCM lookahead, foreground synthesis, bounded interrupt playback.
- Preserve controls, MIDI, Chaos, acid slides, accents and effects.
- Two 256-frame blocks at 44.1 kHz; underrun fade and recovery, render timing counters.
- Hardware timing and resource margin remain unverified until an FM-1 run.

## 1.7.1

Firmware identity FM-1_825.

- **The acid line is closer to a real TB-303.**
  - **Slides** take 60 ms, as on a 303 (45 before), so they are a little slower and a big slide at a
    fast tempo stops further short of its note.
  - **An accent on a slid note** no longer starts a new accent: as on a 303 it takes the filter
    envelope where it has decayed to, so it is a weak one. Accents on notes of their own are as before.
  - **Env Mod lowers where the sweep starts**, as on a 303: up to 1.5 octaves under the Cutoff at
    100. The top of the sweep stays where it was.
  - **The attack:** a plain note fades in softly (3 ms), an accent hits at once, which gives the
    accent its click.
  - **The square** is now made from the saw the way the 303 does it: rounded on the way up, with a
    slant on top, instead of a clean square.

  Saved projects load as before; they just sound more like a 303.

## 1.7.0

Firmware identity FM-1_824.

- **Glue is now a light mastering chain**, in the spirit of a one-knob mastering plugin. Its
  amount stays on the Master page (knob 4); how it sounds is on a new **Glue page** (GLOBAL's second,
  so Groove, Setup and Scene move one page on):
  - **Glue mix:** parallel compression, how much of the compressed mix against what goes in. 50 by
    default: tight, but the hits keep their punch. A gentle transient expander in the compressor lets
    the hits through.
  - **Glue EQ:** an EQ that listens. It follows the balance between lows and highs and leans the mix
    towards a pink slope of Ã¢Ë†â€™4.5 dB an octave, adds a little foundation and air, takes a little box
    away, and turns the harsh 2Ã¢â‚¬â€œ9 kHz down while it stands out. 50 by default, 0 flat.
  - **Color:** an inflator, denser and fuller near the top. 50 by default.
  - **Width:** 0 mono, 50 as it is, 100 twice as wide; the lows under 120 Hz always in mono. 60 by
    default.
- As before, the Glue is loudness matched (the volume is Level's), and 0 turns all of it off. With
  the Glue on, the limiter's ceiling is Ã¢Ë†â€™1 dBFS (Ã¢Ë†â€™0.3 dBFS off).
- Project format 11: a saved project or preset gets the Glue page's defaults, so one with Glue up
  sounds a little different (more finished) than before. Glue mix 100, Glue EQ 0, Color 0 and Width
  50 come close to the old Glue.

## 1.6.1

Firmware identity FM-1_823.

- **Space moves the delay like an analog delay.** Instead of fading from one length to the next,
  the delay time glides there, so the repeats bend in pitch on the way: up when it gets shorter,
  down when it gets longer, smoothly at both ends. A 1-bar glide between a quarter and a sixteenth
  bends the most (about five semitones at its fastest), a 4-bar glide only a little.

## 1.6.0

Firmware identity FM-1_822.

- **Space (Drift page, knob 4).** The reverb and the delay wander between places of their own: the
  reverb from a small room through the usual one to a big hall (length and tone), the delay between
  a dotted eighth (as before), an eighth, a sixteenth and a quarter. At 0 they stay put as before;
  the higher, the more often they move, but always slowly: each glide takes 1Ã¢â‚¬â€œ4 bars and each place
  is held at least 4 bars (about 8 on average at 100). The delay fades from one length to the next,
  so nothing bends in pitch. With Space up, SEL also moves them by the same chance (always at 100).
  Chaos and Motion no longer move the delay time or the reverb's length and tone; Space does.
  Project format 10 (a saved project gets Space 0).

## 1.5.0

Firmware identity FM-1_821.

- **A new kind of sound: Clap.** Three or four quick bursts of band-passed noise, 6Ã¢â‚¬â€œ13 ms apart,
  then the tail, with a short FM knock under it and now and then the resonator as a room. The
  bursts are drawn anew on every hit, so no two claps are the same. Choose it with Kind on the Tone
  page and draw a new sound (black 11). Its patterns lean on beats 2 and 4, like a snare's, with a
  few pushes around them.

## 1.4.1

Firmware identity FM-1_820.

- **More octaves in the acid line, the 303 way.** Notes side by side are pushed apart: next to a
  low note the next one goes up an octave, and after one up the next comes back down (now and then
  below the root). The same Octaves setting gives about 40 % of neighbouring notes an octave apart
  where 1.4.0 gave about 25 %; at 35 % and up most of the line jumps. Octaves starts at 30 % in a
  new project (a saved project keeps its value). Chaos loosens the rule as it loosens the others.
- **Beat repeat, shorter and with the swing.** A repeat lasts an eighth at most, and an eighth goes
  by before the next glitch, so a fill becomes a repeat, a pause, a repeat. With swing its sixteenths
  and thirty-seconds come on the swung grid (long, short), so the repeat keeps the groove. In bars 4
  and 8 the eighths of the third quarter can bring a glitch too.

## 1.4.0

Firmware identity FM-1_819.

The acid line is rebuilt after the TB-303, its sequencer and its sound.

- **A musical generator.** A new pattern starts from the rhythm: notes likelier on the one, the
  beats and the offbeat eighths, with rests between. It is a motif, a bar or half a bar twice, that
  repeats over the bars with small variations, most at the end of bars 2 and 4. Then the notes: the
  root and the fifth most, then the third and the seventh, mostly small steps or repeats with a leap
  now and then, and now and then a note outside the scale. Then the octaves (up likelier off the
  beat), accents where they push the rhythm (off the beat, after a rest), and slides where they move
  the line: most into a different note, and most of all into an octave jump.
- **Chaos for the acid line** (Tone page, KNOB 4, 0Ã¢â‚¬â€œ100 %, 25 % to start with). At 0 % the line
  follows the rules above; at 100 % the scale, the beat and the motif are gone: any note, wild
  octaves (up to two), accents and slides anywhere. In between, every chance moves step by step.
- **The 303's gate and slide.** A note's gate is half a step. A slide holds the gate into the next
  step, which glides to its note with no new attack: the VCA and the filter envelope go on as they
  are. A note that is not slid into is a new attack; a rest closes the gate; a slide into a rest
  makes a longer note. Slides in a row make one long movement, also from the last step into the
  first. The oscillator runs on and is never reset.
- **The 303's slide.** The pitch glides through an RC (45 ms), at the same speed whatever the
  interval, so a big slide may not get there before the next note, which then slides on from where
  the pitch is.
- **The 303's filter.** A model of its diode ladder (one stage an octave lower than the other
  three, about 18 dB/oct in the octave above the cutoff) with the 303's high-pass in the resonance
  feedback, so the bass stays when the resonance goes up. It saturates where it resonates and stays
  stable at full resonance. Its level is matched across the resonance.
- **Accent** raises the VCA and the cutoff through a sweep capacitor: accents in a row build up,
  the more the higher the resonance. An accent on a slid note works without a new attack.
- Projects and presets from before keep everything; the acid line's Chaos starts at 25 %.

## 1.3.1

Firmware identity FM-1_818.

- **SEL mode Chance:** while the scene by chance plays, the mute keys (black 5-9) set the original's
  mutes, not the scene's: choose what plays when SEL brings the original back. Their lights show the
  original's mutes meanwhile.

## 1.3.0

Firmware identity FM-1_817.

- **SEL has three modes**, chosen on the new Scene page (GLOBAL, page 4): **Chance** (as before, a
  scene by chance), **A/B** (the first SEL copies everything, patterns, sounds, effects and settings,
  and each SEL after swaps between the two, so two scenes can be worked up and switched between) and
  **Copy** (SEL plays on in a copy; SEL again goes back to the original and the copy's changes are
  gone). The Setup and Scene pages are never swapped.
- **GLOBAL opens on the Master page** (hi cut, lo cut, resonance, Glue), then Groove, Setup, Scene.

## 1.2.0

Firmware identity FM-1_816.

- **SEL: a scene by chance.** SEL draws which tracks play (at least two) and each sound's effects:
  reverb and delay sends, filter share, drive and modulation effect. SEL is lit while the scene
  plays; SEL again puts everything back as it was before, whatever was turned in the scene. Loading a
  preset or a reset ends the scene where it is.
- **Mutade** (Drift page, knob 3): in bars 4 and 8 of the phrase, a quarter at a time, a different
  set of tracks plays by chance (muted ones may come in, playing ones drop out), most often in the
  last quarter. It always goes back to the mutes as set. 0 = off, the default.

## 1.1.0

Firmware identity FM-1_815.

- **A modulation effect per sound** (FX, page 2: Mod): phaser, flanger, chorus, ring modulator or
  tremolo, for each drum track and the acid line, with Amount, Rate and Chance (the share of hits, or
  notes, that get it, drawn per hit). Its sweep runs on between hits, and its loudness is matched to
  what goes in. Projects and presets from 1.0.0 load with it off.
- **Chaos has rules now.** In bars 4 and 8 of the phrase glitches come on the beat, mostly in the last
  quarter, and never run past the bar's end. Elsewhere they come only on an eighth off the beat, an
  eighth long.
- **Tape stop**, a new glitch: the mix slows down to a stop and comes back. Beat repeat can now also
  repeat a 32nd at the end of a fill.
- The texts here, the manual included, are in English.

## 1.0.0

The first public version, tested on the FM-1 (firmware identity FM-1_814: install it with M-UPGRADE).

- **Five tracks:** four FM percussion sounds (kick, snare, hat, perc, click, metal), every hit a
  little different, and **an acid line** in the manner of a TB-303 (saw, square, pulse, tri, sine,
  duo; a resonant ladder filter, accent, slide) with its own random sequencer.
- **Effects per track:** reverb and delay sends that move by chance per hit up to the level set, a
  moving filter, and a warm, loudness-compensated drive. **Chaos** brings glitches: beat repeat and
  reverse.
- **Master page:** hi cut, lo cut, resonance and **Glue**, a loudness-matched bus compressor with a
  limiter at -0.3 dBFS after it.
- **Live:** mutes for all five tracks on black keys 5-9, **undo / redo** on black 3 / 4 (12 deep),
  64 presets (SAVE to save, ARP to load; Program Change loads), a PRESETS encoder that turns any
  parameter (Glue by default).
- **Screen:** the beat counts big over the tracks in the phrase's fourth and eighth bars; a knob
  turned shows its name and value big; the selected track's row stands out.
- **USB:** MIDI and a class-compliant audio input ("n001se FM-1"), switchable in Setup to save CPU.
- The full manual: [docs/MANUAL.txt](docs/MANUAL.txt).

## 0.1.0

- **n001se, the first build.** Four FM percussion sounds (kick, snare, hat, perc to start; click and
  metal too) where every hit varies a little, a random pattern sequencer over them with 1Ã¢â‚¬â€œ8 bars a
  track, and drifting effects (filter, drive, delay, reverb) that pick which hits they touch.
- **The keys are the sequencer:** the white keys are the 16 steps of a bar, lit where there is a hit,
  with the playhead walking over them; black keys 1Ã¢â‚¬â€œ8 choose the bar; 9Ã¢â‚¬â€œ11 mute, new pattern, new sound.
- **The phrase:** eight bars across the screen, the eighth red, counting 1 2 3 4 to the next round.
- MIDI in (USB and TRS): notes for new patterns, controllers for the macros and effects, MIDI clock.

## Earlier: FiMba-1

The versions below are FiMba-1's, the kalimba firmware n001se was built from.

## 0.6.1

- **Less work with Reverb at 0:** once the reverb's tail has died away, the reverb stops running until
  you turn it up again (on the FM-1, about 24% Ã¢â€ â€™ 15% CPU when nothing is playing). Nothing changes in
  the sound.

## 0.6.0

- **Reset to the defaults:** hold SAVE for 1.5 s, then press SAVE again to confirm (anything else, or
  waiting 5 s, keeps everything). Every sound setting goes back to its default; the Setup page (Tune,
  MIDI, Theme) stays as it is.
- **MIDI out is off by default.** Turn it on on the Setup page (GLO, KNOB 3). Settings you've already
  saved keep what they had.
- **Wah needs a sound hole:** on the Board and None bodies, Wah and Wah rate are greyed out and don't
  turn ("NO HOLE: BOX OR GOURD").
- **The middle of the kalimba glows:** the key under the lowest tine (white key 9 in Tine, 8 in
  Mirror; in Keyboard, the key that plays middle C) is dimly lit, and lights fully while it rings.

## 0.5.2

- **Wah is how much wah.** With Wah rate on, the hand now flutters from open to the Wah setting and
  back, so Wah 100 is the full sweep and low settings a gentle one. In 0.5.1 it was the other way
  round: the flutter filled what Wah left open, so Wah 1 swept the whole way and Wah 100 didn't move.
- The fastest Wah rates (up to ~8 Hz) now sweep fully instead of being smoothed away.

## 0.5.1

- **The wah is now heard.** Covering the sound hole (ENV Ã¢â€ â€™ Body: Wah, the mod wheel, pressure, the Hole
  key, or Wah rate) muffles the sound like a vowel closing: bright with the hole open, dark and a little
  quieter covered, with a vowel-like peak sweeping through the tines on the way. Before, it only moved a
  resonance below the lowest tine, and barely changed the sound. Box and Gourd have a hole; Board and
  None don't.
- The hand glides over the hole in about 20 ms, so the Hole key covers and uncovers it without a click.
- **Wah rate**'s lowest setting reads **Off** (it was "Hand"): the hole moves only when you move it.

## 0.5.0

- **Theme**, a new knob on the Setup page (GLO, KNOB 4): **Light**, the pale wood and dark ink as
  before, or **Dark**, near-black with cream and amber. In Dark the knob bars and the chips turn cream
  and brown, so amber only means a tine sounding or a knob touched. The tines keep their metal in both.
  The screen changes at once, and the theme is saved with your settings.
- The Setup page's header shows the firmware's version, beside the CPU load.
- Saved settings carry over; Theme starts on Light.

## 0.4.1

- **Tine is now left-first**, as on a 17-key kalimba in C. C4 sits on white key 9, the next note up
  (D4) is on its left, and the scale alternates outward: D6 on the far left, C6 on the far right
  ([#1](https://github.com/jadamsowers/fm1-fimba/issues/1)).
- **Mirror**, a new Layout value (Keys page, KNOB 1), keeps the old right-first arrangement: C4 on
  key 8, D4 on its right.
- Saved settings carry over. A project saved on Tine plays the new way; pick Mirror for the old one.

## 0.4.0

- **16 voices** (up from 12): fewer tines cut short when long-ringing steel tines and patterns overlap.
  Upper overtones that have died away are skipped, so ordinary playing costs less.
- **Worn**: an old, played-in kalimba. Each tine gets its own quirks: a little out of tune (up to Ã‚Â±12
  cents), a longer or shorter sustain, a faster or slower beat, uneven brightness and level.
- **Pages by button**: each page button owns its pages; press it again for the next (dots in the
  header show which). HOME: Tine Ã¢â€ â€™ **Character** (Worn, Glide, Tuning, Release). ENV: Body.
  FX: Space Ã¢â€ â€™ Color. LFO: Grain Ã¢â€ â€™ Spread. SEL: Keys Ã¢â€ â€™ Play. SEQ: Pattern. GLO: Setup.
- **ARP**: tap to start or stop the pattern; **hold** to open its settings.
- Settings saved by earlier versions carry over; new settings start at their defaults.

## 0.3.0

- The web page installs FiMba-1 on the FM-1 over USB, and offers the highest release.
- It runs on the FM-1: the first install on real hardware boots and plays.

## 0.2.0

- CI builds the firmware package and checks it against FoMni's; a version tag makes a release.

## 0.1.0

- The first release.

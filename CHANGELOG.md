# Changelog

A version is a `VERSION` change with its section below (CI checks the section is there). Firmware
files are not published here: the author hands them out. To build one yourself, see BUILDING.md; a
version's own build is `python3 tools/build.py --release X.Y.Z --id <BUILD_ID>` (M-UPGRADE takes only
a short identity, higher than the one installed).

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
    towards a pink slope of −4.5 dB an octave, adds a little foundation and air, takes a little box
    away, and turns the harsh 2–9 kHz down while it stands out. 50 by default, 0 flat.
  - **Color:** an inflator, denser and fuller near the top. 50 by default.
  - **Width:** 0 mono, 50 as it is, 100 twice as wide; the lows under 120 Hz always in mono. 60 by
    default.
- As before, the Glue is loudness matched (the volume is Level's), and 0 turns all of it off. With
  the Glue on, the limiter's ceiling is −1 dBFS (−0.3 dBFS off).
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
  the higher, the more often they move, but always slowly: each glide takes 1–4 bars and each place
  is held at least 4 bars (about 8 on average at 100). The delay fades from one length to the next,
  so nothing bends in pitch. With Space up, SEL also moves them by the same chance (always at 100).
  Chaos and Motion no longer move the delay time or the reverb's length and tone; Space does.
  Project format 10 (a saved project gets Space 0).

## 1.5.0

Firmware identity FM-1_821.

- **A new kind of sound: Clap.** Three or four quick bursts of band-passed noise, 6–13 ms apart,
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
- **Chaos for the acid line** (Tone page, KNOB 4, 0–100 %, 25 % to start with). At 0 % the line
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
  metal too) where every hit varies a little, a random pattern sequencer over them with 1–8 bars a
  track, and drifting effects (filter, drive, delay, reverb) that pick which hits they touch.
- **The keys are the sequencer:** the white keys are the 16 steps of a bar, lit where there is a hit,
  with the playhead walking over them; black keys 1–8 choose the bar; 9–11 mute, new pattern, new sound.
- **The phrase:** eight bars across the screen, the eighth red, counting 1 2 3 4 to the next round.
- MIDI in (USB and TRS): notes for new patterns, controllers for the macros and effects, MIDI clock.

## Earlier: FiMba-1

The versions below are FiMba-1's, the kalimba firmware n001se was built from.

## 0.6.1

- **Less work with Reverb at 0:** once the reverb's tail has died away, the reverb stops running until
  you turn it up again (on the FM-1, about 24% → 15% CPU when nothing is playing). Nothing changes in
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

- **The wah is now heard.** Covering the sound hole (ENV → Body: Wah, the mod wheel, pressure, the Hole
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
- **Worn**: an old, played-in kalimba. Each tine gets its own quirks: a little out of tune (up to ±12
  cents), a longer or shorter sustain, a faster or slower beat, uneven brightness and level.
- **Pages by button**: each page button owns its pages; press it again for the next (dots in the
  header show which). HOME: Tine → **Character** (Worn, Glide, Tuning, Release). ENV: Body.
  FX: Space → Color. LFO: Grain → Spread. SEL: Keys → Play. SEQ: Pattern. GLO: Setup.
- **ARP**: tap to start or stop the pattern; **hold** to open its settings.
- Settings saved by earlier versions carry over; new settings start at their defaults.

## 0.3.0

- The web page installs FiMba-1 on the FM-1 over USB, and offers the highest release.
- It runs on the FM-1: the first install on real hardware boots and plays.

## 0.2.0

- CI builds the firmware package and checks it against FoMni's; a version tag makes a release.

## 0.1.0

- The first release.

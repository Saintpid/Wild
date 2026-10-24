# n001se: FM-slagverk som aldrig låter likadant, för M-VAVE FM-1

n001se är en egen firmware för **M-VAVE FM-1** som gör den till en slumpande FM-trummaskin för live.
Den bygger på idén bakom Beatsurfing RANDOM: minimalism med slump. Fyra ljud, varje slag lite olika,
mönster som uppstår av sig själva, och effekter som driver långsamt. Det får aldrig bli plottrigt.

![Spelar](docs/img/playing.png) ![Takt 8 räknar in](docs/img/phrase.png) ![Effekter](docs/img/fx.png)

> **Status: första bygget (0.1.0).** Byggt och testat i simulatorn, ännu inte på riktig hårdvara.
> Installera på egen risk. Plattformen är FiMba-1:s oförändrade: chipets egen uppdateringsmod kan
> inte skrivas över, firmwaren faller tillbaka till ett säkert läge om den inte startar, och
> originalfirmwaren går alltid att lägga tillbaka (se [Installera](#installera)).

## Så låter den

- **Fyra ljud**, ett per spår: en FM-röst med fyra operatorer i åtta algoritmer, brus, wavefolder,
  bitcrush och en resonator. Varje slag varierar runt ljudets grund (Deviance). Ett nytt ljud dras
  ur en sort: kick, snare, hat, perc, click eller metal.
- **Mönster som slumpas fram:** varje spår loopar över sin egen längd, 1–8 takter om 16 steg, så
  spåren glider mot varandra. Ett nytt mönster lägger ett slumpat antal slag (Hits min–max, 1–64)
  där ljudets sort brukar spela. Evolve flyttar ibland ett slag när loopen går runt.
- **Effekter som väljer slag:** filter och drive på enstaka slag, delay och reverb (ett ljud i taget).
  Amount är hur många slag som får effekten, Chaos hur långt inställningarna vandrar, Motion hur fort.
- **Aldrig för mycket:** Max hits begränsar hur många ljud som slår på samma steg.

## Kontrollerna

| Kontroll | Gör |
| --- | --- |
| **Vita tangenter** (SEQ-läge) | De 16 stegen i takten som visas, för valt spår. Tänd = slag, svag = spelhuvudet. Tryck sätter ett slag; tryck på ett slag tar bort det när du släpper |
| Håll ett steg + **KNOB 1 / 2** | Slagets velocity / ton (±12 halvtoner) |
| **Svarta tangenter 1–8** | Takterna. Svag = finns i spåret, stark = spelas nu. Tryck visar och redigerar den takten och stannar där (blinkar); tryck igen så följer vyn spelhuvudet |
| **Svart 9 / 10 / 11** | Tysta spåret / nytt mönster / nytt ljud av samma sort |
| **PLAY** | Start / stopp |
| **REC** | Nya mönster på alla otystade spår |
| **SEQ** | Växla SEQ-läge och LIVE-läge (vita tangenter spelar ljuden: fyra tangenter per ljud, uppåt i skalan) |
| **OCT− / OCT+**, **SELECT** | Välj spår |
| **ALGORITHM** | Bläddra mellan sidorna |
| **PRESETS** | Tempo |
| **HOME EDIT ENV FX LFO GLO** | Sidorna (tryck igen för knappens nästa sida) |
| **SAVE** | Spara (sparar också själv efter en ändring, men bara när den står still: en flashskrivning tystar ljudet ett ögonblick). Håll: återställ |
| **OCT− + OCT+ i 5 s** | Uppdateringsläge (för att installera firmware) |

| Sida | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| Pattern (HOME) | Bars | Hits min | Hits max | Evolve |
| Track (EDIT) | Rnd len (slumpad längd 1–N takter) | Pitch (tonomfång) | Level | Mute |
| Sound (ENV) | Deviance | Length | Edge | Ring |
| Tone (ENV igen) | Kind | Ring tone | | |
| FX (FX) | Reverb | Delay | Filter | Drive (0 = av) |
| Drift (LFO) | Chaos | Motion | | |
| Groove (GLO) | Tempo | Swing | Loose | Max hits |
| Setup (GLO igen) | Level | MIDI ch | | |

**Skärmen** visar de fyra spåren med en takt var (vald takt för valt spår, spelande takt för de
andra), varje ljuds nivå, och R / D när ljudet äger reverben / delayen. Under dem **frasen**: åtta
takter, den åttonde röd. När den åttonde takten börjar räknas 1 2 3 4 till höger, så du vet att nästa
varv (ett break, ett drop) kommer.

### MIDI (USB och TRS)

| In | Gör |
| --- | --- |
| C1 D1 … (36–39) | Nytt mönster på spår 1–4 (startar om det står still) |
| C2 (48) | Nya mönster på alla spår |
| C3–C5 (60–84) | Grundton för spår med Pitch |
| CC 102–106 | Deviance, Length, Edge, Ring, Ring tone (alla spår) |
| CC 107 / 108 | Chaos / Motion |
| CC 109–112 | Filter, Drive, Delay, Reverb (0 = av) |
| CC 113 / 114 / 115 | Swing / Loose / Max hits |
| CC 7, 116 | Level |
| CC 117 / 118 (≥ 64) | Nya ljud / nya mönster på alla spår |
| CC 120, 123 | Stopp |
| Klocka | Tempot följer, Start / Stop startar och stoppar |

## Installera

Enklast från FiMba-1:s webbsida, som installerar vilken Felucca-firmware som helst: öppna
<https://jadamsowers.github.io/fm1-fimba/#install> i Chrome eller Edge, anslut FM-1 med USB, välj
`n001se.fwsc` under "Or a firmware file" och tryck **Install this file**. Ladda först ner M-VAVE:s
originalfirmware (FM-1 V15, "PC Firmware" på m-vave.com/download) så att du alltid kan gå tillbaka;
samma sida lägger tillbaka den under "Back to the stock firmware".

Från kommandoraden: `python3 tools/fm1_install.py build/n001se.fwsc`.

## Bygga och testa

Detaljerna finns i [BUILDING.md](BUILDING.md) (JieLis Linux-verktygskedja och tre filer ur AC79-SDK:n).

```
tests/run_tests.sh      # motorn, plattformen och hela appen i simulatorn
./build.sh              # build/n001se.fwsc
```

**Simulatorn** (`build/host/n001se_host SKRIPT UTMAPP`) kör hela firmwaren på datorn från ett skript
(`tests/scenarios/*.kal`): skärmbilder, ljud och MIDI kommer ut. Bilderna här är dess.

## Släktträd och tack

n001se står på en kedja av öppen firmware för FM-1:
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton Instruments) gjorde FM-1
programmerbar; [X0X](https://github.com/charlesvestal/fm1-x0x) och
[FoMni](https://github.com/charlesvestal/fm1-fomni) (Charles Vestal) byggde andra instrument på den
plattformen och lade till simulatorn; [FiMba-1](https://github.com/jadamsowers/fm1-fimba) (Adam
Sowers) är kalimban som n001se är byggd ur. Plattform, uppdatering, säkert läge och simulator är
deras. Ljudmotorn, sequencern, effekterna och gränssnittet är n001se:s.

## Licens

GPL-3.0-only, som hela kedjan. Se [LICENSING.md](LICENSING.md). "M-VAVE" och "FM-1" är sina ägares
varumärken; n001se är fristående och inte knuten till dem.

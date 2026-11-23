# The Outside Inside

Generative field recordings for the M-VAVE FM-1, made entirely by synthesis: fifteen places to stand in, four layers of
sound that never quite repeat, for playing along with or leaving on in the background.

By [TheGrizard](https://github.com/TheGrizard). Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) and felucca-bsp. Works on both FM-1 flash
chips: the original Puya P25Q80 and the UC25HQ80 in units sold since autumn 2026.

## Playing it

The home screen shows the scene. Where a bird or a car appears on screen is where you hear it in the stereo field.
Turn a knob and the four layers' levels rise up from the bottom of the screen, one column a knob, left to right as
the knobs are; each column's bar shows the sound's level as it plays, the white mark the volume you set.

| Control | What it does |
|---|---|
| K1 to K4 | volume of WEATHER, ANIMALS, LANDSCAPE, HUMANS (the columns show while you turn) |
| SEL held + K1 to K4 | each layer's density: how often its events happen |
| HOME held | shows what the knobs do now: the macros; turn K1 to K4 to set WEATHER, TIME, ACTIVITY, ENTROPY |
| ALGORITHM | change environment, from a list: JOURNEY, DREAM, the fifteen built in A to Z, then your own (USER 1 ...) |
| SELECT (turn), or tap SEL | choose the layer to edit (its preset list opens) |
| PRESET | change the chosen layer's preset, A to Z |
| REC | randomise the macros |
| SAVE | save as your own environment (USER 1, USER 2 ... up to 8) |
| PLAY | stop and start (it fades) |
| OCT- / OCT+ | the previous / next weather, A to Z (on a journey: hold here / travel on now) |
| first four white keys | trigger one event now from WEATHER, ANIMALS, LANDSCAPE, HUMANS |
| HOME tapped | processor load, cores, whether settings are being saved, and what each layer costs |

Only the buttons and keys that do something light up: they glow, and light fully while pressed (PLAY green while
playing, SAVE while there is something unsaved).

A built-in environment starts with its own volumes and the densities as designed; your own environments keep the
volumes they were saved with (and MEADOW starts calm). Each world has its own weather, and ENTROPY only drifts
through that world's:

- Earth, A to Z: breeze, calm, downpour, drizzle, fog, gale, rain, snow, thunder (dry, with the odd light shower),
  thunderstorm, windy
- Space: ice dust, magnetic storm, particle rain, solar flare, solar wind, still void
- the alien world: crystal fog, howling gale, ion drizzle, ion rain, ion storm, singing wind, still air
- the Arctic (it never rains): blizzard, diamond dust, gale, ice fog, light snow, snow, still cold, windy
- the desert (no rain, no snow): breeze, dry thunder, heat haze, sandstorm, still air, windy
- the savanna (no snow): breeze, calm, downpour, heat haze, morning mist, rain, thunder, thunderstorm, windy

(Mountain and Village have all of Earth's.) Meadow starts calm, the Arctic in light snow, the desert in a heat haze,
the savanna in a breeze.

Space's and the alien world's weather also wanders slowly by itself as ENTROPY rises, over minutes. In heavy rain,
a storm or a gale the animals go quiet and take cover (not in the jungle, which sings on; frogs love the rain).

The macros:

- **WEATHER** picks the weather type. ENTROPY lets it drift towards the types either side of it, calmer or wilder.
- **TIME** is the time of day: dawn chorus, daytime, dusk, night. Different animals and people come out at each.
- **ACTIVITY** is how much animal and human life there is.
- **ENTROPY** is how much the scene changes by itself. At zero the scene stays as you set it. Turned up, the weather
  drifts, the light changes and the place moves through its states (a forest's still morning, its rain shower).

An environment with a star after its name (`FOREST*`) has had a layer's patch changed. SAVE keeps it as your own.
The current scene (levels, macros, patches) is kept by itself a few seconds after your last change, so the FM-1
comes back where you left it.

MIDI in: CC 20 to 23 are the layer volumes, CC 24 to 27 the macros, on any channel.

### The places

Alien, Arctic, Caves, City, Desert, Forest, Industrial, Jungle, Meadow, Mountain, Ocean, Savanna, Space, Swamp, Village.
The five newest: **Mountain** (a ridge wind, a buzzard, ravens and choughs, marmots, a far waterfall and rockfalls;
cowbells and a church tolling in the valley below, a helicopter), **Arctic** (a polar wind and spindrift, the ice
cracking and groaning, bearded seals trilling under it, matched to a recording of them, wolves, terns; a research
station, its flag, a snowmobile), **Desert** (mostly stillness, as recorded: a dry wind carrying sand, booming dunes,
stones ticking in the heat, a ticking insect, a lark's chip now and then, a scops owl, jackals; a caravan's bells, a 4x4),
**Savanna** (a storm wind, the long grass, a lion's roars and grunts as recorded, hyenas, hornbills, doves and weavers; a camp's fire
and generator, a jeep) and **Village** (rain on roofs, big trees and the pond, rooks, garden birds, ducks, a cockerel,
cows and a dog; the church's bells ringing changes or one tolling, as recorded; a lawnmower, the village road).

### Journey

JOURNEY (amber), first in the environment list, travels round the Earth's places on its own, about an hour a loop: forest,
meadow, city, industrial, ocean, swamp, jungle, caves and back to the forest. It stays six minutes in each,
then takes a minute and a half to get to the next. On the way the weather turns first, then the other layers one
at a time (the one loudest where you're going first, the one loudest where you were last), so for a while you hear
both places at once and the sound never drops out. The weather glides to one that suits the next place, the day
goes round (a day an hour: one lap is one day, so started at dawn the forest is always morning and the caves
always night) and the places are evened out in loudness: made to play under a live set.

- **OCT+** travels on to the next place now; **OCT-** holds the journey where it is (and lets it go again).
- **K1 to K4** (or CC 20 to 23) set the mix of the four layers for the whole journey.
- Turning **WEATHER** takes the weather over until the next place; turning **TIME** sets the time of day, and the
  day goes on from there. ACTIVITY and ENTROPY work as ever.
- **SAVE** keeps the place you're at as one of your own environments; the journey goes on.
- Turned off on a journey, the FM-1 comes back on a journey, from the place it was in.

### Dream

DREAM (violet), second in the list, travels as the journey does (the same pace, the layers changing one at a time, the day
going round, OCT+ and OCT-, K1 to K4, SAVE), but each place is a mix drawn at random from all fifteen: the land from
one, and the animals, the people and the weather from it or from anywhere else, so an alien plain fills with a
factory's machines, a cave with a meadow's birds. The weather is the weather layer's world's (Space's and the alien
world's their own). The screen's top line says DREAM (or JOURNEY), and under it the land and where the life and the
people are from (LAND/LIFE/PEOPLE; a journey: the place, and > NEXT on the way). A mix is never
heavier on the processor than the heaviest place.

## Building it

The repository holds the app in `app/` and [fm1-base](https://github.com/charlesvestal/fm1-base) in `base/`, as a
git submodule. JieLi's compiler for the FM-1's chip runs on Linux (on Windows, in WSL; on a Mac, fm1-base runs it in a
container).

    git clone --recursive https://github.com/TheGrizard/The-Outside-Inside
    cd The-Outside-Inside
    base/tools/setup_toolchain.sh        # JieLi's compiler and the AC79 SDK's three files, checked against a lock
    ./build.sh                           # app/build/the-outside-inside.fwsc

Install it with the FM-1 plugged in by USB (needs `pip install mido python-rtmidi`; on Windows, Python 3.12):

    python3 base/tools/fm1_install.py app/build/the-outside-inside.fwsc

### Trying it on the computer first

    ./build.sh --host
    tests/run_tests.sh                   # the simulator's checks: controls, lists, saving, the screen reader
    app/build/host/the-outside-inside_host app/tests/home.txt /tmp/shots   # screenshots and a WAV of a run

`make -C app/tools` builds `render`, which plays an environment through a scripted tour of its weathers and times
without the screen: `app/tools/render ocean 120 7 ocean.wav`.

### Releases

Pushing a tag such as `v0.1` builds the release on GitHub (`.github/workflows/release.yml`, the FM-1 builder shared
by fm1-base's firmwares), runs the tests and signs the packages, so the FM-1 Firmware Hub can show it as built from
source.

## On the new flash chip

FM-1s sold since autumn 2026 carry a UC25HQ80 flash chip instead of the Puya. fm1-base recognises both, and its flash
driver waits out the slower erases of the new part (up to 2 s, then resets the part safely), so saving works on both.
If an FM-1 ever has a chip neither knows, The Outside Inside still plays, but tapping HOME shows **NO SAVING** and SAVE says
**NOT SAVED**: settings last until power off.

## Licence

MPL-2.0. See fm1-base's LICENSING.md for the parts it is built from. The screen is in DejaVu Sans (app/fonts, its
licence there: the Bitstream Vera licence, DejaVu's changes in the public domain).

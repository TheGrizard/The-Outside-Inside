# The Outside Inside

Generative field recordings for the M-VAVE FM-1, made entirely by synthesis: nine places to stand in, four layers of
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
| ALGORITHM | change environment, from a list: the nine built in A to Z, then your own (USER 1 ...) |
| SELECT (turn), or tap SEL | choose the layer to edit (its preset list opens) |
| PRESET | change the chosen layer's preset, A to Z |
| REC | randomise the macros |
| SAVE | save as your own environment (USER 1, USER 2 ... up to 8) |
| PLAY | stop and start (it fades) |
| OCT- / OCT+ | the previous / next weather, A to Z |
| first four white keys | trigger one event now from WEATHER, ANIMALS, LANDSCAPE, HUMANS |
| HOME tapped | processor load, cores, whether settings are being saved, and what each layer costs |

A built-in environment starts with its own volumes and the densities as designed; your own environments keep the
volumes they were saved with. The weather types, A to Z: breeze, calm, downpour, drizzle, fog, gale, rain, snow,
thunderstorm, windy.

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

MPL-2.0. See fm1-base's LICENSING.md for the parts it is built from.

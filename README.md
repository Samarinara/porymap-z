# Porymap-Z

[![Build Porymap-Z](https://github.com/Samarinara/porymap-z/actions/workflows/main.yml/badge.svg)](https://github.com/Samarinara/porymap-z/actions/workflows/main.yml)

Porymap-Z is a fork of [Porymap](https://github.com/huderlem/porymap) that is growing from a map editor into an integrated game editor for Pokémon generation 3 decompilation projects. It retains the existing game runtime and Porymap's map, event, tileset, region map, and wild encounter tools.

**First milestone:** assembly dialogue editing for **pokeemerald**. Open a map and choose the **Dialogue** main tab to edit its dialogue by label or its full event scripts. The default **Current scene** scope follows map selection, including shared scripts; **Whole project** lets you browse other dialogue. Save directly or use Save/Ctrl+S. See the [dialogue guide](docs/porymap-z/dialogue.md) for supported syntax and limitations.

**Battle editing:** stock **pokeemerald and pokefirered** C trainer data and current **pokeemerald-expansion `.party`** source are supported. Choose the **Battle** main tab to edit trainer settings, Pokémon parties, trainer IDs, and scene battle scripts. It includes searchable structured fields, project-constant suggestions, party navigation, adding Pokémon, duplication/deletion, and full source access. See the [battle guide](docs/porymap-z/battles.md). Further game systems are described in the [roadmap](docs/porymap-z/roadmap.md); a standalone runtime and Poryscript editing remain outside the current implementation.

## Build

Use Qt 5.14.2 or newer (including Qt 6), a C++17 compiler, and make. Qt Widgets is required; QML, Charts, and Network enable additional upstream features. See [INSTALL.md](INSTALL.md) for platform prerequisites; use the fork URL and build commands below in place of its upstream examples.

```sh
git clone https://github.com/Samarinara/porymap-z.git
cd porymap-z
mkdir -p build/app
cd build/app
qmake ../../porymap-z.pro
make -j4
./porymap-z
```

On macOS, run `porymap-z.app/Contents/MacOS/porymap-z` instead. Build with `porymap.pro` to retain the upstream application target; fork features are enabled by `porymap-z.pro`.

There are no Porymap-Z binary releases yet. The [original Porymap releases](https://github.com/huderlem/porymap/releases) do not contain these additions.

## Tests

```sh
mkdir -p build/dialogue-tests
cd build/dialogue-tests
qmake ../../tests/porymapz/dialogue.pro
make -j2
./tst_dialogue
cd ..
mkdir -p dialogue-ui-tests
cd dialogue-ui-tests
qmake ../../tests/porymapz/editor.pro
make -j2
QT_QPA_PLATFORM=offscreen ./tst_dialogueeditor
cd ..
mkdir -p battle-tests
cd battle-tests
qmake ../../tests/porymapz/battle.pro
make -j2
QT_QPA_PLATFORM=offscreen ./tst_battle
```

To test the main-window tab integration as well, run from the repository root:

```sh
cd build/app
qmake CONFIG+=porymap_z_tests ../../porymap-z.pro
make -j4
QT_QPA_PLATFORM=offscreen ./tst_mainwindow
qmake ../../porymap-z.pro
make -j4
```

For an optional round-trip test against a real pokeemerald checkout, run `PORYMAP_Z_TEST_PROJECT=/path/to/pokeemerald ./tst_dialogue` from the dialogue test build directory. This test edits temporary copies and leaves that checkout untouched.

## Upstream and licensing

Porymap's history and [license](LICENSE.md) are retained. New editor code lives in `src/porymapz` and `include/porymapz`, with a separate qmake entry point to limit upstream merge conflicts. Follow the [upstream maintenance guide](docs/porymap-z/upstream.md) to integrate changes. A weekly GitHub Actions check reports upstream commits; integration is reviewed and tested before merging.

The [Porymap manual](https://huderlem.github.io/porymap/) and [upstream changelog](CHANGELOG.md) cover inherited functionality. Fork-specific behavior is documented under [docs/porymap-z](docs/porymap-z/).

### Play the current map

Click **Play** in the toolbar, configure a demo trainer (name, boy/girl character,
partner Pokémon, and level), and select an installed GBA emulator. mGBA and common
VBA executables are detected on `PATH`; **Browse** accepts another emulator that
opens a ROM passed as its first argument. Click **OK**, then click a tile on the
current map. Escape or right-click cancels tile selection without editing the map.

To test receiving a starter, enable **Start with an empty party** on the
**Character** tab. The partner and level controls are disabled. On **Story state**,
use **Add flag** to choose a project flag and **Set** or **Clear** it, and **Add
variable** to choose a story variable and give it a value from 0 to 65535. Names
are searchable, and you can type a project's `FLAG_...` or `VAR_...` constant.
Remove rows to leave those values at the game's normal new-game defaults.
Duplicate names and malformed constants are rejected; undefined constants are
reported by the game compiler. These settings are remembered per project.

Overrides apply after new-game defaults, before loading the starting map, and
are restored once after the initial map's temporary-state reset. The starting
map's scripts then run normally and can change the values. Later map visits do
not reapply the overrides, so acquiring a starter can progress the story normally.
Choose flags and variable values based on the conditions in your project's
starter scripts; the required state differs between games and forks. All changes
remain in the isolated demo, and each launch starts from a fresh save.

Porymap saves current project edits and builds an isolated demo that skips the intro
and starts a fresh character at the selected tile. **Demo area** defaults to the
current map. Choose immediate connections and warp destinations for a small area,
or all maps reachable through connections and warps for a larger demo. Unselected
layout payloads are omitted; layouts referenced by scripts and engine features
(such as puzzles) stay available alongside map IDs, scripts, battles, Pokémon and
shared engine assets. Connections across the demo boundary are removed, and
scripted or dynamic warps outside the area return to the current position.
Script-only destinations are not discovered by the scope selector. Check **Build
whole game** to include every map and allow unrestricted travel while still using
the demo character and starting tile. Scope settings are remembered per project.

Builds use available CPU cores (unless Build arguments or `MAKEFLAGS` specify a
job count). An isolated cache under the application's cache directory retains
compiled files, so repeat launches rebuild only changed dependencies. Unchanged
generated map headers retain their timestamps to avoid recompiling the engine
after geometry edits. Compatible existing build files seed the first demo cache.
Source edits are synchronized before every launch; normal project sources and builds are left
untouched. Game targets and build arguments have separate caches. Demo saves are
removed when the emulator closes. **Stop** cancels the build or emulator; the
output window shows build and launch errors. Deleting the application's `playtest`
cache folder while no demo is running forces a fresh build.

This requires **Python 3**, **make**, and the project's usual GBA build toolchain.
The startup adapter supports pret **pokeemerald**, **pokefirered**, and
**pokeemerald-expansion** startup functions; forks with different startup code may require an adapter. Projects with
symlinks are rejected to protect the original files. Optional build arguments
(e.g. `modern`) and character settings are remembered per project. The initial
copy and build can take a while and need enough temporary disk space for the
project. The first build still compiles the shared game engine. Normal map scripts
still run, including any map-specific story events.

For current **pokeemerald-expansion**, choose **Emerald**, **FireRed**, or
**LeafGreen** in Play’s **Build target** field. Emerald uses Hoenn maps;
FireRed and LeafGreen use Kanto maps (such as `PalletTown_Frlg`). The target is
remembered per project. **Project default** keeps the existing Makefile/build
arguments behavior. With an explicit target, use Build arguments for options
such as `release` or `LTO=0`, and remove game selectors such as `firered` or
`BUILD=leafgreen` from that field.

Keep expansion’s project **base_game_version** set to **pokeemerald** for all
three ROM targets. The editor reads each layout’s `layout_version` metadata to
handle Emerald and FRLG border dimensions, tile/palette splits, and 16/32-bit
metatile attributes together. Existing `region` and `layout_version` fields are
preserved when saving; new layouts/maps inherit their primary tileset’s format
and region. Changing the ROM build target does not change the project’s data format.

To verify Play against a real game checkout using its normal toolchain, run:

```sh
python3 tests/porymapz/verify_playtest_build.py /path/to/pokeemerald-expansion \
    --artifact-dir build/playtest-verification
```

This runs the same temporary-copy build as Play, checks the generated GBA header
and checksum, exports the ROM and build artifacts, and verifies that the original
startup sources are unchanged. It uses a ROM checker in place of the emulator;
it does not test gameplay. Use `--make-arg=modern` or other repeated
`--make-arg=...` options when the project needs additional build arguments.

To check startup in mGBA as well, build the headless checker against an mGBA
source checkout (requires CMake and a native C/C++ compiler):

```sh
cmake -S tests/porymapz/boot -B build/playtest-boot -DMGBA_SOURCE_DIR=/path/to/mgba
cmake --build build/playtest-boot --target playtest_boot -j
python3 tests/porymapz/verify_playtest_build.py /path/to/pokeemerald-expansion \
    --artifact-dir build/playtest-verification \
    --boot-checker build/playtest-boot/playtest_boot
```

The boot check runs 900 emulated frames from a fresh save and requires the
overworld callbacks, selected map/tile, and a visible map image. It records
`boot.json` and rejects black screens and startup hangs. This tests startup,
not full gameplay. Existing exported artifacts can be checked without rebuilding
the ROM using `verify_playtest_boot.py ARTIFACT_DIR --checker CHECKER_PATH`.

Verify each expansion ROM target with:

```sh
python3 tests/porymapz/verify_playtest_build.py /path/to/pokeemerald-expansion \
    --build-target emerald --map MAP_ROUTE105 --artifact-dir build/verify-emerald
python3 tests/porymapz/verify_playtest_build.py /path/to/pokeemerald-expansion \
    --build-target firered --map MAP_PALLET_TOWN --artifact-dir build/verify-firered
python3 tests/porymapz/verify_playtest_build.py /path/to/pokeemerald-expansion \
    --build-target leafgreen --map MAP_PALLET_TOWN --artifact-dir build/verify-leafgreen
```

Explicit-target verification also checks the GBA game code (`BPEE`, `BPRE`, or
`BPGE`). Run `python3 -m unittest discover -s tests/porymapz -p test_playtest.py`
for launcher regressions. Set `PORYMAP_Z_EXPANSION_PROJECT=/path/to/pokeemerald-expansion`
when running `tst_mainwindow` to check real Hoenn/Kanto map loading and byte-for-byte
metatile attribute round trips, using temporary output files.

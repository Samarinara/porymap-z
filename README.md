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

Porymap saves current project edits, copies the project into a temporary directory,
builds a demo ROM, and launches the emulator there. The demo skips the intro and
starts a fresh character at the selected tile. Its source patches, ROM, and saves
are discarded when the emulator closes. **Stop** cancels the build or emulator;
the output window shows build and launch errors.

This requires **Python 3**, **make**, and the project's usual GBA build toolchain.
The startup adapter supports pret **pokeemerald**, **pokefirered**, and
**pokeemerald-expansion** startup functions; forks with different startup code may require an adapter. Projects with
symlinks are rejected to protect the original files. Optional build arguments
(e.g. `modern`) and character settings are remembered per project. The initial
copy and build can take a while and need enough temporary disk space for the
project. Normal map scripts still run, including any map-specific story events.

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

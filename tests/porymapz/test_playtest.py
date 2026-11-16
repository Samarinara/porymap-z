"""Playtest isolation, patch validation, build failure, and emulator handoff."""
import importlib.util
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

HELPER = Path(__file__).resolve().parents[2] / 'resources/demo/playtest.py'
spec = importlib.util.spec_from_file_location('playtest', HELPER)
playtest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(playtest)


class PlaytestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='demo test ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'original'
        (self.root / 'src').mkdir(parents=True)
        (self.root / 'src/intro.c').write_text('''
#include "global.h"
void CB2_InitCopyrightScreenAfterBootup(void) {
    if (!SetUpCopyrightScreen()) {
        SetSaveBlocksPointers(0);
        LoadGameSave(SAVE_NORMAL);
        SetPokemonCryStereo(gSaveBlock2Ptr->optionsSound);
    }
}
''')
        (self.root / 'src/new_game.c').write_text('''
#include "global.h"
#include "pokemon.h"
void NewGameInitData(void) {
    /* A brace in a comment: } */
    ClearSav1();
    WarpToTruck();
    ResetFlags();
}
void OtherFunction(void) { }
''')
        (self.root / 'src/overworld.c').write_text('''
void CB2_NewGame(void) {
    NewGameInitData();
    gFieldCallback = ExecuteTruckSequence;
    DoMapLoadLoop(&gMain.state);
}
''')
        self.config = dict(root=str(self.root), map='MAP_ROUTE101', name='DEMO', gender=1,
                           species='SPECIES_TREECKO', level=25, x=3, y=7, emulator='unused')

    def scope_fixture(self):
        for name, connections, warps in (
                ('Route101', ['MAP_TOWN'], []),
                ('Town', ['MAP_ROUTE101'], ['MAP_HOUSE']),
                ('House', [], ['MAP_TOWN']), ('Island', [], [])):
            folder = self.root / 'data/maps' / name
            folder.mkdir(parents=True)
            (folder / 'map.json').write_text(json.dumps(dict(
                id='MAP_' + name.upper(), name=name, layout='LAYOUT_' + name.upper(),
                connections=[dict(map=key, direction='up', offset=0) for key in connections],
                warp_events=[dict(dest_map=key, dest_warp_id='0') for key in warps])))
        (self.root / 'data/layouts').mkdir()
        (self.root / 'data/layouts/layouts.json').write_text(json.dumps(dict(layouts=[
            dict(id='LAYOUT_' + name.upper(), name=name + '_Layout')
            for name in ('Route101', 'Town', 'House', 'Island')])))
        (self.root / 'map_data_rules.mk').write_text(
            'data/layouts/layouts.inc: data/layouts/layouts.json\n'
            '\t$(MAPJSON) layouts emerald $< data/layouts include/constants\n')
        path = self.root / 'src/overworld.c'
        path.write_text('#include "global.h"\n' + path.read_text() + '''
void WarpIntoMap(void) {
    ApplyCurrentWarp();
    LoadCurrentMapData();
    SetPlayerCoordsFromWarp();
}
/* sWarpDestination is normally declared above these functions. */
''')

    def test_map_scopes_follow_connections_and_warps_without_including_islands(self):
        self.scope_fixture()
        expected = dict(current={'MAP_ROUTE101'}, nearby={'MAP_ROUTE101', 'MAP_TOWN'},
                        reachable={'MAP_ROUTE101', 'MAP_TOWN', 'MAP_HOUSE'})
        for scope, selected in expected.items():
            with self.subTest(scope=scope):
                self.assertEqual(playtest.demo_maps(self.root, dict(self.config, scope=scope))[1], selected)
        self.assertIsNone(playtest.demo_maps(self.root, dict(self.config, scope='full')))
        with self.assertRaisesRegex(ValueError, 'Invalid demo scope'):
            playtest.demo_maps(self.root, dict(self.config, scope='unknown'))

    def test_scope_guards_warps_and_removes_boundary_connections_preserving_events(self):
        self.scope_fixture()
        target = Path(self.temp.name) / 'snapshot'
        playtest.copy_project(self.root, target)
        selection = playtest.demo_maps(self.root, dict(self.config, scope='current'))
        playtest.scope_project(target, self.root, self.config, selection)
        playtest.patch_project(target, self.config, original=self.root, selection=selection)
        source = (target / 'src/overworld.c').read_text()
        self.assertIn('sWarpDestination.mapNum == MAP_NUM(MAP_ROUTE101)', source)
        self.assertIn('sWarpDestination.x = gSaveBlock1Ptr->pos.x;', source)
        self.assertNotIn('MAP_NUM(MAP_TOWN)', source)
        town = json.loads((target / 'data/maps/Town/map.json').read_text())
        self.assertEqual(town['connections'], [])
        self.assertEqual(town['warp_events'][0]['dest_map'], 'MAP_HOUSE')
        self.assertIn('--mapjson', (target / 'map_data_rules.mk').read_text())
        playtest.scope_project(target, self.root, self.config, None)
        playtest.patch_project(target, self.config, original=self.root)
        self.assertNotIn('--trim-layouts', (target / 'map_data_rules.mk').read_text())
        self.assertIn('--mapjson', (target / 'map_data_rules.mk').read_text())
        self.assertNotIn('sWarpDestination.mapNum', (target / 'src/overworld.c').read_text())
        self.assertEqual((target / 'data/maps/Town/map.json').read_bytes(),
                         (self.root / 'data/maps/Town/map.json').read_bytes())

    def test_trim_layouts_keeps_symbols_and_selected_payload(self):
        folder = self.root / 'data/layouts'
        folder.mkdir(parents=True)
        source = ''.join(f'''{name}_Border::
    .incbin "{name}/border.bin"
{name}_Blockdata::
    .incbin "{name}/map.bin"
    .align 2
{name}::
    .4byte {name}_Blockdata
''' for name in ('Zulu', 'Selected', 'Alpha'))
        path = folder / 'layouts.inc'
        path.write_text(source)
        (self.root / '.porymap-demo-layouts.json').write_text(json.dumps(dict(
            aliases=dict(Alpha='Selected', Zulu='Selected', OtherRegion='Selected'))))
        playtest.trim_layouts(self.root)
        trimmed = path.read_text()
        self.assertIn('.incbin "Selected/map.bin"', trimmed)
        self.assertNotIn('.incbin "Zulu/', trimmed)
        self.assertNotIn('.incbin "Alpha/', trimmed)
        for name in ('Zulu', 'Alpha'):
            self.assertIn(f'.set {name}, Selected', trimmed)
            self.assertIn(f'.global {name}_Blockdata', trimmed)
        playtest.trim_layouts(self.root)
        self.assertEqual(path.read_text(), trimmed)

    def test_scope_keeps_alternate_layouts_used_by_scripts_and_shared_engine(self):
        self.scope_fixture()
        (self.root / 'data/maps/Route101/scripts.inc').write_text('setmaplayout LAYOUT_HOUSE\n')
        (self.root / 'src/puzzle.c').write_text('return LAYOUT_TOWN;\n')
        # Generated definitions must not cause every layout to be retained.
        (self.root / 'include/constants').mkdir(parents=True)
        (self.root / 'include/constants/layouts.h').write_text('#define LAYOUT_ISLAND 4\n')
        target = Path(self.temp.name) / 'snapshot'
        playtest.copy_project(self.root, target)
        selection = playtest.demo_maps(self.root, dict(self.config, scope='current'))
        playtest.scope_project(target, self.root, self.config, selection)
        manifest = json.loads((target / '.porymap-demo-layouts.json').read_text())
        self.assertEqual(manifest['aliases'], {'Island_Layout': 'Route101_Layout'})

    def test_cached_sync_preserves_patches_and_build_outputs_but_updates_sources(self):
        target = Path(self.temp.name) / 'cache/project'
        (self.root / 'build').mkdir()
        (self.root / 'build/intro.o').write_bytes(b'original object')
        constants = self.root / 'include/constants/layouts.h'
        constants.parent.mkdir(parents=True)
        constants.write_text('original generated constants')
        other = self.root / 'src/other.c'
        other.write_text('original source')
        removed = self.root / 'src/removed.c'
        removed.write_text('removed later')
        playtest.sync_project(self.root, target)
        playtest.scope_project(target, self.root, self.config, None)
        playtest.patch_project(target, self.config, original=self.root)
        patched = target / 'src/new_game.c'
        timestamp = patched.stat().st_mtime_ns
        (target / 'build/intro.o').write_bytes(b'demo object')
        (self.root / 'build/intro.o').write_bytes(b'new original object')
        other.write_text('updated source')
        removed.unlink()
        playtest.sync_project(self.root, target)
        playtest.scope_project(target, self.root, self.config, None)
        playtest.patch_project(target, self.config, original=self.root)
        self.assertEqual(patched.stat().st_mtime_ns, timestamp)
        self.assertEqual((target / 'build/intro.o').read_bytes(), b'demo object')
        self.assertEqual((target / 'include/constants/layouts.h').read_text(),
                         'original generated constants')
        self.assertEqual((target / 'src/other.c').read_text(), 'updated source')
        self.assertFalse((target / 'src/removed.c').exists())
        # A change to a patched original must also reach the next demo.
        original = self.root / 'src/new_game.c'
        original.write_text(original.read_text().replace('ResetFlags();', 'ResetFlags(); /* edit */'))
        playtest.sync_project(self.root, target)
        playtest.patch_project(target, self.config, original=self.root)
        self.assertIn('/* edit */', patched.read_text())

    def test_parallel_map_converters_do_not_race_on_generated_outputs(self):
        converter = self.root / 'converter.py'
        converter.write_text('''
from pathlib import Path
import time
marker = Path('converter-active')
marker.mkdir()
try:
    time.sleep(0.05)
finally:
    marker.rmdir()
''')
        command = [sys.executable, str(HELPER), '--mapjson', sys.executable, str(converter)]
        processes = [subprocess.Popen(command, cwd=self.root, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True) for _ in range(2)]
        for process in processes:
            output, error = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 0, output + error)

    def test_make_parallelism_respects_explicit_jobs(self):
        with patch.dict(os.environ, {'MAKEFLAGS': ''}), patch.object(os, 'cpu_count', return_value=8):
            self.assertEqual(playtest.make_command(['modern']), ['make', '-j8', 'modern'])
            self.assertEqual(playtest.make_command(['-j3']), ['make', '-j3'])
        with patch.dict(os.environ, {'MAKEFLAGS': ' -j4 --jobserver-auth=3,4'}):
            self.assertEqual(playtest.make_command([]), ['make'])

    def test_map_converter_preserves_identical_headers_but_updates_changed_constants(self):
        folder = self.root / 'include/constants'
        folder.mkdir(parents=True)
        path = folder / 'layouts.h'
        path.write_text('#define LAYOUT_TEST 1\n')
        os.utime(path, ns=(1_000_000_000, 1_000_000_000))
        generated = self.root / 'data/maps/groups.inc'
        generated.parent.mkdir(parents=True)
        generated.write_text('unchanged map groups')
        os.utime(generated, ns=(1_000_000_000, 1_000_000_000))
        normalized = self.root / 'src/data/heal_locations.json'
        normalized.parent.mkdir(parents=True)
        normalized.write_text('{}')
        os.utime(normalized, ns=(1_000_000_000, 1_000_000_000))
        old_cwd = Path.cwd()
        try:
            os.chdir(self.root)
            for text in ('#define LAYOUT_TEST 1\n', '#define LAYOUT_TEST 2\n'):
                result = playtest.run_mapjson([sys.executable, '-c',
                    f'from pathlib import Path; Path("include/constants/layouts.h").write_text({text!r}); '
                    'Path("data/maps/groups.inc").write_text("unchanged map groups"); '
                    'Path("src/data/heal_locations.json").write_text("{}")'])
                self.assertEqual(result, 0)
                self.assertEqual(path.stat().st_mtime_ns == 1_000_000_000, text.endswith('1\n'))
                self.assertEqual(generated.stat().st_mtime_ns, 1_000_000_000)
                self.assertEqual(normalized.stat().st_mtime_ns, 1_000_000_000)
        finally:
            os.chdir(old_cwd)

    def test_cache_is_locked_and_must_be_outside_source(self):
        config = dict(self.config, cache_dir=str(self.root / 'cache'))
        with self.assertRaisesRegex(ValueError, 'outside'):
            with playtest.build_directory(config, []):
                pass
        config['cache_dir'] = str(Path(self.temp.name) / 'cache')
        with playtest.build_directory(config, []) as (project, cached):
            self.assertTrue(cached)
            with self.assertRaisesRegex(RuntimeError, 'in use'):
                with playtest.build_directory(config, []):
                    pass

    @unittest.skipIf(os.name != 'posix', 'POSIX executable fixture')
    def test_cached_launch_is_incremental_and_discards_demo_saves(self):
        (self.root / 'Makefile').write_text(
            'demo.gba: src/new_game.c\n\techo build >> builds.txt\n\ttouch demo.gba\n')
        emulator = Path(self.temp.name) / 'emulator'
        emulator.write_text('#!' + sys.executable + '\nfrom pathlib import Path\n'
                            'assert not Path("demo.sav").exists()\n'
                            'Path("demo.sav").write_text("disposable save")\n')
        emulator.chmod(0o755)
        config = dict(self.config, emulator=str(emulator),
                      cache_dir=str(Path(self.temp.name) / 'cache'))
        config_path = Path(self.temp.name) / 'config.json'
        def run():
            config_path.write_text(json.dumps(config))
            result = subprocess.run([sys.executable, str(HELPER), str(config_path)],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        run()
        cached_project, = (Path(self.temp.name) / 'cache').glob('*/project')
        self.assertFalse((cached_project / 'demo.sav').exists())
        run()
        self.assertEqual((cached_project / 'builds.txt').read_text().splitlines(), ['build'])
        config['name'] = 'OTHER'
        run()
        self.assertEqual(len((cached_project / 'builds.txt').read_text().splitlines()), 2)
        self.assertFalse((self.root / 'demo.gba').exists())

    def test_patch_starts_new_character_at_requested_tile(self):
        playtest.patch_project(self.root, self.config)
        new = (self.root / 'src/new_game.c').read_text()
        self.assertIn('playerGender = 1;', new)
        self.assertIn('_("DEMO")', new)
        self.assertIn('SPECIES_TREECKO, 25,', new)
        self.assertIn('MAP_NUM(MAP_ROUTE101), -1, -1, -1', new)
        self.assertIn('gSaveBlock1Ptr->pos.x = 3;', new)
        self.assertIn('gSaveBlock1Ptr->pos.y = 7;', new)
        self.assertLess(new.index('ResetFlags();'), new.index('/* Porymap'))
        self.assertLess(new.index('/* Porymap'), new.index('void OtherFunction'))
        self.assertIn('SetMainCallback2(CB2_NewGame)', (self.root / 'src/intro.c').read_text())
        self.assertNotIn('ExecuteTruckSequence', (self.root / 'src/overworld.c').read_text())

    def story_fixture(self):
        path = self.root / 'src/overworld.c'
        path.write_text('#include "global.h"\n' + path.read_text() + '''
void LoadStartingMap(void) {
    ClearTempFieldEventData();
    RunOnTransitionMapScript();
}
''')
        self.config.update(flags={'FLAG_RECEIVED_STARTER': False, 'FLAG_TEMP_TEST': True},
                           variables={'VAR_STORY': 2, 'VAR_TEMP_TEST': 65535})

    def test_empty_party_supports_classic_and_modern_party_storage(self):
        path = self.root / 'src/new_game.c'
        original = path.read_text()
        for modern in (False, True):
            with self.subTest(modern=modern):
                path.write_text(original.replace('ClearSav1();',
                    'ClearSav1(); gPartiesCount[B_TRAINER_PLAYER] = 0;') if modern else original)
                # Preserve the original startup sources for the next iteration.
                intro = (self.root / 'src/intro.c').read_text()
                overworld = (self.root / 'src/overworld.c').read_text()
                playtest.patch_project(self.root, dict(self.config, empty_party=True, species='', level=0))
                source = path.read_text()
                self.assertIn('ZeroPlayerPartyMons();', source)
                self.assertIn(('gPartiesCount[B_TRAINER_PLAYER]' if modern else 'gPlayerPartyCount') + ' = 0;', source)
                self.assertNotIn('CreateMon(', source)
                self.assertNotIn('CreateRandomMon(', source)
                (self.root / 'src/intro.c').write_text(intro)
                (self.root / 'src/overworld.c').write_text(overworld)

    def test_story_overrides_follow_defaults_and_precede_initial_map_scripts(self):
        self.story_fixture()
        playtest.patch_project(self.root, self.config)
        source = (self.root / 'src/new_game.c').read_text()
        self.assertIn('FlagClear(FLAG_RECEIVED_STARTER);', source)
        self.assertIn('FlagSet(FLAG_TEMP_TEST);', source)
        self.assertIn('VarSet(VAR_STORY, 2);', source)
        self.assertIn('VarSet(VAR_TEMP_TEST, 65535);', source)
        self.assertLess(source.index('ResetFlags();'), source.index('PorymapDemo_ApplyStoryState();'))
        self.assertLess(source.index('PorymapDemo_ApplyStoryState();'), source.index('WarpIntoMap();'))
        source = (self.root / 'src/overworld.c').read_text()
        self.assertLess(source.index('ClearTempFieldEventData();'), source.index('PorymapDemo_ApplyInitialStoryState();'))
        self.assertLess(source.index('PorymapDemo_ApplyInitialStoryState();'), source.index('RunOnTransitionMapScript();'))

    @unittest.skipUnless(shutil.which('cc'), 'Requires a C compiler')
    def test_story_overrides_restore_initial_temp_state_without_resetting_later_progress(self):
        self.story_fixture()
        playtest.patch_project(self.root, self.config)
        source = (self.root / 'src/new_game.c').read_text()
        helpers = source[source.index('/* Demo overrides follow new-game defaults'):]
        harness = self.root / 'story.c'
        harness.write_text('''
#include <assert.h>
typedef unsigned char bool8;
#define FALSE 0
#define TRUE 1
#define FLAG_RECEIVED_STARTER 1
#define FLAG_TEMP_TEST 2
#define VAR_STORY 1
#define VAR_TEMP_TEST 2
static unsigned flags[3], vars[3];
void FlagSet(unsigned id) { flags[id] = 1; }
void FlagClear(unsigned id) { flags[id] = 0; }
void VarSet(unsigned id, unsigned value) { vars[id] = value; }
''' + helpers + '''
int main(void) {
    flags[FLAG_RECEIVED_STARTER] = 1; /* new-game defaults */
    PorymapDemo_ApplyStoryState();
    assert(!flags[FLAG_RECEIVED_STARTER] && vars[VAR_STORY] == 2);
    flags[FLAG_TEMP_TEST] = vars[VAR_TEMP_TEST] = 0; /* initial map reset */
    PorymapDemo_ApplyInitialStoryState();
    assert(flags[FLAG_TEMP_TEST] && vars[VAR_TEMP_TEST] == 65535);
    flags[FLAG_RECEIVED_STARTER] = 1; /* receiving a starter progresses the story */
    vars[VAR_STORY] = 3;
    flags[FLAG_TEMP_TEST] = vars[VAR_TEMP_TEST] = 0; /* visit another map */
    PorymapDemo_ApplyInitialStoryState();
    assert(flags[FLAG_RECEIVED_STARTER] && vars[VAR_STORY] == 3);
    assert(!flags[FLAG_TEMP_TEST] && vars[VAR_TEMP_TEST] == 0);
    return 0;
}
''')
        binary = self.root / 'story-test'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Werror', str(harness), '-o', str(binary)],
                       check=True, capture_output=True, timeout=20)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=10)

    def test_invalid_story_overrides_fail_before_modifying_sources(self):
        original = (self.root / 'src/new_game.c').read_bytes()
        for override in (
                {'flags': []}, {'flags': {'FLAG_TEST': 1}}, {'flags': {'FLAG_X);Bad(': True}},
                {'variables': []}, {'variables': {'VAR_TEST': True}},
                {'variables': {'VAR_TEST': -1}}, {'variables': {'VAR_TEST': 65536}},
                {'variables': {'VAR_TEST': '2'}}, {'variables': {'VAR_X);Bad(': 0}},
                {'empty_party': 'true'}):
            with self.subTest(override=override), self.assertRaises(ValueError):
                playtest.patch_project(self.root, dict(self.config, **override))
            self.assertEqual((self.root / 'src/new_game.c').read_bytes(), original)

    def test_missing_temp_reset_rejects_story_overrides_without_partial_patch(self):
        original = (self.root / 'src/new_game.c').read_bytes()
        with self.assertRaisesRegex(ValueError, 'ClearTempFieldEventData'):
            playtest.patch_project(self.root, dict(self.config, flags={'FLAG_TEST': True}))
        self.assertEqual((self.root / 'src/new_game.c').read_bytes(), original)

    def test_cached_story_overrides_can_be_removed_and_party_restored(self):
        self.story_fixture()
        target = Path(self.temp.name) / 'snapshot'
        playtest.copy_project(self.root, target)
        playtest.patch_project(target, dict(self.config, empty_party=True), original=self.root)
        config = dict(self.config, flags={}, variables={}, empty_party=False)
        playtest.patch_project(target, config, original=self.root)
        self.assertNotIn('PorymapDemo_ApplyStoryState', (target / 'src/new_game.c').read_text())
        self.assertNotIn('PorymapDemo_ApplyInitialStoryState', (target / 'src/overworld.c').read_text())
        self.assertIn('gPlayerPartyCount = 1;', (target / 'src/new_game.c').read_text())

    @unittest.skipUnless(shutil.which('cc'), 'Requires a C compiler')
    def test_patched_sources_compile_with_headers_that_require_global_types(self):
        include = self.root / 'include'
        (include / 'constants').mkdir(parents=True)
        (include / 'global.h').write_text('''
#pragma once
typedef unsigned char u8;
#define FALSE 0
#define OT_ID_PLAYER_ID 0
#define B_TRAINER_PLAYER 0
#define _(text) {text}
struct Pokemon { int level; };
struct Save2 { u8 playerGender, playerName[8], optionsSound; };
struct Coords { short x, y; };
struct Save1 { unsigned money; struct Coords location, pos; };
extern struct Save2 *gSaveBlock2Ptr;
extern struct Save1 *gSaveBlock1Ptr;
extern struct Pokemon gPlayerParty[6], gParties[1][6];
extern u8 gPlayerPartyCount, gPartiesCount[1];
enum { SAVE_NORMAL };
int SetUpCopyrightScreen(void);
void SetSaveBlocksPointers(int);
void LoadGameSave(int);
void SetPokemonCryStereo(u8);
void Sav2_ClearSetDefault(void);
void SetMainCallback2(void (*callback)(void));
void ClearSav1(void);
void WarpToTruck(void);
void ResetFlags(void);
void SetMoney(unsigned *, unsigned);
void SetWarpDestination(signed char, signed char, signed char, signed char, signed char);
void WarpIntoMap(void);
''')
        (include / 'overworld.h').write_text('''
#pragma once
struct InitialPlayerAvatarState { u8 transitionFlags, direction; };
void CB2_NewGame(void);
''')
        (include / 'string_util.h').write_text('#pragma once\nvoid StringCopy(u8 *, const u8 *);\n')
        (include / 'constants/species.h').write_text('#define SPECIES_TREECKO 277\n')
        (include / 'constants/maps.h').write_text('''
#define MAP_ROUTE101 0x0101
#define MAP_GROUP(map) ((map) >> 8)
#define MAP_NUM(map) ((map) & 0xFF)
''')
        original = {path: path.read_text() for path in (self.root / 'src').glob('*.c')}
        for modern in (False, True):
            with self.subTest(modern_pokemon_api=modern):
                for path, source in original.items():
                    path.write_text(source)
                if modern:
                    (include / 'pokemon.h').write_text(
                        'void CreateRandomMon(struct Pokemon *, int, int);\n')
                    path = self.root / 'src/new_game.c'
                    path.write_text(path.read_text().replace(
                        'ClearSav1();', 'ClearSav1(); gPartiesCount[B_TRAINER_PLAYER] = 0;'))
                else:
                    (include / 'pokemon.h').write_text(
                        'void CreateMon(struct Pokemon *, int, int, int, int, int, int, int);\n')
                # Tall/wide maps must not pass their s16 coordinates through
                # SetWarpDestination's signed 8-bit parameters.
                self.config.update(x=139, y=130)
                playtest.patch_project(self.root, self.config)
                for name in ('intro', 'new_game'):
                    result = subprocess.run(
                        ['cc', '-std=c99', '-Werror', '-Wconversion', '-fsyntax-only', '-I', str(include),
                         str(self.root / 'src' / f'{name}.c')],
                        capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_expansion_conditional_callbacks_start_on_route105(self):
        path = self.root / 'src/overworld.c'
        path.write_text("""
void CB2_NewGame(void) {
    NewGameInitData();
    if (IS_FRLG)
        gFieldCallback = FieldCB_WarpExitFadeFromBlack;
    else
        gFieldCallback = ExecuteTruckSequence;
    gFieldCallback2 = NULL;
    DoMapLoadLoop(&gMain.state);
}
""")
        self.config['map'] = 'MAP_ROUTE105'
        playtest.patch_project(self.root, self.config)
        patched = path.read_text()
        self.assertEqual(patched.count('gFieldCallback = FieldCB_WarpExitFadeFromBlack;'), 2)
        self.assertNotIn('ExecuteTruckSequence', patched)
        self.assertIn('gFieldCallback2 = NULL;', patched)
        self.assertIn('MAP_NUM(MAP_ROUTE105), -1, -1, -1',
                      (self.root / 'src/new_game.c').read_text())

    @unittest.skipUnless(shutil.which('cc'), 'Requires a C compiler')
    def test_frlg_rival_name_does_not_read_unterminated_fresh_save(self):
        path = self.root / 'src/new_game.c'
        path.write_text('''
#include "global.h"
void NewGameInitData(void) {
#if IS_FRLG
    u8 rivalName[8];
    StringCopy(rivalName, gSaveBlock1Ptr->rivalName);
#endif
    ClearSav1();
#if IS_FRLG
    StringCopy(gSaveBlock1Ptr->rivalName, rivalName);
#endif
}
''')
        playtest.patch_project(self.root, self.config)
        source = path.read_text()
        start, _ = playtest.function_span(source, 'NewGameInitData')
        initialization = source[start + 1:source.index('/* Porymap disposable demo character. */')]
        # Execute the patched preservation code with a fresh zero-filled save.
        # The bounded copy fails if it reads a name without the game's EOS byte.
        harness = self.root / 'rival.c'
        harness.write_text('''
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
#define _(text) {text "\\xff"}
struct Save1 { u8 rivalName[8]; } save;
struct Save1 *gSaveBlock1Ptr = &save;
void StringCopy(u8 *dest, const u8 *src) {
    for (unsigned i = 0; i < 8; ++i) {
        dest[i] = src[i];
        if (src[i] == 0xff) return;
    }
    exit(42);
}
void ClearSav1(void) { memset(&save, 0, sizeof(save)); }
void initialize(void) {
''' + initialization + '''
}
int main(void) {
    initialize();
#if IS_FRLG
    return memcmp(save.rivalName, "RIVAL\\xff", 6) != 0;
#else
    return 0;
#endif
}
''')
        for frlg in (0, 1):
            with self.subTest(IS_FRLG=frlg):
                binary = self.root / 'rival-test'
                subprocess.run(['cc', '-std=c99', f'-DIS_FRLG={frlg}', str(harness),
                                '-o', str(binary)], check=True, capture_output=True, timeout=20)
                result = subprocess.run([str(binary)], capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 0, 'Rival copy read an unterminated name')

    def test_expansion_modern_pokemon_creation_and_party_storage(self):
        (self.root / 'include').mkdir()
        (self.root / 'include/pokemon.h').write_text(
            'void CreateRandomMon(struct Pokemon *mon, enum Species species, u8 level);\n')
        path = self.root / 'src/new_game.c'
        path.write_text(path.read_text().replace('ClearSav1();',
                                                'ClearSav1(); gPartiesCount[B_TRAINER_PLAYER] = 0;'))
        playtest.patch_project(self.root, self.config)
        patched = path.read_text()
        self.assertIn('CreateRandomMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 25);', patched)
        self.assertIn('gPartiesCount[B_TRAINER_PLAYER] = 1;', patched)
        self.assertNotIn('gPlayerParty', patched)
        self.assertNotIn('OT_ID_PLAYER_ID', patched)

    def test_missing_field_callback_does_not_partially_patch(self):
        original = (self.root / 'src/new_game.c').read_bytes()
        (self.root / 'src/overworld.c').write_text('void CB2_NewGame(void) { NewGameInitData(); }')
        with self.assertRaisesRegex(ValueError, 'no field callback assignment'):
            playtest.patch_project(self.root, self.config)
        self.assertEqual((self.root / 'src/new_game.c').read_bytes(), original)

    def test_unsupported_engine_does_not_partially_patch(self):
        original = (self.root / 'src/intro.c').read_bytes()
        (self.root / 'src/overworld.c').write_text('void UnsupportedEngine(void) {}')
        with self.assertRaisesRegex(ValueError, 'Unsupported project'):
            playtest.patch_project(self.root, self.config)
        self.assertEqual((self.root / 'src/intro.c').read_bytes(), original)

    def test_invalid_character_does_not_inject_c(self):
        self.config['name'] = '"; Bad();'
        with self.assertRaises(ValueError):
            playtest.patch_project(self.root, self.config)

    def test_expansion_build_targets_keep_release_goal(self):
        for target in playtest.BUILD_TARGETS:
            with self.subTest(target=target):
                config = dict(self.config, build_target=target, make_args=['release', 'LTO=0'])
                self.assertEqual(playtest.build_arguments(config),
                                 [f'BUILD={target}', 'release', 'LTO=0'])
        self.assertEqual(playtest.build_arguments(dict(self.config, make_args=['modern'])), ['modern'])

    def test_explicit_target_rejects_conflicting_game_arguments(self):
        for arg in ('emerald', 'firered', 'leafgreen', 'BUILD=emerald',
                    'GAME_VERSION=EMERALD', 'MAP_VERSION=emerald', 'BUILD_NAME=emerald'):
            with self.subTest(arg=arg), self.assertRaisesRegex(ValueError, 'game selectors'):
                playtest.build_arguments(dict(self.config, build_target='firered', make_args=[arg]))
        with self.assertRaisesRegex(ValueError, 'Invalid build target'):
            playtest.build_arguments(dict(self.config, build_target='unknown'))

    def test_expansion_target_requires_map_from_matching_region(self):
        maps = self.root / 'data/maps'
        for name, region in [('Route101', 'REGION_HOENN'), ('PalletTown_Frlg', 'REGION_KANTO')]:
            folder = maps / name
            folder.mkdir(parents=True)
            (folder / 'map.json').write_text(json.dumps(dict(id='MAP_' + name.upper(), region=region)))
        for target in playtest.BUILD_TARGETS:
            for map_id in ('MAP_ROUTE101', 'MAP_PALLETTOWN_FRLG'):
                config = dict(self.config, build_target=target, map=map_id)
                if (target == 'emerald') == (map_id == 'MAP_ROUTE101'):
                    playtest.validate_target_map(self.root, config)
                else:
                    with self.assertRaisesRegex(ValueError, 'not included'):
                        playtest.validate_target_map(self.root, config)

    def test_snapshot_excludes_real_save_and_rom(self):
        (self.root / 'normal.sav').write_text('private save')
        (self.root / 'normal.gba').write_text('normal rom')
        target = Path(self.temp.name) / 'snapshot'
        playtest.copy_project(self.root, target)
        self.assertTrue((target / 'src/new_game.c').exists())
        self.assertFalse((target / 'normal.sav').exists())
        self.assertFalse((target / 'normal.gba').exists())

    def test_snapshot_opts_out_of_history_check_without_changing_original(self):
        (self.root / '.git').mkdir()
        (self.root / '.git/HEAD').write_text('ref: refs/heads/master\n')
        target = Path(self.temp.name) / 'snapshot'
        playtest.copy_project(self.root, target)
        self.assertTrue((target / '.histignore').is_file())
        self.assertFalse((target / '.git').exists())
        self.assertFalse((self.root / '.histignore').exists())
        self.assertEqual((self.root / '.git/HEAD').read_text(), 'ref: refs/heads/master\n')

    def test_snapshot_preserves_multiboot_roms_used_as_source_assets(self):
        (self.root / 'data').mkdir()
        for name in ('mb_berry_fix.gba', 'mb_ereader.gba', 'mb_colosseum.gba'):
            (self.root / 'data' / name).write_bytes(b'embedded multiboot program')
        target = Path(self.temp.name) / 'snapshot'
        playtest.copy_project(self.root, target)
        for name in ('mb_berry_fix.gba', 'mb_ereader.gba', 'mb_colosseum.gba'):
            self.assertEqual((target / 'data' / name).read_bytes(), b'embedded multiboot program')

    @unittest.skipIf(os.name != 'posix', 'POSIX executable fixture')
    def test_symlink_cannot_modify_original(self):
        (self.root / 'src/link.c').symlink_to(self.root / 'src/new_game.c')
        with self.assertRaisesRegex(ValueError, 'symlinks'):
            playtest.copy_project(self.root, Path(self.temp.name) / 'snapshot')

    def run_helper(self, fail=False, target=''):
        # A real make invocation and executable emulate the boundary without a GBA toolchain.
        (self.root / 'Makefile').write_text(
            'all:\n\ttest -f .histignore\n\t' + ('false' if fail else
            ('test "$(BUILD)" = "' + target + '" && touch poke$(BUILD).gba' if target else 'touch demo.gba')) + '\n')
        record = Path(self.temp.name) / 'emulator-input.json'
        emulator = Path(self.temp.name) / 'fake emulator'
        emulator.write_text('#!' + sys.executable + '\nimport json, pathlib, sys\n'
                            + f'pathlib.Path({str(record)!r}).write_text(json.dumps(dict(rom=sys.argv[1], '
                            + 'source=pathlib.Path("src/new_game.c").read_text())))\n')
        emulator.chmod(0o755)
        self.config['emulator'] = str(emulator)
        self.config['build_target'] = target
        config_path = Path(self.temp.name) / 'config.json'
        config_path.write_text(json.dumps(self.config))
        result = subprocess.run([sys.executable, str(HELPER), str(config_path)],
                                capture_output=True, text=True, timeout=20)
        return result, record

    @unittest.skipIf(os.name != 'posix', 'POSIX executable fixture')
    def test_build_and_emulator_receive_disposable_rom(self):
        original = (self.root / 'src/new_game.c').read_bytes()
        result, record = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = json.loads(record.read_text())
        self.assertIn('MAP_NUM(MAP_ROUTE101), -1, -1, -1', data['source'])
        self.assertIn('gSaveBlock1Ptr->pos.x = 3;', data['source'])
        self.assertIn('gSaveBlock1Ptr->pos.y = 7;', data['source'])
        self.assertNotEqual(Path(data['rom']).parent, self.root)
        self.assertFalse(Path(data['rom']).exists(), 'temporary demo should be removed')
        self.assertEqual((self.root / 'src/new_game.c').read_bytes(), original)
        self.assertFalse((self.root / 'demo.gba').exists())

    @unittest.skipIf(os.name != 'posix', 'POSIX process groups')
    def test_cancel_stops_running_build(self):
        (self.root / 'Makefile').write_text('all:\n\tsleep 60\n')
        config_path = Path(self.temp.name) / 'config.json'
        config_path.write_text(json.dumps(self.config))
        process = subprocess.Popen([sys.executable, '-u', str(HELPER), str(config_path)],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            self.assertIn('Copying project', process.stdout.readline())
            self.assertIn('Building demo ROM', process.stdout.readline())
            # Once make emits its command, the helper owns the active child process.
            self.assertIn('sleep 60', process.stdout.readline())
            process.terminate()
            process.communicate(timeout=5)
            self.assertEqual(process.returncode, 130)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

    @unittest.skipIf(os.name != 'posix', 'POSIX executable fixture')
    def test_failed_build_does_not_launch_emulator(self):
        result, record = self.run_helper(fail=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Demo build failed', result.stderr)
        self.assertFalse(record.exists())

    @unittest.skipIf(os.name != 'posix', 'POSIX executable fixture')
    def test_all_expansion_targets_reach_make_and_emulator(self):
        for target in playtest.BUILD_TARGETS:
            with self.subTest(target=target):
                result, record = self.run_helper(target=target)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(Path(json.loads(record.read_text())['rom']).name, f'poke{target}.gba')


if __name__ == '__main__':
    unittest.main()

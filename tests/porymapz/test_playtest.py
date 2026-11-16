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

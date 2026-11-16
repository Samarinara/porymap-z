"""Playtest isolation, patch validation, build failure, and emulator handoff."""
import importlib.util
import json
import os
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
void CB2_InitCopyrightScreenAfterBootup(void) {
    if (!SetUpCopyrightScreen()) {
        SetSaveBlocksPointers(0);
        LoadGameSave(SAVE_NORMAL);
        SetPokemonCryStereo(gSaveBlock2Ptr->optionsSound);
    }
}
''')
        (self.root / 'src/new_game.c').write_text('''
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
        self.assertIn('MAP_NUM(MAP_ROUTE101), -1, 3, 7', new)
        self.assertLess(new.index('ResetFlags();'), new.index('/* Porymap'))
        self.assertLess(new.index('/* Porymap'), new.index('void OtherFunction'))
        self.assertIn('SetMainCallback2(CB2_NewGame)', (self.root / 'src/intro.c').read_text())
        self.assertNotIn('ExecuteTruckSequence', (self.root / 'src/overworld.c').read_text())

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
        self.assertIn('MAP_NUM(MAP_ROUTE105), -1, 3, 7',
                      (self.root / 'src/new_game.c').read_text())

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

    def test_snapshot_excludes_real_save_and_rom(self):
        (self.root / 'normal.sav').write_text('private save')
        (self.root / 'normal.gba').write_text('normal rom')
        target = Path(self.temp.name) / 'snapshot'
        playtest.copy_project(self.root, target)
        self.assertTrue((target / 'src/new_game.c').exists())
        self.assertFalse((target / 'normal.sav').exists())
        self.assertFalse((target / 'normal.gba').exists())

    @unittest.skipIf(os.name != 'posix', 'POSIX executable fixture')
    def test_symlink_cannot_modify_original(self):
        (self.root / 'src/link.c').symlink_to(self.root / 'src/new_game.c')
        with self.assertRaisesRegex(ValueError, 'symlinks'):
            playtest.copy_project(self.root, Path(self.temp.name) / 'snapshot')

    def run_helper(self, fail=False):
        # A real make invocation and executable emulate the boundary without a GBA toolchain.
        (self.root / 'Makefile').write_text('all:\n\t' + ('false' if fail else 'touch demo.gba') + '\n')
        record = Path(self.temp.name) / 'emulator-input.json'
        emulator = Path(self.temp.name) / 'fake emulator'
        emulator.write_text('#!' + sys.executable + '\nimport json, pathlib, sys\n'
                            + f'pathlib.Path({str(record)!r}).write_text(json.dumps(dict(rom=sys.argv[1], '
                            + 'source=pathlib.Path("src/new_game.c").read_text())))\n')
        emulator.chmod(0o755)
        self.config['emulator'] = str(emulator)
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
        self.assertIn('MAP_NUM(MAP_ROUTE101), -1, 3, 7', data['source'])
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


if __name__ == '__main__':
    unittest.main()

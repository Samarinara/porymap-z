#!/usr/bin/env python3
"""Build and run a disposable pret Emerald/FireRed/expansion playtest project."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile


def function_span(source, name):
    # Mask comments and literals so braces inside them do not affect nesting.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: ' ' * len(m[0]), source, flags=re.S)
    match = re.search(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', masked)
    if not match:
        raise ValueError(f'Unsupported project: missing function {name}')
    start = masked.index('{', match.start())
    depth = 1
    end = start + 1
    while end < len(masked) and depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    if depth:
        raise ValueError(f'Unterminated function {name}')
    return start, end - 1


def patch_project(root, config):
    root = Path(root)
    name = config['name']
    species = config['species']
    map_id = config['map']
    if not re.fullmatch(r'[A-Za-z0-9 ]{1,7}', name):
        raise ValueError('Use 1–7 letters, digits or spaces for the trainer name.')
    if not re.fullmatch(r'SPECIES_[A-Z0-9_]+', species):
        raise ValueError('Invalid Pokémon species constant.')
    if not re.fullmatch(r'MAP_[A-Z0-9_]+', map_id):
        raise ValueError('Invalid map constant.')
    if config['gender'] not in (0, 1) or not 1 <= config['level'] <= 100:
        raise ValueError('Invalid character configuration.')
    if not all(isinstance(config[k], int) and 0 <= config[k] <= 32767 for k in ('x', 'y')):
        raise ValueError('Invalid starting tile.')

    paths = {key: root / 'src' / f'{key}.c' for key in ('intro', 'new_game', 'overworld')}
    sources = {key: path.read_text() for key, path in paths.items()}
    # Fail before modifying anything when this engine has a different startup path.
    for key, func in (('intro', 'CB2_InitCopyrightScreenAfterBootup'),
                      ('new_game', 'NewGameInitData'), ('overworld', 'CB2_NewGame')):
        function_span(sources[key], func)
    intro = sources['intro']
    start, end = function_span(intro, 'CB2_InitCopyrightScreenAfterBootup')
    body = intro[start:end]
    anchor = 'SetPokemonCryStereo(gSaveBlock2Ptr->optionsSound);'
    if body.count(anchor) != 1:
        raise ValueError('Unsupported project: copyright initialization differs.')
    body = body.replace(anchor, anchor + '\n        Sav2_ClearSetDefault();\n        SetMainCallback2(CB2_NewGame);')
    sources['intro'] = '#include "overworld.h"\n' + intro[:start] + body + intro[end:]

    new_game = sources['new_game']
    _, end = function_span(new_game, 'NewGameInitData')
    pokemon_header = root / 'include' / 'pokemon.h'
    pokemon_api = pokemon_header.read_text() if pokemon_header.exists() else ''
    # Expansion's newer party storage and creation API coexist with older forks.
    modern_party = bool(re.search(r'\bgPartiesCount\s*\[\s*B_TRAINER_PLAYER\s*\]', new_game))
    party = 'gParties[B_TRAINER_PLAYER]' if modern_party else 'gPlayerParty'
    party_count = 'gPartiesCount[B_TRAINER_PLAYER]' if modern_party else 'gPlayerPartyCount'
    if re.search(r'\bvoid\s+CreateRandomMon\s*\(', pokemon_api):
        create_mon = f"CreateRandomMon(&{party}[0], {species}, {config['level']});"
    else:
        create_mon = (f"CreateMon(&{party}[0], {species}, {config['level']}, "
                      "32, FALSE, 0, OT_ID_PLAYER_ID, 0);")
    setup = f'''
    /* Porymap disposable demo character. */
    gSaveBlock2Ptr->playerGender = {config['gender']};
    StringCopy(gSaveBlock2Ptr->playerName, _("{name}"));
    {create_mon}
    {party_count} = 1;
    SetMoney(&gSaveBlock1Ptr->money, 3000);
    SetWarpDestination(MAP_GROUP({map_id}), MAP_NUM({map_id}), -1, {config['x']}, {config['y']});
    WarpIntoMap();
'''
    sources['new_game'] = ('#include "string_util.h"\n#include "constants/species.h"\n'
                           '#include "constants/maps.h"\n' + new_game[:end] + setup + new_game[end:])
    overworld = sources['overworld']
    start, end = function_span(overworld, 'CB2_NewGame')
    body = overworld[start:end]
    body, count = re.subn(r'gFieldCallback\s*=\s*\w+\s*;',
                         'gFieldCallback = FieldCB_WarpExitFadeFromBlack;', body)
    # Expansion selects the FRLG fade or Emerald truck callback in separate
    # branches. Replace every branch so none can run the truck intro.
    if count == 0:
        raise ValueError('Unsupported project: no field callback assignment found in CB2_NewGame.')
    sources['overworld'] = overworld[:start] + body + overworld[end:]
    for key, path in paths.items():
        path.write_text(sources[key])


def copy_project(source, target):
    source = Path(source).resolve()
    # Never follow a symlink back into the real project, including tool binaries.
    def ignore(directory, names):
        excluded = {'.git', '.porymap', '.idea', '__pycache__'}
        result = []
        for name in names:
            path = Path(directory) / name
            if path.is_symlink():
                raise ValueError(f'Playtest requires a project without symlinks: {path}')
            if name in excluded or path.suffix in {'.gba', '.elf', '.sav', '.ss0', '.ss1'}:
                result.append(name)
        return result
    shutil.copytree(source, target, ignore=ignore)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('config')
    args = parser.parse_args()
    config = json.loads(Path(args.config).read_text())
    child = None

    def stop(signum, frame):
        # Do not call Popen.wait() from a signal handler: it may interrupt
        # another wait() while its internal lock is held.
        raise SystemExit(130)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    with tempfile.TemporaryDirectory(prefix='porymap-demo-') as directory:
        try:
            project = Path(directory) / 'project'
            print('Copying project for an isolated demo build…', flush=True)
            copy_project(config['root'], project)
            patch_project(project, config)
            print('Building demo ROM (requires the project’s normal build toolchain)…', flush=True)
            child = subprocess.Popen(['make', '-j2', *config.get('make_args', [])], cwd=project,
                                     start_new_session=(os.name == 'posix'))
            if child.wait() != 0:
                raise RuntimeError('Demo build failed. See the build output above.')
            roms = sorted(project.glob('*.gba'))
            if len(roms) != 1:
                raise RuntimeError('Expected one .gba ROM in the project root after building.')
            print('Starting emulator. Close it to finish the demo.', flush=True)
            child = subprocess.Popen([config['emulator'], str(roms[0])], cwd=project,
                                     start_new_session=(os.name == 'posix'))
            if child.wait() != 0:
                raise RuntimeError('The emulator exited with an error.')
        finally:
            if child is not None and child.poll() is None:
                if os.name == 'posix':
                    os.killpg(child.pid, signal.SIGTERM)
                else:
                    child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    if os.name == 'posix':
                        os.killpg(child.pid, signal.SIGKILL)
                    else:
                        child.kill()
                    child.wait()



if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'Playtest error: {error}', file=sys.stderr, flush=True)
        sys.exit(1)

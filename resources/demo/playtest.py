#!/usr/bin/env python3
"""Build and run a disposable pret Emerald/FireRed/expansion playtest project."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import signal
import subprocess
import sys
import tempfile


BUILD_TARGETS = ('emerald', 'firered', 'leafgreen')


def write_changed(path, text):
    path = Path(path)
    if not path.exists() or path.read_text() != text:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


def demo_maps(root, config):
    """Keep numeric map/layout IDs stable; only omit unselected map payloads."""
    scope = config.get('scope', 'full')
    if scope not in ('current', 'nearby', 'reachable', 'full'):
        raise ValueError('Invalid demo scope.')
    if scope == 'full':
        return None
    maps = {data['id']: (path, data) for path in (Path(root) / 'data/maps').glob('*/map.json')
            for data in [json.loads(path.read_text())]}
    if config['map'] not in maps:
        raise ValueError('Demo scope requires a map.json for the starting map. Try Build whole game.')
    selected = {config['map']}
    frontier = set(selected)
    while frontier and scope != 'current':
        destinations = set()
        for key in frontier:
            data = maps[key][1]
            destinations.update(c['map'] for c in data.get('connections') or [])
            destinations.update(w['dest_map'] for w in data.get('warp_events', []))
        frontier = (destinations & maps.keys()) - selected
        selected.update(frontier)
        if scope != 'reachable':
            break
    return maps, selected


def trim_layouts(root):
    """Called by make after mapjson, preserving its constants and table indices."""
    root = Path(root)
    manifest = json.loads((root / '.porymap-demo-layouts.json').read_text())
    path = root / 'data/layouts/layouts.inc'
    source = path.read_text()
    # Standard pret mapjson emits consecutive Border, Blockdata and Layout
    # definitions per layout. Keep every symbol for engine/script references.
    def replace(match):
        name = match['name']
        fallback = manifest['aliases'].get(name)
        if fallback is None:
            return match[0]
        if not re.search(r'(?m)^' + re.escape(fallback) + r'::', source):
            raise ValueError('Starting layout is not in this build. Try Build whole game.')
        return ''.join(
            f'\t.global {name}{suffix}\n\t.set {name}{suffix}, {fallback}{suffix}\n'
            for suffix in ('_Border', '_Blockdata', ''))
    # Replace in a single pass: subsequent matches must never consume aliases
    # already emitted for preceding or following layouts.
    source = re.sub(r'(?m)^(?P<name>\w+)_Border::.*?(?=^\w+_Border::|\Z)',
                    replace, source, flags=re.S)
    write_changed(path, source)


@contextmanager
def file_lock(path, blocking=False):
    with Path(path).open('a+b') as lock:
        try:
            if os.name == 'posix':
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX | (0 if blocking else fcntl.LOCK_NB))
            else:
                import msvcrt
                lock.write(b'0')
                lock.flush()
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK if blocking else msvcrt.LK_NBLCK, 1)
        except OSError as error:
            if not blocking:
                raise BlockingIOError('Demo cache is locked') from error
            raise
        yield


def run_mapjson(command):
    # Parallel make may run the same multi-output rule twice. Serialize writers
    # so snapshots cannot observe another converter's partially written header.
    with file_lock('.porymap-demo-mapjson.lock', blocking=True):
        return mapjson_outputs(command)


def mapjson_outputs(command):
    # Mapjson rewrites constants even when only map geometry/connections changed.
    # Preserve identical headers' timestamps to avoid rebuilding the whole engine.
    outputs = [Path('include/constants') / name for name in
               ('layouts.h', 'map_groups.h', 'map_event_ids.h')]
    outputs.append(Path('src/data/map_group_count.h'))
    # Expansion also normalizes this source JSON while generating map groups.
    outputs.append(Path('src/data/heal_locations.json'))
    outputs.extend(Path('data/maps') / name for name in
                   ('headers.inc', 'groups.inc', 'connections.inc', 'events.inc'))
    outputs.extend(Path('data/layouts') / name for name in ('layouts.inc', 'layouts_table.inc'))
    previous = {path: (path.read_bytes(), path.stat()) for path in outputs if path.exists()}
    result = subprocess.run(command)
    if result.returncode == 0:
        if len(command) > 1 and command[1] == 'layouts' and Path('.porymap-demo-layouts.json').exists():
            trim_layouts('.')
        for path, (content, stat) in previous.items():
            if path.exists() and path.read_bytes() == content:
                os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns))
    return result.returncode


def scope_project(root, original, config, selection):
    root, original = Path(root), Path(original)
    rules_path = original / 'map_data_rules.mk'
    rules = rules_path.read_text() if rules_path.exists() else ''
    helper = shlex.quote(sys.executable) + ' .porymap-demo.py'
    if rules:
        rules = re.sub(r'\$\(MAPJSON\)(?= (?:layouts|groups|event_constants)\b)',
                       lambda m: helper + ' --mapjson ' + m[0], rules)
        write_changed(root / '.porymap-demo.py', Path(__file__).read_text())
    if selection is None:
        manifest_path = root / '.porymap-demo-layouts.json'
        if manifest_path.exists():
            (root / 'data/layouts/layouts.inc').unlink(missing_ok=True)
            manifest_path.unlink()
        if rules_path.exists():
            write_changed(root / 'map_data_rules.mk', rules)
        for path in (original / 'data/maps').glob('*/map.json'):
            write_changed(root / path.relative_to(original), path.read_text())
        return
    maps, selected = selection
    layouts = json.loads((original / 'data/layouts/layouts.json').read_text())['layouts']
    keep = {maps[key][1]['layout'] for key in selected}
    by_id = {layout['id']: layout for layout in layouts}
    # Map scripts and engine features can switch layouts without changing maps
    # (gym puzzles, tides, secret bases). Retain those shared dependencies too.
    references = set()
    for directory in ('src', 'include', 'data'):
        for path in (original / directory).rglob('*'):
            relative = path.relative_to(original).as_posix()
            if (path.suffix not in {'.c', '.h', '.s', '.inc', '.pory'}
                    or relative.startswith('data/layouts/')
                    or relative == 'include/constants/layouts.h'
                    or (relative.startswith('data/maps/') and
                        path.name in {'header.inc', 'events.inc', 'connections.inc'})):
                continue
            references.update(re.findall(r'\bLAYOUT_[A-Z0-9_]+\b', path.read_text()))
    keep.update(references & by_id.keys())
    fallback = by_id[maps[config['map']][1]['layout']]['name']
    aliases = {layout['name']: fallback for layout in layouts if layout['id'] not in keep}
    manifest = json.dumps(dict(aliases=aliases), sort_keys=True)
    manifest_path = root / '.porymap-demo-layouts.json'
    changed = not manifest_path.exists() or manifest_path.read_text() != manifest
    write_changed(manifest_path, manifest)
    # The mapjson wrapper ensures regenerating layouts never restores omitted
    # payloads. Shared engine, scripts, tilesets and battle assets stay available.
    pattern = r'(?m)^(\t[^\n]*\$\(MAPJSON\) layouts[^\n]*)$'
    if len(re.findall(pattern, rules)) != 1:
        raise ValueError('Unsupported map build rules. Try Build whole game.')
    write_changed(root / 'map_data_rules.mk', rules)
    if changed:
        # The manifest is not a prerequisite in the project's original rules.
        (root / 'data/layouts/layouts.inc').unlink(missing_ok=True)
    for key, (path, data) in maps.items():
        # Connection loading reads neighboring layouts before a warp happens.
        # Strip edges across the boundary; keep warp indices and event IDs intact.
        if data.get('connections'):
            data['connections'] = [c for c in data['connections']
                                   if key in selected and c['map'] in selected]
        write_changed(root / path.relative_to(original), json.dumps(data, indent=2) + '\n')
    print(f'Demo scope: {len(selected)} of {len(maps)} maps; '
          f'{len(keep)} layouts. Shared engine and script dependencies are retained.', flush=True)


def build_arguments(config):
    args = list(config.get('make_args', []))
    target = config.get('build_target', '')
    if not target:
        return args
    if target not in BUILD_TARGETS:
        raise ValueError('Invalid build target. Choose Emerald, FireRed, or LeafGreen.')
    # BUILD also works with goals such as release. Expansion compares the entire
    # MAKECMDGOALS string, so appending "firered" to other goals is insufficient.
    for arg in args:
        if arg in BUILD_TARGETS or re.match(
                r'^(BUILD|GAME_VERSION|BUILD_NAME|MAP_VERSION|TITLE|GAME_CODE)\s*[:?+]?=', arg):
            raise ValueError('Remove game selectors from Build arguments when choosing a build target.')
    return [f'BUILD={target}', *args]


def validate_target_map(root, config):
    target = config.get('build_target', '')
    if not target:
        return
    region = 'REGION_HOENN' if target == 'emerald' else 'REGION_KANTO'
    for path in (Path(root) / 'data/maps').glob('*/map.json'):
        data = json.loads(path.read_text())
        if data.get('id') == config['map']:
            if data.get('region', region) != region:
                raise ValueError(f"{config['map']} is not included in the {target} build. "
                                 'Choose a map from the target game’s region.')
            return


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


def add_game_headers(source, headers):
    # Game headers rely on the types and macros initialized by global.h.
    anchor = re.search(r'^\s*#\s*include\s*"global\.h"[^\n]*(?:\n|$)', source, re.M)
    if not anchor:
        raise ValueError('Unsupported project: missing global.h include.')
    added = ''.join(f'#include "{header}"\n' for header in headers
                    if not re.search(r'^\s*#\s*include\s*"' + re.escape(header) + r'"', source, re.M))
    return source[:anchor.end()] + added + source[anchor.end():]


def patch_project(root, config, original=None, selection=None):
    root = Path(root)
    original = Path(original) if original is not None else root
    name = config['name']
    empty_party = config.get('empty_party', False)
    if type(empty_party) is not bool:
        raise ValueError('Invalid empty party setting.')
    species = config.get('species', '')
    flags = config.get('flags', {})
    variables = config.get('variables', {})
    if (not isinstance(flags, dict) or any(
            not re.fullmatch(r'FLAG_[A-Z0-9_]+', key) or type(value) is not bool
            for key, value in flags.items())):
        raise ValueError('Starting flags must map FLAG_ constants to true (set) or false (clear).')
    if (not isinstance(variables, dict) or any(
            not re.fullmatch(r'VAR_[A-Z0-9_]+', key) or type(value) is not int or not 0 <= value <= 65535
            for key, value in variables.items())):
        raise ValueError('Starting variables must map VAR_ constants to integers from 0 to 65535.')
    map_id = config['map']
    if not re.fullmatch(r'[A-Za-z0-9 ]{1,7}', name):
        raise ValueError('Use 1–7 letters, digits or spaces for the trainer name.')
    if not empty_party and not re.fullmatch(r'SPECIES_[A-Z0-9_]+', species):
        raise ValueError('Invalid Pokémon species constant.')
    if not re.fullmatch(r'MAP_[A-Z0-9_]+', map_id):
        raise ValueError('Invalid map constant.')
    if config['gender'] not in (0, 1) or (not empty_party and not 1 <= config['level'] <= 100):
        raise ValueError('Invalid character configuration.')
    if not all(isinstance(config[k], int) and 0 <= config[k] <= 32767 for k in ('x', 'y')):
        raise ValueError('Invalid starting tile.')

    paths = {key: root / 'src' / f'{key}.c' for key in ('intro', 'new_game', 'overworld')}
    sources = {key: (original / 'src' / f'{key}.c').read_text() for key in paths}
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
    sources['intro'] = add_game_headers(intro[:start] + body + intro[end:], ['overworld.h'])

    new_game = sources['new_game']
    start, _ = function_span(new_game, 'NewGameInitData')
    # _() expands to a brace initializer after the game's text preprocessing.
    # It must initialize an array rather than appear as a function argument.
    trainer_name = f'\n    static const u8 sPorymapDemoTrainerName[] = _("{name}");\n'
    new_game = new_game[:start + 1] + trainer_name + new_game[start + 1:]
    # FRLG normally gets a terminated rival name from the naming intro. A fresh
    # demo save contains zeros instead of EOS, so preserving that name with
    # StringCopy would overrun the local name buffer and corrupt the stack.
    new_game = re.sub(
        r'\bStringCopy\s*\(\s*(\w+)\s*,\s*gSaveBlock1Ptr->rivalName\s*\)\s*;',
        lambda match: ('{\n        static const u8 sPorymapDemoRivalName[] = _("RIVAL");\n'
                       f'        StringCopy({match[1]}, sPorymapDemoRivalName);\n    }}'),
        new_game)
    _, end = function_span(new_game, 'NewGameInitData')
    pokemon_header = root / 'include' / 'pokemon.h'
    pokemon_api = pokemon_header.read_text() if pokemon_header.exists() else ''
    # Expansion's newer party storage and creation API coexist with older forks.
    modern_party = bool(re.search(r'\bgPartiesCount\s*\[\s*B_TRAINER_PLAYER\s*\]', new_game))
    party = 'gParties[B_TRAINER_PLAYER]' if modern_party else 'gPlayerParty'
    party_count = 'gPartiesCount[B_TRAINER_PLAYER]' if modern_party else 'gPlayerPartyCount'
    if empty_party:
        create_mon = 'ZeroPlayerPartyMons();'
    elif re.search(r'\bvoid\s+CreateRandomMon\s*\(', pokemon_api):
        create_mon = f"CreateRandomMon(&{party}[0], {species}, {config['level']});"
    else:
        create_mon = (f"CreateMon(&{party}[0], {species}, {config['level']}, "
                      "32, FALSE, 0, OT_ID_PLAYER_ID, 0);")
    setup = f'''
    /* Porymap disposable demo character. */
    gSaveBlock2Ptr->playerGender = {config['gender']};
    StringCopy(gSaveBlock2Ptr->playerName, sPorymapDemoTrainerName);
    {create_mon}
    {party_count} = {0 if empty_party else 1};
    SetMoney(&gSaveBlock1Ptr->money, 3000);
    {'PorymapDemo_ApplyStoryState();' if flags or variables else ''}
    SetWarpDestination(MAP_GROUP({map_id}), MAP_NUM({map_id}), -1, -1, -1);
    WarpIntoMap();
    /* The warp API narrows coordinates to s8; the save fields are s16. */
    gSaveBlock1Ptr->location.x = {config['x']};
    gSaveBlock1Ptr->location.y = {config['y']};
    gSaveBlock1Ptr->pos.x = {config['x']};
    gSaveBlock1Ptr->pos.y = {config['y']};
'''
    sources['new_game'] = add_game_headers(new_game[:end] + setup + new_game[end:],
                                          ['string_util.h', 'constants/species.h', 'constants/maps.h'])
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
    story_header = None
    if flags or variables:
        calls = ''.join(f"    {'FlagSet' if value else 'FlagClear'}({key});\n"
                        for key, value in sorted(flags.items()))
        calls += ''.join(f'    VarSet({key}, {value});\n' for key, value in sorted(variables.items()))
        sources['new_game'] = add_game_headers(sources['new_game'],
            ['event_data.h', 'constants/flags.h', 'constants/vars.h', 'porymap_playtest_state.h'])
        sources['new_game'] += '''
/* Demo overrides follow new-game defaults, before loading the starting map. */
void PorymapDemo_ApplyStoryState(void)
{
''' + calls + '''}

/* Initial map loading clears temporary flags/vars. Restore them once before
 * map scripts run, then let normal gameplay change the state freely. */
void PorymapDemo_ApplyInitialStoryState(void)
{
    static bool8 applied = FALSE;
    if (!applied)
    {
        applied = TRUE;
        PorymapDemo_ApplyStoryState();
    }
}
'''
        overworld, count = re.subn(r'\bClearTempFieldEventData\s*\(\s*\)\s*;',
            'ClearTempFieldEventData();\n    PorymapDemo_ApplyInitialStoryState();', sources['overworld'])
        if count == 0:
            raise ValueError('Unsupported initial map setup for story overrides: '
                             'missing ClearTempFieldEventData call.')
        sources['overworld'] = add_game_headers(overworld, ['porymap_playtest_state.h'])
        story_header = ('/* Generated only in the isolated Porymap demo. */\n#pragma once\n'
                        'void PorymapDemo_ApplyStoryState(void);\n'
                        'void PorymapDemo_ApplyInitialStoryState(void);\n')
    if selection is not None:
        # Gate every warp path, including script, escape rope and dynamic warps.
        # Reuse the current position when a script asks to leave the demo area.
        overworld = sources['overworld']
        if 'sWarpDestination' not in overworld:
            raise ValueError('Unsupported warp implementation. Try Build whole game.')
        start, _ = function_span(overworld, 'WarpIntoMap')
        allowed = ' ||\n        '.join(
            f'(sWarpDestination.mapGroup == MAP_GROUP({key}) && '
            f'sWarpDestination.mapNum == MAP_NUM({key}))' for key in sorted(selection[1]))
        guard = f'''
    /* Keep scripted and dynamic warps inside the selected demo area. */
    if (!({allowed}))
    {{
        sWarpDestination = gSaveBlock1Ptr->location;
        sWarpDestination.warpId = -1;
        sWarpDestination.x = gSaveBlock1Ptr->pos.x;
        sWarpDestination.y = gSaveBlock1Ptr->pos.y;
    }}
'''
        sources['overworld'] = add_game_headers(
            overworld[:start + 1] + guard + overworld[start + 1:], ['constants/maps.h'])
    for key, path in paths.items():
        write_changed(path, sources[key])
    if story_header is not None:
        write_changed(root / 'include/porymap_playtest_state.h', story_header)


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
            # Only root-level ROMs are build outputs. Nested .gba files include
            # required multiboot programs consumed by the game's assembler.
            if name in excluded or (Path(directory) == source and
                                    path.suffix in {'.gba', '.elf', '.sav', '.ss0', '.ss1'}):
                result.append(name)
        return result
    shutil.copytree(source, target, ignore=ignore)
    # Expansion requires git history or this opt-out marker. The disposable
    # snapshot intentionally has no .git; leave the original project untouched.
    (Path(target) / '.histignore').touch()


@contextmanager
def build_directory(config, make_args):
    cache = config.get('cache_dir')
    if not cache:
        with tempfile.TemporaryDirectory(prefix='porymap-demo-') as directory:
            yield Path(directory) / 'project', False
        return
    source = Path(config['root']).resolve()
    cache = Path(cache).resolve()
    if cache == source or source in cache.parents:
        raise ValueError('The demo cache must be outside the game project.')
    key = hashlib.sha256((str(source) + json.dumps(make_args) +
                          Path(__file__).read_text()).encode()).hexdigest()[:24]
    directory = cache / key
    directory.mkdir(parents=True, exist_ok=True)
    try:
        with file_lock(directory / 'lock'):
            # Holding this through emulation protects the ROM and save files.
            yield directory / 'project', True
    except BlockingIOError:
        raise RuntimeError('This demo cache is in use. Close the other demo and try again.')


def sync_project(source, target):
    """Refresh changed source files without overwriting demo build products."""
    source, target = Path(source).resolve(), Path(target)
    manifest_path = target.parent / 'source-files.json'
    previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    current = {}
    derived = {'src/intro.c', 'src/new_game.c', 'src/overworld.c', 'map_data_rules.mk'}
    generated = {'data/layouts/layouts.inc', 'data/layouts/layouts_table.inc',
                 'data/maps/headers.inc', 'data/maps/events.inc', 'data/maps/connections.inc',
                 'data/maps/groups.inc', 'include/constants/layouts.h',
                 'include/constants/map_groups.h', 'include/constants/map_event_ids.h',
                 'src/data/map_group_count.h', '.map_version'}
    for directory, folders, files in os.walk(source):
        directory = Path(directory)
        for name in folders + files:
            if (directory / name).is_symlink():
                raise ValueError(f'Playtest requires a project without symlinks: {directory / name}')
        folders[:] = [name for name in folders if name not in
                      {'.git', '.porymap', '.idea', '__pycache__'}]
        # Seed existing engine objects once. Later source-project builds must
        # never overwrite objects compiled against our patched startup files.
        if directory == source and previous:
            folders[:] = [name for name in folders if name != 'build']
        for name in files:
            path = directory / name
            if directory == source and path.suffix in {'.gba', '.elf', '.sav', '.ss0', '.ss1'}:
                continue
            relative = path.relative_to(source).as_posix()
            if relative in generated or (relative.startswith('data/maps/') and
                                         name in {'header.inc', 'events.inc', 'connections.inc'}):
                # Seed constants alongside existing engine objects once. The
                # map converter preserves their times if the IDs are unchanged.
                # Never overwrite demo-generated constants on subsequent runs.
                if not previous and relative in generated and path.suffix == '.h':
                    destination = target / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, destination)
                continue
            stat = path.stat()
            current[relative] = [stat.st_mtime_ns, stat.st_size, stat.st_mode]
            destination = target / relative
            if relative in derived or (relative.startswith('data/maps/') and name == 'map.json'):
                # These are rewritten from the original source by the adapters.
                continue
            if current[relative] != previous.get(relative) or not destination.exists():
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, destination)
    for relative in previous.keys() - current.keys():
        if relative.startswith('build/'):
            continue
        (target / relative).unlink(missing_ok=True)
    (target / 'src').mkdir(parents=True, exist_ok=True)
    (target / '.histignore').touch(exist_ok=True)
    write_changed(manifest_path, json.dumps(current, sort_keys=True))


def make_command(make_args):
    # Respect explicit job counts/jobserver settings; otherwise use every CPU.
    explicit = any(re.match(r'^(-j|--jobs|--jobserver)', arg) for arg in make_args)
    inherited = re.search(r'(^|\s)(-?j\d*|--jobs\b|--jobserver)', os.environ.get('MAKEFLAGS', ''))
    return ['make', *([] if explicit or inherited else [f'-j{os.cpu_count() or 1}']), *make_args]


def main():
    if len(sys.argv) > 2 and sys.argv[1] == '--mapjson':
        sys.exit(run_mapjson(sys.argv[2:]))
    if len(sys.argv) == 3 and sys.argv[1] == '--trim-layouts':
        trim_layouts(sys.argv[2])
        return
    parser = argparse.ArgumentParser()
    parser.add_argument('config')
    args = parser.parse_args()
    config = json.loads(Path(args.config).read_text())
    make_args = build_arguments(config)
    validate_target_map(config['root'], config)
    selection = demo_maps(config['root'], config)
    child = None

    def stop(signum, frame):
        # Do not call Popen.wait() from a signal handler: it may interrupt
        # another wait() while its internal lock is held.
        raise SystemExit(130)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    with build_directory(config, make_args) as (project, cached):
        try:
            if cached:
                print('Updating isolated demo cache (unchanged build files are reused)…', flush=True)
                sync_project(config['root'], project)
            else:
                print('Copying project for an isolated demo build…', flush=True)
                copy_project(config['root'], project)
            scope_project(project, config['root'], config, selection)
            patch_project(project, config, original=config['root'], selection=selection)
            # Every launch starts a fresh character, even when reusing a ROM.
            for save in project.glob('*.sav'):
                save.unlink()
            print('Building demo ROM (requires the project’s normal build toolchain)…', flush=True)
            child = subprocess.Popen(make_command(make_args), cwd=project,
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
            if cached:
                for pattern in ('*.sav', '*.ss[0-9]'):
                    for save in project.glob(pattern):
                        save.unlink(missing_ok=True)



if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'Playtest error: {error}', file=sys.stderr, flush=True)
        sys.exit(1)

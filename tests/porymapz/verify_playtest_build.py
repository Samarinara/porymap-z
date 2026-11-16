#!/usr/bin/env python3
"""Build a real Play ROM through the launcher, without opening an emulator.

Requires an existing game checkout and its normal build toolchain on PATH.
The emulator stand-in validates and exports the ROM before temporary cleanup.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HELPER = Path(__file__).resolve().parents[2] / 'resources/demo/playtest.py'


def source_hashes(project):
    return {name: hashlib.sha256((project / 'src' / f'{name}.c').read_bytes()).hexdigest()
            for name in ('intro', 'new_game', 'overworld')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('project', type=Path)
    parser.add_argument('--artifact-dir', type=Path, required=True)
    parser.add_argument('--make-arg', action='append', default=[])
    parser.add_argument('--map', default='MAP_ROUTE105')
    parser.add_argument('--species', default='SPECIES_TREECKO')
    parser.add_argument('--name', default='DEMO')
    parser.add_argument('--gender', type=int, choices=(0, 1), default=0)
    parser.add_argument('--level', type=int, default=5)
    parser.add_argument('--x', type=int, default=3)
    parser.add_argument('--y', type=int, default=7)
    args = parser.parse_args()
    project = args.project.resolve()
    artifacts = args.artifact_dir.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    original = source_hashes(project)
    with tempfile.TemporaryDirectory(prefix='porymap-play-build-check-') as directory:
        temp = Path(directory)
        emulator = temp / 'verify-rom'
        emulator.write_text(f'''#!{sys.executable}
import hashlib
import json
from pathlib import Path
import shutil
import sys
rom = Path(sys.argv[1])
data = rom.read_bytes()
if len(data) < 0xC0 or data[0xB2] != 0x96:
    raise SystemExit('Invalid GBA ROM header')
if data[0xBD] != ((-sum(data[0xA0:0xBD]) - 0x19) & 0xFF):
    raise SystemExit('Invalid GBA ROM header checksum')
artifacts = Path({str(artifacts)!r})
shutil.copy2(rom, artifacts / rom.name)
for suffix in ('.elf', '.map'):
    companion = rom.with_suffix(suffix)
    if companion.exists():
        shutil.copy2(companion, artifacts / companion.name)
(artifacts / 'rom.json').write_text(json.dumps(dict(
    filename=rom.name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()), indent=2) + '\\n')
print('Verified GBA ROM header and exported build artifacts.', flush=True)
''')
        emulator.chmod(0o755)
        config = dict(root=str(project), emulator=str(emulator), make_args=args.make_arg,
                      map=args.map, species=args.species, name=args.name, gender=args.gender,
                      level=args.level, x=args.x, y=args.y)
        config_path = temp / 'config.json'
        config_path.write_text(json.dumps(config))
        try:
            subprocess.run([sys.executable, '-B', str(HELPER), str(config_path)], check=True)
        finally:
            if source_hashes(project) != original:
                raise RuntimeError('The Play build changed the original game sources.')
        config.pop('emulator')
        config['source_hashes'] = original
        (artifacts / 'configuration.json').write_text(json.dumps(config, indent=2) + '\n')
        print(f'Play build passed; original game sources unchanged. Artifacts: {artifacts}')


if __name__ == '__main__':
    main()

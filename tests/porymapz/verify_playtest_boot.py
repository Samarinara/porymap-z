#!/usr/bin/env python3
"""Verify an exported Play ROM reaches its selected, visible map in mGBA."""
import argparse
import json
from pathlib import Path
import subprocess


def verify_boot(artifacts, checker, nm='arm-none-eabi-nm', frames=900):
    artifacts = Path(artifacts).resolve()
    checker = Path(checker).resolve()
    config = json.loads((artifacts / 'configuration.json').read_text())
    rom_info = json.loads((artifacts / 'rom.json').read_text())
    rom = artifacts / rom_info['filename']
    elf = rom.with_suffix('.elf')
    output = subprocess.check_output([nm, str(elf)], text=True)
    symbols = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    names = ('gMain', 'gSaveBlock1Ptr', 'CB1_Overworld', 'CB2_Overworld', config['map'])
    missing = [name for name in names if name not in symbols]
    if missing:
        raise RuntimeError('Missing ELF symbols for boot verification: ' + ', '.join(missing))
    command = [str(checker), str(rom), *(hex(symbols[name]) for name in names),
               str(config['x']), str(config['y']), str(frames)]
    if 'empty_party' in config:
        party_count = symbols.get('gPartiesCount', symbols.get('gPlayerPartyCount'))
        if party_count is None:
            raise RuntimeError('Missing player party count symbol for boot verification.')
        command.extend((hex(party_count), '0' if config['empty_party'] else '1'))
    result = subprocess.run(command,
        capture_output=True, text=True, timeout=60)
    (artifacts / 'boot.json').write_text(result.stdout)
    (artifacts / 'boot-stderr.log').write_text(result.stderr)
    result.check_returncode()
    state = json.loads(result.stdout)
    if not state.get('passed'):
        raise RuntimeError('The demo did not reach its requested visible overworld.')
    print(f"mGBA boot passed: map ({state['map_group']}, {state['map_number']}), "
          f"tile ({state['x']}, {state['y']}), {state['overworld_frames']} overworld frames.")
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', type=Path)
    parser.add_argument('--checker', type=Path, required=True)
    parser.add_argument('--nm', default='arm-none-eabi-nm')
    parser.add_argument('--frames', type=int, default=900)
    args = parser.parse_args()
    verify_boot(args.artifacts, args.checker, args.nm, args.frames)


if __name__ == '__main__':
    main()

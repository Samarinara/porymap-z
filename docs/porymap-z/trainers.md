# Trainer tools

A trainer is spread across several files: its ID, its data, its party, and the script that starts the battle. The Events tab creates and checks those files together. Party and battle editing stay in the [Battle tab](battles.md).

## Adding a trainer

Open the arrow beside **New Object** on the Events tab and choose **New Trainer...**. To turn an existing object into a trainer, select it and choose **Make Trainer...** in its panel.

Fill in the trainer's name, class, picture, music, battle type, map sprite, sight radius, and dialogue. **Create Trainer** then:

- Adds the ID and updates the trainer count.
- Adds the trainer data and a placeholder party, with one level 5 Pokémon (two for a double battle).
- Adds the battle script and text to the map's script file, as assembly or Poryscript to match that file.
- Places the object on the map, or updates the selected object.

The source files are written immediately and are not covered by Undo. The map event is saved with the map as usual.

## Object panel

Selecting an object shows which trainer its script battles, following `goto` and `call` into shared scripts. It also warns about a trainer type with no battle, a sight radius of 0, and problems with the linked trainer. **Edit Party** opens the trainer in the Battle tab.

## Checking and repairing files

Choose **Check Trainer Files...** from the same menu, or **Check Files...** in the object panel. Select a problem to preview its repair, tick the repairs you want, and choose **Repair Selected**. Nothing is written until then.

| Area | Repaired automatically | Reported for you to fix |
| --- | --- | --- |
| IDs | Missing, repeated, or reused IDs, and a wrong trainer count | One name defined with two values, IDs past `MAX_TRAINERS_COUNT` |
| Trainer table | Missing commas and brackets, stray braces, an unclosed table, identical duplicate entries | Differing duplicate entries, unknown class, picture, or music constants, names that are too long |
| Parties | Missing commas, braces, and semicolons, missing parties, wrong party type or size | Empty or oversized parties, unused parties |
| `.party` files | Malformed headers, a missing blank line, missing `Name` or `Pic`, trainers with no Pokémon | Differing duplicate sections, oversized parties |
| Scripts | Battles with undefined trainers, missing battle text | |

When a trainer points to a party that doesn't exist and its expected party is unused, the repair reconnects that party instead of creating a placeholder.

Raising `MAX_TRAINERS_COUNT` is never automatic, because each trainer uses a defeat flag and the change affects save data.

## Supported projects

The tools support pokeemerald, pokefirered, and pokeemerald-expansion, including expansion's separate FireRed/LeafGreen trainer list. They expect the standard file locations. Saves preserve line endings and refuse to overwrite files changed by another program.

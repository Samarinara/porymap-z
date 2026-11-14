# Porymap-Z direction

Porymap-Z expands the existing Qt editor while retaining the game project's runtime, source formats, and build tools. The aim is to edit more of a game from one workspace without making projects depend on a new proprietary format.

## Milestone 1: dialogue (implemented)

Browse standard pokeemerald assembly dialogue, search within files, edit text by label, preserve surrounding source, and refuse saves when external edits are detected. Source preservation and save failures have automated regression coverage.

## Milestone 2: trainers and battles (planned)

Add a pokeemerald trainer adapter for trainer definitions and parties, including species, level, held items, moves, trainer class, AI flags, and battle type. Link map trainer events and battle scripts to the corresponding definitions. Preserve source expressions the editor cannot interpret and validate references against the project's own constants. Gate saves behind source-preservation tests for each supported format.

## Milestone 3: connected game editing (planned)

Add dialogue references from events, flags and variables, items, shops, and script navigation. Extend dialogue support to Poryscript source and configurable directories. Add undo/history and contextual validation without replacing the game's build pipeline.

## Extension boundaries

Keep new document models and game-format adapters in `src/porymapz` with matching public headers in `include/porymapz`. UI consumes these models rather than putting parsing or writing into MainWindow. Add only small, conditional integration hooks to upstream classes. The fork build entry point includes the upstream `.pro` file and adds its sources; the upstream target stays buildable.

Future adapters should discover project constants and configured paths, retain unknown fields and source constructs, reject unsupported writes, compare against the last loaded source, and use atomic replacement. Each independently saved resource should have an explicit owner and save lifecycle. Do not introduce a duplicate database of game content unless a migration and synchronization design is agreed first.

These later milestones are design direction, not implemented features or release-date commitments.

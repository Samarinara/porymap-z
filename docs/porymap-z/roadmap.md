# Porymap-Z direction

Porymap-Z expands the existing Qt editor while retaining the game project's runtime, source formats, and build tools. The aim is to edit more of a game from one workspace without making projects depend on a new proprietary format.

## Milestone 1: dialogue (implemented)

Edit the active map from a Dialogue main tab: dialogue by label or the complete scene script source, including shared scripts. Browse other project dialogue with an explicit scope switch. Save with the main Save action, preserve surrounding source, and refuse saves when external edits are detected. Source preservation and save failures have automated regression coverage.

## Milestone 2: trainers and battles (implemented; validation and event linking remain)

The Battle main tab edits pokeemerald trainer definitions and parties, including species, level, IVs, held items, moves, trainer class, AI flags, and battle type. It provides party navigation, project-constant completion, adding Pokémon, duplication/deletion, trainer ID source access, and scene scripts. Source spans preserve unrelated expressions and comments, and saves detect external modifications. See the [battle guide](battles.md). Direct navigation from map events and semantic/reference validation remain future work.

## Milestone 3: connected game editing (planned)

The [map script popup](scripts.md) now provides event-to-script navigation, map JSON groups, shared-label references, and assembly block editing. Add further dialogue references from events, flags and variables, items, and shops. Extend dialogue support to Poryscript source and configurable directories. Add undo/history and contextual validation without replacing the game's build pipeline.

## Extension boundaries

Keep new document models and game-format adapters in `src/porymapz` with matching public headers in `include/porymapz`. UI consumes these models rather than putting parsing or writing into MainWindow. Add only small, conditional integration hooks to upstream classes. The fork build entry point includes the upstream `.pro` file and adds its sources; the upstream target stays buildable.

Future adapters should discover project constants and configured paths, retain unknown fields and source constructs, reject unsupported writes, compare against the last loaded source, and use atomic replacement. Each independently saved resource should have an explicit owner and save lifecycle. Do not introduce a duplicate database of game content unless a migration and synchronization design is agreed first.

These later milestones are design direction, not implemented features or release-date commitments.

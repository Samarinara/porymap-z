# Battle editor

Open a pokeemerald, pokefirered, or pokeemerald-expansion project, select a map, and choose **Battle** beside **Dialogue** in the main tab bar.

**Trainers & Parties** is a text editor with a searchable record list, following the Dialogue editor's layout. Choose a data file, then select a trainer or party to jump to its definition. Edit the source directly, including names, battle settings, Pokémon, moves, and custom expressions. Add, duplicate, or delete definitions by editing text. The editor uses a fixed-width font and supports **Undo** and **Redo**. There are no field forms or separate advanced editing mode.

The text editor always contains the complete selected file. **Current map**, **Whole project**, and search filter only the navigation list; they never hide or discard source edits. Typing updates the list without moving the text cursor, including while the source is incomplete.

The list defaults to **Current map**. Selecting a different map resets this scope and updates the list. It reads literal `TRAINER_…` references from resolved scene scripts, including shared scripts reached through event labels. Comments and quoted dialogue are ignored, and `.pory` source takes precedence over adjacent generated `.inc` files. Stock C parties are matched through the trainer's actual party references, including union and party-macro references. Expansion trainers are matched by section ID. The Expansion FRLG file is selected automatically when it contains the map's trainers.

Maps without static trainer references show an empty list with an explanation. Use **Whole project** for dynamic battles, rematch variants, or helpers outside the resolved scene. This is a static index, not runtime script analysis. Script saves and Reload refresh it.

**Battle Scripts** uses the same scene editor as the [Dialogue tab](dialogue.md), with Script selected initially. Edit `trainerbattle`, wild battle setup, battle flags, text labels, and other event commands directly. Its Dialogue view remains available for introductions and defeat text. C trainer data remains available in projects using Poryscript; Battle Scripts is disabled for those projects to avoid editing generated scripts.

**Save Battles**, Ctrl+S, and Save Project write pending battle data and script edits. Switching main tabs retains edits. Changing data files, maps, layouts, projects, or closing prompts to save, discard, or cancel. Reload rereads the selected file after resolving pending edits. Saving retains undo history; changing files or discarding changes resets it.

Saves use atomic replacement, preserve UTF-8/BOM and LF/CRLF conventions, and refuse to overwrite externally modified files. If Dialogue and Battle Scripts open the same script, reload the other view after saving; stale edits are refused on save.

## Data files

Stock projects start with `src/data/trainers.h`, `src/data/trainer_parties.h`, and `include/constants/trainers.h`. If a trainer data file is missing, the editor looks for a unique matching C declaration in trainer-related files under `src/data`. **Locate File…** selects a relocated source file inside the project for this session. Missing-file errors include the resolved path and recovery instructions. Symlinks and files outside the project cannot be saved.

Expansion projects use `src/data/trainers.party` and, when present, `src/data/trainers_frlg.party`. These files contain trainer settings and Pokémon together. Edit their native text syntax, including Showdown Pokémon headers, levels, abilities, natures, IVs, EVs, and moves. The expansion build generates C data from these files; the editor prioritizes `.party` and blocks corresponding generated headers.

The navigation index recognizes stock C `struct Trainer` and `struct TrainerMon…` declarations and Expansion `.party` sections. Custom syntax may have no navigation entries; source text remains editable. This editor is not a compiler or battle simulator. Keep party flags, array types, party sizes, references, and trainer IDs consistent. Text can contain incomplete code; compile your project and test battles in game after editing. Wild encounter tables remain in **Wild Pokemon**. Unsupported games and layout-only mode do not expose battle editing.

## Optional corpus checks

Set `PORYMAP_Z_BATTLE_CORPUS` to a temporary directory containing stock copies at `emerald/src/data/{trainers.h,trainer_parties.h}` and `firered/src/data/{trainers.h,trainer_parties.h}`, then run `QT_QPA_PLATFORM=offscreen ./tst_battle`. The optional check parses both formats, checks macro references, and verifies navigation without modifying the copies.

Set `PORYMAP_Z_EXPANSION_CORPUS` to a temporary copy of either trainer `.party` file for the Expansion parser check. Optionally set `PORYMAP_Z_TRAINERPROC` to the Expansion trainerproc executable to validate edited source conversion through `cpp -traditional-cpp` and trainerproc. These checks do not build or emulate a ROM.

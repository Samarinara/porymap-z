# Battle editor

Open a pokeemerald assembly project, select a map, and choose **Battle** beside **Dialogue** in the main tab bar.

- **Trainers & Parties** provides searchable records from `src/data/trainers.h` and `src/data/trainer_parties.h`. Choose a file in the selector above the views, select a record, expand its fields, and double-click a value. All existing fields are exposed, including trainer name, class, portrait, encounter music, gender flags, battle format, AI flags, items, party size/reference, species, level, IVs, held items, and custom moves. Values remain C expressions. Completion suggests constants from your project's headers.
- **Go to Party** opens the selected trainer's `sParty_` reference. Custom party symbols can be found with the party search. Switching files offers Save/Discard/Cancel for pending changes.
- **Add Pokémon** appends a starter entry to the selected party (up to six members), using its item/move format. Edit its defaults before saving. **Duplicate Trainer / Party / Pokémon** copies the selected record or Pokémon group. Trainers and parties need a unique new C symbol; define new trainer IDs through **Trainer IDs (Data Source)**. Select a trainer, party, or Pokémon group and choose **Delete Selected** to remove it after confirmation. Update references when deleting or duplicating records.
- **Data Source** edits the complete selected data file, including trainer ID constants. Use it to create entirely new declarations, change party structure/types, or edit syntax outside the structured view. Returning to the structured view shows the current source immediately. Source edits support the text widget's normal undo/redo.
- **Battle Scripts** follows the current map's resolved script directory, including shared scripts. Its Script view starts selected. Edit `trainerbattle`, wild battle setup, battle flags, text labels, and other event commands directly; the Dialogue view is available for battle introductions and defeat text. This uses the same scene editor and scope controls as the [Dialogue tab](dialogue.md).

**Save Battles**, Ctrl+S, and Save Project write pending battle data and script edits. Switching the main tabs retains edits; the Battle tab and window title indicate unsaved changes. Changing files, maps, layouts, projects, or closing prompts to save, discard, or cancel. Reload rereads the selected file after resolving pending edits.

Structured field edits patch source spans and retain unrelated bytes and comments. Saves use atomic replacement, preserve UTF-8/BOM and LF/CRLF conventions, and refuse to overwrite externally modified files. If Dialogue and Battle Scripts both open the same file, reload the other view after saving; stale edits are refused on save.

## Formats and validation

The structured view supports stock C `struct Trainer` arrays with `[TRAINER_...]` entries and `struct TrainerMon...` party arrays, including all four stock item/custom-move combinations. Nested initializer lists expose items and moves individually. Expressions, comments, escaped strings, and custom fields are retained. Macro-generated declarations and conditional initializer layouts may require Data Source. The data selector uses the standard pokeemerald paths; it does not discover relocated/custom data files. Missing files show an explanation; symlinks and paths outside the project cannot be saved.

Field edits check delimiter balance and reject attempts to insert multiple fields. This is a source editor, not a C compiler or battle simulator. Keep party flags, party union member, array type, size, references, and trainer ID definitions consistent. Strings retain their source quoting and `_()` wrappers. Raw source can contain incomplete code, and adding/deleting records does not automatically update script references or assign trainer IDs. Compile your project and test battles in game after editing. Wild encounter tables remain available in the existing **Wild Pokemon** tab.

As with Dialogue, unsupported games, layout-only mode, and projects configured for Poryscript do not expose battle editing.
